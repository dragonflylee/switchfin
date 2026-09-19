// Native application POSIX support; no changes to the SDK or other platforms.
#ifndef PS5_NATIVE_GPU
#error "Compile native POSIX support only for PS5_NATIVE_GPU"
#endif

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include "utils/ps5_native_random.hpp"

namespace {
constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr unsigned collision_limit = 128;

bool random_suffix(char (&suffix)[6]) noexcept
{
    std::size_t used = 0;
    // Rejection sampling gives each of the 62 characters equal probability.
    // Bound pathological rejection as well as filesystem collisions.
    for (unsigned round = 0; round != 8; ++round) {
        unsigned char bytes[32];
        if (!ps5_native_random::fill(bytes, sizeof bytes)) return false;
        for (unsigned char value : bytes) {
            if (value >= 248) continue;
            suffix[used++] = alphabet[value % 62];
            if (used == sizeof suffix) return true;
        }
    }
    return false;
}
}

extern "C" int mkstemp(char* pattern)
{
    if (!pattern) { errno = EINVAL; return -1; }
    const std::size_t size = std::strlen(pattern);
    if (size < 6 || std::memcmp(pattern + size - 6, "XXXXXX", 6) != 0) {
        errno = EINVAL;
        return -1;
    }
    char* const suffix = pattern + size - 6;
    for (unsigned attempt = 0; attempt != collision_limit; ++attempt) {
        char replacement[6];
        if (!random_suffix(replacement)) {
            std::memcpy(suffix, "XXXXXX", 6);
            errno = EIO;
            return -1;
        }
        std::memcpy(suffix, replacement, 6);
        // Atomic exclusive creation also rejects existing symlinks. The caller
        // owns a successful descriptor; an existing file is never truncated.
        const int fd = open(pattern, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) return fd;
        const int error = errno;
        if (error != EEXIST) {
            std::memcpy(suffix, "XXXXXX", 6);
            errno = error;
            return -1;
        }
    }
    std::memcpy(suffix, "XXXXXX", 6);
    errno = EEXIST;
    return -1;
}
