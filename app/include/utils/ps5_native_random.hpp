#pragma once

#include <cstddef>
#include <string>

namespace ps5_native_random {
// Call once on the main thread before starting workers. Checks the SDK's linked
// sysctl entry and probes its read-only kernel entropy query before publishing
// readiness. Requests at most 32 bytes per call; no retry or entropy fallback.
bool initialize() noexcept;
// Requires successful initialization for nonempty output. On failure, erase the
// entire valid caller buffer, including bytes preceding a failed service chunk.
bool fill(void* output, std::size_t size) noexcept;
// Most recently observed failure; successful worker calls leave it unchanged.
// API failures retain their return value; adapter errors -3/-4 mean unavailable
// entry or non-exact returned length, respectively.
int last_error() noexcept;
// Fixed initialization phase, for main-thread failure reporting without secrets.
const char* initialization_stage() noexcept;
// len is a byte count; zero is valid. Returns exactly 2 * len lowercase digits.
std::string hex(int len);
} // namespace ps5_native_random
