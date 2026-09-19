#pragma once

// PS5 transport errors. The exception owns numeric values, never a
// CURL handle, URL, header, response body, address or input text.
#include <curl/curl.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include "utils/ps5_native_resolver_diagnostics.hpp"

namespace ps5_native_http {
// These describe libcurl's first error message, not a proved platform cause.
enum class ErrorStage : unsigned {
    Unknown, SocketOpen, CloseOnExec, ConnectSummary, Other, Empty,
    ResolverStart, AddressFormat, EasyInMulti, HttpsAlpn, EntropySeed
};

// Application boundary, distinct from the first libcurl error-buffer category.
// A result of -1 at DnsShare/EasyCreate means no CURLcode was returned.
enum class RequestStage : unsigned { Unknown, GlobalInit, DnsShare, EasyCreate, ShareOption, TlsOptions, Perform, SocketOptions };

struct Snapshot {
    int result = 0;
    unsigned valid = 0;
    long os_error = 0, http_status = 0, connections = 0, proxy = 0;
    curl_off_t lookup_us = 0, connect_us = 0, total_us = 0;
    int error_buffer = -1;
    ErrorStage error_stage = ErrorStage::Unknown;
    RequestStage request_stage = RequestStage::Unknown;
    ResolverObservation resolver;
};

// Owned by HTTP, whose destructor cleans up the easy handle before destroying
// its members. Keeping the buffer alive through cleanup also covers failed
// setopt calls on reused handles; no stack pointer needs to be detached.
class ErrorBuffer {
public:
    ErrorBuffer() = default;
    ErrorBuffer(const ErrorBuffer&) = delete;
    ErrorBuffer& operator=(const ErrorBuffer&) = delete;

    void begin(CURL* easy) noexcept {
        const int saved_errno = errno;
        std::memset(text, 0, sizeof(text));
        status = curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, text);
        errno = saved_errno;
    }

    void finish(Snapshot* value = nullptr) noexcept {
        const int saved_errno = errno;
        if (value) {
            value->error_buffer = static_cast<int>(status);
            value->error_stage = ErrorStage::Unknown;
            if (status == CURLE_OK) {
                // Match only fixed prefixes from the pinned curl source. The
                // variable suffix can contain addresses and never leaves here.
                if (!text[0]) value->error_stage = ErrorStage::Empty;
                else if (startsWith("failed to open socket: ")) value->error_stage = ErrorStage::SocketOpen;
                else if (startsWith("fcntl set CLOEXEC: ")) value->error_stage = ErrorStage::CloseOnExec;
                else if (startsWith("Failed to connect to ")) value->error_stage = ErrorStage::ConnectSummary;
                else if (startsWith("getaddrinfo() thread failed")) value->error_stage = ErrorStage::ResolverStart;
                else if (startsWith("curl_sa_addr inet_ntop() failed with errno ")) value->error_stage = ErrorStage::AddressFormat;
                else if (startsWith("easy handle already used in multi handle")) value->error_stage = ErrorStage::EasyInMulti;
                else if (startsWith("https-connect filter create with unsupported ")) value->error_stage = ErrorStage::HttpsAlpn;
                else if (startsWith("Insufficient randomness")) value->error_stage = ErrorStage::EntropySeed;
                else value->error_stage = ErrorStage::Other;
            }
        }
        std::memset(text, 0, sizeof(text));
        errno = saved_errno;
    }

private:
    template <std::size_t N>
    bool startsWith(const char (&prefix)[N]) const noexcept {
        static_assert(N <= CURL_ERROR_SIZE);
        return std::memcmp(text, prefix, N - 1) == 0;
    }
    char text[CURL_ERROR_SIZE]{};
    CURLcode status = CURLE_FAILED_INIT;
};

inline Snapshot capture(CURL* easy, CURLcode result) noexcept {
    const int saved_errno = errno;
    Snapshot value;
    value.result = static_cast<int>(result);
    value.request_stage = RequestStage::Perform;
    if (easy) {
        auto query = [&](CURLINFO field, auto& output, unsigned bit) {
            // Do not trust an output accompanying a failed getinfo call.
            auto temporary = output;
            if (curl_easy_getinfo(easy, field, &temporary) == CURLE_OK) {
                output = temporary;
                value.valid |= bit;
            }
        };
        query(CURLINFO_OS_ERRNO, value.os_error, 1u);
        query(CURLINFO_RESPONSE_CODE, value.http_status, 2u);
        query(CURLINFO_NUM_CONNECTS, value.connections, 4u);
        query(CURLINFO_USED_PROXY, value.proxy, 8u);
        query(CURLINFO_NAMELOOKUP_TIME_T, value.lookup_us, 16u);
        query(CURLINFO_CONNECT_TIME_T, value.connect_us, 32u);
        query(CURLINFO_TOTAL_TIME_T, value.total_us, 64u);
    }
    errno = saved_errno;
    return value;
}

class Failure : public std::exception {
public:
    explicit Failure(Snapshot value) noexcept : value(value) {}
    const char* what() const noexcept override {
        // Same static libcurl explanation as the existing curl_error.
        if (value.result < 0) return "Could not initialize HTTP request";
        return curl_easy_strerror(static_cast<CURLcode>(value.result));
    }
    const Snapshot& snapshot() const noexcept { return value; }
private:
    Snapshot value;
};

inline Failure initializationFailure(RequestStage stage, int result) noexcept {
    Snapshot value;
    value.result = result;
    value.request_stage = stage;
    // No handle/getinfo observation exists at these constructor boundaries.
    return Failure(value);
}

enum class Outcome { Success, TransportFailure, OtherFailure };

// Called only by the existing ServerAdd UI-thread completion. This also keeps
// the startup sink's single-thread ownership and limits synchronous file I/O.
template <typename Sink>
inline void report(Outcome outcome, const Snapshot& value, Sink sink) noexcept {
    static unsigned reports = 0;
    if (reports == 16) return;
    const int saved_errno = errno;
    const char* phase = outcome == Outcome::Success ? "public-info-ok" :
        outcome == Outcome::TransportFailure ? "transport-failure" : "other-failure";
    char line[1024];
    const int count = std::snprintf(line, sizeof(line),
        "SERVER-CONNECT report=%u phase=%s result=%d valid=0x%02x os=%ld http=%ld connections=%ld proxy=%ld lookup-us=%lld connect-us=%lld total-us=%lld error-buffer=%d error-stage=%u request-stage=%u resolver-pipes=%u resolver-pipe-stage=%u resolver-pipe-error=%d resolver-thread-stage=%u resolver-thread-error=%d resolver-cloexec-fallbacks=%u resolver-cloexec-error=%d socket-cloexec-calls=%u socket-cloexec-stage=%u socket-cloexec-error=%d socket-cloexec-fallbacks=%u socket-cloexec-fallback-error=%d",
        ++reports, phase, value.result, value.valid, value.os_error, value.http_status,
        value.connections, value.proxy, static_cast<long long>(value.lookup_us),
        static_cast<long long>(value.connect_us), static_cast<long long>(value.total_us),
        value.error_buffer, static_cast<unsigned>(value.error_stage), static_cast<unsigned>(value.request_stage),
        value.resolver.pipe_calls, value.resolver.pipe_stage, value.resolver.pipe_error,
        value.resolver.thread_stage, value.resolver.thread_error,
        value.resolver.cloexec_fallbacks, value.resolver.cloexec_error,
        value.resolver.socket_calls, value.resolver.socket_stage, value.resolver.socket_error,
        value.resolver.socket_fallbacks, value.resolver.socket_fallback_error);
    if (count >= 0 && static_cast<std::size_t>(count) < sizeof(line)) sink(line);
    errno = saved_errno;
}
} // namespace ps5_native_http
