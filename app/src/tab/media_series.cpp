/*
    Copyright 2023 dragonflylee
*/

#include "activity/player_view.hpp"
#include "api/plex.hpp"
#include "api/plex/watchlist.hpp"
#include "tab/media_series.hpp"
#include "view/h_recycling.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/icon_button.hpp"
#include "view/svg_image.hpp"
#include "view/text_box.hpp"
#include "view/video_card.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "view/recyling_video.hpp"
#include "view/presenter.hpp"
#include "view/context_menu.hpp"
#include "utils/keybind.hpp"
#include "utils/rating.hpp"
#include "utils/download.hpp"
#include "utils/dialog.hpp"
#include "utils/media_source.hpp"
#include "utils/offline_library.hpp"
#include "utils/network_state.hpp"
#include "tab/remote_view.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

class EpisodeCardCell : public BaseCardCell {
public:
    EpisodeCardCell() { this->inflateFromXMLRes("xml/view/episode_card.xml"); }

    BRLS_BIND(brls::Label, labelName, "episode/card/name");
    BRLS_BIND(brls::Label, labelOverview, "episode/card/overview");
    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
    BRLS_BIND(brls::Box, badgeDownload, "video/card/badge/download");
};

/// Season view header cell (cover + info + download button):
/// FIRST cell of the flow, it scrolls with the episodes.
/// Not focusable itself: navigation reaches the button via
/// getDefaultFocus (cf. DownloadSectionHeader).
class SeasonHeaderCell : public RecyclingGridItem {
public:
    SeasonHeaderCell() {
        this->inflateFromXMLRes("xml/view/season_header.xml");
        this->setFocusable(false);
        // the button is wired ONCE; cellForRow only replaces onDownload
        // (registerClickAction on every bind would accumulate actions)
        this->btnDownload->registerClickAction([this](...) {
            if (this->onDownload) this->onDownload();
            return true;
        });
    }

    void setItem(const plex::Item& item, const std::string& fallbackSummary) {
        this->picture->clear();
        const std::string& thumb = item.thumb.empty() ? item.parentThumb : item.thumb;
        if (!thumb.empty()) Image::load(this->picture, thumb, 225);
        this->labelTitle->setText(item.title);
        std::string meta;
        if (item.leafCount > 0) {
            meta = fmt::format("{} {}", item.leafCount,
                item.leafCount > 1 ? "main/media/episodes"_i18n : "main/media/episode"_i18n);
            if (item.year > 0) meta = fmt::format("{}  ·  {}", item.year, meta);
        } else if (item.year > 0) {
            meta = std::to_string(item.year);
        }
        this->labelMeta->setText(meta);
        this->labelMeta->setVisibility(meta.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        this->labelOverview->setText(item.summary.empty() ? fallbackSummary : item.summary);
    }

    void setDownloadVisible(bool v) {
        this->btnDownload->setVisibility(v ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    std::function<void()> onDownload = nullptr;

private:
    BRLS_BIND(brls::Image, picture, "season/image/poster");
    BRLS_BIND(brls::Label, labelTitle, "season/label/title");
    BRLS_BIND(brls::Label, labelMeta, "season/label/meta");
    BRLS_BIND(TextBox, labelOverview, "season/label/overview");
    BRLS_BIND(IconButton, btnDownload, "season/download");
};

/// Season view source: index 0 = header, 1..N = episodes. FIXED heights
/// via heightForRow — no yoga measuring (a TextBox with empty text
/// measured NaN and degenerated the layout: seasons without their own
/// summary, cf. textBoxMeasureFunc).
class SeasonEpisodesDataSource : public RecyclingGridDataSource {
public:
    static constexpr float HEADER_HEIGHT = 255;  // cover 225 + air 30
    static constexpr float CARD_HEIGHT = 190;    // thumbnail 180 + padding 2x5

    using MediaList = std::vector<plex::Item>;

    SeasonEpisodesDataSource(
        const plex::Item& season, const std::string& fallback, const MediaList& episodes, bool localContext = false)
        : season(season), fallbackSummary(fallback), list(episodes), localContext(localContext) {}

    size_t getItemCount() override { return this->list.size() + 1; }

    float heightForRow(brls::View* recycler, size_t index) override {
        return index == 0 ? HEADER_HEIGHT : CARD_HEIGHT;
    }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        if (index == 0) {
            auto* header = dynamic_cast<SeasonHeaderCell*>(recycler->dequeueReusableCell("Header"));
            header->setItem(this->season, this->fallbackSummary);
            header->onDownload = [this]() { this->downloadRemaining(); };
            // no new downloads possible offline
            header->setDownloadVisible(!NetworkState::isOffline());
            return header;
        }

        EpisodeCardCell* cell = dynamic_cast<EpisodeCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index - 1);
        cell->setId(item.ratingKey);

        // episode thumbnail, otherwise the show's (cell purged first)
        cell->picture->clear();
        if (!item.thumb.empty()) {
            Image::load(cell->picture, item.thumb, 300);
        } else if (!item.grandparentThumb.empty()) {
            Image::load(cell->picture, item.grandparentThumb, 300);
        }

        if (item.index > 0) {
            cell->labelName->setText(fmt::format("{}. {}", item.index, item.title));
        } else {
            cell->labelName->setText(item.title);
        }
        cell->labelOverview->setText(item.summary);

        if (item.played()) {
            cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
            cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
            cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        } else if (item.viewOffset > 0 && item.duration > 0) {
            cell->rectProgress->setWidthPercentage(float(item.viewOffset) / float(item.duration) * 100.f);
            cell->rectProgress->getParent()->setVisibility(brls::Visibility::VISIBLE);
            cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        } else {
            cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
            cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        }

        // "downloaded" badge (also visible online, so the fiche shows what is
        // already available offline)
        cell->badgeDownload->setVisibility(DownloadManager::instance().isDownloaded(item.ratingKey)
                                               ? brls::Visibility::VISIBLE
                                               : brls::Visibility::GONE);

        // offline browsing: dim the episodes that aren't downloaded — they are
        // shown for structure but greyed and non-playable (SPEC AC9)
        if (this->localContext) {
            cell->setAlpha(DownloadManager::instance().isDownloaded(item.ratingKey) ? 1.0f : 0.4f);
        }

        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        if (index == 0) return;  // the header only acts through its button
        auto& item = this->list.at(index - 1);
        auto& dm = DownloadManager::instance();

        // in the downloads area / offline a downloaded episode plays its local
        // file; from the ONLINE library it keeps streaming (server resume)
        std::string local = media::preferLocal(this->localContext) && dm.isDownloaded(item.ratingKey)
                                ? dm.getLocalPath(item.ratingKey)
                                : "";
        if (!local.empty()) {
            std::string title = item.grandparentTitle.empty()
                                    ? fmt::format("S{}E{} — {}", item.parentIndex, item.index, item.title)
                                    : fmt::format("{} · S{}E{} — {}", item.grandparentTitle, item.parentIndex,
                                          item.index, item.title);
            RemoteView::play(local, title, "Local");
            return;
        }
        // not downloaded: offline it is unavailable; online (downloads area) it
        // can be fetched on the spot
        if (NetworkState::isOffline()) {
            brls::Application::notify("main/download/unavailable_offline"_i18n);
            return;
        }
        if (this->localContext) {
            std::string id = item.ratingKey;
            Dialog::cancelable("main/download/confirm_episode"_i18n, [id]() {
                DownloadManager::instance().addDownload(id);
            });
            return;
        }

        PlayerView* view = new PlayerView(item);
        view->setTitie(item.grandparentTitle.empty()
                           ? fmt::format("S{}E{} — {}", item.parentIndex, item.index, item.title)
                           : fmt::format("{} · S{}E{} — {}", item.grandparentTitle, item.parentIndex, item.index,
                                 item.title));
        if (!item.grandparentRatingKey.empty()) view->setSeries(item.grandparentRatingKey);
        brls::sync([view]() { brls::Application::giveFocus(view); });
    }

    void onContextMenu(brls::Box* recycler, size_t index) {
        if (index == 0) return;
        auto& item = this->list.at(index - 1);
        brls::Box* menu = new ContextMenu(item, recycler);
        brls::Application::pushActivity(new brls::Activity(menu));
    }

    /// show summary that arrived afterwards ("go to season" path):
    /// used at the next header bind
    void setFallbackSummary(const std::string& s) { this->fallbackSummary = s; }

    const plex::Item& getSeason() const { return this->season; }

    void clearData() override { this->list.clear(); }

private:
    /// queues the download of all episodes not yet fetched
    void downloadRemaining() {
        auto& dm = DownloadManager::instance();
        std::vector<std::string> wanted;
        for (auto& ep : this->list) {
            if (!dm.isDownloaded(ep.ratingKey) && !dm.isDownloading(ep.ratingKey)) wanted.push_back(ep.ratingKey);
        }
        if (wanted.empty()) {
            brls::Application::notify("main/download/completed"_i18n);
            return;
        }
        Dialog::cancelable(fmt::format(fmt::runtime("main/download/confirm_season"_i18n), wanted.size()), [wanted]() {
            auto& dm = DownloadManager::instance();
            for (auto& key : wanted) dm.addDownload(key);
            brls::Application::notify("main/download/season_queued"_i18n);
        });
    }

    plex::Item season;
    std::string fallbackSummary;
    MediaList list;
    bool localContext;  // offline downloads area (grey non-downloaded episodes)
};

/// Loading skeleton that matches the season layout (header cover + info, then
/// landscape-thumbnail episode rows) — the generic poster-card skeleton looked
/// nothing like the real content.
class SeasonSkeletonCell : public RecyclingGridItem {
public:
    SeasonSkeletonCell() { this->setFocusable(false); }
    static RecyclingGridItem* create() { return new SeasonSkeletonCell(); }

    bool header = false;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override {
        brls::Time curTime = brls::getCPUTimeUsec() / 1000;
        float p = (curTime % 1000) * 1.0f / 1000;
        p = std::fabs(0.5f - p) + 0.25f;
        NVGcolor end = this->bg;
        end.a = p;
        NVGpaint paint = nvgLinearGradient(vg, x, y, x + width, y + height, a(this->bg), a(end));
        auto bar = [&](float bx, float by, float bw, float bh, float r) {
            nvgBeginPath(vg);
            nvgFillPaint(vg, paint);
            nvgRoundedRect(vg, bx, by, bw, bh, r);
            nvgFill(vg);
        };

        if (this->header) {
            // left portrait cover (season_header.xml: 150x225) + text block
            bar(x, y, 150, 225, 10);
            float rx = x + 150 + 24;  // marginLeft 24
            float rw = width - 150 - 24;
            bar(rx, y + 30, rw * 0.5f, 26, 8);    // title
            bar(rx, y + 72, rw * 0.30f, 15, 6);   // meta
            bar(rx, y + 104, rw * 0.92f, 12, 5);  // overview line 1
            bar(rx, y + 124, rw * 0.85f, 12, 5);  // overview line 2
            bar(rx, y + 160, 150, 36, 18);        // download button
        } else {
            // left landscape thumbnail (episode_card.xml: 300x180) + text lines
            bar(x, y, 300, 180, 8);
            float rx = x + 300 + 20;  // marginLeft 20
            float rw = width - 300 - 20;
            bar(rx, y + 14, rw * 0.45f, 18, 7);   // episode name
            bar(rx, y + 48, rw * 0.95f, 12, 5);   // overview lines
            bar(rx, y + 68, rw * 0.90f, 12, 5);
            bar(rx, y + 88, rw * 0.70f, 12, 5);
        }
    }

private:
    NVGcolor bg = brls::Application::getTheme()["color/grey_3"];
};

class SeasonSkeletonSource : public RecyclingGridDataSource {
public:
    explicit SeasonSkeletonSource(size_t episodes) : episodes(episodes) {}

    size_t getItemCount() override { return 1 + this->episodes; }

    float heightForRow(brls::View*, size_t index) override {
        return index == 0 ? 255 : 190;  // HEADER_HEIGHT / CARD_HEIGHT
    }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        auto* cell = dynamic_cast<SeasonSkeletonCell*>(recycler->dequeueReusableCell("SeasonSkeleton"));
        cell->header = index == 0;
        cell->setHeight(index == 0 ? 255 : 190);
        return cell;
    }

    void clearData() override { this->episodes = 0; }

private:
    size_t episodes;
};

/// Season detail view: EVERYTHING scrolls — the header (cover + info +
/// button) is the first cell of the RecyclingGrid (flow), followed by the
/// episodes. Stacked on top of the show page via ui::presentDetail — the
/// refresh after playback goes through Presenter (VIDEO_CLOSE ->
/// doRequest).
class MediaSeason : public brls::Box, public Presenter {
public:
    MediaSeason(const plex::Item& item, const std::string& fallbackSummary, bool localContext = false)
        : season(item), fallbackSummary(fallbackSummary), localContext(localContext) {
        this->inflateFromXMLRes("xml/tabs/seasons.xml");

        this->recycler->registerCell("Header", []() { return new SeasonHeaderCell(); });
        this->recycler->registerCell("Cell", []() {
            auto cell = new EpisodeCardCell();
            auto actionListener = [cell](brls::View*) -> bool {
                // same robust tree climb as VideoCardCell (video_card.cpp)
                brls::Box* view = cell->getParent();
                RecyclingView* recycler = nullptr;
                while (view && !(recycler = dynamic_cast<RecyclingView*>(view))) view = view->getParent();
                if (!recycler) return false;
                auto* dataSrc = dynamic_cast<SeasonEpisodesDataSource*>(recycler->getDataSource());
                if (!dataSrc) return false;
                dataSrc->onContextMenu(view, cell->getIndex());
                return true;
            };
            // visible hint ("X Options"), cf. video_card.cpp
            cell->registerAction("hints/option"_i18n, brls::BUTTON_X, actionListener);
            cell->registerAction(KeyBind::getSetting(), actionListener);
            return cell;
        });

        // season-shaped loading skeleton (header + episode rows) — replaces the
        // generic poster-card skeleton that looked nothing like the content
        this->recycler->registerCell("SeasonSkeleton", SeasonSkeletonCell::create);
        size_t skel = item.leafCount > 0 ? (item.leafCount > 12 ? 12 : (size_t)item.leafCount) : 6;
        this->recycler->setDataSource(new SeasonSkeletonSource(skel));

        // season summary missing AND show summary not yet known ("go to
        // season" path: doSeries has not answered) -> fetch it ourselves,
        // lightweight request (metadata without checkFiles)
        if (item.summary.empty() && fallbackSummary.empty() && !item.parentRatingKey.empty()) this->doSummary();

        this->doRequest();
    }

    /// (re)loads the episodes — also triggered when the player closes
    /// to refresh watched badges / progress (Presenter)
    void doRequest() override {
        // downloads area / offline: episodes from the local catalog — the full
        // season is shown, non-downloaded ones greyed and non-playable (AC9)
        if (media::preferLocal(this->localContext)) {
            auto eps = OfflineLibrary::instance().children(this->season.ratingKey);
            this->recycler->setDataSource(
                new SeasonEpisodesDataSource(this->season, this->fallbackSummary, eps, true));
            return;
        }

        ASYNC_RETAIN
        // season episodes: GET /library/metadata/{seasonKey}/children
        plex::getJSON<plex::Container<plex::Item>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
                ASYNC_RELEASE
                this->recycler->setDataSource(
                    new SeasonEpisodesDataSource(this->season, this->fallbackSummary, r.Items));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->recycler->setError(ex);
            },
            plex::apiChildren, this->season.ratingKey, "");
    }

private:
    /// fallback summary (the show's) when the season has none
    void doSummary() {
        ASYNC_RETAIN
        plex::getJSON<plex::Container<plex::Item>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
                ASYNC_RELEASE
                if (r.Items.empty() || r.Items.front().summary.empty()) return;
                this->fallbackSummary = r.Items.front().summary;
                // data already displayed: update the source and the header
                // if it is attached (otherwise the next bind is enough)
                auto* src = dynamic_cast<SeasonEpisodesDataSource*>(this->recycler->getDataSource());
                if (!src) return;
                src->setFallbackSummary(this->fallbackSummary);
                auto* header = dynamic_cast<SeasonHeaderCell*>(this->recycler->getGridItemByIndex(0));
                if (header) header->setItem(src->getSeason(), this->fallbackSummary);
            },
            [ASYNC_TOKEN](const std::string& ex) { ASYNC_RELEASE },
            plex::apiMetadata, this->season.parentRatingKey, "");
    }

    BRLS_BIND(RecyclingGrid, recycler, "media/episodes");

    plex::Item season;
    std::string fallbackSummary;
    bool localContext;  // offline downloads area
};

/// "Seasons" row of the show page: poster cards (season title localized
/// by the server + "N episodes"). Click -> stacked season view.
class SeasonDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Item>;

    /// `fallbackSummary` points to MediaSeries::seriesSummary (stable
    /// address, shared lifetime: the source dies with the page's recycler)
    /// — the summary is only known after doSeries.
    SeasonDataSource(const MediaList& r, const std::string* fallbackSummary, bool localContext = false)
        : list(std::move(r)), fallbackSummary(fallbackSummary), localContext(localContext) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.ratingKey);

        cell->labelTitle->setText(item.title);
        if (item.leafCount > 0) {
            cell->labelExt->setText(fmt::format("{} {}", item.leafCount,
                item.leafCount > 1 ? "main/media/episodes"_i18n : "main/media/episode"_i18n));
            cell->labelExt->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        }

        // season poster, fallback to the show's (cell purged: an item
        // without a poster would keep the previous occupant's)
        cell->picture->clear();
        if (!item.thumb.empty()) {
            Image::load(cell->picture, item.thumb, 325);
        } else if (!item.parentThumb.empty()) {
            Image::load(cell->picture, item.parentThumb, 325);
        }

        // fully watched season -> badge; no progress bar
        if (item.leafCount > 0 && item.viewedLeafCount >= item.leafCount) {
            cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
            cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        }
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        // a season is in the offline catalog iff at least one of its episodes
        // is downloaded -> "downloaded" badge on the season card
        cell->badgeDownload->setVisibility(
            OfflineLibrary::instance().hasItem(item.ratingKey) ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        ui::presentDetail(recycler, new MediaSeason(item, *this->fallbackSummary, this->localContext));
    }

    void clearData() override { this->list.clear(); }

private:
    MediaList list;
    const std::string* fallbackSummary;
    bool localContext;  // offline downloads area (propagated to the season view)
};

MediaSeries::MediaSeries(const plex::Item& item, bool localContext)
    : seriesId(item.type == plex::mediaTypeSeason && !item.parentRatingKey.empty() ? item.parentRatingKey
                                                                                   : item.ratingKey)
    , localContext(localContext) {
    brls::Logger::debug("Tab MediaSeries: create");
    if (item.type == plex::mediaTypeSeason) this->wantedSeason = item.ratingKey;
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");

    this->labelTitle->setText(item.title);
    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
        this->imageFade->setImageFromRes("img/fade-bottom-light.png");
    // poster of the show (or the selected season)
    Image::load(this->imagePoster, item.thumb.empty() ? item.parentThumb : item.thumb, 325);
    this->seasons->registerCell("Cell", VideoCardCell::create);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->special->registerCell("Cell", VideoCardCell::create);

    // the buttons and the seasons row have no geometric overlap:
    // explicit route (cf. media_movie.cpp)
    this->btnPlay->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");
    this->btnDownload->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");
    this->btnWatchlist->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");

    this->btnPlay->registerClickAction([this](...) {
        this->doPlay();
        return true;
    });
    this->btnDownload->registerClickAction([this](...) {
        this->doDownloadSeries();
        return true;
    });
    // offline: no new downloads possible
    if (NetworkState::isOffline()) this->btnDownload->setVisibility(brls::Visibility::GONE);

    this->doSeason();
    this->doSeries();
    this->doNextup();
    this->doRelated();
    this->doSpecial();
}

MediaSeries::~MediaSeries() {
    brls::Logger::debug("Tab MediaSeries: delete");
    Image::cancel(this->imageLogo);
    Image::cancel(this->imagePoster);
    Image::cancel(this->imageBackdrop);
}

void MediaSeries::doRequest() {
    // after playback: next episode (Play button) + watched states of the
    // season cards; the episodes are refreshed by MediaSeason itself
    this->doNextup();
    this->doSeason();
}

void MediaSeries::doPlay() {
    if (this->onDeck.ratingKey.empty()) return;
    // copy: "Replay" (finished show) forces the start at 0 — PlayerView
    // would otherwise resume at the episode's residual viewOffset (player_view.cpp:101)
    plex::Item item = this->onDeck;
    if (this->replay) item.viewOffset = 0;
    PlayerView* view = new PlayerView(item);
    view->setTitie(item.grandparentTitle.empty()
                       ? fmt::format("S{}E{} — {}", item.parentIndex, item.index, item.title)
                       : fmt::format(
                             "{} · S{}E{} — {}", item.grandparentTitle, item.parentIndex, item.index, item.title));
    if (!item.grandparentRatingKey.empty()) view->setSeries(item.grandparentRatingKey);
    brls::sync([view]() { brls::Application::giveFocus(view); });
}

void MediaSeries::doDownloadSeries() {
    ASYNC_RETAIN
    // whole show: GET /library/metadata/{showId}/allLeaves -> filter
    // episodes neither downloaded nor in progress -> confirmation -> queue
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            auto& dm = DownloadManager::instance();
            std::vector<std::string> wanted;
            for (auto& ep : r.Items) {
                if (!dm.isDownloaded(ep.ratingKey) && !dm.isDownloading(ep.ratingKey))
                    wanted.push_back(ep.ratingKey);
            }
            if (wanted.empty()) {
                brls::Application::notify("main/download/completed"_i18n);
                return;
            }
            Dialog::cancelable(
                fmt::format(fmt::runtime("main/download/confirm_season"_i18n), wanted.size()), [wanted]() {
                    auto& dm = DownloadManager::instance();
                    for (auto& key : wanted) dm.addDownload(key);
                    brls::Application::notify("main/download/season_queued"_i18n);
                });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        plex::apiAllLeaves, this->seriesId);
}

void MediaSeries::initWatchlist(const std::string& guid) {
    this->seriesGuid = guid;
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
            brls::Logger::warning("MediaSeries watchlist state: {}", ex);
        });
}

void MediaSeries::toggleWatchlist() {
    bool add = !this->watchlisted;
    ASYNC_RETAIN
    // PUT discover.provider/actions/addToWatchlist|removeFromWatchlist
    plex::setWatchlisted(
        this->seriesGuid, add,
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

void MediaSeries::updateWatchlistButton() {
    // filled bookmark = already in the Watchlist (Plex convention)
    this->btnWatchlist->setIcon(
        this->watchlisted ? "@res/icon/ico-bookmark-fill-light.svg" : "@res/icon/ico-bookmark-light.svg");
}

void MediaSeries::doSeries() {
    // downloaded show opened from the downloads area, or fully offline: render
    // from the local catalog (SPEC AC5/AC6)
    if (media::preferLocal(this->localContext)) {
        plex::Item it;
        if (OfflineLibrary::instance().getItem(this->seriesId, it)) {
            this->applySeries(it);
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
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->people->setVisibility(brls::Visibility::GONE);
                return;
            }
            this->applySeries(r.Items.front());
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        plex::apiMetadata, this->seriesId, query);
}

// Renders the show fiche from an Item — shared by the server and local paths.
void MediaSeries::applySeries(const plex::Item& item) {
    this->labelTitle->setText(item.title);
    Image::load(this->imagePoster, item.thumb, 325);
    // banner: backdrop (art) + cut-out logo nested at the bottom of
    // the fade; the text title is ALWAYS shown (below the banner).
    // The banner stays shown while loading (dark placeholder):
    // no layout jump when the image arrives — and the
    // gone->visible transition triggered a first-render bug
    // (gradient + image fill).
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
    // "year · N seasons" pill (childCount = number of seasons of the show)
    if (item.childCount > 0) {
        this->labelYear->setText(fmt::format("{}  ·  {} {}", item.year, item.childCount,
            item.childCount > 1 ? "main/media/seasons"_i18n : "main/media/season"_i18n));
    } else {
        this->labelYear->setText(std::to_string(item.year));
    }
    if (item.contentRating.empty()) {
        this->parentalRating->getParent()->setVisibility(brls::Visibility::GONE);
    } else {
        this->parentalRating->setText(item.contentRating);
        this->parentalRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
    }
    // critic (ratingImage) + audience (audienceRatingImage) with the
    // official Plex icons; generic-star fallback, hidden when absent
    rating::applyPill(this->iconRating, this->labelRating, item.ratingImage, item.rating);
    rating::applyPill(this->iconAudience, this->labelAudience, item.audienceRatingImage, item.audienceRating);
    this->labelOverview->setText(item.summary);
    this->seriesSummary = item.summary;

    if (item.genres.empty()) {
        this->labelGenres->setVisibility(brls::Visibility::GONE);
    } else {
        this->labelGenres->setText(fmt::format("{}", fmt::join(item.genres, ", ")));
        this->labelGenres->setVisibility(brls::Visibility::VISIBLE);
    }
    if (item.roles.size() > 0) {
        this->people->setDataSource(new PeopleDataSource(item.roles));
    } else {
        this->people->setVisibility(brls::Visibility::GONE);
    }

    // the watchlist applies to the SHOW (online only — needs the plex.tv account)
    if (!NetworkState::isOffline()) this->initWatchlist(item.guid);
}

void MediaSeries::doSeason() {
    // downloads area / offline: seasons from the local catalog (only seasons
    // with a downloaded episode survive pruning — SPEC AC10)
    if (media::preferLocal(this->localContext)) {
        auto seasons = OfflineLibrary::instance().children(this->seriesId);
        if (seasons.empty()) {
            this->labelSeasons->setVisibility(brls::Visibility::GONE);
            this->seasons->setVisibility(brls::Visibility::GONE);
        } else {
            this->seasons->setDataSource(new SeasonDataSource(seasons, &this->seriesSummary, true));
            if (!this->wantedSeason.empty()) {
                for (auto& it : seasons) {
                    if (it.ratingKey != this->wantedSeason) continue;
                    // doSeason runs inside the constructor, before this view is
                    // attached; defer so presentDetail resolves the tab-frame
                    // detail stack (not the unattached-parent fallback)
                    plex::Item season = it;
                    std::string summary = this->seriesSummary;
                    ASYNC_RETAIN
                    brls::sync([ASYNC_TOKEN, season, summary]() {
                        ASYNC_RELEASE
                        ui::presentDetail(this, new MediaSeason(season, summary, true));
                    });
                    break;
                }
                this->wantedSeason.clear();
            }
        }
        return;
    }

    ASYNC_RETAIN
    // seasons: GET /library/metadata/{showKey}/children
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->labelSeasons->setVisibility(brls::Visibility::GONE);
                this->seasons->setVisibility(brls::Visibility::GONE);
                return;
            }
            this->seasons->setDataSource(new SeasonDataSource(r.Items, &this->seriesSummary));

            // "go to season": the wanted season opens on top of the show
            // page (B goes back to the page)
            if (!this->wantedSeason.empty()) {
                for (auto& it : r.Items) {
                    if (it.ratingKey != this->wantedSeason) continue;
                    ui::presentDetail(this, new MediaSeason(it, this->seriesSummary));
                    break;
                }
                this->wantedSeason.clear();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("doSeason {}", ex);
        },
        plex::apiChildren, this->seriesId, "");
}

void MediaSeries::doNextup() {
    // OnDeck comes from the server; offline there is no "next up" — episodes are
    // played from the season list instead (SPEC AC11)
    if (NetworkState::isOffline()) {
        this->onDeck = plex::Item();
        this->btnPlay->setVisibility(brls::Visibility::GONE);
        return;
    }

    auto& conf = AppConfig::instance();
    std::string url = conf.getUrl() + fmt::format("/library/metadata/{}?includeOnDeck=1", this->seriesId);
    // fully watched show (no OnDeck): first episode across all seasons,
    // via allLeaves paginated 0-1 -> "Replay" button
    HTTP::Form firstQuery;
    plex::addPagination(firstQuery, 0, 1);
    std::string firstUrl = conf.getUrl() + fmt::format(fmt::runtime(plex::apiAllLeaves), this->seriesId) + "?" +
                           HTTP::encode_form(firstQuery);
    // `token` is reserved by ASYNC_RETAIN (borealis/core/view.hpp:60)
    std::string accessToken = conf.getToken();

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url, firstUrl, accessToken]() {
        std::vector<plex::Item> items;
        bool fromStart = false;
        try {
            // next episode: Metadata[0].OnDeck.Metadata is an OBJECT,
            // not an array -> manual extraction
            nlohmann::json j = plex::getSync(url, accessToken);
            auto& meta = j.at("MediaContainer").at("Metadata").at(0);
            if (meta.contains("OnDeck") && meta["OnDeck"].contains("Metadata")) {
                items.push_back(meta["OnDeck"]["Metadata"].get<plex::Item>());
            } else {
                // verified on a real server 2026-06-10 (show 1024863 watched 9/9):
                // Metadata[0] = S1E1 with grandparent*/index/parentIndex
                auto first = plex::getSync(firstUrl, accessToken).get<plex::Container<plex::Item>>();
                if (!first.Items.empty()) {
                    items.push_back(first.Items.front());
                    fromStart = true;
                }
            }
        } catch (const std::exception& ex) {
            brls::Logger::warning("doNextup {}", ex.what());
        }
        brls::sync([ASYNC_TOKEN, items, fromStart]() {
            ASYNC_RELEASE
            // feeds the "Play"/"Replay" button (which replaces the
            // "Up next" row); show without episodes or error -> hidden
            this->replay = fromStart;
            if (!items.empty()) {
                this->onDeck = items.front();
                this->btnPlay->setText(fromStart ? "main/media/replay"_i18n : "main/media/play"_i18n);
                this->btnPlay->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->onDeck = plex::Item();
                this->btnPlay->setVisibility(brls::Visibility::GONE);
            }
        });
    });
}

void MediaSeries::doRelated() {
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
        plex::apiHubRelated, this->seriesId, query);
}

void MediaSeries::doSpecial() {
    // extras aren't cached offline
    if (NetworkState::isOffline()) {
        this->special->setVisibility(brls::Visibility::GONE);
        this->labelSpecial->setVisibility(brls::Visibility::GONE);
        return;
    }

    ASYNC_RETAIN
    // extras: GET /library/metadata/{key}/extras;
    // type "clip" -> direct playback handled by VideoDataSource
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                this->special->setDataSource(new VideoDataSource(r.Items));
                this->special->setVisibility(brls::Visibility::VISIBLE);
                this->labelSpecial->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->special->setVisibility(brls::Visibility::GONE);
                this->labelSpecial->setVisibility(brls::Visibility::GONE);
                this->special->clearData();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->special->setVisibility(brls::Visibility::GONE);
            this->labelSpecial->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        plex::apiExtras, this->seriesId);
}
