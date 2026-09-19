#pragma once

// PS5 only. Opens a bounded numeric journal before workers start. The descriptor
// lives until process exit so a late worker cannot race a close/reused fd.
// No signal handler, allocator policy change or application control is involved.
int ps5_native_heap_failure_initialize() noexcept;

// First occurrence of each boundary in this process only. These observations
// are global approximate heap state, not per-component allocation ownership.
enum class Ps5HeapPhase : unsigned {
    InitBegin = 1, HandleCreated, Initialized, Configured, RenderContextCreated,
    LoadBefore, LoadAfter, StartFile, FileLoaded, RenderBegin, RenderEnd, EndFile,
};
// No allocation, file I/O, clock query or reset. Completed slots are immutable
// and may be read safely by the thread reporting the first allocation failure.
void ps5_native_heap_checkpoint(Ps5HeapPhase phase) noexcept;
