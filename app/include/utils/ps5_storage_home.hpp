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
#include <atomic>
#include <mutex>
#include <vector>
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

struct DownloadLocation {
    std::string root;
    std::string label;
    std::string directory;
};

struct LocationSystem {
    static bool mounted(const std::string& root) {
        struct stat mount{}, parent{};
        const auto parentPath = root.substr(0, root.find_last_of('/'));
        return ::stat(root.c_str(), &mount) == 0 && S_ISDIR(mount.st_mode) &&
            ::stat(parentPath.c_str(), &parent) == 0 && mount.st_dev != parent.st_dev;
    }

    static bool writable(const std::string& directory) {
        // Downloads require new item directories, not just access to an already
        // open file. Promoted sandbox credentials can deny mkdir despite chmod.
        const auto parent = directory.substr(0, directory.find_last_of('/'));
        for (const auto& path : {parent, directory}) {
            if (::mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        static std::atomic<unsigned> sequence{0};
        std::string probe;
        bool created = false;
        for (int retry = 0; retry < 8; ++retry) {
            probe = directory + "/.switchfin-location-" + std::to_string(::getpid()) + "-" +
                std::to_string(sequence.fetch_add(1));
            if (::mkdir(probe.c_str(), 0700) == 0) { created = true; break; }
            if (errno != EEXIST) return false;
        }
        if (!created) return false;
        const auto file = probe + "/write";
        const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        bool ok = false;
        if (fd >= 0) {
            ssize_t written;
            do { written = ::write(fd, "x", 1); } while (written < 0 && errno == EINTR);
            ok = written == 1;
            if (::close(fd) != 0) ok = false;
            if (::unlink(file.c_str()) != 0) ok = false;
        }
        if (::rmdir(probe.c_str()) != 0) ok = false;
        return ok;
    }
};

template <class Ops = LocationSystem>
inline std::vector<DownloadLocation> discoverDownloadLocations(const std::string& sandbox) {
    std::vector<DownloadLocation> locations;
    if (Ops::writable(sandbox)) locations.push_back({"sandbox", "Internal (sandbox)", sandbox});
    const auto& report = state();
    if (!report.resolved || report.sandboxRoot.empty()) return locations;
    if (report.elevated && Ops::writable(elevatedDownloads))
        locations.push_back({"/data", "Internal (/data)", elevatedDownloads});
    for (const auto& type : {std::string("usb"), std::string("ext")}) {
        const int count = type == "usb" ? 8 : 2;
        for (int i = 0; i < count; ++i) {
            const auto name = type + std::to_string(i);
            const auto root = "/mnt/" + name;
            const auto directory = root + "/switchfin/downloads";
            if (Ops::mounted(root) && Ops::writable(directory))
                locations.push_back({root, "External (" + name + ")", directory});
        }
    }
    return locations;
}

// A saved empty value meant /data in older releases. New installs prefer the
// sandbox. Falling back never replaces the saved preference or moves media.
inline int downloadLocationIndex(const std::vector<DownloadLocation>& locations, const std::string& preference) {
    const std::string root = preference.empty() ? "/data" : preference;
    for (size_t i = 0; i < locations.size(); ++i)
        if (locations[i].root == root) return static_cast<int>(i);
    // Only internal storage is an automatic fallback; never choose a different
    // external drive merely because the saved one is disconnected.
    for (size_t i = 0; i < locations.size(); ++i)
        if (locations[i].root == "sandbox" || locations[i].root == "/data") return static_cast<int>(i);
    return -1;
}

struct DownloadSelection {
    std::mutex mutex;
    std::string preference = "sandbox";
    std::vector<DownloadLocation> locations;
};

inline DownloadSelection& downloadSelection() {
    static DownloadSelection selection;
    return selection;
}

inline void setDownloadLocation(const std::string& root) {
    auto& selection = downloadSelection();
    std::lock_guard<std::mutex> lock(selection.mutex);
    selection.preference = root;
}

inline std::string downloadLocationPreference() {
    auto& selection = downloadSelection();
    std::lock_guard<std::mutex> lock(selection.mutex);
    return selection.preference;
}

inline void refreshDownloadLocations(const std::string& sandbox) {
    auto locations = discoverDownloadLocations(sandbox);
    auto& selection = downloadSelection();
    std::lock_guard<std::mutex> lock(selection.mutex);
    selection.locations = std::move(locations);
}

inline std::vector<DownloadLocation> downloadLocations() {
    auto& selection = downloadSelection();
    std::lock_guard<std::mutex> lock(selection.mutex);
    std::vector<DownloadLocation> locations;
    for (const auto& location : selection.locations) {
        if (location.root.rfind("/mnt/", 0) == 0 && !LocationSystem::mounted(location.root)) continue;
        locations.push_back(location);
    }
    return locations;
}

inline std::string downloadLocationLabel() {
    const auto locations = downloadLocations();
    const auto preference = downloadLocationPreference();
    const int index = downloadLocationIndex(locations, preference);
    if (index < 0) return "No writable download location";
    const auto& location = locations[index];
    return location.label + (location.root == (preference.empty() ? "/data" : preference) ? "" : " (fallback)");
}

// Probes run at startup and when opening the selector, not on progress updates.
// Recheck removable mounts here so unplugging a drive cannot target /mnt itself.
inline std::string downloadHome(const std::string& sandbox) {
    const auto locations = downloadLocations();
    const int index = downloadLocationIndex(locations, downloadLocationPreference());
    return index < 0 ? sandbox : locations[index].directory;
}

// Empty while jailed; the real sandbox root prefix once promoted out of it. Set
// by resolve(), so callers must run after startup's storage-home stage.
inline const std::string& sandboxRoot() { return state().sandboxRoot; }
inline bool promoted() { return !state().sandboxRoot.empty(); }

}  // namespace ps5::storage
