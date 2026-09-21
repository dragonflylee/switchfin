#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <streambuf>

namespace ps5::artwork {

// Encoded response policy, independent of drawable or decoded image dimensions.
// These are requested buffer capacities, including old/new growth overlap, not
// a claim about allocator overhead, decoder scratch or total application memory.
inline constexpr size_t encodedResponseLimit = 32 * 1024 * 1024;
inline constexpr size_t encodedResidentLimit = 48 * 1024 * 1024;

class EncodedBudget {
public:
    explicit EncodedBudget(size_t limit = encodedResidentLimit) : limit(limit) {}

    bool acquire(size_t bytes) noexcept {
        size_t current = resident.load(std::memory_order_relaxed);
        do {
            if (bytes > limit - current) return false;
        } while (!resident.compare_exchange_weak(current, current + bytes, std::memory_order_acq_rel));
        return true;
    }

    void release(size_t bytes) noexcept { resident.fetch_sub(bytes, std::memory_order_acq_rel); }
    size_t bytes() const noexcept { return resident.load(std::memory_order_acquire); }

private:
    const size_t limit;
    std::atomic<size_t> resident{0};
};

struct EncodedHeap {
    static void* allocate(size_t bytes) noexcept { return std::malloc(bytes); }
    static void deallocate(void* value) noexcept { std::free(value); }
};

// Append-only, contiguous, non-copying decoder input. No put area is exposed:
// both single-character and bulk ostream writes must pass through admission.
// The budget outlives all bodies. The heap interface must not throw.
template <class Heap = EncodedHeap>
class EncodedBody : public std::streambuf {
public:
    enum class Failure { None, ResponseLimit, ResidentLimit, Allocation };

    explicit EncodedBody(EncodedBudget& budget, size_t limit = encodedResponseLimit)
        : budget(budget), limit(limit) {}
    ~EncodedBody() override {
        Heap::deallocate(buffer);
        budget.release(capacity);
    }

    EncodedBody(const EncodedBody&) = delete;
    EncodedBody& operator=(const EncodedBody&) = delete;
    const char* data() const noexcept { return buffer; }
    size_t size() const noexcept { return length; }
    bool empty() const noexcept { return length == 0; }
    Failure failure() const noexcept { return failed; }

protected:
    std::streamsize xsputn(const char* source, std::streamsize count) override {
        if (count <= 0) return 0;
        return append(source, static_cast<size_t>(count)) ? count : 0;
    }

    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
        const char character = traits_type::to_char_type(value);
        return append(&character, 1) ? value : traits_type::eof();
    }

private:
    EncodedBudget& budget;
    const size_t limit;
    char* buffer = nullptr;
    size_t length = 0;
    size_t capacity = 0;
    Failure failed = Failure::None;

    bool append(const char* source, size_t count) noexcept {
        if (failed != Failure::None) return false;
        if (count > limit - length) {
            failed = Failure::ResponseLimit;
            return false;
        }
        const size_t needed = length + count;
        if (needed > capacity) {
            size_t next = capacity ? capacity : (limit < 16384 ? limit : 16384);
            while (next < needed) next = next > limit / 2 ? limit : next * 2;
            // Reserve the entire replacement while the old buffer still lives.
            // Never block an HTTP worker waiting for another request to finish.
            if (!budget.acquire(next)) {
                failed = Failure::ResidentLimit;
                return false;
            }
            auto* replacement = static_cast<char*>(Heap::allocate(next));
            if (!replacement) {
                budget.release(next);
                failed = Failure::Allocation;
                return false;
            }
            if (length) std::memcpy(replacement, buffer, length);
            Heap::deallocate(buffer);
            budget.release(capacity);
            buffer = replacement;
            capacity = next;
        }
        std::memcpy(buffer + length, source, count);
        length = needed;
        return true;
    }
};

} // namespace ps5::artwork
