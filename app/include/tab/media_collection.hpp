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
    /// @param itemId clé de section OU ratingKey de collection
    /// @param itemType type Plex : "movie" | "show" | "photo" | "collection"
    /// @param genresId clé de genre Plex (filtre genre=)
    explicit MediaCollection(
        const std::string& itemId, const std::string& itemType = "", const std::string& genresId = "");

    brls::View* getDefaultFocus() override;

    static void clearPref() { customPrefs.clear(); }

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/series");
    BRLS_BIND(AutoTabFrame, tabFrame, "media/tabFrame");

    void doRequest();

    void loadFilter();
    void saveFilter();

    std::string itemId;
    std::string genresId;
    std::string itemType;
    size_t pageSize;
    size_t startIndex;

    static std::map<std::string, std::string> customPrefs;
};
