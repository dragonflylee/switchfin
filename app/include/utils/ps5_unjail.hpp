#pragma once

// opt-in escalation for a writable home outside the sandbox image.
//
// A one-shot unjail payload (SvenGDK/unjail-ps5app-payload), sent to the
// console before this title launches, listens on loopback and promotes a
// process that asks. The request is a fixed 2576-byte structure: a magic, the
// command, the caller's pid, and a return field the daemon overwrites. On
// success the process's filesystem root is the real root and /data becomes
// reachable and writable.
//
// This is best-effort and never required. If nothing is listening -- the
// ordinary case -- connect() fails at once with ECONNREFUSED on loopback, the
// request is abandoned, and the caller keeps the sandbox home. Nothing here
// throws, blocks the UI, or changes behaviour when the daemon is absent.
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ps5::unjail {

inline constexpr uint32_t requestMagic = 0xDEADBEEFu;
inline constexpr int32_t promoteCommand = 5;
inline constexpr uint16_t port = 9069;
inline constexpr size_t messageBytes = 2576;

// The wire message. Only the first four fields are interpreted; the rest is
// the zeroed reserved area the daemon expects to receive and return.
struct Message {
    uint32_t magic;
    int32_t command;
    int32_t pid;
    int32_t ret;
    unsigned char reserved[messageBytes - 16];
};
static_assert(sizeof(Message) == messageBytes, "unjail message must be exactly 2576 bytes");

enum class Outcome {
    Promoted,       // the daemon answered success
    Refused,        // nothing was listening, or it declined
    Unsupported,    // a socket could not even be created
};

struct System {
    static int open() noexcept { return ::socket(AF_INET, SOCK_STREAM, 0); }
    static int connect(int fd, const sockaddr_in& address) noexcept {
        return ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    }
    static ssize_t send(int fd, const void* buffer, size_t length) noexcept {
        return ::send(fd, buffer, length, 0);
    }
    static ssize_t receive(int fd, void* buffer, size_t length) noexcept {
        return ::recv(fd, buffer, length, 0);
    }
    static void close(int fd) noexcept { ::close(fd); }
    static int pid() noexcept { return static_cast<int>(::getpid()); }
};

// Sends one promotion request and reports what came back. Loopback only, so a
// full 2576-byte write and read complete without partial-transfer handling in
// practice; the loops below still tolerate short transfers rather than assume.
template <class Ops = System>
inline Outcome request() noexcept {
    const int saved = errno;
    const int fd = Ops::open();
    if (fd < 0) { errno = saved; return Outcome::Unsupported; }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    Outcome outcome = Outcome::Refused;
    if (Ops::connect(fd, address) == 0) {
        Message message{};
        message.magic = requestMagic;
        message.command = promoteCommand;
        message.pid = Ops::pid();
        message.ret = 0;

        const auto* out = reinterpret_cast<const unsigned char*>(&message);
        size_t offset = 0;
        bool ok = message.pid > 0;
        while (ok && offset < messageBytes) {
            const ssize_t n = Ops::send(fd, out + offset, messageBytes - offset);
            if (n <= 0) { ok = false; break; }
            offset += static_cast<size_t>(n);
        }

        if (ok) {
            Message reply{};
            auto* in = reinterpret_cast<unsigned char*>(&reply);
            offset = 0;
            while (offset < messageBytes) {
                const ssize_t n = Ops::receive(fd, in + offset, messageBytes - offset);
                if (n <= 0) { ok = false; break; }
                offset += static_cast<size_t>(n);
            }
            // The daemon zeroes the magic and sets ret to 0 on success.
            if (ok && offset == messageBytes && reply.ret == 0) outcome = Outcome::Promoted;
        }
    }
    Ops::close(fd);
    errno = saved;
    return outcome;
}

}  // namespace ps5::unjail
