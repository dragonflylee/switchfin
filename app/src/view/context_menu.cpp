#include "view/context_menu.hpp"
#include "view/svg_image.hpp"
#include "view/auto_tab_frame.hpp"
#include "tab/media_series.hpp"
#include "api/plex.hpp"
#include "api/plex/watchlist.hpp"
#include "utils/download.hpp"

using namespace brls::literals;

const std::string menuItemXML = R"xml(
    <brls:Box
        height="50"
        axis="row"
        focusable="true"
        cornerRadius="10"
        highlightCornerRadius="10"
        alignItems="center"
        paddingLeft="14"
        paddingRight="14">

        <SVGImage
            id="menu_item/icon"
            width="20"
            height="20"
            marginRight="14" />

        <brls:Label
            id="menu_item/title"
            fontSize="16"
            grow="1.0" />

        <SVGImage
            id="menu_item/check"
            width="18"
            height="18"
            visibility="invisible"
            svg="@res/icon/ico-checkmark.svg" />

    </brls:Box>
)xml";

MenuItem::MenuItem() {
    this->inflateFromXMLString(menuItemXML);

    this->registerStringXMLAttribute("icon", [this](std::string value) { this->setIcon(value); });
    this->registerStringXMLAttribute("title", [this](std::string value) { this->setTitle(value); });
}

void MenuItem::setIcon(const std::string& res) {
    std::string path = res;
    const std::string prefix = "@res/";
    if (path.rfind(prefix, 0) == 0) path = path.substr(prefix.size());
    this->icon->setImageFromSVGRes(path);
}

void MenuItem::setTitle(const std::string& text) { this->title->setText(text); }

void MenuItem::setSelected(bool selected) {
    this->selected = selected;
    this->check->setVisibility(selected ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
}

brls::View* MenuItem::create() { return new MenuItem(); }

ContextMenu::ContextMenu(const plex::Item& item, brls::Box* host) : itemId(item.ratingKey) {
    this->inflateFromXMLRes("xml/view/context_menu.xml");
    brls::Logger::debug("ContextMenu: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    // contextual header: what are we acting on?
    if (item.type == plex::mediaTypeEpisode) {
        this->labelTitle->setText(item.grandparentTitle.empty() ? item.title : item.grandparentTitle);
        this->labelSubtitle->setText(fmt::format("S{}E{} — {}", item.parentIndex, item.index, item.title));
    } else {
        this->labelTitle->setText(item.title);
        this->labelSubtitle->setVisibility(brls::Visibility::GONE);
    }

    // navigation: mostly useful when the primary click starts playback
    // (episodes of the "Continue watching" rows) or opens something else
    if (!item.grandparentRatingKey.empty()) {
        this->btnGoSeries->setVisibility(brls::Visibility::VISIBLE);
        this->btnGoSeries->registerClickAction([item, host](...) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [item, host]() {
                plex::Item series;
                series.ratingKey = item.grandparentRatingKey;
                series.type = plex::mediaTypeShow;
                series.title = item.grandparentTitle;
                series.thumb = item.grandparentThumb;
                ui::presentDetail(host, new MediaSeries(series));
            });
            return true;
        });
        this->btnGoSeries->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnGoSeries));
    } else if (item.type == plex::mediaTypeSeason && !item.parentRatingKey.empty()) {
        this->btnGoSeries->setVisibility(brls::Visibility::VISIBLE);
        this->btnGoSeries->registerClickAction([item, host](...) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [item, host]() {
                plex::Item series;
                series.ratingKey = item.parentRatingKey;
                series.type = plex::mediaTypeShow;
                series.title = item.parentTitle;
                ui::presentDetail(host, new MediaSeries(series));
            });
            return true;
        });
        this->btnGoSeries->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnGoSeries));
    }

    if (item.type == plex::mediaTypeEpisode && !item.parentRatingKey.empty()) {
        this->btnGoSeason->setVisibility(brls::Visibility::VISIBLE);
        this->btnGoSeason->registerClickAction([item, host](...) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [item, host]() {
                plex::Item season;
                season.ratingKey = item.parentRatingKey;
                season.type = plex::mediaTypeSeason;
                season.parentRatingKey = item.grandparentRatingKey;
                season.title = item.grandparentTitle;
                season.thumb = item.parentThumb;
                ui::presentDetail(host, new MediaSeries(season));
            });
            return true;
        });
        this->btnGoSeason->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnGoSeason));
    }

    this->btnMarkPlay->registerClickAction([this](brls::View* view) {
        if (this->btnMarkPlay->getSelected())
            return this->unPlayed();
        else
            return this->doPlayed();
    });
    this->btnMarkPlay->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnMarkPlay));
    this->btnMarkPlay->setSelected(item.played());

    // plex.tv watchlist: movies and shows only (the provider API accepts
    // neither episodes nor seasons); the entry stays hidden until the
    // state is known (async provider request — see initWatchlist)
    if (item.type == plex::mediaTypeMovie || item.type == plex::mediaTypeShow) {
        if (!item.guid.empty()) {
            this->initWatchlist(item.guid);
        } else {
            // guid missing from some listings: fetch it from the server
            // before querying the provider
            ASYNC_RETAIN
            plex::getJSON<plex::Container<plex::Item>>(
                AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
                [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
                    ASYNC_RELEASE
                    if (!r.Items.empty()) this->initWatchlist(r.Items.front().guid);
                },
                [ASYNC_TOKEN](const std::string& ex) {
                    ASYNC_RELEASE
                    brls::Logger::warning("ContextMenu watchlist guid: {}", ex);
                },
                plex::apiMetadata, this->itemId, "");
        }
    }

    auto& dm = DownloadManager::instance();
    if (item.type == plex::mediaTypeMovie || item.type == plex::mediaTypeEpisode ||
        item.type == plex::mediaTypeClip) {
        if (dm.isDownloaded(item.ratingKey)) {
            this->btnDownload->title->setText("main/download/completed"_i18n);
            this->btnDownload->setSelected(true);
        } else if (dm.isDownloading(item.ratingKey)) {
            this->btnDownload->title->setText("main/download/downloading"_i18n);
            this->btnDownload->setSelected(true);
        }
        this->btnDownload->registerClickAction([this](brls::View* view) {
            auto& dm = DownloadManager::instance();
            if (dm.isDownloaded(this->itemId)) {
                brls::Application::notify("main/download/completed"_i18n);
            } else if (dm.isDownloading(this->itemId)) {
                brls::Application::notify("main/download/downloading"_i18n);
            } else {
                dm.addDownload(this->itemId);
                brls::Application::notify("main/download/queued"_i18n);
                this->btnDownload->setSelected(true);
            }
            return true;
        });
        this->btnDownload->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnDownload));
    } else {
        this->btnDownload->setVisibility(brls::Visibility::GONE);
    }
}

void ContextMenu::initWatchlist(const std::string& guid) {
    this->itemGuid = guid;
    // legacy agent (non plex:// guid): title not addressable on the provider
    if (plex::providerRatingKey(guid).empty()) return;

    this->btnWatchlist->registerClickAction([this](brls::View* view) { return this->toggleWatchlist(); });
    this->btnWatchlist->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnWatchlist));

    ASYNC_RETAIN
    // state: GET metadata.provider/library/metadata/{key}?includeUserState=1
    // (api/plex/watchlist.hpp — watchlistedAt field present iff watchlisted)
    plex::fetchWatchlistState(
        guid,
        [ASYNC_TOKEN](bool state) {
            ASYNC_RELEASE
            this->watchlisted = state;
            this->btnWatchlist->setTitle(state ? "main/watchlist/remove"_i18n : "main/watchlist/add"_i18n);
            this->btnWatchlist->setVisibility(brls::Visibility::VISIBLE);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            // unknown state: the entry stays hidden rather than offering a
            // backwards action
            brls::Logger::warning("ContextMenu watchlist state: {}", ex);
        });
}

bool ContextMenu::toggleWatchlist() {
    bool add = !this->watchlisted;
    ASYNC_RETAIN
    // PUT discover.provider/actions/addToWatchlist|removeFromWatchlist
    plex::setWatchlisted(
        this->itemGuid, add,
        [ASYNC_TOKEN, add]() {
            ASYNC_RELEASE
            this->watchlisted = add;
            this->btnWatchlist->setTitle(add ? "main/watchlist/remove"_i18n : "main/watchlist/add"_i18n);
            brls::Application::notify(add ? "main/watchlist/added"_i18n : "main/watchlist/removed"_i18n);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        });
    return true;
}

bool ContextMenu::doPlayed() {
    auto& conf = AppConfig::instance();
    ASYNC_RETAIN
    // GET /:/scrobble
    plex::getAction(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        plex::apiScrobble, this->itemId);
    this->btnMarkPlay->setSelected(true);
    return true;
}

bool ContextMenu::unPlayed() {
    auto& conf = AppConfig::instance();
    ASYNC_RETAIN
    // GET /:/unscrobble
    plex::getAction(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        plex::apiUnscrobble, this->itemId);
    this->btnMarkPlay->setSelected(false);
    return true;
}
