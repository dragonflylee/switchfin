/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <view/presenter.hpp>

class SuggestShow : public AttachedView, public Presenter {
public:
    explicit SuggestShow(const std::string& itemId);
    ~SuggestShow() override;

    void onCreate() override;

    void doRequest() override;

private:
    BRLS_BIND(brls::Box, box, "suggest/box");

    std::string itemId;  // section key
};