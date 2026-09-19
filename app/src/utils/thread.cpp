#include <borealis/core/logger.hpp>
#include <fmt/format.h>
#include "utils/thread.hpp"
#include "api/http.hpp"
#ifdef PS5_NATIVE_GPU
#include <optional>
#include <stdexcept>

namespace {
// Fixed numeric stages only; even failure reporting must not escape a worker.
void reportNativePoolFailure(unsigned stage) noexcept {
    static std::atomic<unsigned> reported{0};
    unsigned count = reported.load(std::memory_order_relaxed);
    do {
        if (count >= 16) return;
    } while (!reported.compare_exchange_weak(count, count + 1, std::memory_order_relaxed));
    try { brls::Logger::error("ps5 native worker failure stage={}", stage); }
    catch (...) {}
}
} // namespace
#endif

constexpr std::chrono::milliseconds max_idle_time{60000};

#ifdef BOREALIS_USE_STD_THREAD
size_t ThreadPool::max_thread_num = std::thread::hardware_concurrency();
#elif defined(__PSV__)
size_t ThreadPool::max_thread_num = 2;
#else
size_t ThreadPool::max_thread_num = 4;
#endif

#ifdef PS5_NATIVE_GPU
ThreadPool::ThreadPool() : isStop(false) {
#else
ThreadPool::ThreadPool() {
#endif
    this->start(max_thread_num > 0 ? max_thread_num : 1);
}

ThreadPool::~ThreadPool() { this->stop(); }

void ThreadPool::start(size_t num) {
#ifndef PS5_NATIVE_GPU
    // 已停止则不再创建新线程
#endif
    if (this->isStop.load()) return;
#ifdef PS5_NATIVE_GPU
    // Called by the owner/UI thread. Retain a handle slot before launching;
    // allocation failure after launch must never orphan a running worker.
    std::lock_guard<std::mutex> locker(this->threadMutex);
    try {
        while (this->threads.size() < num) {
#else
    while (this->threads.size() < num) {
#endif
#ifdef BOREALIS_USE_STD_THREAD
#ifdef PS5_NATIVE_GPU
            auto thread = std::make_shared<std::thread>();
            this->threads.push_back(thread);
            try { *thread = std::thread(task_loop, this); }
            catch (...) { this->threads.pop_back(); throw; }
#else
        Thread th = std::make_shared<std::thread>(task_loop, this);
#endif
#else
#ifdef PS5_NATIVE_GPU
            this->threads.emplace_back();
            if (pthread_create(&this->threads.back(), nullptr, task_loop, this) != 0) {
                this->threads.pop_back();
                throw std::runtime_error("Native worker creation failed");
            }
#else
        Thread th = 0;
        pthread_create(&th, nullptr, task_loop, this);
#endif
#endif
#ifdef PS5_NATIVE_GPU
        }
    } catch (...) {
        // Constructor failure destroys the pool's mutex/queue fields next.
        // Every successfully launched worker must be joined before that.
        this->stop();
        reportNativePoolFailure(4);
        throw;
#else
        std::lock_guard<std::mutex> locker(this->threadMutex);
        this->threads.push_back(th);
#endif
    }
#ifndef PS5_NATIVE_GPU
    brls::Logger::info("ThreadPool start {}", this->threads.size());
#endif
}

void *ThreadPool::task_loop(void *ptr) {
    ThreadPool *p = reinterpret_cast<ThreadPool *>(ptr);
#ifdef PS5_NATIVE_GPU
    std::optional<HTTP> http;
    while (true) {
#else
    HTTP s;
    while (!p->isStop.load()) {
        Task task;

#endif
        {
            std::unique_lock<std::mutex> locker(p->taskMutex);
            p->taskCond.wait_for(locker, std::chrono::milliseconds(max_idle_time),
                [p]() { return p->isStop.load() || !p->tasks.empty(); });
#ifdef PS5_NATIVE_GPU
            if (p->isStop.load()) break;
            if (p->tasks.empty()) continue;
        }
#endif

#ifdef PS5_NATIVE_GPU
        // Initialize outside the queue lock and before claiming a task. A
        // transient HTTP initialization failure cannot discard queued work.
        if (!http) {
            try { http.emplace(); }
            catch (...) {
                reportNativePoolFailure(1);
                std::unique_lock<std::mutex> locker(p->taskMutex);
                p->taskCond.wait_for(locker, std::chrono::milliseconds(100),
                    [p]() { return p->isStop.load(); });
#else
            if (p->tasks.empty()) {
#endif
                continue;
            }
#ifdef PS5_NATIVE_GPU
        }
#endif

#ifdef PS5_NATIVE_GPU
        Task task;
        {
            std::lock_guard<std::mutex> locker(p->taskMutex);
            if (p->isStop.load()) break;
            if (p->tasks.empty()) continue;
#endif
            task = std::move(p->tasks.front());
            p->tasks.pop_front();
        }
#ifndef PS5_NATIVE_GPU

#endif
        if (task) {
#ifdef PS5_NATIVE_GPU
            try { task(*http); }
            catch (...) { reportNativePoolFailure(2); }
            // Retire an unexpectedly unusable handle before another task.
            try { http->reset(); }
            catch (...) { reportNativePoolFailure(3); http.reset(); }
#else
            try {
                task(s);
            } catch (const std::exception &ex) {
                brls::Logger::error("error: pool task {}", ex.what());
            }
#endif
        }
    }
#ifndef PS5_NATIVE_GPU

    brls::Logger::verbose("thread: exit {}", fmt::ptr(p));
#endif
    return nullptr;
}

void ThreadPool::stop() {
#ifdef PS5_NATIVE_GPU
    {
        // Use the predicate's mutex so stop cannot notify between the worker's
        // false predicate check and its atomic unlock-and-wait transition.
        std::lock_guard<std::mutex> locker(this->taskMutex);
        if (this->isStop.exchange(true)) return;
    }
#else
    // 幂等：重复调用（显式 stop + 析构）时直接返回，避免对已 join 的线程再次 join
    if (this->isStop.exchange(true)) return;

#endif
    this->taskCond.notify_all();

    for (auto &th : this->threads) {
#ifdef BOREALIS_USE_STD_THREAD
        th->join();
#else
        pthread_join(th, nullptr);
#endif
    }
    threads.clear();
#ifdef PS5_NATIVE_GPU
}

#else
}
#endif
