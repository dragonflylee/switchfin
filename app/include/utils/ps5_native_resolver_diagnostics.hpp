#pragma once

#include <cerrno>
#include <climits>

namespace ps5_native_http {
// Last blocking resolver pipe / subsequent thread admission observed during
// this perform call. No descriptors, thread handles, pointers or names escape.
struct ResolverObservation {
    unsigned pipe_calls = 0;
    unsigned pipe_stage = 0; // 0 unobserved, 1 pipe, 2 first CLOEXEC, 3 second, 4 ready
    int pipe_error = 0;      // errno at failure, before curl's cleanup/replacement
    unsigned thread_stage = 0; // 0 unobserved, 1 bookkeeping, 2 pthread rejected, 3 admitted
    int thread_error = 0;   // pthread return code, not ambient errno
    unsigned cloexec_fallbacks = 0; // FIOCLEX attempts for this blocking pipe
    int cloexec_error = 0;  // last fallback errno; zero after success
    // Last cf-socket flag operation in this perform; counts span all attempts.
    unsigned socket_calls = 0;
    unsigned socket_stage = 0; // 0 unseen, 1 primary OK, 2 fallback OK, 3/4 failure, 5 ABI guard
    int socket_error = 0;      // primary failure errno, retained after fallback success
    unsigned socket_fallbacks = 0;
    int socket_fallback_error = 0;
};

// Stack-owned and thread-confined, including nested/reentrant perform calls.
// The native V7 runtime supplies emulated TLS; it may allocate per-thread TLS
// storage on first use. This is not an allocation-free runtime claim.
class ResolverScope {
public:
    ResolverScope() noexcept {
        const int saved = errno;
        previous = active;
        active = this;
        errno = saved;
    }
    ~ResolverScope() {
        const int saved = errno;
        active = previous;
        errno = saved;
    }
    ResolverScope(const ResolverScope&) = delete;
    ResolverScope& operator=(const ResolverScope&) = delete;
    static ResolverScope* current() noexcept { return active; }
    ResolverObservation observation;
    bool creating_thread = false;
private:
    ResolverScope* previous;
    inline static thread_local ResolverScope* active = nullptr;
};

// Derived from curl 8.18.0 lib/socketpair.c, Copyright (C) Daniel Stenberg et al.
// SPDX-License-Identifier: curl (COPYING is packaged in the dependency licenses).
// Native-selected Curl_pipe blocking call/cleanup order. Ops may provide an
// equivalent close-on-exec fallback; descriptor cleanup remains mandatory.
// Only this fixed-signature helper's own fcntl calls are observed; there is no
// process-wide variadic fcntl interception. Nonblocking/out-of-scope calls keep
// using the original libcurl implementation. Ops permits actual-source parity
// fault tests, including failure errno overwritten by close().
template <typename Ops>
int observedResolverPipe(int socks[2], Ops& ops, ResolverObservation& value) noexcept {
    if (value.pipe_calls != UINT_MAX) ++value.pipe_calls;
    value.pipe_error = 0;
    value.thread_stage = 0;
    value.thread_error = 0;
    value.cloexec_fallbacks = 0;
    value.cloexec_error = 0;
    value.pipe_stage = 1;
    if (ops.pipe(socks)) {
        value.pipe_error = errno;
        return -1;
    }
    value.pipe_stage = 2;
    int rc = ops.cloexec(socks[0]);
    if (!rc) {
        value.pipe_stage = 3;
        rc = ops.cloexec(socks[1]);
    }
    if (rc) {
        value.pipe_error = errno;
        ops.close(socks[0]);
        ops.close(socks[1]);
        socks[0] = socks[1] = -1;
        return -1;
    }
    value.pipe_stage = 4;
    return 0;
}
} // namespace ps5_native_http
