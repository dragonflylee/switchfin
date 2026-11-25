/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <vector>

class RecylingVideo;

class FavoriteTab : public AttachedView {
public:
    FavoriteTab();
    ~FavoriteTab() override;

    void onCreate() override;

    static brls::View* create();

private:
    BRLS_BIND(brls::Box, boxFavorite, "favorite/box");

    /// 按媒体类型分组，每组一个横向滚动列表
    std::vector<RecylingVideo*> groups;

    void doRequest();
};
