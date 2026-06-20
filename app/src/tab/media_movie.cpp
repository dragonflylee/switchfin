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
#include "api/backend.hpp"
#include "utils/misc.hpp"
#include "utils/dialog.hpp"
#include "utils/download.hpp"
#include "utils/rating.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

namespace {

/// Single-line label with explicit size/color (used to compose source rows).
brls::Label* sourceLabel(const std::string& text, float size, NVGcolor color, bool grow = false) {
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(color);
    l->setSingleLine(true);
    if (grow) l->setGrow(1);
    return l;
}

/// Small rounded badge (quality / status / source-type chip).
brls::Box* sourcePill(const std::string& text, NVGcolor bg, NVGcolor fg) {
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::ROW);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setHeight(22);
    box->setCornerRadius(11);
    box->setBackgroundColor(bg);
    box->setPaddingLeft(9);
    box->setPaddingRight(9);
    box->setMarginRight(8);
    box->addView(sourceLabel(text, 12, fg));
    return box;
}

/// A selectable release/source line. Paints a light-orange "selected" fill on
/// focus (like the active sidebar menu) instead of the default dark highlight
/// background — the rows ARE the primary interaction for Stremio, so the
/// selected state must read clearly on a 10-foot screen.
class SourceRow : public brls::Box {
public:
    SourceRow() {
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setHeight(46);
        this->setCornerRadius(8);
        this->setHighlightCornerRadius(8);
        this->setFocusable(true);
        this->setPaddingLeft(12);
        this->setPaddingRight(12);
        this->setMarginBottom(6);
        // we paint our own orange fill; keep the gold focus border, drop the
        // default dark highlight background (wrong color for a "selected" row).
        this->setHideHighlightBackground(true);
    }
    void onFocusGained() override {
        brls::Box::onFocusGained();
        NVGcolor c = brls::Application::getTheme().getColor("color/app");
        c.a = 0.22f;  // light orange tint
        this->setBackgroundColor(c);
    }
    void onFocusLost() override {
        brls::Box::onFocusLost();
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    }
};

}  // namespace

MediaMovie::MediaMovie(const plex::Item& item) : itemId(item.ratingKey) {
    brls::Logger::debug("Tab MediaMovie: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/movie.xml");

    // Backs playback before doMovie resolves; non-Stremio backends are always
    // playable (Stremio defers until its sources are resolved).
    this->movieItem = item;
    bool stremioBackend = AppConfig::instance().backend().type() == media::BackendType::Stremio;
    this->hasPlayableSource = !stremioBackend;
    // Stremio has no Lire/version buttons — the inline source list is the play
    // UI. Hide them up front so they never flash before doMovie resolves.
    if (stremioBackend) {
        this->btnPlay->setVisibility(brls::Visibility::GONE);
        this->btnSource->setVisibility(brls::Visibility::GONE);
    }

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

    this->btnPlay->registerClickAction([this](...) {
        // Enabled: play the best source. Muted (Stremio, no playable source):
        // explain why instead of launching a player that would just fail.
        if (this->hasPlayableSource) {
            this->playSource(-1);
        } else {
            Dialog::show("main/stremio/source/none"_i18n);
        }
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
            brls::Application::notify("main/download/completed"_i18n);
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
    // Backends without original-quality download (Stremio: debrid links are
    // ephemeral) never show this button — avoids offering a download next to a
    // movie that has no playable source either.
    if (!AppConfig::instance().backend().caps().downloadOriginal) {
        this->btnDownload->setVisibility(brls::Visibility::GONE);
        return;
    }
    this->btnDownload->setVisibility(brls::Visibility::VISIBLE);
    auto& dm = DownloadManager::instance();
    if (dm.isDownloaded(this->itemId)) {
        this->btnDownload->setText("main/download/completed"_i18n);
    } else if (dm.isDownloading(this->itemId)) {
        this->btnDownload->setText("main/download/downloading"_i18n);
    } else {
        this->btnDownload->setText("main/download/start"_i18n);
    }
}

void MediaMovie::initWatchlist(const media::Item& item) {
    auto& be = AppConfig::instance().backend();
    // gated by the backend's personal-list capability + per-item applicability
    if (be.caps().listKind == media::ListKind::None || !be.canList(item)) return;
    this->listItem = item;
    // label matches the backend's personal list: Plex → Watchlist, Jellyfin/Emby → Favoris
    this->btnWatchlist->setText(be.caps().listKind == media::ListKind::Favorites
                                    ? "main/favorites/title"_i18n
                                    : "main/watchlist/title"_i18n);

    this->btnWatchlist->registerClickAction([this](...) {
        this->toggleWatchlist();
        return true;
    });

    ASYNC_RETAIN
    // the button stays hidden until the state (watchlisted / favorite) is known
    be.getWatchlistState(
        item,
        [ASYNC_TOKEN](bool state) {
            ASYNC_RELEASE
            this->watchlisted = state;
            this->updateWatchlistButton();
            this->btnWatchlist->setVisibility(brls::Visibility::VISIBLE);
            // Favoris is shown: make sure the buttons row (collapsed for Stremio
            // when it had no visible button) is visible again.
            if (this->btnWatchlist->getParent())
                this->btnWatchlist->getParent()->setVisibility(brls::Visibility::VISIBLE);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaMovie list state: {}", ex);
        });
}

void MediaMovie::toggleWatchlist() {
    bool add = !this->watchlisted;
    auto& be = AppConfig::instance().backend();
    bool fav = be.caps().listKind == media::ListKind::Favorites;
    ASYNC_RETAIN
    be.setWatchlisted(
        this->listItem, add,
        [ASYNC_TOKEN, add, fav]() {
            ASYNC_RELEASE
            this->watchlisted = add;
            this->updateWatchlistButton();
            brls::Application::notify(add ? (fav ? "main/favorites/added"_i18n : "main/watchlist/added"_i18n)
                                          : (fav ? "main/favorites/removed"_i18n : "main/watchlist/removed"_i18n));
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
    ASYNC_RETAIN
    // detail (full: streams/chapters/markers)
    AppConfig::instance().backend().getItemDetail(
        this->itemId, true,
        [ASYNC_TOKEN](const media::Item& item) {
            ASYNC_RELEASE
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
                // no backdrop (e.g. an un-scanned Jellyfin/Emby item): drop the
                // banner AND the overlap margins that assumed it. The poster
                // (marginTop 64 in XML, to rise into the banner) and the info
                // column (marginTop 184, to clear it) must reset too, otherwise
                // the title jams against the very top, misaligned with the poster.
                this->bannerBox->setVisibility(brls::Visibility::GONE);
                float topPad = brls::getStyle()["main/content_padding_top_bottom"];
                this->contentRow->setMarginTop(topPad);
                this->contentInfo->setMarginTop(0);
                this->imagePoster->getParent()->setMarginTop(0);
                // no banner: vertically center the info column (title, pills,
                // buttons, synopsis) against the poster instead of top-aligning
                this->contentRow->setAlignItems(brls::AlignItems::CENTER);
                this->invalidate();
            }
            if (item.duration > 0) {
                int min = int(item.duration / 60000);
                this->labelYear->setText(min >= 60
                                             ? fmt::format("{}  ·  {} h {:02d}", item.year, min / 60, min % 60)
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
                // no cast/crew: hide the section header too, not just the row,
                // otherwise a lone "People" title sits over an empty space
                this->peopleHeader->setVisibility(brls::Visibility::GONE);
                this->people->setVisibility(brls::Visibility::GONE);
            }

            this->movieItem = item;  // resolved detail backs per-source playback
            this->viewOffsetMs = item.viewOffset;

            if (AppConfig::instance().backend().type() == media::BackendType::Stremio) {
                // Stremio: no Lire/version buttons — the inline source list is the
                // play/download UI (built here; sets hasPlayableSource). Collapse
                // the now-empty buttons row so it leaves no gap between the genres
                // and the synopsis; initWatchlist re-shows it if Favoris appears.
                this->btnPlay->setVisibility(brls::Visibility::GONE);
                this->btnSource->setVisibility(brls::Visibility::GONE);
                if (this->btnPlay->getParent()) this->btnPlay->getParent()->setVisibility(brls::Visibility::GONE);
                this->buildSources(item);
            } else {
                this->sourcesBox->setVisibility(brls::Visibility::GONE);
                this->hasPlayableSource = true;
                // Plex multiple versions: the selector remembers the choice but
                // v1 playback always uses the first accessible version.
                if (item.media.size() > 1) {
                    std::vector<std::string> names;
                    for (auto& m : item.media)
                        names.push_back(fmt::format("{} {} ({} kbps)", m.videoResolution, m.videoCodec, m.bitrate));
                    this->btnSource->init("main/setting/version"_i18n, names, 0,
                        [this](int index) { this->selectedVersion = index; });
                    this->btnSource->setVisibility(brls::Visibility::VISIBLE);
                } else {
                    this->btnSource->setVisibility(brls::Visibility::GONE);
                }
                this->btnPlay->setMuted(false);
                this->btnPlay->setText(
                    this->viewOffsetMs > 0 ? misc::sec2Time(this->viewOffsetMs / 1000) : "main/media/play"_i18n);
            }

            this->initWatchlist(item);
            // Open the detail at the TOP. "centered" auto-centers the focused
            // Play button on first appear, which scrolls the page down for no
            // reason — hiding the title (backdrop-less items) or the top of the
            // banner (Plex). We snap back to the top once loaded; the centered
            // follow-focus behavior still applies as soon as the user navigates
            // down (buttons → cast → related). Deferred a frame so the layout
            // (collapsed banner/cast) is settled before resetting.
            ASYNC_RETAIN
            brls::sync([ASYNC_TOKEN]() {
                ASYNC_RELEASE
                // Focus a VISIBLE target: the first selectable release (Stremio)
                // or the Play button (Plex/Jellyfin). Stremio with zero sources
                // hides btnPlay, so focusing it would strand the highlight; fall
                // back to Favoris, else let borealis pick (cast/related).
                brls::View* target = this->firstSourceRow;
                if (!target && this->btnPlay->getVisibility() == brls::Visibility::VISIBLE)
                    target = this->btnPlay;
                if (!target && this->btnWatchlist->getVisibility() == brls::Visibility::VISIBLE)
                    target = this->btnWatchlist;
                if (target) brls::Application::giveFocus(target);
                // The scroll uses the "ensure" behavior (movie.xml): focusing the
                // source row keeps it visible WITH the poster/title above it,
                // instead of centering it — so no manual snap-to-top is needed.
            });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->peopleHeader->setVisibility(brls::Visibility::GONE);
            this->people->setVisibility(brls::Visibility::GONE);
        });
}

void MediaMovie::playSource(int mediaIndex) {
    // mediaIndex -1 = best (first accessible); otherwise the chosen source row.
    PlayerView* view = new PlayerView(this->movieItem, this->viewOffsetMs, mediaIndex);
    view->setTitie(this->movieItem.year ? fmt::format("{} ({})", this->movieItem.title, this->movieItem.year)
                                        : this->movieItem.title);
}

void MediaMovie::downloadSource(int mediaIndex) {
    if (mediaIndex < 0 || mediaIndex >= (int)this->movieItem.media.size()) return;
    const media::Media& m = this->movieItem.media[mediaIndex];
    if (!m.playable()) return;  // only playable sources carry a downloadable URL
    // addDownload dedups by ratingKey (one download per movie); reflect the real
    // outcome instead of always claiming "queued".
    auto& dm = DownloadManager::instance();
    if (dm.isDownloaded(this->itemId)) {
        brls::Application::notify("main/download/completed"_i18n);
        return;
    }
    if (dm.isDownloading(this->itemId)) {
        brls::Application::notify("main/download/downloading"_i18n);
        return;
    }
    dm.addDownload(this->movieItem, m.parts.front().key);
    brls::Application::notify("main/download/queued"_i18n);
}

void MediaMovie::buildSources(const media::Item& item) {
    this->sourcesBox->clearViews();
    this->sourcesBox->setVisibility(brls::Visibility::GONE);
    this->noticeBox->clearViews();
    this->noticeBox->setVisibility(brls::Visibility::GONE);
    this->firstSourceRow = nullptr;
    this->scroll->setScrollTopAnchor(nullptr);  // cleared; re-set below if rows exist
    auto theme = brls::Application::getTheme();
    NVGcolor pillBg = theme.getColor("color/pill");
    NVGcolor textCol = theme.getColor("brls/text");
    NVGcolor greyCol = theme.getColor("font/grey");
    NVGcolor goldBg = theme.getColor("color/app");
    NVGcolor goldFg = theme.getColor("brls/button/primary_enabled_text");

    int playable = 0, torrents = 0, links = 0;
    for (auto& m : item.media) {
        if (m.playable()) playable++;
        else if (m.kind == media::SourceKind::Torrent) torrents++;
        else links++;  // External / Youtube
    }
    this->hasPlayableSource = playable > 0;

    std::vector<SourceRow*> rows;     // collected to wire D-pad navigation between lines
    std::vector<std::string> rowIds;  // matching ids (View has no getId() accessor)
    auto makeRow = [&](std::vector<brls::View*> cells) -> SourceRow* {
        auto* row = new SourceRow();
        for (auto* c : cells) row->addView(c);
        std::string id = "movie/source/" + std::to_string(rows.size());
        row->setId(id);
        rows.push_back(row);
        rowIds.push_back(id);
        this->sourcesBox->addView(row);
        return row;
    };

    if (item.media.empty()) {
        // No sources at all (Stremio has no Lire button): an INFO notice — a
        // tinted, accent-bordered card with an info glyph — so it reads as a
        // system message and no longer blends into the grey synopsis above it.
        NVGcolor accent = theme.getColor("color/app");
        NVGcolor tint = accent;
        tint.a = 0.12f;
        NVGcolor border = accent;
        border.a = 0.55f;

        auto* notice = new brls::Box();
        notice->setAxis(brls::Axis::COLUMN);
        notice->setAlignItems(brls::AlignItems::CENTER);
        notice->setCornerRadius(10);
        notice->setHighlightCornerRadius(10);
        notice->setBackgroundColor(tint);
        notice->setBorderColor(border);
        notice->setBorderThickness(1.5f);
        notice->setPadding(18, 18, 18, 18);
        // Focusable so the D-pad can land on it: with no Play button and no
        // source rows, focus would otherwise drop to the cast row with no way
        // back up. Keep its accent border/tint; only hide the default dark
        // highlight background (the focus ring still shows).
        notice->setFocusable(true);
        notice->setHideHighlightBackground(true);
        notice->setId("movie/source/0");

        auto* icon = new SVGImage();
        icon->setImageFromSVGRes("icon/ico-info.svg");
        icon->setWidth(22);
        icon->setHeight(22);
        icon->setMarginBottom(10);
        notice->addView(icon);

        auto* msg = sourceLabel("main/stremio/source/none"_i18n, 14, textCol);
        msg->setSingleLine(false);
        msg->setWidthPercentage(100);
        msg->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        notice->addView(msg);

        // The notice sits ABOVE the synopsis (its own container); the source
        // list (when present) is below it.
        this->noticeBox->addView(notice);
        // Same D-pad wiring as a source row: page top anchor (focusing it scrolls
        // to the top), reached from Favoris above, leading to the cast below.
        notice->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");
        this->btnWatchlist->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/source/0");
        this->firstSourceRow = notice;
        this->scroll->setScrollTopAnchor(notice);
        this->noticeBox->setVisibility(brls::Visibility::VISIBLE);
        return;
    }

    // Playable rows first (ordered best-first by the backend). A = play this
    // release, X = download it. Capped so the list never buries the synopsis.
    const int MAX_PLAYABLE_ROWS = 12;
    int shown = 0;
    for (size_t i = 0; i < item.media.size(); i++) {
        const media::Media& m = item.media[i];
        if (!m.playable()) continue;
        if (shown >= MAX_PLAYABLE_ROWS) break;
        std::vector<brls::View*> cells;
        // quality chip
        cells.push_back(sourcePill(m.videoResolution.empty() ? "SD" : m.videoResolution, pillBg, textCol));
        // status chip: cached debrid (gold) / uncached debrid / direct
        if (m.kind == media::SourceKind::Debrid)
            cells.push_back(m.cached ? sourcePill("main/stremio/source/cached"_i18n, goldBg, goldFg)
                                     : sourcePill("main/stremio/source/uncached"_i18n, pillBg, greyCol));
        else
            cells.push_back(sourcePill("main/stremio/source/direct"_i18n, pillBg, textCol));
        // source name (grows) + meta (codec · size)
        cells.push_back(sourceLabel(m.label, 15, textCol, true));
        if (!m.detail.empty()) cells.push_back(sourceLabel(m.detail, 13, greyCol));
        // trailing download glyph: signals the line is downloadable (X button)
        auto* dl = new SVGImage();
        dl->setImageFromSVGRes("icon/ico-download-light.svg");
        dl->setWidth(17);
        dl->setHeight(17);
        dl->setMarginLeft(12);
        cells.push_back(dl);

        SourceRow* row = makeRow(cells);
        int idx = (int)i;
        row->registerClickAction([this, idx](brls::View*) {
            this->playSource(idx);
            return true;
        });
        // A = play this release (relabel the default "OK" hint to "Lire")
        row->updateActionHint(brls::BUTTON_A, "main/media/play"_i18n);
        // X = download this exact release (hint shown in the action bar)
        row->registerAction("main/download/start"_i18n, brls::BUTTON_X, [this, idx](brls::View*) {
            this->downloadSource(idx);
            return true;
        });
        shown++;
    }
    if (playable > shown) {
        auto* l = sourceLabel(fmt::format(fmt::runtime("main/stremio/source/more"_i18n), playable - shown), 13, greyCol);
        l->setMarginBottom(6);
        this->sourcesBox->addView(l);
    }

    // Non-playable groups: one explanatory, selectable summary row each (A = why).
    if (torrents > 0) {
        SourceRow* row = makeRow({sourcePill("main/stremio/source/torrent_badge"_i18n, pillBg, greyCol),
            sourceLabel(fmt::format(fmt::runtime("main/stremio/source/torrents"_i18n), torrents), 14, greyCol, true)});
        row->registerClickAction([](brls::View*) {
            Dialog::show("main/stremio/source/torrents_help"_i18n);
            return true;
        });
    }
    if (links > 0) {
        SourceRow* row = makeRow({sourcePill("main/stremio/source/link_badge"_i18n, pillBg, greyCol),
            sourceLabel(fmt::format(fmt::runtime("main/stremio/source/links"_i18n), links), 14, greyCol, true)});
        row->registerClickAction([](brls::View*) {
            Dialog::show("main/stremio/source/links_help"_i18n);
            return true;
        });
    }

    // D-pad wiring: the source rows live in the info column; the cast frame
    // below does not geometrically overlap them, so the up/down jumps must be
    // explicit (the old Play button used the same custom DOWN route to the cast).
    for (size_t i = 0; i < rows.size(); i++) {
        if (i > 0) rows[i]->setCustomNavigationRoute(brls::FocusDirection::UP, rowIds[i - 1]);
        if (i + 1 < rows.size()) rows[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN, rowIds[i + 1]);
    }
    if (!rows.empty()) {
        rows.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");
        this->btnWatchlist->setCustomNavigationRoute(brls::FocusDirection::DOWN, rowIds.front());
        this->firstSourceRow = rows.front();
        // First row = scroll's top anchor: focusing it scrolls to the top (poster
        // + synopsis stay visible) instead of centering it — on open and when
        // navigating back up. The rest of the page centers normally.
        this->scroll->setScrollTopAnchor(rows.front());
    }
    this->sourcesBox->setVisibility(brls::Visibility::VISIBLE);
}

void MediaMovie::doRelated() {
    ASYNC_RETAIN
    // all the server's "related" rows, localized titles
    AppConfig::instance().backend().getRelated(this->itemId, 12,
        [ASYNC_TOKEN](const media::Container<media::Hub>& r) {
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
        });
}
