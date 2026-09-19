/*
    Copyright 2024 dragonflylee
*/

#pragma once

#include <stdint.h>
#include <string>
#include <atomic>
#ifdef PS5_NATIVE_GPU
#include <memory>
#include "api/websocket_transport.hpp"
#else
#include <mutex>
#endif
#ifdef BOREALIS_USE_STD_THREAD
#include <thread>
#else
#include <pthread.h>
#endif
#ifndef PS5_NATIVE_GPU
#include <borealis/core/timer.hpp>
#endif
#include <curl/system.h>

class websocket {
public:
#ifdef PS5_NATIVE_GPU
    websocket(const std::string& url, const std::string& authorization = "");
#else
    websocket(const std::string& url);
#endif
    ~websocket();
#ifdef PS5_NATIVE_GPU
    websocket(const websocket&) = delete;
    websocket& operator=(const websocket&) = delete;
#endif

private:
    static void* wsRecv(void*);
    static size_t onMsg(char *b, size_t size, size_t nitems, void *p);
#ifdef PS5_NATIVE_GPU
    static void onPlayNow(const std::string& itemId, uint64_t seekTicks,
                         const std::shared_ptr<std::atomic_bool>& active);
#else
    static void onPlayNow(const std::string& itemId, uint64_t seekTicks);
#endif

#ifdef BOREALIS_USE_STD_THREAD
    std::shared_ptr<std::thread> th;
#else
    pthread_t th;
#endif
#ifdef PS5_NATIVE_GPU
    bool threadStarted = false;
    std::string url;
    std::string authorization;
    WebSocketTransport transport;
    std::shared_ptr<std::atomic_bool> active = std::make_shared<std::atomic_bool>(true);
};

#else
    brls::RepeatingTimer hb;
    std::atomic_bool isStop{false};
    std::mutex easyMutex;
    void *easy = nullptr;
};
#endif
