/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class RecyclingGrid;
class MediaFilter;

class MediaCollection : public brls::Box {
public:
    MediaCollection(const std::string& itemId, const std::string& itemType = "");

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/series");

    /// @brief 获取显示配置
    void doPreferences();
    void doRequest();
    
    void loadFilter();
    void saveFilter();

    std::string itemId;
    std::string itemType;
    size_t pageSize;
    size_t startIndex;

    std::string prefId;
    std::string prefKey;
    std::map<std::string, std::string> customPrefs;
};