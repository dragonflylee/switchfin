#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>

class ButtonClose;

class ContextMenu : public brls::Box {
public:
    ContextMenu(const jellyfin::Item& item);
    ContextMenu(const jellyfin::Episode& episode);

    bool isTranslucent() override { return true; }

    View* getDefaultFocus() override { return this->context->getDefaultFocus(); }

private:
    void setup(const jellyfin::Item& item);

    BRLS_BIND(brls::ScrollingFrame, context, "video/context/menu");
    BRLS_BIND(brls::Box, cancel, "video/cancel");

    BRLS_BIND(brls::RadioCell, btnFavorite, "menu/favorite");
    BRLS_BIND(brls::RadioCell, btnMarkPlay, "menu/mark/play");
    BRLS_BIND(brls::RadioCell, btnDownload, "menu/download");

    bool doPlayed();
    bool doFavorite();
    bool unPlayed();
    bool unFavorite();

    std::string itemId;
    jellyfin::Item item;
    std::string episodeSeriesName;
    int episodeSeasonIndex = 0;
    int episodeIndex = 0;
    bool hasEpisodeData = false;
};