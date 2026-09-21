#pragma once

// Main-thread startup diagnostics, independent of SDL, C++ allocation and Logger.
// Pass only static stage literals: current_stage retains the supplied pointer.
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <system_error>
#include <fcntl.h>
#include "utils/ps5_native_module.hpp"

extern "C" {
int sceKernelOpen(const char*, int, unsigned);
long sceKernelWrite(int, const void*, std::size_t);
int sceKernelFsync(int);
int sceKernelClose(int);
int sceKernelRename(const char*, const char*);
int sceKernelUsleep(unsigned);
}

namespace ps5_native_startup {
namespace detail {
inline constexpr const char* path = "/download0/switchfin-native-startup.log";
inline constexpr const char* previous = "/download0/switchfin-native-startup.log.previous";
// Include bounded frame distributions plus at most 153 owned-heap samples.
// Later events may still be omitted at this cap; absence is not success evidence.
inline constexpr std::size_t capacity = 256 * 1024;
struct State {
    int fd = -1;
    int error = 0;
    bool attempted = false;
    bool syncDeferred = false;
    bool pendingSync = false;
    std::size_t bytes = 0;
    const char* stage = "entry";
};
inline State state;

inline void close_file() {
    if (state.fd < 0) return;
    const int fd = state.fd;
    state.fd = -1;
    state.pendingSync = false;
    const int result = sceKernelClose(fd);
    if (result < 0) state.error = result;
}

inline void sync_file() {
    if (state.fd < 0 || !state.pendingSync) return;
    state.pendingSync = false;
    const int synced = sceKernelFsync(state.fd);
    if (synced < 0) { state.error = synced; close_file(); }
}

// Main-thread numeric report batching only. Writes keep their existing bounds
// and error handling; the outermost scope syncs the completed report once.
// Other checkpoints remain immediately synced. No renderer sync is affected.
class Batch {
    bool previous;
public:
    Batch() noexcept : previous(state.syncDeferred) { state.syncDeferred = true; }
    ~Batch() { state.syncDeferred = previous; if (!previous) sync_file(); }
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
};

inline void open_file() {
    if (state.attempted) return;
    state.attempted = true;
    constexpr int flags = O_WRONLY | O_CREAT | O_EXCL;
    // Match the existing native diagnostic logs: ftpsrv cannot collect a 0600
    // file owned by this sandbox. Stage labels and details are redacted below.
    int fd = sceKernelOpen(path, flags, 0644);
    if (fd < 0) {
        // Never truncate the previous run before successfully preserving it.
        const int renamed = sceKernelRename(path, previous);
        if (renamed < 0) { state.error = renamed; return; }
        fd = sceKernelOpen(path, flags, 0644);
    }
    if (fd < 0) { state.error = fd; return; }
    state.fd = fd;
}

inline void line(const char* format, ...) {
    open_file();
    if (state.fd < 0 || state.bytes == capacity) return;
    char buffer[768];
    va_list args;
    va_start(args, format);
    const int count = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (count < 0) return;
    std::size_t length = static_cast<std::size_t>(count);
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    if (length > capacity - state.bytes) length = capacity - state.bytes;
    std::size_t offset = 0;
    while (offset < length) {
        const long wrote = sceKernelWrite(state.fd, buffer + offset, length - offset);
        if (wrote <= 0 || static_cast<std::size_t>(wrote) > length - offset) {
            state.error = wrote < 0 ? static_cast<int>(wrote) : -1;
            close_file();
            return;
        }
        offset += static_cast<std::size_t>(wrote);
        state.bytes += static_cast<std::size_t>(wrote);
        state.pendingSync = true;
    }
    if (!state.syncDeferred) sync_file();
}

inline char lower(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
inline bool contains(const char* text, std::size_t size, const char* needle) {
    for (std::size_t i = 0; i < size; ++i) {
        std::size_t j = 0;
        while (needle[j] && i + j < size && lower(text[i + j]) == needle[j]) ++j;
        if (!needle[j]) return true;
    }
    return false;
}

inline const char* safe_detail(const char* input, char (&output)[257]) {
    if (!input) return "(no detail)";
    std::size_t size = 0;
    while (size < 2048 && input[size]) ++size;
    // Withhold oversized details entirely: a secret may follow the prefix.
    if (size == 2048) return "[detail withheld]";
    constexpr const char* words[] = {"auth", "token", "password", "passwd", "secret", "apikey",
        "api_key", "api-key", "cookie", "bearer", "http", "www."};
    for (const char* word : words) {
        if (contains(input, size, word)) return "[detail withheld]";
    }
    // Deliberately conservative for URLs, credentials, queries and file paths.
    // The caller can report a numeric error and a fixed stage independently.
    for (std::size_t i = 0; i < size; ++i) {
        if (input[i] == '/' || input[i] == '\\' || input[i] == '@' ||
            input[i] == '?' || input[i] == '=') return "[detail withheld]";
        if (input[i] == '.' && i + 1 < size && lower(input[i + 1]) >= 'a' &&
            lower(input[i + 1]) <= 'z') return "[detail withheld]";
    }
    const std::size_t copy = size > 256 ? 256 : size;
    for (std::size_t i = 0; i < copy; ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        output[i] = c < 32 || c == 127 ? ' ' : input[i];
    }
    output[copy] = '\0';
    return output;
}
} // namespace detail

inline const char* current_stage() { return detail::state.stage; }
inline int last_error() { return detail::state.error; }
inline void checkpoint(const char* static_stage) {
    detail::state.stage = static_stage ? static_stage : "(unknown)";
    detail::line("STAGE %.160s\n", detail::state.stage);
}
inline void failure(const char* message) {
    char sanitized[257];
    detail::line("FAIL stage=%.160s detail=%s\n", current_stage(), detail::safe_detail(message, sanitized));
}
inline void failure_code(const char* description, int code) {
    char sanitized[257];
    // Keep the numeric service result outside the strictly redacted description.
    detail::line("FAIL stage=%.160s detail=%s status=0x%08x\n", current_stage(),
        detail::safe_detail(description, sanitized), static_cast<unsigned>(code));
}
inline void failure_error_code(const char* description, const std::error_code& code) noexcept {
    char sanitized[257];
    // Category names/messages are arbitrary virtual calls and may expose paths
    // or credentials. Identify only the standard singleton objects by address.
    const auto* category = &code.category();
    const char* label = category == &std::generic_category() ? "generic" :
        category == &std::system_category() ? "system" : "other";
    detail::line("FAIL stage=%.160s detail=%s category=%s code=%d\n", current_stage(),
        detail::safe_detail(description, sanitized), label, code.value());
}
inline void lookup(const char* description, const ps5_native_module::LookupEvidence& info) noexcept {
    char sanitized[257];
    const char* method = info.method == ps5_native_module::LookupMethod::Name ? "name" :
        info.method == ps5_native_module::LookupMethod::Identifier ? "identifier" : "none";
    detail::line("LOOKUP stage=%.160s detail=%s method=%s name-status=0x%08x name-address=%d identifier-attempted=%d identifier-status=0x%08x identifier-address=%d\n",
        current_stage(), detail::safe_detail(description, sanitized), method,
        static_cast<unsigned>(info.name_status), info.name_address_present, info.identifier_attempted,
        static_cast<unsigned>(info.identifier_status), info.identifier_address_present);
}
inline void finish(int status) {
    detail::line("FINISH stage=%.160s status=%d\n", current_stage(), status);
    detail::close_file();
}
// Keep the sandbox mounted after recording startup failure;
// the user closes the application normally after collecting its mounted logs.
[[noreturn]] inline void hold_failure() {
    detail::line("HOLD stage=%.160s startup failure; user may close application\n", current_stage());
    detail::close_file();
    for (;;) (void)sceKernelUsleep(1000000);
}
} // namespace ps5_native_startup
