#ifdef PS5_NATIVE_GPU

#include "utils/ps5_native_netdb.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Host fault fixtures override allocation calls while compiling this source.
#ifndef PS5_NATIVE_NETDB_CALLOC
#define PS5_NATIVE_NETDB_CALLOC std::calloc
#define PS5_NATIVE_NETDB_MALLOC std::malloc
#define PS5_NATIVE_NETDB_FREE std::free
#endif

// Public SDK 13ccc2d5bf2ac396cdf5007c2b72493cb3d5c8bb:
// libc/netdb.c resolver ABI and samples/http2_get sceNetInit contract.
extern "C" {
int sceNetInit(void);
int sceNetPoolCreate(const char*, int, int);
int sceNetPoolDestroy(int);
int sceNetResolverCreate(const char*, int, int);
int sceNetResolverDestroy(int);
int sceNetResolverStartNtoa(int, const char*, in_addr_t*, int, int, int);
int sceNetResolverStartAton(int, const in_addr_t*, char*, int, int, int, int);
}

namespace {
// Public service pattern: ps5-payload-sdk libc/netdb.c and samples/http2_get.
// IPv6 numeric addresses are supported locally. The referenced resolver only
// provides IPv4 DNS; AF_INET6 hostname queries fail honestly with EAI_FAMILY.
struct Services {
    int (*init)();
    int (*poolCreate)(const char*, int, int);
    int (*poolDestroy)(int);
    int (*create)(const char*, int, int);
    int (*destroy)(int);
    int (*ntoa)(int, const char*, in_addr_t*, int, int, int);
    int (*aton)(int, const in_addr_t*, char*, int, int, int, int);
};
Services services{};
bool attempted = false;
bool ready = false;
std::atomic<bool> resolverUnusable{false};
int initializationError = 0;
const char* initializationPhase = "network-not-started";

struct Query {
    int pool = -1, resolver = -1;
    bool open() noexcept {
        if (!ready || resolverUnusable.load(std::memory_order_acquire)) return false;
        pool = services.poolCreate("", 0x4000, 0);
        if (pool < 0) return false;
        resolver = services.create("", pool, 0);
        return resolver >= 0;
    }
    bool close() noexcept {
        if (resolver >= 0) {
            if (services.destroy(resolver) != 0) {
                // Destruction failure does not prove the child was released.
                // Keep both native resources alive until process exit. Do not
                // retry or destroy its parent; stop new named-DNS allocations.
                resolverUnusable.store(true, std::memory_order_release);
                resolver = pool = -1;
                return false;
            }
            resolver = -1;
        }
        if (pool >= 0) {
            const int rc = services.poolDestroy(pool);
            pool = -1;
            if (rc != 0) {
                resolverUnusable.store(true, std::memory_order_release);
                return false; // Retained native pool; no retry after ambiguity.
            }
        }
        return true;
    }
    ~Query() { close(); }
};

bool decimal(const char* value, std::uint32_t maximum, std::uint32_t& result) noexcept {
    if (!value || !*value) return false;
    std::uint32_t number = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const unsigned digit = *p - '0';
        if (number > (maximum - digit) / 10) return false;
        number = number * 10 + digit;
    }
    result = number;
    return true;
}

struct Address {
    int family = AF_UNSPEC;
    in_addr v4{};
    in6_addr v6{};
    std::uint32_t scope = 0;
};

int numeric(const char* host, Address& address) noexcept {
    if (inet_pton(AF_INET, host, &address.v4) == 1) { address.family = AF_INET; return 1; }
    char text[INET6_ADDRSTRLEN];
    const char* scope = std::strchr(host, '%');
    const size_t length = scope ? static_cast<size_t>(scope - host) : std::strlen(host);
    if (length < sizeof(text)) {
        std::memcpy(text, host, length); text[length] = 0;
        if (inet_pton(AF_INET6, text, &address.v6) == 1) {
            if (scope && !decimal(scope + 1, UINT32_MAX, address.scope)) return -1;
            address.family = AF_INET6; return 1;
        }
    }
    return std::strchr(host, ':') || scope ? -1 : 0;
}

int append(addrinfo*& head, addrinfo*& tail, const Address& address, int type,
           int protocol, unsigned port, int flags, const char* canonical) noexcept {
    auto* item = static_cast<addrinfo*>(PS5_NATIVE_NETDB_CALLOC(1, sizeof(addrinfo)));
    if (!item) return EAI_MEMORY;
    const size_t size = address.family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
    item->ai_addr = static_cast<sockaddr*>(PS5_NATIVE_NETDB_CALLOC(1, size));
    if (!item->ai_addr) { PS5_NATIVE_NETDB_FREE(item); return EAI_MEMORY; }
    item->ai_family = address.family; item->ai_addrlen = static_cast<socklen_t>(size);
    item->ai_flags = flags; item->ai_socktype = type; item->ai_protocol = protocol;
    if (address.family == AF_INET) {
        auto* output = reinterpret_cast<sockaddr_in*>(item->ai_addr);
        output->sin_len = sizeof(*output);
        output->sin_family = AF_INET; output->sin_port = htons(port); output->sin_addr = address.v4;
    } else {
        auto* output = reinterpret_cast<sockaddr_in6*>(item->ai_addr);
        output->sin6_len = sizeof(*output);
        output->sin6_family = AF_INET6; output->sin6_port = htons(port);
        output->sin6_addr = address.v6; output->sin6_scope_id = address.scope;
    }
    if (canonical && !head) {
        const size_t length = std::strlen(canonical) + 1;
        item->ai_canonname = static_cast<char*>(PS5_NATIVE_NETDB_MALLOC(length));
        if (!item->ai_canonname) { PS5_NATIVE_NETDB_FREE(item->ai_addr); PS5_NATIVE_NETDB_FREE(item); return EAI_MEMORY; }
        // Resolver API does not expose CNAMEs; preserve the queried name.
        std::memcpy(item->ai_canonname, canonical, length);
    }
    if (tail) tail->ai_next = item; else head = item;
    tail = item;
    return 0;
}
} // namespace

namespace ps5_native_netdb {
bool initialize() noexcept {
    if (attempted) return ready;
    attempted = true;
    initializationPhase = "network-api-check";
    // Read every ordinary imported entry through volatile storage: native
    // availability must be checked even though each declaration is strong.
    volatile Services imported;
    imported.init = &sceNetInit;
    imported.poolCreate = &sceNetPoolCreate;
    imported.poolDestroy = &sceNetPoolDestroy;
    imported.create = &sceNetResolverCreate;
    imported.destroy = &sceNetResolverDestroy;
    imported.ntoa = &sceNetResolverStartNtoa;
    imported.aton = &sceNetResolverStartAton;
    const Services candidate{imported.init, imported.poolCreate, imported.poolDestroy,
        imported.create, imported.destroy, imported.ntoa, imported.aton};
    if (!candidate.init || !candidate.poolCreate || !candidate.poolDestroy ||
        !candidate.create || !candidate.destroy || !candidate.ntoa || !candidate.aton) {
        initializationError = -1;
        return false;
    }
    initializationPhase = "network-service-init";
    const int rc = candidate.init();
    if (rc != 0) { initializationError = rc; return false; }
    services = candidate;
    ready = true;
    initializationPhase = "network-ready";
    return true;
}
int last_error() noexcept { return initializationError; }
const char* initialization_stage() noexcept { return initializationPhase; }
} // namespace ps5_native_netdb

extern "C" void freeaddrinfo(addrinfo* result) noexcept(noexcept(::freeaddrinfo(result))) {
    while (result) {
        addrinfo* next = result->ai_next;
        PS5_NATIVE_NETDB_FREE(result->ai_canonname); PS5_NATIVE_NETDB_FREE(result->ai_addr); PS5_NATIVE_NETDB_FREE(result);
        result = next;
    }
}

extern "C" int getaddrinfo(const char* host, const char* service, const addrinfo* hints, addrinfo** result)
    noexcept(noexcept(::getaddrinfo(host, service, hints, result))) {
    if (!result) return EAI_FAIL;
    *result = nullptr;
    if (!host && !service) return EAI_NONAME;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int type = hints ? hints->ai_socktype : 0;
    int protocol = hints ? hints->ai_protocol : 0;
    const int flags = hints ? hints->ai_flags : 0;
    constexpr int supported = AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV;
    if (flags & ~supported) return EAI_BADFLAGS;
    if (hints && (hints->ai_addrlen || hints->ai_addr || hints->ai_canonname || hints->ai_next)) return EAI_BADFLAGS;
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) return EAI_FAMILY;
    if (type != 0 && type != SOCK_STREAM && type != SOCK_DGRAM) return EAI_SOCKTYPE;
    if (protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) return EAI_SERVICE;
    if ((type == SOCK_STREAM && protocol == IPPROTO_UDP) ||
        (type == SOCK_DGRAM && protocol == IPPROTO_TCP)) return EAI_SOCKTYPE;
    if (!type && protocol) type = protocol == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM;
    if (!protocol && type) protocol = type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    std::uint32_t port = 0;
    if (service && !decimal(service, 65535, port)) {
        if (flags & AI_NUMERICSERV) return EAI_NONAME;
        if (std::strcmp(service, "http") == 0) port = 80;
        else if (std::strcmp(service, "https") == 0) port = 443;
        else return EAI_SERVICE;
        if (type == SOCK_DGRAM) return EAI_SERVICE;
        type = SOCK_STREAM; protocol = IPPROTO_TCP;
    }
    Address addresses[2];
    unsigned count = 1;
    if (!host) {
        addresses[0].family = family == AF_INET6 ? AF_INET6 : AF_INET;
        if (family == AF_UNSPEC) { addresses[1].family = AF_INET6; count = 2; }
        for (unsigned i = 0; i < count; ++i) {
            if (!(flags & AI_PASSIVE)) {
                if (addresses[i].family == AF_INET) addresses[i].v4.s_addr = htonl(INADDR_LOOPBACK);
                else addresses[i].v6.s6_addr[15] = 1;
            }
        }
    } else {
        if (!*host) return EAI_NONAME;
        if (std::strlen(host) >= NI_MAXHOST) return EAI_OVERFLOW;
        const int parsed = numeric(host, addresses[0]);
        if (parsed < 0) return EAI_NONAME;
        if (parsed > 0) {
            if (family != AF_UNSPEC && family != addresses[0].family) return EAI_FAMILY;
        } else {
            if (flags & AI_NUMERICHOST) return EAI_NONAME;
            if (family == AF_INET6) return EAI_FAMILY;
            if (!ready) { errno = ENETDOWN; return EAI_SYSTEM; }
            Query query;
            if (!query.open()) return EAI_FAIL;
            const int rc = services.ntoa(query.resolver, host, &addresses[0].v4.s_addr, 0, 0, 0);
            const bool released = query.close();
            if (rc != 0 || !released) return EAI_FAIL;
            addresses[0].family = AF_INET;
        }
    }
    addrinfo* head = nullptr;
    addrinfo* tail = nullptr;
    for (unsigned i = 0; i < count; ++i) {
        const int types[] = {SOCK_STREAM, SOCK_DGRAM};
        for (int current : types) {
            if (type && type != current) continue;
            const int rc = append(head, tail, addresses[i], current,
                current == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP, port, flags,
                (flags & AI_CANONNAME) ? host : nullptr);
            if (rc != 0) { freeaddrinfo(head); return rc; }
        }
    }
    *result = head;
    return 0;
}

using NameInfoLength = size_t; // Public PS5 SDK netdb.h uses size_t, unlike macOS/Linux.
extern "C" int getnameinfo(const sockaddr* address, socklen_t length, char* host,
    NameInfoLength hostLength, char* service, NameInfoLength serviceLength, int flags)
    noexcept(noexcept(::getnameinfo(address, length, host, hostLength, service, serviceLength, flags))) {
    constexpr int supported = NI_NUMERICHOST | NI_NUMERICSERV | NI_NAMEREQD | NI_DGRAM;
    if (flags & ~supported) return EAI_BADFLAGS;
    // RFC 3493: either a null pointer or zero length omits that output.
    if (!hostLength) host = nullptr;
    if (!serviceLength) service = nullptr;
    if (!host && !service) return EAI_NONAME;
    if (!address || length < offsetof(sockaddr, sa_data)) return EAI_FAMILY;
    char hostText[NI_MAXHOST]{};
    char serviceText[16]{};
    const void* raw = nullptr;
    unsigned port;
    std::uint32_t scope = 0;
    if (address->sa_family == AF_INET && length >= sizeof(sockaddr_in)) {
        const auto* input = reinterpret_cast<const sockaddr_in*>(address);
        raw = &input->sin_addr; port = ntohs(input->sin_port);
    } else if (address->sa_family == AF_INET6 && length >= sizeof(sockaddr_in6)) {
        const auto* input = reinterpret_cast<const sockaddr_in6*>(address);
        raw = &input->sin6_addr; port = ntohs(input->sin6_port); scope = input->sin6_scope_id;
    } else return EAI_FAMILY;
    if (host) {
        bool named = false;
        if (!(flags & NI_NUMERICHOST) && address->sa_family == AF_INET && ready) {
            Query query;
            int rc = -1;
            if (query.open()) {
                rc = services.aton(query.resolver, static_cast<const in_addr_t*>(raw),
                    hostText, sizeof(hostText), 0, 0, 0);
            }
            if (!query.close()) return EAI_FAIL;
            named = rc == 0 && hostText[0] && std::memchr(hostText, 0, sizeof(hostText));
        }
        if (!named) {
            if (!(flags & NI_NUMERICHOST) && (flags & NI_NAMEREQD)) return EAI_NONAME;
            if (!inet_ntop(address->sa_family, raw, hostText, sizeof(hostText))) return EAI_FAIL;
            if (scope) {
                const size_t used = std::strlen(hostText);
                const int n = std::snprintf(hostText + used, sizeof(hostText) - used, "%%%u", scope);
                if (n < 0 || static_cast<size_t>(n) >= sizeof(hostText) - used) return EAI_OVERFLOW;
            }
        }
        if (hostLength <= std::strlen(hostText)) return EAI_OVERFLOW;
    }
    if (service) {
        const char* name = (!(flags & (NI_NUMERICSERV | NI_DGRAM))) ?
            (port == 80 ? "http" : port == 443 ? "https" : nullptr) : nullptr;
        if (name) std::strcpy(serviceText, name);
        else std::snprintf(serviceText, sizeof(serviceText), "%u", port);
        if (serviceLength <= std::strlen(serviceText)) return EAI_OVERFLOW;
    }
    if (host) std::memcpy(host, hostText, std::strlen(hostText) + 1);
    if (service) std::memcpy(service, serviceText, std::strlen(serviceText) + 1);
    return 0;
}

extern "C" const char* gai_strerror(int error) noexcept(noexcept(::gai_strerror(error))) {
    switch (error) {
    case 0: return "Success";
    case EAI_BADFLAGS: return "Unsupported address lookup flags or hints";
    case EAI_NONAME: return "Name or service unavailable";
    case EAI_AGAIN: return "Temporary name lookup failure";
    case EAI_FAIL: return "Name lookup failed";
    case EAI_FAMILY: return "Address family or IPv6 DNS unsupported";
    case EAI_SOCKTYPE: return "Unsupported or contradictory socket type";
    case EAI_SERVICE: return "Unsupported service or protocol";
    case EAI_MEMORY: return "Address allocation failed";
    case EAI_SYSTEM: return "Network service unavailable; inspect errno";
    case EAI_OVERFLOW: return "Address output buffer too small";
    default: return "Unknown address lookup error";
    }
}
#endif
