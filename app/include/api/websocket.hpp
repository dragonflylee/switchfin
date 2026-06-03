/*
    Copyright 2024 dragonflylee
*/

#pragma once

#include <stdint.h>
#include <string>
#include <atomic>
#ifdef BOREALIS_USE_STD_THREAD
#include <thread>
#else
#include <pthread.h>
#endif
#include <borealis/core/timer.hpp>
#include <curl/system.h>

class websocket {
public:
    websocket(const std::string& url);
    ~websocket();

private:
    static void* wsRecv(void*);
    static size_t onMsg(char *b, size_t size, size_t nitems, void *p);
    static void onPlayNow(const std::string& itemId, uint64_t seekTicks);

#ifdef BOREALIS_USE_STD_THREAD
    std::shared_ptr<std::thread> th;
#else
    pthread_t th;
#endif
    brls::RepeatingTimer hb;
    std::atomic_bool isStop;
    void *easy;
};