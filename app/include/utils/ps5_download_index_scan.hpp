#pragma once

#include "utils/ps5_download_removal.hpp"
#include <cerrno>
#include <cstdint>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

namespace ps5::downloads {

// what the download index claims, checked once against what the
// filesystem actually holds. A completed entry that plays from the Downloads
// tab but cannot be found in Local Storage is either a file the browser never
// reaches or a file that is not there, and only stat separates the two. The
// application is the only process that may read its own directory -- it is
// 0700 under a uid FTP does not have -- so the check has to happen here.
//
// Nothing here retains or reports a name: the caller turns the result into the
// journal's numeric row.
struct ScanOps {
    static int size(const char* path, uint64_t& out) noexcept {
        struct stat state;
        if (::stat(path, &state) != 0) return -1;
        out = state.st_size > 0 ? static_cast<uint64_t>(state.st_size) : 0;
        return 0;
    }
    // opendir/readdir/closedir/lstat, for the same reason downloads avoid
    // std::filesystem here: openat and fdopendir are undefined in this image.
    static void* openDirectory(const char* path) noexcept { return ::opendir(path); }
    static const char* readEntry(void* handle) noexcept {
        const dirent* entry = ::readdir(static_cast<DIR*>(handle));
        return entry ? entry->d_name : nullptr;
    }
    static void closeDirectory(void* handle) noexcept { ::closedir(static_cast<DIR*>(handle)); }
    static bool isDirectory(const char* path) noexcept {
        struct stat state;
        return ::lstat(path, &state) == 0 && S_ISDIR(state.st_mode);
    }
};

struct IndexEntryScan {
    int result = -2;  // 0 the file is there, -1 stat failed, -2 the index named no file
    int error = 0;    // errno as stat returned it, when result is -1
    uint64_t size = 0;
    bool named = false;
};

struct DirectoryScan {
    int result = -1;  // 0 listed to the end, -1 could not be opened, -2 stopped part way
    int error = 0;
    uint64_t entries = 0;
    uint64_t directories = 0;
};

template <class Ops = ScanOps>
IndexEntryScan scanIndexEntry(const std::string& dir, const std::string& itemId,
                              const std::string& fileName) {
    IndexEntryScan scan;
    // The same admission the worker uses, so a scan can never walk out of the
    // download directory on a damaged index entry.
    if (fileName.empty() || !isItemComponent(itemId)) return scan;
    if (fileName.find_first_of("/\\") != std::string::npos) return scan;
    scan.named = true;
    const std::string path = dir + "/" + itemId + "/" + fileName;
    errno = 0;
    if (Ops::size(path.c_str(), scan.size) != 0) {
        scan.result = -1;
        scan.error = errno;
        scan.size = 0;
        return scan;
    }
    scan.result = 0;
    return scan;
}

// Counts children without keeping a name. Bounded: a directory that never ends
// stops at the limit rather than being walked forever.
template <class Ops = ScanOps>
DirectoryScan scanDirectory(const std::string& path) {
    constexpr uint64_t entryLimit = 4096;
    DirectoryScan scan;
    errno = 0;
    void* handle = Ops::openDirectory(path.c_str());
    if (!handle) {
        scan.error = errno;
        return scan;
    }
    scan.result = 0;
    while (const char* name = Ops::readEntry(handle)) {
        const std::string entry(name);
        if (entry == "." || entry == "..") continue;
        if (scan.entries >= entryLimit) {
            scan.result = -2;
            break;
        }
        scan.entries++;
        if (Ops::isDirectory((path + "/" + entry).c_str())) scan.directories++;
    }
    Ops::closeDirectory(handle);
    return scan;
}

}  // namespace ps5::downloads
