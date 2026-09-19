#ifdef PS5_NATIVE_GPU
#include "utils/ps5_native_heap_failure.hpp"
#include "utils/ps5_native_heap.h"
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>

extern "C" {
int sceKernelOpen(const char*, int, unsigned);
long sceKernelWrite(int, const void*, std::size_t);
int sceKernelFsync(int);
int sceKernelClose(int);
int sceKernelRename(const char*, const char*);
void* __real_sceLibcMspaceMalloc(void*, std::size_t);
void* __real_sceLibcMspaceCalloc(void*, std::size_t, std::size_t);
void* __real_sceLibcMspaceRealloc(void*, void*, std::size_t);
int __real_sceLibcMspacePosixMemalign(void*, void**, std::size_t, std::size_t);
}

namespace {
constexpr const char* path = "/download0/switchfin-native-heap-failure.log";
constexpr const char* previous = "/download0/switchfin-native-heap-failure.log.previous";
constexpr char header[] = "STAGE native-startup-v57-configurable-heap\nHEAP-JOURNAL schema=2 armed=1\n";
std::atomic<int> descriptor{-1};
std::atomic_flag initialized = ATOMIC_FLAG_INIT;
std::atomic_flag reported = ATOMIC_FLAG_INIT;
static_assert(std::atomic<int>::is_always_lock_free, "failure journal must not acquire a runtime lock");
static_assert(std::atomic<unsigned>::is_always_lock_free, "phase publication must not acquire a runtime lock");
constexpr unsigned phaseCount = 12;
struct PhaseSlot {
    // 0=unclaimed, 1=being written, 2=complete and immutable.
    std::atomic<unsigned> state{0};
    unsigned sequence = 0;
    ps5_native_heap_observation heap{};
};
PhaseSlot phases[phaseCount];
std::atomic<unsigned> phaseSequence{0};

struct PreserveErrno {
    int saved = errno;
    ~PreserveErrno() { errno = saved; }
};

bool writeBounded(int fd, const char* data, std::size_t length,
                  unsigned* sharedBudget = nullptr) noexcept {
    // At most 16 writes, including short writes. No retry on errors/EINTR and
    // no unbounded loop in the failed allocator. Native I/O latency is unknown.
    unsigned localBudget = 16;
    unsigned& budget = sharedBudget ? *sharedBudget : localBudget;
    while (length && budget) {
        --budget;
        const long written = sceKernelWrite(fd, data, length);
        if (written <= 0 || static_cast<std::size_t>(written) > length) return false;
        data += written;
        length -= static_cast<std::size_t>(written);
    }
    return length == 0;
}

struct Row {
    char data[512]{};
    std::size_t length = 0;
    bool valid = true;
    void literal(const char* text) noexcept {
        while (*text) {
            if (length == sizeof(data)) { valid = false; return; }
            data[length++] = *text++;
        }
    }
    void number(std::uint64_t value) noexcept {
        char digits[20]; unsigned count = 0;
        do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
        if (count > sizeof(data) - length) { valid = false; return; }
        while (count) data[length++] = digits[--count];
    }
    void signedNumber(std::int64_t value) noexcept {
        if (value < 0) { literal("-"); number(std::uint64_t{0} - static_cast<std::uint64_t>(value)); }
        else number(static_cast<std::uint64_t>(value));
    }
};

void failure(unsigned operation, std::size_t size, std::size_t count,
             std::size_t alignment, int result) noexcept {
    PreserveErrno preserve;
    const int fd = descriptor.load(std::memory_order_acquire);
    if (fd < 0 || reported.test_and_set(std::memory_order_relaxed)) return;
    // Freeze eligibility before sampling the failure. Never inspect a slot
    // still being written, nor include one published after this observation.
    unsigned completed = 0;
    for (unsigned n = 0; n < phaseCount; ++n)
        if (phases[n].state.load(std::memory_order_acquire) == 2) completed |= 1u << n;
    ps5_native_heap_observation heap{};
    ps5_native_heap_observe(&heap);
    Row row;
    row.literal("HEAP-FAIL schema=1 operation="); row.number(operation);
    row.literal(" size="); row.number(size);
    row.literal(" count="); row.number(count);
    row.literal(" alignment="); row.number(alignment);
    row.literal(" result="); row.signedNumber(result);
    row.literal(" errno="); row.signedNumber(preserve.saved);
    row.literal(" state="); row.signedNumber(heap.state);
    row.literal(" capacity="); row.number(heap.capacity_bytes);
    row.literal(" live="); row.number(heap.live_bytes);
    row.literal(" peak="); row.number(heap.peak_bytes);
    row.literal(" blocks="); row.number(heap.blocks);
    row.literal(" prior-failures="); row.number(heap.failures);
    row.literal(" ambiguous-zero-reallocs="); row.number(heap.ambiguous_zero_reallocs);
    row.literal(" phase-mask="); row.number(completed);
    row.literal("\n");
    unsigned budget = 16; // Total for failure plus all phase rows, not per row.
    if (!row.valid || !writeBounded(fd, row.data, row.length, &budget)) return;
    // Preserve the original failure row before attempting supplemental rows.
    (void)sceKernelFsync(fd);
    bool supplemental = false;
    for (unsigned n = 0; n < phaseCount && budget; ++n) {
        if (!(completed & (1u << n))) continue;
        const auto& phase = phases[n];
        Row saved;
        saved.literal("HEAP-PHASE schema=1 phase="); saved.number(n + 1);
        saved.literal(" sequence="); saved.number(phase.sequence);
        saved.literal(" state="); saved.signedNumber(phase.heap.state);
        saved.literal(" capacity="); saved.number(phase.heap.capacity_bytes);
        saved.literal(" live="); saved.number(phase.heap.live_bytes);
        saved.literal(" peak="); saved.number(phase.heap.peak_bytes);
        saved.literal(" blocks="); saved.number(phase.heap.blocks);
        saved.literal(" failures="); saved.number(phase.heap.failures);
        saved.literal(" ambiguous-zero-reallocs="); saved.number(phase.heap.ambiguous_zero_reallocs);
        saved.literal("\n");
        supplemental = true;
        if (!saved.valid || !writeBounded(fd, saved.data, saved.length, &budget)) break;
    }
    if (supplemental) (void)sceKernelFsync(fd);
    // This observation precedes the existing wrapper's failure increment.
    // It captures one mspace API failure, not foreign/fallback allocations,
    // FFmpeg max_alloc_size rejection, GPU memory or contiguous free space.
}
} // namespace

void ps5_native_heap_checkpoint(Ps5HeapPhase phase) noexcept {
    const unsigned index = static_cast<unsigned>(phase) - 1;
    if (index >= phaseCount) return;
    auto& slot = phases[index];
    if (slot.state.load(std::memory_order_relaxed) != 0) return;
    unsigned expected = 0;
    if (!slot.state.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) return;
    PreserveErrno preserve;
    slot.sequence = phaseSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    ps5_native_heap_observe(&slot.heap);
    slot.state.store(2, std::memory_order_release);
}

int ps5_native_heap_failure_initialize() noexcept {
    PreserveErrno preserve;
    if (initialized.test_and_set(std::memory_order_relaxed)) return -1;
    int fd = sceKernelOpen(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        const int renamed = sceKernelRename(path, previous);
        if (renamed < 0) return renamed;
        fd = sceKernelOpen(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    }
    if (fd < 0) return fd;
    if (!writeBounded(fd, header, sizeof(header) - 1)) { (void)sceKernelClose(fd); return -1; }
    const int synced = sceKernelFsync(fd);
    if (synced < 0) { (void)sceKernelClose(fd); return synced; }
    descriptor.store(fd, std::memory_order_release);
    return 0;
}

extern "C" void* __wrap_sceLibcMspaceMalloc(void* heap, std::size_t size) {
    void* result = __real_sceLibcMspaceMalloc(heap, size);
    if (!result && size) failure(1, size, 1, 0, 0);
    return result;
}
extern "C" void* __wrap_sceLibcMspaceCalloc(void* heap, std::size_t count, std::size_t size) {
    void* result = __real_sceLibcMspaceCalloc(heap, count, size);
    if (!result && count && size) failure(2, size, count, 0, 0);
    return result;
}
extern "C" void* __wrap_sceLibcMspaceRealloc(void* heap, void* address, std::size_t size) {
    void* result = __real_sceLibcMspaceRealloc(heap, address, size);
    if (!result && size) failure(3, size, 1, 0, 0);
    return result;
}
extern "C" int __wrap_sceLibcMspacePosixMemalign(void* heap, void** address,
                                               std::size_t alignment, std::size_t size) {
    int result = __real_sceLibcMspacePosixMemalign(heap, address, alignment, size);
    if (result || (size && !*address)) failure(4, size, 1, alignment, result);
    return result;
}
#endif
