/*
    GMCA — view of a playlist (see playlist_view.hpp).
    Reuses VideoDataSource: movies -> detail page, episodes -> playback,
    X/long-press context menu included (video_card.cpp).
*/

#include "tab/playlist_view.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/music_now_playing.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

PlaylistView::PlaylistView(const plex::Item& item) : playlistId(item.ratingKey), knownCount(item.leafCount) {
    brls::Logger::debug("PlaylistView: create {} ({})", item.title, item.ratingKey);
    this->inflateFromXMLRes("xml/tabs/playlist.xml");

    // header scrolled WITH the grid (title + meta): it belongs to the
    // recycler, which offsets all its cells by the given height
    brls::View* header = brls::View::createFromXMLResource("view/grid_header.xml");
    this->labelTitle = dynamic_cast<brls::Label*>(header->getView("grid/header/title"));
    this->labelMeta = dynamic_cast<brls::Label*>(header->getView("grid/header/meta"));
    this->recycler->setHeaderView(header, 84);

    this->labelTitle->setText(item.title);
    this->updateMeta(item.leafCount, item.duration);

    this->pageSize = this->recycler->spanCount * 3;
    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    auto actionRefresh = [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    };
    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->doRequest();
}

brls::View* PlaylistView::getDefaultFocus() { return this->recycler; }

void PlaylistView::updateMeta(int64_t count, int64_t durationMs) {
    if (count <= 0) {
        this->labelMeta->setVisibility(brls::Visibility::GONE);
        return;
    }
    std::string meta =
        fmt::format("{} {}", count, count > 1 ? "main/playlist/items"_i18n : "main/playlist/item"_i18n);
    if (durationMs > 0) {
        // same convention as the movie page (media_movie.cpp): h/min
        int min = int(durationMs / 60000);
        meta += min >= 60 ? fmt::format("  ·  {} h {:02d}", min / 60, min % 60) : fmt::format("  ·  {} min", min);
    }
    this->labelMeta->setText(meta);
    this->labelMeta->setVisibility(brls::Visibility::VISIBLE);
}

void PlaylistView::doRequest() {
    ASYNC_RETAIN
    // requested offset, not r.StartIndex: Jellyfin/Emby omit StartIndex on an
    // empty past-the-end page (it parses to 0) and would wipe a filled grid
    size_t reqStart = this->startIndex;
    AppConfig::instance().backend().getPlaylistItems(this->playlistId, this->startIndex, this->pageSize,
        [ASYNC_TOKEN, reqStart](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = reqStart + this->pageSize;
            if (r.TotalRecordCount == 0 && reqStart == 0) {
                this->recycler->setEmpty();
            } else if (reqStart == 0) {
                // incomplete header (Item without leafCount): completed on the 1st batch
                if (this->knownCount <= 0) {
                    this->knownCount = r.TotalRecordCount;
                    this->updateMeta(r.TotalRecordCount, 0);
                }
                this->recycler->setDataSource(new VideoDataSource(r.Items));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN, reqStart](const std::string& ex) {
            ASYNC_RELEASE
            if (reqStart > 0) {
                brls::Application::notify(ex);
            } else {
                this->recycler->setError(ex);
            }
        });
}

void ui::presentPlaylist(brls::View* from, const media::Item& item) {
    if (item.playlistType == "audio") {
        // Audio playlist: load its tracks into the shared music controller and
        // open Now Playing (issue #11), not the video poster grid. A single
        // large page fetches the whole queue at once (parity with "play artist"
        // / getArtistTracks, which are likewise unbounded); playlists larger
        // than this are effectively unbounded for playback purposes.
        std::string id = item.ratingKey;
        AppConfig::instance().backend().getPlaylistItems(
            id, 0, 10000,
            [](const media::Container<media::Item>& r) {
                if (!r.Items.empty()) MusicNowPlaying::present(r.Items, 0, false);
            },
            [](const std::string& ex) { brls::Application::notify(ex); });
        return;
    }
    ui::presentDetail(from, new PlaylistView(item));
}
