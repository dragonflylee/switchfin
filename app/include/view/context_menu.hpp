#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>

class SVGImage;

/// Entrée du menu contextuel : icône + libellé + coche d'état (selected).
class MenuItem : public brls::Box {
public:
    MenuItem();

    void setIcon(const std::string& res);
    void setTitle(const std::string& text);
    void setSelected(bool selected);
    bool getSelected() const { return this->selected; }

    static brls::View* create();

    BRLS_BIND(brls::Label, title, "menu_item/title");

private:
    BRLS_BIND(SVGImage, icon, "menu_item/icon");
    BRLS_BIND(SVGImage, check, "menu_item/check");

    bool selected = false;
};

/// Menu d'actions contextuelles d'un média (bouton X / clic long).
/// `host` est la vue d'origine : les navigations (série, saison) sont
/// présentées dans son AppletFrame après fermeture du menu.
class ContextMenu : public brls::Box {
public:
    ContextMenu(const plex::Item& item, brls::Box* host);

    bool isTranslucent() override { return true; }

    View* getDefaultFocus() override { return this->panel->getDefaultFocus(); }

private:
    BRLS_BIND(brls::Box, panel, "video/context/menu");
    BRLS_BIND(brls::Box, cancel, "video/cancel");

    BRLS_BIND(brls::Label, labelTitle, "menu/title");
    BRLS_BIND(brls::Label, labelSubtitle, "menu/subtitle");
    BRLS_BIND(MenuItem, btnGoSeries, "menu/go/series");
    BRLS_BIND(MenuItem, btnGoSeason, "menu/go/season");
    BRLS_BIND(MenuItem, btnMarkPlay, "menu/mark/play");
    BRLS_BIND(MenuItem, btnWatchlist, "menu/watchlist");
    BRLS_BIND(MenuItem, btnDownload, "menu/download");

    bool doPlayed();
    bool unPlayed();
    /// révèle l'entrée watchlist une fois l'état provider connu (guid plex://)
    void initWatchlist(const std::string& guid);
    bool toggleWatchlist();

    std::string itemId;
    std::string itemGuid;
    bool watchlisted = false;
};
