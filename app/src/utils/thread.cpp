#include <borealis/core/logger.hpp>
#include <fmt/format.h>
#include "utils/thread.hpp"
#include "utils/config.hpp"

constexpr std::chrono::milliseconds max_idle_time{60000};

#ifdef __SWITCH__
size_t ThreadPool::max_thread_num = 4;
#else
size_t ThreadPool::max_thread_num = std::thread::hardware_concurrency();
#endif

ThreadPool::ThreadPool() {
    size_t num = AppConfig::instance().getItem(AppConfig::REQUEST_THREADS, max_thread_num);
    this->start(num < max_thread_num ? num : max_thread_num);
}

ThreadPool::~ThreadPool() {}

void ThreadPool::start(size_t num) {
    brls::Logger::info("ThreadPool start {}", num);

    while (this->threads.size() < num) {
#ifdef BOREALIS_USE_STD_THREAD
        Thread th = std::make_shared<std::thread>(task_loop, this);
#else
        Thread th = 0;
        pthread_create(&th, nullptr, task_loop, this);
#endif
        std::lock_guard<std::mutex> locker(this->threadMutex);
        this->threads.push_back(th);
    }
}

void *ThreadPool::task_loop(void *ptr) {
    ThreadPool *p = reinterpret_cast<ThreadPool *>(ptr);
    while (!p->isStop.load()) {
        Task task;

        {
            std::unique_lock<std::mutex> locker(p->taskMutex);
            p->taskCond.wait_for(locker, std::chrono::milliseconds(max_idle_time),
                [p]() { return p->isStop.load() || !p->tasks.empty(); });

            if (p->tasks.empty()) {
                continue;
            }

            task = std::move(p->tasks.front());
            p->tasks.pop_front();
        }

        if (task) {
            try {
                task();
            } catch (const std::exception &ex) {
                brls::Logger::error("error: pool task {}", ex.what());
            }
        }
    }

    brls::Logger::verbose("thread: exit {}", fmt::ptr(p));
    return nullptr;
}

void ThreadPool::stop() {
    this->isStop.store(true);
    this->taskCond.notify_all();

    for (auto &th : this->threads) {
#ifdef BOREALIS_USE_STD_THREAD
        th->join();
#else
        pthread_join(th, nullptr);
#endif
    }
    threads.clear();
}