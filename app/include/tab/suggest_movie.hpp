/*
    Copyright 2025 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>

class SuggestMovie : public AttachedView {
public:
    explicit SuggestMovie(const std::string itemId);

    void onCreate() override;

private:
    BRLS_BIND(brls::Box, box, "suggest/box");

    std::string itemId;  // section key

    void doHubs();
};