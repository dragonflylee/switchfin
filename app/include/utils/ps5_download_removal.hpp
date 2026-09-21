#pragma once

#include <atomic>
#include <cerrno>
#include <dirent.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace ps5::downloads {

inline constexpr char removalPrefix[] = ".switchfin-removing-";

inline bool isRetiredDirectory(const std::string& name) noexcept {
    return name.compare(0, sizeof(removalPrefix) - 1, removalPrefix) == 0;
}

inline bool isItemComponent(const std::string& name) noexcept {
    return !name.empty() && name != "." && name != ".." &&
        name.find_first_of("/\\") == std::string::npos && name.find('\0') == std::string::npos &&
        !isRetiredDirectory(name);
}

enum class Retirement { Moved, Missing, Failed };

struct RemovalOps {
    static int createDirectory(const char* path) noexcept { return ::mkdir(path, 0700); }
    static int move(const char* source, const char* target) noexcept { return std::rename(source, target); }
    static int removeEmpty(const char* path) noexcept { return ::rmdir(path); }
    static bool missing(const char* path) noexcept {
        struct stat state;
        return ::lstat(path, &state) != 0 && errno == ENOENT;
    }
    // std::filesystem::remove_all walks with openat/fdopendir/unlinkat, which
    // are undefined in this image: calling it jumps to a null address. These
    // are the calls the console actually provides.
    static void* openDirectory(const char* path) noexcept { return ::opendir(path); }
    static const char* readEntry(void* handle) noexcept {
        const dirent* entry = ::readdir(static_cast<DIR*>(handle));
        return entry ? entry->d_name : nullptr;
    }
    static void closeDirectory(void* handle) noexcept { ::closedir(static_cast<DIR*>(handle)); }
    static int removeFile(const char* path) noexcept { return ::unlink(path); }
    static bool isDirectory(const char* path) noexcept {
        struct stat state;
        return ::lstat(path, &state) == 0 && S_ISDIR(state.st_mode);
    }
};
// Depth-bounded recursive delete. A symbolic link is unlinked, never followed,
// so the walk cannot leave the tree. Returns false and stops at the first
// failure; a missing path counts as removed.
template <class Ops = RemovalOps>
bool removeTree(const std::string& path, unsigned depth = 0) {
    constexpr unsigned depthLimit = 16;
    if (Ops::missing(path.c_str())) return true;
    if (!Ops::isDirectory(path.c_str())) return Ops::removeFile(path.c_str()) == 0 || Ops::missing(path.c_str());
    if (depth >= depthLimit) return false;
    void* handle = Ops::openDirectory(path.c_str());
    if (!handle) return false;
    bool ok = true;
    while (const char* name = Ops::readEntry(handle)) {
        const std::string entry(name);
        if (entry == "." || entry == "..") continue;
        if (entry.find('/') != std::string::npos) { ok = false; continue; }
        if (!removeTree<Ops>(path + "/" + entry, depth + 1)) ok = false;
    }
    Ops::closeDirectory(handle);
    if (!ok) return false;
    return Ops::removeEmpty(path.c_str()) == 0 || Ops::missing(path.c_str());
}

// Reserve an empty destination exclusively, then rename the old directory over
// it. Deferred deletion must receive only the returned retired path. A new item
// may then safely reuse its original directory. Existing retired paths are never
// overwritten; abandoned ones are retained for a separate recovery policy.
template <class Ops = RemovalOps>
Retirement retireDirectory(const std::string& root, const std::string& item, std::string& retired) {
    retired.clear();
    if (!isItemComponent(item)) return Retirement::Failed;
    const std::string source = root + "/" + item;
    static std::atomic_uint64_t sequence{0};
    for (unsigned attempt = 0; attempt != 128; ++attempt) {
        std::string destination = root + "/" + removalPrefix + std::to_string(sequence.fetch_add(1));
        if (Ops::createDirectory(destination.c_str()) != 0) {
            if (errno == EEXIST) continue;
            return Retirement::Failed;
        }
        if (Ops::move(source.c_str(), destination.c_str()) == 0) {
            retired.swap(destination); // cannot throw after the successful rename
            return Retirement::Moved;
        }
        const int error = errno;
        Ops::removeEmpty(destination.c_str());
        return error == ENOENT && Ops::missing(source.c_str()) ? Retirement::Missing : Retirement::Failed;
    }
    return Retirement::Failed;
}

} // namespace ps5::downloads
