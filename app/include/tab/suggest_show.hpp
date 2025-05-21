/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/presenter.hpp>

class RecylingVideo;

class SuggestShow : public AttachedView, public Presenter {
public:
    explicit SuggestShow(const std::string& itemId);
    ~SuggestShow() override;

    void onCreate() override;

    void doRequest() override;

private:
    BRLS_BIND(RecylingVideo, resume, "suggest/show/resume");
    BRLS_BIND(RecylingVideo, latest, "suggest/show/latest");
    BRLS_BIND(RecylingVideo, nextUp, "suggest/show/nextup");

    std::string itemId;
};