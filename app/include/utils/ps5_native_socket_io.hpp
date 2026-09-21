#pragma once

#include <cerrno>
#include <sys/socket.h>

namespace ps5_native_socket_io {
// Only the receipt-pinned curl socket filter binds here. Preserve its socket
// domain and errno convention; enforce nonblocking reads per call without
// changing shared descriptor flags. Writes preserve the caller's flags.
// Never retry, discard bytes, close, or weaken TLS.
struct System {
    static ssize_t receive(int fd, void* buffer, size_t length, int flags) noexcept {
        return ::recv(fd, buffer, length, flags);
    }
    static ssize_t send(int fd, const void* buffer, size_t length, int flags) noexcept {
        return ::send(fd, buffer, length, flags);
    }
};

template<class Ops = System>
inline ssize_t receive(int fd, void* buffer, size_t length, int flags) noexcept {
    return Ops::receive(fd, buffer, length, flags | MSG_DONTWAIT);
}

template<class Ops = System>
inline ssize_t send(int fd, const void* buffer, size_t length, int flags) noexcept {
    return Ops::send(fd, buffer, length, flags);
}
} // namespace ps5_native_socket_io
