#pragma once

// Project-local C/C++ diagnostic interface, not a platform ABI. Only the exact
// pinned app_heap.c counter implementation may provide it. No pointers escape.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ps5_native_heap_observation {
    int32_t state;
    uint32_t capacity_bytes;
    uint64_t live_bytes;
    uint64_t peak_bytes;
    uint64_t blocks;
    uint64_t failures;
    uint64_t ambiguous_zero_reallocs;
};
// Null is allowed. Does not initialize the allocator, allocate, log or lock.
// Independent atomic reads are approximate during concurrent allocation.
// Only owned-heap usable bytes are counted: not RSS, direct/GPU allocations or
// fallback/foreign heaps. Capacity minus live is NOT available allocation space.
// Any ambiguous_zero_reallocs makes live/peak/block accounting inconclusive.
void ps5_native_heap_observe(struct ps5_native_heap_observation* result);
#ifdef __cplusplus
}
#endif
