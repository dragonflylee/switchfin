#pragma once

#include <cstddef>
#include <cstdio>

extern "C" {
const char* sceKernelGetFsSandboxRandomWord();
int sceKernelLoadStartModule(const char*, std::size_t, const void*, unsigned, const void*, int*);
int sceKernelDlsym(int, const char*, void**);
}

namespace ps5_native_module {
enum class Service { Random, Net };
struct Symbol { const char* name; const char* identifier; };
// Fixed published identities; see native-player/SERVICES.md and its symbol
// audit. Identifier identity does not establish firmware lookup acceptance.
inline constexpr Symbol random_number{"sceRandomGetRandomNumber", "PI7jIZj4pcE"};
inline constexpr Symbol net_init{"sceNetInit", "Nlev7Lg8k3A"};
inline constexpr Symbol net_pool_create{"sceNetPoolCreate", "dgJBaeJnGpo"};
inline constexpr Symbol net_pool_destroy{"sceNetPoolDestroy", "K7RlrTkI-mw"};
inline constexpr Symbol net_resolver_create{"sceNetResolverCreate", "C4UgDHHPvdw"};
inline constexpr Symbol net_resolver_destroy{"sceNetResolverDestroy", "kJlYH5uMAWI"};
inline constexpr Symbol net_resolver_ntoa{"sceNetResolverStartNtoa", "Nd91WaWmG2w"};
inline constexpr Symbol net_resolver_aton{"sceNetResolverStartAton", "Apb4YDxKsRI"};
enum class LookupMethod { Unavailable, Name, Identifier };
struct LookupEvidence {
    int name_status = -1;
    int identifier_status = -1;
    bool identifier_attempted = false;
    bool name_address_present = false;
    bool identifier_address_present = false;
    LookupMethod method = LookupMethod::Unavailable;
    int last_status() const noexcept { return identifier_attempted ? identifier_status : name_status; }
};
using LookupObserver = void (*)(const char*, const LookupEvidence&) noexcept;
struct ResolvedSymbol { void* address = nullptr; LookupEvidence evidence; };

// Both attempts use the same owned module and one fixed service identity.
// Never accept a stale pointer accompanying failure or an unwritten output.
// This is an ordinary API compatibility attempt, not an alternate loader.
inline ResolvedSymbol resolve(int handle, Symbol symbol) noexcept {
    ResolvedSymbol result;
    void* address = nullptr;
    result.evidence.name_status = sceKernelDlsym(handle, symbol.name, &address);
    result.evidence.name_address_present = address != nullptr;
    if (result.evidence.name_status == 0 && address) {
        result.address = address;
        result.evidence.method = LookupMethod::Name;
        return result;
    }
    address = nullptr;
    result.evidence.identifier_attempted = true;
    result.evidence.identifier_status = sceKernelDlsym(handle, symbol.identifier, &address);
    result.evidence.identifier_address_present = address != nullptr;
    if (result.evidence.identifier_status == 0 && address) {
        result.address = address;
        result.evidence.method = LookupMethod::Identifier;
    }
    return result;
}
struct Result {
    int handle;
    int error;
    const char* stage;
};

// Native titles see system modules within their own mounted namespace. The
// global /system path visible to FTP/payloads is not their filesystem contract.
// Use only fixed service names and a bounded, validated platform-provided word.
// The word and resulting path must never be included in public diagnostics.
inline Result load(Service service) noexcept {
    const char* name = service == Service::Random ? "libSceRandom.sprx" : "libSceNet.sprx";
    const char* word = sceKernelGetFsSandboxRandomWord();
    constexpr std::size_t maximum_word = 128;
    std::size_t length = 0;
    if (!word) return {-1, -1, "module-sandbox-word"};
    while (length < maximum_word && word[length]) {
        const char c = word[length++];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return {-1, -2, "module-sandbox-word"};
    }
    if (!length || length == maximum_word) return {-1, -3, "module-sandbox-word"};
    char path[192];
    const int count = std::snprintf(path, sizeof(path), "/%s/common/lib/%s", word, name);
    if (count < 0 || static_cast<std::size_t>(count) >= sizeof(path))
        return {-1, -4, "module-sandbox-path"};
    int started = -1;
    const int handle = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, &started);
    if (handle < 0) return {handle, handle, "module-load"};
    // Keep a successful load reference for process lifetime even if startup
    // failed: no unload/retry after an ambiguous service initialization.
    if (started < 0) return {handle, started, "module-start"};
    return {handle, 0, "module-ready"};
}
} // namespace ps5_native_module
