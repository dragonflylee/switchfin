/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class HRecyclerFrame;

class RecylingVideo : public brls::Box {
public:
    RecylingVideo();
    ~RecylingVideo() override;

    static brls::View* create();

    using Callback = std::function<std::string(size_t, size_t)>;

    void reset() { this->start = 0; }
    void setTitle(const std::string& text);
    void setFrameHeight(float height);
    void setItemWidth(float width);
    void setPageSize( size_t pageSize);
    void onQuery(const Callback& callback = nullptr);
    void doRequest(bool refresh = false);
    void doLatest(bool refresh = false);
    void doLiveTV(bool refresh = false);

private:
    BRLS_BIND(brls::Header, title, "recycler/title");
    BRLS_BIND(HRecyclerFrame, recycler, "recycler/videos");

    Callback queryCallback = nullptr;
    size_t start = 0;
    size_t pageSize = 10;
};