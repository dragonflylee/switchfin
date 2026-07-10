#include "activity/player_view.hpp"
#include "tab/media_movie.hpp"
#include "view/h_recycling.hpp"
#include "view/icon_button.hpp"
#include "view/video_card.hpp"
#include "view/text_box.hpp"
#include "view/people_source.hpp"
#include "view/recyling_video.hpp"
#include "view/mpv_core.hpp"
#include "api/plex.hpp"
#include "api/plex/watchlist.hpp"
#include "utils/misc.hpp"
#include "utils/dialog.hpp"
#include "utils/download.hpp"
#include "utils/rating.hpp"
#include "utils/media_source.hpp"
#include "utils/offline_library.hpp"
#include "utils/network_state.hpp"
#include "tab/remote_view.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

MediaMovie::MediaMovie(const plex::Item& item, bool localContext)
    : itemId(item.ratingKey), localContext(localContext) {
    brls::Logger::debug("Tab MediaMovie: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/movie.xml");

    this->labelTitle->setText(item.title);
    Image::load(this->imagePoster, item.thumb, 325);
    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
        this->imageFade->setImageFromRes("img/fade-bottom-light.png");
    this->people->registerCell("Cell", MediaCardCell::create);
    // the buttons and the cast row have no geometric overlap: D-pad nav
    // cannot find it. Explicit route — the row materializes its first cell
    // if needed (HRecyclerFrame::getDefaultFocus) and the "centered" scroll
    // follows the focus
    this->btnPlay->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");
    this->btnDownload->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");
    this->btnWatchlist->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");

    this->btnPlay->registerClickAction([this, item](...) {
        std::string title = item.year ? fmt::format("{} ({})", item.title, item.year) : item.title;
        // in the offline downloads area (or fully offline) a downloaded movie
        // plays from the local file; from the ONLINE library it keeps streaming
        // with the server resume position (SPEC — no online regression)
        auto& dm = DownloadManager::instance();
        std::string local = media::preferLocal(this->localContext) && dm.isDownloaded(this->itemId)
                                ? dm.getLocalPath(this->itemId)
                                : "";
        if (!local.empty()) {
            RemoteView::play(local, title, "Local");
            return true;
        }
        PlayerView* view = new PlayerView(item, this->viewOffsetMs);
        view->setTitie(title);
        return true;
    });

    auto& dm = DownloadManager::instance();
    this->updateDownloadButton();
    // live progress on the button (events emitted on the UI thread)
    this->progressSub = dm.getProgressEvent()->subscribe(
        [this](const std::string& id, int64_t downloaded, int64_t total, double) {
            if (id != this->itemId || total <= 0) return;
            // "Downloading... (42%)" — a bare percentage does not say what
            // the button does; completion goes back through updateDownloadButton
            this->btnDownload->setText(fmt::format(
                "{} ({:.0f}%)", "main/download/downloading"_i18n, downloaded * 100.0 / total));
        });
    this->statusSub = dm.getStatusEvent()->subscribe([this](const std::string& id, DownloadStatus status) {
        if (id == this->itemId) this->updateDownloadButton();
    });
    this->btnDownload->registerClickAction([this](...) {
        auto& dm = DownloadManager::instance();
        if (dm.isDownloading(this->itemId)) {
            Dialog::cancelable("main/download/confirm_cancel"_i18n, [this]() {
                DownloadManager::instance().cancelDownload(this->itemId);
                this->updateDownloadButton();
            });
        } else if (dm.isDownloaded(this->itemId)) {
            // already downloaded: offer to remove it (clicking "Downloaded"
            // should do something useful, not just re-announce the state)
            Dialog::cancelable("main/download/confirm_remove"_i18n, [this]() {
                DownloadManager::instance().removeDownload(this->itemId);
                this->updateDownloadButton();
            });
        } else {
            dm.addDownload(this->itemId);
            this->updateDownloadButton();
        }
        return true;
    });

    this->doMovie();
    this->doRelated();
}

MediaMovie::~MediaMovie() {
    brls::Logger::debug("Tab MediaMovie: delete");
    auto& dm = DownloadManager::instance();
    dm.getProgressEvent()->unsubscribe(this->progressSub);
    dm.getStatusEvent()->unsubscribe(this->statusSub);
    Image::cancel(this->imageLogo);
    Image::cancel(this->imagePoster);
    Image::cancel(this->imageBackdrop);
}

void MediaMovie::updateDownloadButton() {
    auto& dm = DownloadManager::instance();
    if (dm.isDownloaded(this->itemId)) {
        this->btnDownload->setText("main/download/completed"_i18n);
    } else if (dm.isDownloading(this->itemId)) {
        this->btnDownload->setText("main/download/downloading"_i18n);
    } else {
        this->btnDownload->setText("main/download/start"_i18n);
    }
}

void MediaMovie::initWatchlist(const std::string& guid) {
    this->itemGuid = guid;
    // legacy agent (non plex:// guid): title not addressable on the provider
    if (plex::providerRatingKey(guid).empty()) return;

    this->btnWatchlist->registerClickAction([this](...) {
        this->toggleWatchlist();
        return true;
    });

    ASYNC_RETAIN
    // state: metadata.provider + includeUserState=1 (api/plex/watchlist.hpp);
    // the button stays hidden until the state is known
    plex::fetchWatchlistState(
        guid,
        [ASYNC_TOKEN](bool state) {
            ASYNC_RELEASE
            this->watchlisted = state;
            this->updateWatchlistButton();
            this->btnWatchlist->setVisibility(brls::Visibility::VISIBLE);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaMovie watchlist state: {}", ex);
        });
}

void MediaMovie::toggleWatchlist() {
    bool add = !this->watchlisted;
    ASYNC_RETAIN
    // PUT discover.provider/actions/addToWatchlist|removeFromWatchlist
    plex::setWatchlisted(
        this->itemGuid, add,
        [ASYNC_TOKEN, add]() {
            ASYNC_RELEASE
            this->watchlisted = add;
            this->updateWatchlistButton();
            brls::Application::notify(add ? "main/watchlist/added"_i18n : "main/watchlist/removed"_i18n);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        });
}

void MediaMovie::updateWatchlistButton() {
    // filled bookmark = already in the Watchlist (Plex convention)
    this->btnWatchlist->setIcon(
        this->watchlisted ? "@res/icon/ico-bookmark-fill-light.svg" : "@res/icon/ico-bookmark-light.svg");
}

void MediaMovie::doRequest() {
    int64_t seconds = MPVCore::instance().playback_time;
    this->viewOffsetMs = seconds * 1000;
    this->btnPlay->setText(seconds > 0 ? misc::sec2Time(seconds) : "main/media/play"_i18n);
}

void MediaMovie::doMovie() {
    // downloaded item, or fully offline: render from the local catalog and skip
    // the server round-trip entirely (SPEC AC5/AC6).
    if (media::preferLocal(this->localContext)) {
        plex::Item it;
        if (OfflineLibrary::instance().getItem(this->itemId, it)) {
            this->applyMovie(it);
            return;
        }
        if (NetworkState::isOffline()) {
            this->people->setVisibility(brls::Visibility::GONE);
            return;
        }
    }

    std::string query = HTTP::encode_form({
        {"includeChapters", "1"},
        {"includeMarkers", "1"},
        {"includeStreams", "1"},
        {"checkFiles", "1"},
    });

    ASYNC_RETAIN
    // detail: GET /library/metadata/{ratingKey}
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->people->setVisibility(brls::Visibility::GONE);
                return;
            }
            this->applyMovie(r.Items.front());
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        plex::apiMetadata, this->itemId, query);
}

// Renders the fiche from an Item — shared by the server and local-catalog
// (offline / downloaded) paths.
void MediaMovie::applyMovie(const plex::Item& item) {
    this->labelTitle->setText(item.title);
    Image::load(this->imagePoster, item.thumb, 325);
    // banner: backdrop (art) + centered cut-out logo; the poster
    // block overlaps the banner (XML margins) to keep the buttons
    // visible. The banner stays shown while loading (dark
    // placeholder): no layout jump when the image arrives — and
    // the gone->visible transition triggered a first-render bug
    // (gradient + image fill).
    // cut-out logo nested at the bottom of the banner fade; the
    // text title is ALWAYS shown above the pills
    if (!item.art.empty()) {
        Image::load(this->imageBackdrop, item.art, 1080, 608);
        if (!item.clearLogo.empty()) {
            Image::load(this->imageLogo, item.clearLogo, 440, 120);
        }
    } else {
        this->bannerBox->setVisibility(brls::Visibility::GONE);
        this->contentRow->setMarginTop(0);
        this->contentInfo->setMarginTop(0);
        this->invalidate();
    }
    if (item.duration > 0) {
        int min = int(item.duration / 60000);
        this->labelYear->setText(min >= 60 ? fmt::format("{}  ·  {} h {:02d}", item.year, min / 60, min % 60)
                                           : fmt::format("{}  ·  {} min", item.year, min));
    } else {
        this->labelYear->setText(std::to_string(item.year));
    }
    if (item.contentRating.empty()) {
        this->parentalRating->getParent()->setVisibility(brls::Visibility::GONE);
    } else {
        this->parentalRating->setText(item.contentRating);
        this->parentalRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
    }
    // critic (ratingImage: RT tomato / IMDb / TMDb) + audience
    // (audienceRatingImage: RT popcorn), official icons with a
    // generic-star fallback; each pill hides itself when absent
    rating::applyPill(this->iconRating, this->labelRating, item.ratingImage, item.rating);
    rating::applyPill(this->iconAudience, this->labelAudience, item.audienceRatingImage, item.audienceRating);
    this->labelOverview->setText(item.summary);

    if (item.genres.empty()) {
        this->labelGenres->setVisibility(brls::Visibility::GONE);
    } else {
        this->labelGenres->setText(fmt::format("{}", fmt::join(item.genres, ", ")));
        this->labelGenres->setVisibility(brls::Visibility::VISIBLE);
    }
    // the director opens the row, subtitled "Director", clickable
    // to their person page like the actors
    std::vector<plex::Role> credits = item.directors;
    for (auto& d : credits) d.role = "main/media/director"_i18n;
    credits.insert(credits.end(), item.roles.begin(), item.roles.end());
    if (credits.size() > 0) {
        this->people->setDataSource(new PeopleDataSource(credits));
    } else {
        this->people->setVisibility(brls::Visibility::GONE);
    }

    // multiple versions (item.media[]): the selector remembers the choice
    // but v1 playback always uses the first accessible version
    if (item.media.size() > 1) {
        std::vector<std::string> names;
        for (auto& m : item.media) {
            names.push_back(fmt::format("{} {} ({} kbps)", m.videoResolution, m.videoCodec, m.bitrate));
        }
        this->btnSource->init(
            "main/setting/version"_i18n, names, 0, [this](int index) { this->selectedVersion = index; });
        this->btnSource->setVisibility(brls::Visibility::VISIBLE);
    } else {
        this->btnSource->setVisibility(brls::Visibility::GONE);
    }

    this->viewOffsetMs = item.viewOffset;
    this->btnPlay->setText(this->viewOffsetMs > 0 ? misc::sec2Time(this->viewOffsetMs / 1000) : "main/media/play"_i18n);

    // watchlist relies on the plex.tv account — online only
    if (!NetworkState::isOffline()) this->initWatchlist(item.guid);
}

void MediaMovie::doRelated() {
    // no "related" rows offline — that content is not downloaded (SPEC AC7)
    if (NetworkState::isOffline()) {
        this->boxRelated->clearViews();
        return;
    }

    std::string query = HTTP::encode_form({{"count", "12"}});

    ASYNC_RETAIN
    // all the server's "related" rows, localized titles
    plex::getJSON<plex::Container<plex::Hub>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Hub>& r) {
            ASYNC_RELEASE
            this->boxRelated->clearViews();
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                RecylingVideo* row = new RecylingVideo();
                row->setTitle(hub.title);
                row->setFrameHeight(brls::getStyle()["app/card/poster/row"]);
                row->setItemWidth(brls::getStyle()["app/card/poster/width"]);
                row->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
                // truncated hub (more=1): "+" card to the full page
                if (hub.more && !hub.key.empty()) {
                    row->setItems(hub.items, hub.title, hub.key);
                } else {
                    row->setItems(hub.items);
                }
                this->boxRelated->addView(row);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        plex::apiHubRelated, this->itemId, query);
}
