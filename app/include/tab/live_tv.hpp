/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#ifdef PS5_NATIVE_GPU
#include <atomic>
#include <memory>
#endif

class RecyclingGrid;
class AutoTabFrame;

class LiveTV : public brls::Box {
public:
    LiveTV(const std::string& itemId);
#ifdef PS5_NATIVE_GPU
    ~LiveTV() override;
#endif

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/series");
    BRLS_BIND(AutoTabFrame, tabFrame, "media/tabFrame");

    void doRequest();
#ifdef PS5_NATIVE_GPU
    size_t requestGeneration = 0;
    std::shared_ptr<std::atomic_bool> requestCancelled;
};
#else
};
#endif
