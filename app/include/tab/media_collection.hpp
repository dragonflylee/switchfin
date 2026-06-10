/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>

class RecyclingGrid;
class MediaFilter;

class MediaCollection : public AttachedView {
public:
    /// @param itemId clé de section OU ratingKey de collection
    /// @param itemType type Plex : "movie" | "show" | "photo" | "collection"
    /// @param genresId clé de genre Plex (filtre genre=)
    explicit MediaCollection(
        const std::string& itemId, const std::string& itemType = "", const std::string& genresId = "");

    brls::View* getDefaultFocus() override;

    /// appelé uniquement en mode onglet de sidebar (createAttachedView)
    void onCreate() override;

    static void clearPref() { customPrefs.clear(); }

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/series");
    BRLS_BIND(AutoTabFrame, tabFrame, "media/tabFrame");

    /// labels de l'en-tête scrollé du mode collection uniquement
    /// (xml/view/grid_header.xml, possédé par la grille via setHeaderView)
    brls::Label* labelTitle = nullptr;
    brls::Label* labelMeta = nullptr;

    void doRequest();

    /// titre de l'en-tête du mode collection : le constructeur ne reçoit que
    /// le ratingKey → GET /library/metadata/{itemId}
    void doMetadata();
    /// « N éléments · X h YY » — durée omise si inconnue, label masqué si N <= 0
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
