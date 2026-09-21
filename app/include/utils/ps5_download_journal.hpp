#pragma once
// sandboxRoot() for promotion-aware log path.
#include "utils/ps5_storage_home.hpp"

#include "utils/ps5_native_startup.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <mutex>
#include <string>

namespace ps5::downloads {

// bounded numeric record of download attempts, written where the other
// journals live so it can be read back without entering the app's private
// directory. Numeric only: item identifiers are reduced to a 32-bit digest, and
// no URL, path, title or server message is ever written. Written from the
// download worker as well as the UI thread, so every call takes the lock.
// I/O failure disables the record, never the download.
class DownloadJournal {
    // Recording must never change what the caller sees in errno.
    struct PreserveErrno { int saved = errno; ~PreserveErrno() { errno = saved; } };

public:
    // Ordered, so a stalled attempt is visible as the phase it stopped at.
    enum Phase : unsigned {
        Queued = 1,
        Started = 2,
        DirectoryReady = 3,
        TransferBegin = 4,
        TransferProgress = 5,
        TransferEnd = 6,
        Completed = 7,
        Failed = 8,
        Cancelled = 9,
    };
    static constexpr const char* path = "/download0/switchfin-native-download.log";
    static constexpr const char* previous = "/download0/switchfin-native-download.log.previous";
    static constexpr size_t capacity = 32768;
    static constexpr unsigned rowLimit = 240;

    static DownloadJournal& instance() {
        static DownloadJournal journal;
        return journal;
    }

    // A non-reversible digest, only for correlating rows of one attempt.
    static uint32_t digest(const char* text, size_t size) {
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < size; ++i) {
            hash ^= static_cast<unsigned char>(text[i]);
            hash *= 16777619u;
        }
        return hash;
    }

    void record(uint32_t item, Phase phase, int code, uint64_t bytes, uint64_t total, uint64_t now) {
        PreserveErrno preserve;
        std::lock_guard<std::mutex> lock(mutex);
        if (failed || rows >= rowLimit) return;
        if (fd < 0 && !open(now)) return;
        char row[192];
        const int used = std::snprintf(row, sizeof(row),
            "DL seq=%u item=%08x phase=%u code=%d bytes=%llu total=%llu elapsed-us=%llu\n",
            ++rows, item, static_cast<unsigned>(phase), code,
            static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(total),
            static_cast<unsigned long long>(now > started ? now - started : 0));
        if (used <= 0 || static_cast<size_t>(used) >= sizeof(row)) return;
        write(row, static_cast<size_t>(used));
    }

    // One index entry as the filesystem answers for it. The Downloads tab reads
    // the index and the browser reads a path, so an entry that is listed in one
    // and absent from the other is only explained by asking stat directly.
    void recordScan(uint32_t item, int status, int result, int error, uint64_t size, int named,
                    uint64_t now) {
        PreserveErrno preserve;
        std::lock_guard<std::mutex> lock(mutex);
        if (failed || rows >= rowLimit) return;
        if (fd < 0 && !open(now)) return;
        char row[224];
        const int used = std::snprintf(row, sizeof(row),
            "DLSCAN seq=%u item=%08x status=%d stat=%d errno=%d size=%llu named=%d elapsed-us=%llu\n",
            ++rows, item, status, result, error, static_cast<unsigned long long>(size), named,
            static_cast<unsigned long long>(now > started ? now - started : 0));
        if (used <= 0 || static_cast<size_t>(used) >= sizeof(row)) return;
        write(row, static_cast<size_t>(used));
    }

    // The download home decision, recorded once at startup: whether promotion
    // out of the sandbox was offered, whether the /data probe wrote and synced,
    // and which home won. Numeric only.
    void recordHome(int unjail, int probe, int error, int elevated, uint64_t now) {
        PreserveErrno preserve;
        std::lock_guard<std::mutex> lock(mutex);
        if (failed || rows >= rowLimit) return;
        if (fd < 0 && !open(now)) return;
        char row[192];
        const int used = std::snprintf(row, sizeof(row),
            "DLHOME seq=%u unjail=%d probe=%d errno=%d elevated=%d elapsed-us=%llu\n",
            ++rows, unjail, probe, error, elevated,
            static_cast<unsigned long long>(now > started ? now - started : 0));
        if (used <= 0 || static_cast<size_t>(used) >= sizeof(row)) return;
        write(row, static_cast<size_t>(used));
    }

    // A directory the browser would have to reach, counted from inside the
    // application -- the only process that may read it, since it is 0700 under
    // a uid FTP does not have. Counts only: no name is recorded.
    void recordRoot(unsigned root, int result, int error, uint64_t entries, uint64_t directories,
                    uint64_t now) {
        PreserveErrno preserve;
        std::lock_guard<std::mutex> lock(mutex);
        if (failed || rows >= rowLimit) return;
        if (fd < 0 && !open(now)) return;
        char row[224];
        const int used = std::snprintf(row, sizeof(row),
            "DLROOT seq=%u root=%u result=%d errno=%d entries=%llu directories=%llu elapsed-us=%llu\n",
            ++rows, root, result, error, static_cast<unsigned long long>(entries),
            static_cast<unsigned long long>(directories),
            static_cast<unsigned long long>(now > started ? now - started : 0));
        if (used <= 0 || static_cast<size_t>(used) >= sizeof(row)) return;
        write(row, static_cast<size_t>(used));
    }

    // The write that ended one transfer: the platform reason, where it stopped
    // and how much the call asked for. libcurl reduces all of this to
    // WRITE_ERROR, which names the category and not the cause.
    void recordFile(uint32_t item, int error, int stream, uint64_t offset, uint64_t requested,
                    uint64_t written, uint64_t now) {
        PreserveErrno preserve;
        std::lock_guard<std::mutex> lock(mutex);
        if (failed || rows >= rowLimit) return;
        if (fd < 0 && !open(now)) return;
        char row[256];
        const int used = std::snprintf(row, sizeof(row),
            "DLFILE seq=%u item=%08x errno=%d stream=%d offset=%llu requested=%llu written=%llu "
            "elapsed-us=%llu\n",
            ++rows, item, error, stream, static_cast<unsigned long long>(offset),
            static_cast<unsigned long long>(requested), static_cast<unsigned long long>(written),
            static_cast<unsigned long long>(now > started ? now - started : 0));
        if (used <= 0 || static_cast<size_t>(used) >= sizeof(row)) return;
        write(row, static_cast<size_t>(used));
    }

    // The transport outcome of one failed transfer. libcurl's result code and
    // the observation accompanying it, which otherwise reaches only a log this
    // console will not surrender. Numeric, like every other row: no endpoint,
    // address, header or message. Shares the sequence and the row budget.
    void recordNetwork(uint32_t item, int result, long osError, unsigned stage, unsigned errorStage,
                       int errorBuffer, unsigned valid, long status, uint64_t lookup, uint64_t connect,
                       uint64_t total, uint64_t now) {
        PreserveErrno preserve;
        std::lock_guard<std::mutex> lock(mutex);
        if (failed || rows >= rowLimit) return;
        if (fd < 0 && !open(now)) return;
        char row[384];
        const int used = std::snprintf(row, sizeof(row),
            "DLNET seq=%u item=%08x result=%d os-errno=%lld stage=%u err-stage=%u err-buffer=%d "
            "valid=%u http=%lld lookup-us=%llu connect-us=%llu total-us=%llu elapsed-us=%llu\n",
            ++rows, item, result, static_cast<long long>(osError), stage, errorStage, errorBuffer,
            valid, static_cast<long long>(status), static_cast<unsigned long long>(lookup),
            static_cast<unsigned long long>(connect), static_cast<unsigned long long>(total),
            static_cast<unsigned long long>(now > started ? now - started : 0));
        if (used <= 0 || static_cast<size_t>(used) >= sizeof(row)) return;
        write(row, static_cast<size_t>(used));
    }

private:
    DownloadJournal() = default;
    ~DownloadJournal() { close(); }

    bool open(uint64_t now) {
        // Once promoted, /download0 is gone; write under the real sandbox root
        // so download faults are still recorded. sandboxRoot() is empty (=no
        // prefix) when jailed, so the path is unchanged then.
        const std::string activePath = ps5::storage::sandboxRoot() + "/download0/switchfin-native-download.log";
        const std::string activePrev = activePath + ".previous";
        const char* const path = activePath.c_str();
        const char* const previous = activePrev.c_str();
        constexpr int flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW;
        fd = sceKernelOpen(path, flags, 0644);
        if (fd < 0) {
            if (sceKernelRename(path, previous) < 0) { failed = true; return false; }
            fd = sceKernelOpen(path, flags, 0644);
        }
        if (fd < 0) { failed = true; return false; }
        bytes = 0; rows = 0; started = now;
        char header[192];
        const int used = std::snprintf(header, sizeof(header),
            "DOWNLOAD marker=native-startup-unversioned schema=1 start-us=%llu\n",
            static_cast<unsigned long long>(now));
        if (used <= 0 || static_cast<size_t>(used) >= sizeof(header)) { failed = true; close(); return false; }
        write(header, static_cast<size_t>(used));
        return fd >= 0;
    }

    void write(const char* row, size_t size) {
        if (fd < 0) return;
        if (size > capacity - bytes) { close(); return; }
        size_t offset = 0;
        while (offset < size) {
            const auto n = sceKernelWrite(fd, row + offset, size - offset);
            if (n <= 0 || static_cast<size_t>(n) > size - offset) { failed = true; close(); return; }
            offset += static_cast<size_t>(n); bytes += static_cast<size_t>(n);
        }
        if (sceKernelFsync(fd) < 0) { failed = true; close(); }
    }

    void close() {
        if (fd >= 0) { sceKernelClose(fd); fd = -1; }
    }

    std::mutex mutex;
    int fd = -1;
    size_t bytes = 0;
    unsigned rows = 0;
    uint64_t started = 0;
    bool failed = false;
};

// A monotonic clock of its own: the record must not depend on, or disturb, the
// application's frame clock, and it is written from a worker thread.
inline uint64_t monotonicUsec() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline void noteScan(const std::string& itemId, int status, int result, int error, uint64_t size,
                     bool named) {
    DownloadJournal::instance().recordScan(DownloadJournal::digest(itemId.data(), itemId.size()), status,
                                           result, error, size, named ? 1 : 0, monotonicUsec());
}

inline void noteRoot(unsigned root, int result, int error, uint64_t entries, uint64_t directories) {
    DownloadJournal::instance().recordRoot(root, result, error, entries, directories, monotonicUsec());
}
inline void noteHome(int unjail, int probe, int error, bool elevated) {
    DownloadJournal::instance().recordHome(unjail, probe, error, elevated ? 1 : 0, monotonicUsec());
}

inline void noteFile(const std::string& itemId, int error, int stream, uint64_t offset,
                     uint64_t requested, uint64_t written) {
    DownloadJournal::instance().recordFile(DownloadJournal::digest(itemId.data(), itemId.size()), error,
                                           stream, offset, requested, written, monotonicUsec());
}

// Scalars, not the snapshot type: the journal stays independent of the HTTP
// diagnostics header, which owns a libcurl dependency.
inline void noteNetwork(const std::string& itemId, int result, long osError, unsigned stage,
                        unsigned errorStage, int errorBuffer, unsigned valid, long status,
                        uint64_t lookup, uint64_t connect, uint64_t total) {
    DownloadJournal::instance().recordNetwork(DownloadJournal::digest(itemId.data(), itemId.size()), result,
                                              osError, stage, errorStage, errorBuffer, valid, status, lookup,
                                              connect, total, monotonicUsec());
}

inline void note(const std::string& itemId, DownloadJournal::Phase phase, int code = 0,
                 uint64_t bytes = 0, uint64_t total = 0) {
    DownloadJournal::instance().record(DownloadJournal::digest(itemId.data(), itemId.size()), phase, code, bytes,
                                       total, monotonicUsec());
}

} // namespace ps5::downloads
