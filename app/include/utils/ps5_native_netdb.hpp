#pragma once

namespace ps5_native_netdb {
// Call once on the main thread before starting consumers. Uses directly linked
// public Net services and requires all seven imported entries before calling
// sceNetInit. A missing entry reports -1 at network-api-check. Readiness is
// published only after sceNetInit returns 0.
// Initialization is never retried; shared Net remains active until process exit.
// Ambiguous resolver/pool destruction retains those resources until process
// exit and permanently disables new DNS allocations; numeric addresses work.
bool initialize() noexcept;
int last_error() noexcept;
// Fixed initialization phase, for main-thread failure reporting without secrets.
const char* initialization_stage() noexcept;
} // namespace ps5_native_netdb
