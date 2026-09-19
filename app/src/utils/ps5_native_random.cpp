#ifdef PS5_NATIVE_GPU

#include "utils/ps5_native_random.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <sys/types.h>
#include <sys/sysctl.h>

extern "C" {
int sceKernelOpen(const char*, int, unsigned);
long sceKernelWrite(int, const void*, std::size_t);
int sceKernelFsync(int);
int sceKernelClose(int);
int sceKernelUsleep(unsigned);
}

namespace ps5_native_random {
namespace {
using EntropyService = decltype(&::sysctl);
constexpr std::size_t service_chunk = 32;
constexpr int not_initialized = -1;
constexpr int invalid_input = -2;
constexpr int unavailable_service = -3;
constexpr int invalid_length = -4;

// Initialization precedes all worker creation. Thereafter the function pointer
// and attempted flag are immutable; only failures update the atomic error code.
EntropyService random_service = nullptr;
bool attempted = false;
const char* initialization_phase = "random-not-started";
std::atomic<int> error{0};
static_assert(std::atomic<int>::is_always_lock_free, "Native RNG errors must not allocate or lock");

void wipe(void* output, std::size_t size) noexcept {
    auto* bytes = static_cast<volatile unsigned char*>(output);
    while (size--) *bytes++ = 0;
}

int read_entropy(EntropyService service, void* output, std::size_t size) noexcept {
    // The pinned public SDK libc uses this read-only entropy MIB. Use only
    // that OS source, not its old arc4random implementation or weak fallbacks.
    const int mib[] = {CTL_KERN, KERN_ARND};
    std::size_t returned = size;
    const int result = service(mib, 2, output, &returned, nullptr, 0);
    if (result != 0) return result;
    return returned == size ? 0 : invalid_length;
}

std::atomic_flag failure_reported = ATOMIC_FLAG_INIT;
std::atomic<bool> failure_report_complete{false};

void report_failure() noexcept {
    if (!failure_reported.test_and_set(std::memory_order_relaxed)) {
        // One fixed record replaces the previous session's failure record.
        // This path is independent of Logger, allocation and startup trace state.
        constexpr char message[] = "Native random service failed; no entropy fallback is permitted.\n";
        const int fd = sceKernelOpen("/download0/switchfin-native-random.log",
            O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            std::size_t offset = 0;
            while (offset < sizeof(message) - 1) {
                const auto remaining = sizeof(message) - 1 - offset;
                const long wrote = sceKernelWrite(fd, message + offset, remaining);
                if (wrote <= 0 || static_cast<std::size_t>(wrote) > remaining) break;
                offset += static_cast<std::size_t>(wrote);
            }
            (void)sceKernelFsync(fd);
            (void)sceKernelClose(fd);
        }
        failure_report_complete.store(true, std::memory_order_release);
    } else {
        // A second failing worker must not abort the process before the first
        // worker finishes its one diagnostic write attempt.
        while (!failure_report_complete.load(std::memory_order_acquire))
            (void)sceKernelUsleep(1000);
    }
}
} // namespace

bool initialize() noexcept {
    if (attempted) return random_service != nullptr;
    attempted = true;
    initialization_phase = "random-api-check";
    // A strong declaration alone lets the compiler assume a non-null address.
    // Read the ordinary imported pointer through a volatile local so the
    // generated native code actually checks availability before calling it.
    EntropyService volatile imported = &::sysctl;
    const EntropyService service = imported;
    if (!service) {
        error.store(unavailable_service, std::memory_order_relaxed);
        return false;
    }
    initialization_phase = "random-entropy-probe";
    unsigned char probe[service_chunk]{};
    const int result = read_entropy(service, probe, sizeof(probe));
    wipe(probe, sizeof(probe));
    if (result != 0) {
        error.store(result, std::memory_order_relaxed);
        return false;
    }
    random_service = service;
    initialization_phase = "random-ready";
    return true;
}

bool fill(void* output, std::size_t size) noexcept {
    if (size == 0) return true;
    if (!output) {
        error.store(invalid_input, std::memory_order_relaxed);
        return false;
    }
    if (!random_service) {
        wipe(output, size);
        error.store(not_initialized, std::memory_order_relaxed);
        return false;
    }
    auto* bytes = static_cast<unsigned char*>(output);
    for (std::size_t offset = 0; offset < size;) {
        const auto remaining = size - offset;
        const auto chunk = remaining < service_chunk ? remaining : service_chunk;
        const int result = read_entropy(random_service, bytes + offset, chunk);
        if (result != 0) {
            wipe(output, size);
            error.store(result, std::memory_order_relaxed);
            return false;
        }
        offset += chunk;
    }
    return true;
}

int last_error() noexcept { return error.load(std::memory_order_relaxed); }
const char* initialization_stage() noexcept { return initialization_phase; }

std::string hex(int len) {
    if (len < 0 || static_cast<std::size_t>(len) > std::string{}.max_size() / 2)
        throw std::runtime_error("Invalid native random byte count");
    std::string output(static_cast<std::size_t>(len) * 2, '\0');
    constexpr char digits[] = "0123456789abcdef";
    unsigned char bytes[service_chunk]{};
    for (std::size_t offset = 0; offset < static_cast<std::size_t>(len);) {
        const auto remaining = static_cast<std::size_t>(len) - offset;
        const auto chunk = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
        if (!fill(bytes, chunk)) {
            wipe(bytes, sizeof(bytes));
            wipe(output.data(), output.size());
            throw std::runtime_error("Native random service unavailable");
        }
        for (std::size_t i = 0; i < chunk; ++i) {
            output[2 * (offset + i)] = digits[bytes[i] >> 4];
            output[2 * (offset + i) + 1] = digits[bytes[i] & 15];
        }
        offset += chunk;
    }
    wipe(bytes, sizeof(bytes));
    return output;
}

[[noreturn]] void fatal_failure() noexcept {
    report_failure();
    for (;;) (void)sceKernelUsleep(1000000);
}
} // namespace ps5_native_random

// Match the C library's existing exception specifications on the native target
// and host test toolchains. These implementations themselves never throw.
extern "C" std::uint32_t arc4random() noexcept(noexcept(::arc4random())) {
    std::uint32_t value = 0;
    if (!ps5_native_random::fill(&value, sizeof(value))) ps5_native_random::fatal_failure();
    return value;
}

extern "C" void arc4random_buf(void* output, std::size_t size)
    noexcept(noexcept(::arc4random_buf(output, size))) {
    if (!ps5_native_random::fill(output, size)) ps5_native_random::fatal_failure();
}
#endif
