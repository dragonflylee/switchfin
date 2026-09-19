#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <ostream>
#include <streambuf>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace ps5::downloads {

enum class Failure { Create, Attach, Write, Close, Publish, State, Cancelled, Empty };

// The first short write ends the transfer, so only the first is kept. libcurl
// reports this to the caller as WRITE_ERROR and nothing else; the platform
// reason lives in errno at exactly this point, and the File is destroyed by
// unwinding before any handler could ask.
struct WriteFault {
    int error = 0;          // errno as the short write returned
    int stream = 0;         // ferror on the same stream
    uint64_t offset = 0;    // bytes successfully written before this call
    uint64_t requested = 0; // bytes this call asked to write
    uint64_t written = 0;   // bytes it actually wrote
    bool observed = false;
};

class Error : public std::exception {
public:
    explicit Error(Failure failure) noexcept : reason(failure) {}
    const Failure reason;
    const char* what() const noexcept override {
        switch (reason) {
        case Failure::Create: return "Could not create download file";
        case Failure::Attach: return "Could not open download stream";
        case Failure::Write: return "Could not finish writing download";
        case Failure::Close: return "Could not close download file";
        case Failure::Publish: return "Could not publish download file";
        case Failure::State: return "Download file is already closed";
        case Failure::Cancelled: return "Cancelled";
        case Failure::Empty: return "Empty download response";
        }
        return "Download file failed";
    }
};

struct FileOps {
    static int create(char* path) noexcept { return ::mkstemp(path); }
    static FILE* attach(int descriptor) noexcept { return ::fdopen(descriptor, "wb"); }
    static int closeDescriptor(int descriptor) noexcept { return ::close(descriptor); }
    static size_t write(const char* data, size_t count, FILE* file) noexcept { return std::fwrite(data, 1, count, file); }
    static int flush(FILE* file) noexcept { return std::fflush(file); }
    static int error(FILE* file) noexcept { return std::ferror(file); }
    static int close(FILE* file) noexcept { return std::fclose(file); }
    static int publish(const char* temporary, const char* destination) noexcept { return std::rename(temporary, destination); }
    static int remove(const char* path) noexcept { return ::unlink(path); }
};

// An exclusively created sibling file is published only after checked output,
// flush and close. This is ordinary file publication, not power-loss durability:
// no fsync/directory-sync guarantee is inferred from a successful rename.
template <class Ops = FileOps>
class File {
    class Buffer : public std::streambuf {
    public:
        FILE* file = nullptr;
        uint64_t bytes = 0;
        int close() noexcept {
            FILE* owned = file;
            file = nullptr; // fclose consumes the stream even when it reports an error.
            return owned ? Ops::close(owned) : 0;
        }

    protected:
        std::streamsize xsputn(const char* source, std::streamsize count) override {
            if (count <= 0 || !file) return 0;
            if (static_cast<uint64_t>(count) > std::numeric_limits<uint64_t>::max() - bytes) return 0;
            const size_t written = Ops::write(source, static_cast<size_t>(count), file);
            // Read errno before anything else can change it, ferror included.
            const int reason = errno;
            if (written < static_cast<size_t>(count) && !fault.observed)
                fault = {reason, Ops::error(file), bytes, static_cast<uint64_t>(count),
                         static_cast<uint64_t>(written), true};
            bytes += written;
            return static_cast<std::streamsize>(written);
        }
        int_type overflow(int_type value) override {
            if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
            const char character = traits_type::to_char_type(value);
            return xsputn(&character, 1) == 1 ? value : traits_type::eof();
        }
        int sync() override { return file && Ops::flush(file) == 0 && !Ops::error(file) ? 0 : -1; }
    public:
        WriteFault fault;
    } buffer;

    const std::string destination;
    std::string temporary;
    std::ostream output;
    bool published = false;
    static constexpr size_t kWriteBufferSize = 8u * 1024u * 1024u;
    char* writeBuffer = nullptr;
    bool preallocated = false;
public:
    // Set the file to its final size up front so /data (exFAT-like) allocates the
    // cluster chain once, instead of walking/extending it on every append (which
    // makes writes O(filesize)). Idempotent; returns true only on the call that
    // performed the ftruncate. ftruncate is in libkernel.so (safe). Best effort.
    bool preallocate(uint64_t size) noexcept {
        if (preallocated || !buffer.file || size == 0) return false;
        preallocated = true;
        return ::ftruncate(::fileno(buffer.file), static_cast<off_t>(size)) == 0;
    }
private:

public:
    explicit File(const std::string& path)
        : destination(path), temporary(path + ".part-XXXXXX"), output(&buffer) {
        const int descriptor = Ops::create(temporary.data());
        if (descriptor < 0) throw Error(Failure::Create);
        buffer.file = Ops::attach(descriptor);
        if (!buffer.file) {
            Ops::closeDescriptor(descriptor);
            Ops::remove(temporary.c_str());
            throw Error(Failure::Attach);
        }
        // /data appends slow O(filesize) (exFAT-like cluster-chain walk per
        // write). A large full-buffering block collapses curl's small chunks
        // into few big writes, cutting the number of appends ~1000x. Best
        // effort: if the allocation fails, the default stdio buffer is used.
        writeBuffer = static_cast<char*>(std::malloc(kWriteBufferSize));
        if (writeBuffer) std::setvbuf(buffer.file, writeBuffer, _IOFBF, kWriteBufferSize);
    }

    ~File() {
        const int saved = errno;
        buffer.close();
        std::free(writeBuffer); // freed after fclose; safe if null
        if (!published) Ops::remove(temporary.c_str());
        errno = saved;
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    std::ostream& stream() noexcept { return output; }
    uint64_t bytes() const noexcept { return buffer.bytes; }
    const WriteFault& writeFault() const noexcept { return buffer.fault; }

    bool commit(const std::atomic_bool* cancelled = nullptr) {
        if (published || !buffer.file) throw Error(Failure::State);
        if (cancelled && cancelled->load()) return false;
        output.flush();
        if (!output.good()) throw Error(Failure::Write);
        if (buffer.close() != 0) throw Error(Failure::Close);
        if (cancelled && cancelled->load()) return false;
        if (Ops::publish(temporary.c_str(), destination.c_str()) != 0) throw Error(Failure::Publish);
        // World-readable: a promoted (uid 0) process must read files a sandboxed
        // (uid 1) session wrote and vice versa; the sandbox pfs does not grant
        // root the usual 0600 bypass. Best effort, never fatal.
        ::chmod(destination.c_str(), 0644);
        published = true;
        return true;
    }
};

} // namespace ps5::downloads
