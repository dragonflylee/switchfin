/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>

class RecyclingGrid;
class RecommendCell;

class SuggestMovie : public AttachedView {
public:
    explicit SuggestMovie(const std::string itemId);

    void onCreate() override;

private:
    RecyclingGrid *recycler = nullptr;
    RecommendCell *latest = nullptr;
    std::string itemId;

    void doLatest();
    void doRecommend();
};