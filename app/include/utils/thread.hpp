#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <set>

class Thread {
public:
private:
    inline static std::mutex _mutex;
    inline static std::vector<std::function<void()>> tasks;
};