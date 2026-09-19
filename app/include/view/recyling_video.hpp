/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#ifdef PS5_NATIVE_GPU
#include <atomic>
#include <memory>
#endif

class HRecyclerFrame;

class RecylingVideo : public brls::Box {
public:
    RecylingVideo();
    ~RecylingVideo() override;

    static brls::View* create();

    using Callback = std::function<std::string(size_t, size_t)>;

#ifdef PS5_NATIVE_GPU
    void reset();
#else
    void reset() { this->start = 0; }
#endif
    void setTitle(const std::string& text);
    void setFrameHeight(float height);
    void setItemWidth(float width);
    void setPageSize( size_t pageSize);
    void onQuery(const Callback& callback = nullptr);
    void doRequest(bool refresh = false);
    void doLatest(bool refresh = false);
#ifdef PS5_NATIVE_GPU
    void setLatestSeries(bool enabled) { latestAsSeries = enabled; }
#endif
    void doLiveTV(bool refresh = false);

private:
#ifdef PS5_NATIVE_GPU
    template <typename Item, typename Source>
    void doPagedRequest(bool refresh);

#endif
    BRLS_BIND(brls::Header, title, "recycler/title");
    BRLS_BIND(HRecyclerFrame, recycler, "recycler/videos");

    Callback queryCallback = nullptr;
    size_t start = 0;
    size_t pageSize = 10;
#ifdef PS5_NATIVE_GPU
    bool latestAsSeries = false;
    size_t requestGeneration = 0;
    std::shared_ptr<std::atomic_bool> requestCancelled;
    bool requestInFlight = false;
    bool hasMore = true;
};
#else
};
#endif
