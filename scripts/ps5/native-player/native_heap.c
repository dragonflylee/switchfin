// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Shared native-app allocator integration, originally used by the CTS runner. */
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *address, size_t size);
void __real_free(void *address);
int __real_posix_memalign(void **address, size_t alignment, size_t size);
size_t __real_malloc_usable_size(const void *address);

void *sceLibcMspaceCreate(const char *name, void *base, size_t size,
                          unsigned flags);
void *sceLibcMspaceMalloc(void *mspace, size_t size);
void *sceLibcMspaceCalloc(void *mspace, size_t count, size_t size);
void *sceLibcMspaceRealloc(void *mspace, void *address, size_t size);
void sceLibcMspaceFree(void *mspace, void *address);
int sceLibcMspacePosixMemalign(void *mspace, void **address, size_t alignment,
                              size_t size);
size_t sceLibcMspaceMallocUsableSize(const void *address);
int64_t sceKernelGetDirectMemorySize(void);
int32_t sceKernelAllocateDirectMemory(int64_t start, int64_t end, size_t length,
                                      size_t alignment, int type, int64_t *offset);
int32_t sceKernelMapDirectMemory(void **address, size_t length, int protection,
                                 int flags, int64_t offset, size_t alignment);
int32_t sceKernelReleaseDirectMemory(int64_t offset, size_t length);

/* Process-lifetime 1 GiB heap. Do not unmap underneath late C++ destructors. */
#define PSS_OPENGL_HEAP_SIZE (1024u * 1024u * 1024u)

static atomic_int pss_heap_state;
static void *pss_heap_base;
static void *pss_heap_mspace;
/* Owned-heap usable bytes only: not GPU mappings, foreign heaps or process RSS.
 * Relaxed snapshots are observations, not an allocator synchronization fence. */
static atomic_size_t pss_heap_live_bytes, pss_heap_peak_bytes, pss_heap_blocks;
static atomic_size_t pss_heap_failures, pss_heap_ambiguous_zero_reallocs;

static void pss_heap_resize_stats(size_t before, size_t after) {
  size_t live = after >= before
      ? atomic_fetch_add_explicit(&pss_heap_live_bytes, after - before,
                                  memory_order_relaxed) + after - before
      : atomic_fetch_sub_explicit(&pss_heap_live_bytes, before - after,
                                  memory_order_relaxed) - (before - after);
  size_t peak = atomic_load_explicit(&pss_heap_peak_bytes, memory_order_relaxed);
  while (live > peak && !atomic_compare_exchange_weak_explicit(
      &pss_heap_peak_bytes, &peak, live, memory_order_relaxed, memory_order_relaxed)) {}
}

static void *pss_heap_record_allocation(void *address, int nonzero) {
  if (address) {
    pss_heap_resize_stats(0, sceLibcMspaceMallocUsableSize(address));
    atomic_fetch_add_explicit(&pss_heap_blocks, 1, memory_order_relaxed);
  } else if (nonzero) {
    atomic_fetch_add_explicit(&pss_heap_failures, 1, memory_order_relaxed);
  }
  return address;
}

static int pss_heap_ready(void) {
  int state = atomic_load_explicit(&pss_heap_state, memory_order_acquire);
  if (state == 2)
    return 1;
  if (state != 0)
    return 0;

  int expected = 0;
  if (!atomic_compare_exchange_strong_explicit(
          &pss_heap_state, &expected, 1, memory_order_acq_rel,
          memory_order_acquire))
    return expected == 2;

  /* Flexible memory cannot hold the UHD decoder buffers. The direct-memory
   * type, protection and alignment are required by the native runtime.
   * If mapping fails after allocation, retain ownership until process exit. */
  int64_t direct_offset = -1;
  void *base = NULL;
  const int64_t direct_limit = sceKernelGetDirectMemorySize();
  if (direct_limit <= 0 ||
      sceKernelAllocateDirectMemory(0, direct_limit, PSS_OPENGL_HEAP_SIZE, 0x4000, 12,
                                    &direct_offset) != 0 || direct_offset < 0 ||
      sceKernelMapDirectMemory(&base, PSS_OPENGL_HEAP_SIZE, 0x33, 0, direct_offset,
                               0x4000) != 0 || base == NULL) {
    atomic_store_explicit(&pss_heap_state, -1, memory_order_release);
    return 0;
  }

  pss_heap_base = base;
  pss_heap_mspace =
      sceLibcMspaceCreate("PSS-OpenGL", base, PSS_OPENGL_HEAP_SIZE, 0);
  if (pss_heap_mspace == NULL) {
    pss_heap_base = NULL;
    if (munmap(base, PSS_OPENGL_HEAP_SIZE) == 0)
      (void)sceKernelReleaseDirectMemory(direct_offset, PSS_OPENGL_HEAP_SIZE);
    atomic_store_explicit(&pss_heap_state, -1, memory_order_release);
    return 0;
  }

  atomic_store_explicit(&pss_heap_state, 2, memory_order_release);
  return 1;
}

static int pss_heap_owns(const void *address) {
  /* Acquire publication before reading non-atomic heap metadata. */
  if (atomic_load_explicit(&pss_heap_state, memory_order_acquire) != 2)
    return 0;
  uintptr_t value = (uintptr_t)address;
  uintptr_t base = (uintptr_t)pss_heap_base;
  return value >= base && value - base < PSS_OPENGL_HEAP_SIZE;
}

void *__wrap_malloc(size_t size) {
  return pss_heap_ready()
      ? pss_heap_record_allocation(sceLibcMspaceMalloc(pss_heap_mspace, size), size != 0)
      : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size) {
  return pss_heap_ready()
      ? pss_heap_record_allocation(sceLibcMspaceCalloc(pss_heap_mspace, count, size),
                                   count != 0 && size != 0)
      : __real_calloc(count, size);
}

void *__wrap_realloc(void *address, size_t size) {
  if (address == NULL)
    return __wrap_malloc(size);
  if (!pss_heap_owns(address))
    return __real_realloc(address, size);
  size_t before = sceLibcMspaceMallocUsableSize(address);
  void *result = sceLibcMspaceRealloc(pss_heap_mspace, address, size);
  if (result)
    pss_heap_resize_stats(before, sceLibcMspaceMallocUsableSize(result));
  else if (size)
    atomic_fetch_add_explicit(&pss_heap_failures, 1, memory_order_relaxed);
  else
    /* Preserve platform realloc(p,0) behavior; NULL does not tell us whether
     * p was freed. Mark the counters inconclusive instead of guessing. */
    atomic_fetch_add_explicit(&pss_heap_ambiguous_zero_reallocs, 1, memory_order_relaxed);
  return result;
}

void __wrap_free(void *address) {
  if (pss_heap_owns(address)) {
    pss_heap_resize_stats(sceLibcMspaceMallocUsableSize(address), 0);
    atomic_fetch_sub_explicit(&pss_heap_blocks, 1, memory_order_relaxed);
    sceLibcMspaceFree(pss_heap_mspace, address);
  } else
    __real_free(address);
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size) {
  if (!pss_heap_ready())
    return __real_posix_memalign(address, alignment, size);
  int result = sceLibcMspacePosixMemalign(pss_heap_mspace, address, alignment, size);
  if (result == 0)
    pss_heap_record_allocation(*address, size != 0);
  else if (result == ENOMEM)
    atomic_fetch_add_explicit(&pss_heap_failures, 1, memory_order_relaxed);
  return result;
}

size_t __wrap_malloc_usable_size(const void *address) {
  return pss_heap_owns(address) ? sceLibcMspaceMallocUsableSize(address)
                                : __real_malloc_usable_size(address);
}

void pss_opengl_heap_stats_print(unsigned iteration) {
  if (iteration == 0) {
    int state = atomic_load_explicit(&pss_heap_state, memory_order_acquire);
    printf("[pss-opengl-cts] mspace state=%d base=%p size=%u\n",
           state, state == 2 ? pss_heap_base : NULL, PSS_OPENGL_HEAP_SIZE);
  }
}

/* The native import converter rejects unresolved weak application symbols.
 * A diagnostic build's strong definition overrides this default no-op. */
__attribute__((weak)) void pss_opengl_gpu_snapshot(const char *phase, unsigned iteration) {
  (void)phase;
  (void)iteration;
}
void pss_opengl_heap_snapshot(const char *phase, unsigned iteration) {
  pss_opengl_gpu_snapshot(phase, iteration);
  printf("[pss-opengl-heap] phase=%s sample=%u state=%d live_bytes=%zu peak_bytes=%zu "
         "blocks=%zu failures=%zu ambiguous_zero_reallocs=%zu\n", phase, iteration,
         atomic_load_explicit(&pss_heap_state, memory_order_acquire),
         atomic_load_explicit(&pss_heap_live_bytes, memory_order_relaxed),
         atomic_load_explicit(&pss_heap_peak_bytes, memory_order_relaxed),
         atomic_load_explicit(&pss_heap_blocks, memory_order_relaxed),
         atomic_load_explicit(&pss_heap_failures, memory_order_relaxed),
         atomic_load_explicit(&pss_heap_ambiguous_zero_reallocs, memory_order_relaxed));
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only extension to BlackBearReloaded's pinned GPL-3.0-or-later app_heap.c.
// Included AFTER the unchanged allocator in one consumer translation unit.
#include "utils/ps5_native_heap.h"
#include <stddef.h>

_Static_assert(sizeof(size_t) == 8, "observer requires the pinned 64-bit allocator");
_Static_assert(sizeof(struct ps5_native_heap_observation) == 48, "observer layout");
_Static_assert(offsetof(struct ps5_native_heap_observation, live_bytes) == 8, "observer layout");
_Static_assert(offsetof(struct ps5_native_heap_observation, ambiguous_zero_reallocs) == 40, "observer layout");

void ps5_native_heap_observe(struct ps5_native_heap_observation* result) {
    if (!result) return;
    result->state = atomic_load_explicit(&pss_heap_state, memory_order_acquire);
    result->capacity_bytes = PSS_OPENGL_HEAP_SIZE;
    result->live_bytes = atomic_load_explicit(&pss_heap_live_bytes, memory_order_relaxed);
    result->peak_bytes = atomic_load_explicit(&pss_heap_peak_bytes, memory_order_relaxed);
    result->blocks = atomic_load_explicit(&pss_heap_blocks, memory_order_relaxed);
    result->failures = atomic_load_explicit(&pss_heap_failures, memory_order_relaxed);
    result->ambiguous_zero_reallocs = atomic_load_explicit(&pss_heap_ambiguous_zero_reallocs, memory_order_relaxed);
}
