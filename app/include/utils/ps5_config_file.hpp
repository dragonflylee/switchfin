#pragma once

#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unistd.h>

namespace ps5::configuration {

struct FileOps {
    static int open(const char* path) { return ::open(path, O_RDWR | O_CREAT, 0600); }
    static off_t seek(int fd, off_t offset) { return ::lseek(fd, offset, SEEK_SET); }
    static ssize_t read(int fd, void* data, size_t size) { return ::read(fd, data, size); }
    static ssize_t write(int fd, const void* data, size_t size) { return ::write(fd, data, size); }
    static int truncate(int fd, off_t size) { return ::ftruncate(fd, size); }
    static int sync(int fd) { return ::fsync(fd); }
    static void close(int fd) { ::close(fd); }
};

// The PFS can deny pathname writes after unjail. Open under the sandbox
// credentials and retain the descriptor for both loading and saving settings.
template <class Ops = FileOps>
class File {
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { if (fd >= 0) Ops::close(fd); }

    bool prepare(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        if (fd < 0) fd = Ops::open(path.c_str());
        return fd >= 0;
    }

    bool read(std::string& contents) {
        std::lock_guard<std::mutex> lock(mutex);
        contents.clear();
        if (fd < 0) { errno = EBADF; return false; }
        if (Ops::seek(fd, 0) < 0) return false;
        char buffer[4096];
        for (;;) {
            const auto count = Ops::read(fd, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) return false;
            if (!count) return true;
            contents.append(buffer, static_cast<size_t>(count));
        }
    }

    bool save(const std::string& contents) {
        std::lock_guard<std::mutex> lock(mutex);
        if (fd < 0) { errno = EBADF; return false; }
        if (Ops::seek(fd, 0) < 0) return false;
        size_t offset = 0;
        while (offset < contents.size()) {
            const auto count = Ops::write(fd, contents.data() + offset, contents.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { if (!count) errno = EIO; return false; }
            offset += static_cast<size_t>(count);
        }
        if (Ops::truncate(fd, static_cast<off_t>(contents.size())) != 0) return false;
        // Some console filesystems accept writes but do not implement fsync.
        if (Ops::sync(fd) != 0 && errno != EINVAL && errno != ENOSYS && errno != EOPNOTSUPP) return false;
        return true;
    }

private:
    int fd = -1;
    std::mutex mutex;
};

inline File<>& settingsFile() {
    static File<> settings;
    return settings;
}

inline File<>& downloadIndexFile() {
    static File<> index;
    return index;
}

} // namespace ps5::configuration
