/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>

class RecyclingGrid;
class MediaFilter;

class MediaCollection : public AttachedView {
public:
    /// @param itemId section key OR collection ratingKey
    /// @param itemType Plex type: "movie" | "show" | "photo" | "collection"
    /// @param genresId Plex genre key (genre= filter)
    explicit MediaCollection(
        const std::string& itemId, const std::string& itemType = "", const std::string& genresId = "");

    brls::View* getDefaultFocus() override;

    /// only called in sidebar tab mode (createAttachedView)
    void onCreate() override;

    static void clearPref() { customPrefs.clear(); }

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/series");
    BRLS_BIND(AutoTabFrame, tabFrame, "media/tabFrame");

    /// labels of the scrolled header, collection mode only
    /// (xml/view/grid_header.xml, owned by the grid via setHeaderView)
    brls::Label* labelTitle = nullptr;
    brls::Label* labelMeta = nullptr;

    void doRequest();

    /// collection mode header title: the constructor only receives the
    /// ratingKey -> GET /library/metadata/{itemId}
    void doMetadata();
    /// "N items · X h YY" — duration omitted if unknown, label hidden if N <= 0
    void updateMeta(int64_t count, int64_t durationMs);

    void loadFilter();
    void saveFilter();

    std::string itemId;
    std::string genresId;
    std::string itemType;
    size_t pageSize;
    size_t startIndex;

    static std::map<std::string, std::string> customPrefs;
};
