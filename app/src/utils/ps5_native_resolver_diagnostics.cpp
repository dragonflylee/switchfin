#ifdef PS5_NATIVE_GPU
#include "utils/ps5_native_resolver_diagnostics.hpp"
#include "utils/ps5_native_socket_io.hpp"
#include <curl/curl.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Private ABI is pinned to the receipt-verified native curl archive by build.py.
// curl_threads.h USE_THREADS_POSIX: pthread_t*, unsigned int (*)(void*).
static_assert(LIBCURL_VERSION_NUM == 0x081200, "Review resolver hooks for new curl");
static_assert(sizeof(curl_socket_t) == sizeof(int), "Review native Curl_pipe ABI");
extern "C" int __real_Curl_pipe(int[2], bool);
extern "C" pthread_t* __real_Curl_thread_create(unsigned int (*)(void*), void*);
extern "C" int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

namespace {
struct PipeOps {
    ps5_native_http::ResolverObservation& observation;
    int pipe(int pair[2]) noexcept { return ::pipe(pair); }
    int cloexec(int fd) noexcept {
        const int saved = errno;
        int rc = ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        if (rc != -1 || errno != EINVAL) return rc;
        // Bound fix27 evidence: the first resolver-pipe F_SETFD is rejected
        // with EINVAL. FIOCLEX sets the same descriptor flag; never ignore a
        // failed flag operation or continue with an inheritable pipe.
        if (observation.cloexec_fallbacks != UINT_MAX) ++observation.cloexec_fallbacks;
        rc = ::ioctl(fd, FIOCLEX, static_cast<void*>(nullptr));
        observation.cloexec_error = rc ? errno : 0;
        if (!rc) errno = saved;
        return rc;
    }
    int close(int fd) noexcept { return ::close(fd); }
};
}

extern "C" int __wrap_Curl_pipe(int pair[2], bool nonblocking) {
    const int saved = errno;
    auto* scope = ps5_native_http::ResolverScope::current();
    errno = saved;
    if (!scope || nonblocking) return __real_Curl_pipe(pair, nonblocking);
    PipeOps ops{scope->observation};
    return ps5_native_http::observedResolverPipe(pair, ops, scope->observation);
}

// Only the two F_SETFD/FD_CLOEXEC references in the exact pinned cf-socket
// object bind here. Both native x86-64 callers pass three integers; this is
// not a process-wide variadic fcntl hook. The builder verifies that boundary.
extern "C" int ps5_native_curl_socket_fcntl(int fd, int command, int flags) noexcept {
    const int saved = errno;
    auto* scope = ps5_native_http::ResolverScope::current();
    errno = saved;
    if (command != F_SETFD || flags != FD_CLOEXEC) {
        if (scope) {
            auto& value = scope->observation;
            if (value.socket_calls != UINT_MAX) ++value.socket_calls;
            value.socket_stage = 5;
            value.socket_error = EINVAL;
            value.socket_fallback_error = 0;
        }
        errno = EINVAL;
        return -1;
    }
    const int rc = ::fcntl(fd, command, flags);
    const int primaryError = rc < 0 ? errno : 0;
    if (scope) {
        auto& value = scope->observation;
        if (value.socket_calls != UINT_MAX) ++value.socket_calls;
        value.socket_error = primaryError;
        value.socket_fallback_error = 0;
        value.socket_stage = rc < 0 ? 3 : 1;
    }
    // The descriptor contract applies to every caller of this receipt-bound
    // curl adapter, including WebSocket multi handles without an HTTP trace.
    // Only observation depends on the optional diagnostic scope.
    if (rc != -1 || primaryError != EINVAL) return rc;
    if (scope && scope->observation.socket_fallbacks != UINT_MAX)
        ++scope->observation.socket_fallbacks;
    const int fallback = ::ioctl(fd, FIOCLEX, static_cast<void*>(nullptr));
    const int fallbackError = fallback < 0 ? errno : fallback ? EINVAL : 0;
    if (scope) {
        scope->observation.socket_fallback_error = fallbackError;
        scope->observation.socket_stage = fallback ? 4 : 2;
    }
    if (!fallback) {
        errno = saved;
        return 0;
    }
    errno = fallbackError;
    return -1;
}

// Only three recv/send references in the pinned cf-socket member bind here,
// including shutdown receive. Resolver pipes and unrelated sockets are untouched.
extern "C" ssize_t ps5_native_curl_socket_recv(int fd, void* buffer, size_t length, int flags) noexcept {
    return ps5_native_socket_io::receive(fd, buffer, length, flags);
}

extern "C" ssize_t ps5_native_curl_socket_send(int fd, const void* buffer, size_t length, int flags) noexcept {
    return ps5_native_socket_io::send(fd, buffer, length, flags);
}

extern "C" pthread_t* __wrap_Curl_thread_create(unsigned int (*func)(void*), void* arg) {
    const int saved = errno;
    auto* scope = ps5_native_http::ResolverScope::current();
    errno = saved;
    if (!scope) return __real_Curl_thread_create(func, arg);
    scope->observation.thread_stage = 1;
    scope->observation.thread_error = 0;
    scope->creating_thread = true;
    auto* result = __real_Curl_thread_create(func, arg);
    scope->creating_thread = false;
    return result;
}

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*func)(void*), void* arg) {
    const int saved = errno;
    auto* scope = ps5_native_http::ResolverScope::current();
    errno = saved;
    const int rc = __real_pthread_create(thread, attr, func, arg);
    if (scope && scope->creating_thread) {
        scope->observation.thread_stage = rc ? 2 : 3;
        scope->observation.thread_error = rc;
    }
    return rc;
}
#endif
