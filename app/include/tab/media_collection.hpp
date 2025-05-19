/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class RecyclingGrid;
class AutoTabFrame;
class MediaFilter;

class MediaCollection : public brls::Box {
public:
    explicit MediaCollection(
        const std::string& itemId, const std::string& itemType = "", const std::string& genresId = "");

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/series");
    BRLS_BIND(AutoTabFrame, tabFrame, "media/tabFrame");
    BRLS_BIND(brls::Box, sidebar, "auto_tab_frame/auto_sidebar");

    /// @brief 获取显示配置
    void doPreferences();
    void doRequest();

    void loadFilter();
    void saveFilter();

    std::string itemId;
    std::string genresId;
    std::string itemType;
    size_t pageSize;
    size_t startIndex;

    std::string prefId;
    std::string prefKey;
    std::map<std::string, std::string> customPrefs;
};