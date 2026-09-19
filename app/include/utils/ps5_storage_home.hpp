#pragma once

// where downloaded media lives.
//
// The sandbox home is a fixed-size image at /download0; a download can only be
// as large as it. If the process is promoted out of the sandbox (see
// ps5_unjail.hpp) then /data is reachable, backed by the full internal drive,
// and downloads can live at /data/switchfin/downloads instead. This resolves
// which of the two to use, once, and remembers it. The decision is made from
// evidence: the elevated path is chosen only when a real create/write/sync/
// unlink probe of it succeeds, never merely because promotion was attempted.
//
// The legacy sandbox path is supplied by the caller (it is configDir() +
// "/downloads"), so this header owns no assumption about where config lives.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils/ps5_unjail.hpp"

namespace ps5::storage {

inline constexpr const char* elevatedDownloads = "/data/switchfin/downloads";

// The real filesystem location of this title's sandbox. Once the process is
// promoted (unjail) the jail root is the real root, so the former /app0 and
// /download0 are reachable here; a visible app0 is how promotion is detected.
inline constexpr const char* sandboxPrefix = "/mnt/sandbox/PPSA99015_000";
inline constexpr const char* promotedApp0 = "/mnt/sandbox/PPSA99015_000/app0";

// The numeric account of the decision, for the journal. No paths, no message.
struct Report {
    bool resolved = false;
    int unjail = 0;        // ps5::unjail::Outcome as int
    int probeResult = -2;  // 0 writable, -1 a step failed, -2 not attempted
    int probeErrno = 0;
    bool elevated = false; // true = /data chosen, false = sandbox home
    std::string sandboxRoot;   // the real jail root once promoted, else empty
};

struct ProbeSystem {
    static int makeDirectory(const char* path) noexcept { return ::mkdir(path, 0700); }
    static bool isDirectory(const char* path) noexcept {
        struct stat state{};
        return ::stat(path, &state) == 0 && S_ISDIR(state.st_mode);
    }
    static int pid() noexcept { return static_cast<int>(::getpid()); }
    static int create(const char* path) noexcept { return ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600); }
    static ssize_t write(int fd, const void* buffer, size_t length) noexcept {
        return ::write(fd, buffer, length);
    }
    static int sync(int fd) noexcept { return ::fsync(fd); }
    static void close(int fd) noexcept { ::close(fd); }
    static int unlink(const char* path) noexcept { return ::unlink(path); }
};

// Ordinary POSIX writability probe of the elevated directory, creating the
// parents it needs. Leaves nothing behind. Returns 0 on a full round trip, or
// -1 with `error` set to the errno of the step that stopped it.
template <class Ops = ProbeSystem>
inline int probeElevated(int& error) noexcept {
    const int saved = errno;
    error = 0;
    const char* const parents[] = {"/data", "/data/switchfin", elevatedDownloads};
    for (const char* dir : parents) {
        errno = 0;
        if (Ops::makeDirectory(dir) < 0 && errno != EEXIST) { error = errno; errno = saved; return -1; }
    }
    if (!Ops::isDirectory(elevatedDownloads)) { error = ENOTDIR; errno = saved; return -1; }

    // Deterministic name + plain create: the probe runs at the storage-home
    // stage, before secure-random-init, so it must not use mkstemp -- the
    // custom mkstemp draws random bytes and returns EIO that early. O_TRUNC
    // overwrites any leftover probe file from a prior run.
    char path[80];
    std::snprintf(path, sizeof(path), "%s/.switchfin-probe-%d", elevatedDownloads, Ops::pid());
    errno = 0;
    const int fd = Ops::create(path);
    if (fd < 0) { error = errno; errno = saved; return -1; }

    constexpr char payload[] = "switchfin";
    int failure = 0;
    if (Ops::write(fd, payload, sizeof(payload) - 1) != static_cast<ssize_t>(sizeof(payload) - 1))
        failure = errno ? errno : EIO;
    // fsync is durability, not a writability signal: some PS5 filesystems
    // reject it while accepting writes, so treat its failure as success.
    if (!failure) Ops::sync(fd);
    Ops::close(fd);
    Ops::unlink(path);
    errno = saved;
    if (failure) { error = failure; return -1; }
    return 0;
}

// Process-wide, resolved once. Not synchronised: resolve() is called from
// startup before any worker thread, and read-only thereafter.
inline Report& state() {
    static Report report;
    return report;
}

// Attempts promotion, then probes /data. Idempotent: only the first call acts.
template <class UnjailOps = ps5::unjail::System, class ProbeOps = ProbeSystem>
inline const Report& resolve() {
    Report& report = state();
    if (report.resolved) return report;
    report.unjail = static_cast<int>(ps5::unjail::request<UnjailOps>());
    // The daemon's reply is advisory: promotion un-chroots the whole process,
    // so whether it took is read from the filesystem -- the real sandbox app0
    // is reachable only once fd_jdir points at the true root -- rather than
    // trusted from a reply that has answered "refused" on a promotion that
    // took. Only once promoted is /data reachable, so probe it for the home.
    if (ProbeOps::isDirectory(promotedApp0)) {
        report.sandboxRoot = sandboxPrefix;
        report.probeResult = probeElevated<ProbeOps>(report.probeErrno);
        report.elevated = report.probeResult == 0;
    }
    report.resolved = true;
    return report;
}

// User-chosen download-location root (e.g. "/mnt/usb0"), empty = the default
// /data. Set from the DOWNLOAD_LOCATION config after resolve(). Only honoured
// when promoted+elevated and the root is still a mounted, writable directory
// (a removed drive falls back to /data so downloads never break).
inline std::string& downloadOverrideRoot() {
    static std::string root;
    return root;
}
inline bool overrideUsable(const std::string& root) {
    if (root.empty()) return false;
    struct ::stat st{};
    if (::stat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    // A removable mount that is gone shares its parent's device id; a live mount
    // differs. /data (and /user/data) are always usable when elevated.
    if (root.rfind("/mnt/", 0) == 0) {
        struct ::stat parent{};
        const std::string par = root.substr(0, root.find_last_of('/'));
        if (::stat(par.c_str(), &parent) == 0 && st.st_dev == parent.st_dev) return false;
    }
    return true;
}

// The download directory to use. Before resolve(), or whenever /data was not
// chosen, this is the sandbox path the caller passed.
inline std::string downloadHome(const std::string& sandbox) {
    const Report& report = state();
    if (!(report.resolved && report.elevated)) return sandbox;
    const std::string& ov = downloadOverrideRoot();
    if (overrideUsable(ov)) return ov + "/switchfin/downloads";
    return std::string(elevatedDownloads);
}

// Empty while jailed; the real sandbox root prefix once promoted out of it. Set
// by resolve(), so callers must run after startup's storage-home stage.
inline const std::string& sandboxRoot() { return state().sandboxRoot; }
inline bool promoted() { return !state().sandboxRoot.empty(); }

}  // namespace ps5::storage
