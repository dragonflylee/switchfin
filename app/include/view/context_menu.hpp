#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>
#include <utils/download.hpp>

class SVGImage;
class BaseCardCell;

/// Context menu entry: icon + label + state check mark (selected).
class MenuItem : public brls::Box {
public:
    MenuItem();

    void setIcon(const std::string& res);
    void setTitle(const std::string& text);

    static brls::View* create();

    BRLS_BIND(brls::Label, title, "menu_item/title");

private:
    BRLS_BIND(SVGImage, icon, "menu_item/icon");
    bool selected = false;
};

class ContextMenu : public brls::Box {
public:
    ContextMenu(const jellyfin::Item& item, BaseCardCell* view = nullptr);
    ~ContextMenu() override;

    bool isTranslucent() override { return true; }

    View* getDefaultFocus() override { return this->context->getDefaultFocus(); }

private:
    BRLS_BIND(brls::Box, context, "video/context/menu");
    BRLS_BIND(brls::Box, cancel, "video/cancel");

    BRLS_BIND(brls::Label, labelTitle, "menu/title");
    BRLS_BIND(MenuItem, btnFavorite, "menu/favorite");
    BRLS_BIND(MenuItem, btnMarkPlay, "menu/mark/play");
    BRLS_BIND(MenuItem, btnDownload, "menu/download");

    bool doPlayed();
    bool unPlayed();
    void updatePlayedButton(bool played);

    bool doFavorite();
    bool unFavorite();
    void updateFavoriteButton(bool favorite);

    void updateDownloadButton();

    std::string itemId;
    bool isPlayed = false;
    bool isFavorite = false;
    BaseCardCell* cell = nullptr;
    DownloadManager::ProgressEvent::Subscription progressSub;
    DownloadManager::StatusEvent::Subscription statusSub;
};