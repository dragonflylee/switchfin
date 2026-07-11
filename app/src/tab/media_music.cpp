/*
    GMCA — music detail views (issue #11). See tab/media_music.hpp.
*/

#include "tab/media_music.hpp"
#include "api/backend.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "utils/image.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/music_now_playing.hpp"
#include "view/icon_button.hpp"

using namespace brls::literals;  // for _i18n

// ---- Album track list --------------------------------------------------------

/// Album header cell (cover + info + Play): first flow cell, scrolls with the
/// tracks. Not focusable itself; navigation reaches the Play button.
class AlbumHeaderCell : public RecyclingGridItem {
public:
    AlbumHeaderCell() {
        this->inflateFromXMLRes("xml/view/album_header.xml");
        this->setFocusable(false);
        this->btnPlay->registerClickAction([this](...) {
            if (this->onPlay) this->onPlay();
            return true;
        });
    }

    void setItem(const media::Item& album, size_t trackCount) {
        this->cover->clear();
        const std::string& art = album.thumb.empty() ? album.parentThumb : album.thumb;
        if (!art.empty()) Image::load(this->cover, art, 225);
        this->labelTitle->setText(album.title);

        std::string meta = album.parentTitle;  // album artist
        if (album.year > 0) meta += (meta.empty() ? "" : "  ·  ") + std::to_string(album.year);
        int64_t n = album.leafCount > 0 ? album.leafCount : (int64_t)trackCount;
        if (n > 0) {
            std::string tracks = fmt::format("{} {}", n, n > 1 ? "main/music/tracks"_i18n : "main/music/track"_i18n);
            meta += (meta.empty() ? "" : "  ·  ") + tracks;
        }
        this->labelMeta->setText(meta);
        this->labelMeta->setVisibility(meta.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }

    void prepareForReuse() override { this->cover->clear(); }  // transparent -> note placeholder
    void cacheForReuse() override { Image::cancel(this->cover); }

    std::function<void()> onPlay = nullptr;

private:
    BRLS_BIND(brls::Image, cover, "album/image/cover");
    BRLS_BIND(brls::Label, labelTitle, "album/label/title");
    BRLS_BIND(brls::Label, labelMeta, "album/label/meta");
    BRLS_BIND(IconButton, btnPlay, "album/play");
};

/// Track row (BaseCardCell for the shared focus/ticker/long-press machinery).
class TrackCell : public BaseCardCell {
public:
    TrackCell() { this->inflateFromXMLRes("xml/view/track_row.xml"); }

    static TrackCell* create() { return new TrackCell(); }

    BRLS_BIND(brls::Label, labelIndex, "track/row/index");
};

/// Album view source: index 0 = header, 1..N = tracks. Fixed heights (no yoga
/// measuring), like SeasonEpisodesDataSource.
class AlbumTracksDataSource : public RecyclingGridDataSource {
public:
    static constexpr float HEADER_HEIGHT = 255;  // cover 225 + air 30
    static constexpr float ROW_HEIGHT = 56;      // 46 row + padding 2x5

    AlbumTracksDataSource(const media::Item& album, const std::vector<media::Item>& tracks)
        : album(album), list(tracks) {}

    size_t getItemCount() override { return this->list.size() + 1; }

    float heightForRow(brls::View* recycler, size_t index) override {
        return index == 0 ? HEADER_HEIGHT : ROW_HEIGHT;
    }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        if (index == 0) {
            auto* header = dynamic_cast<AlbumHeaderCell*>(recycler->dequeueReusableCell("Header"));
            header->setItem(this->album, this->list.size());
            header->onPlay = [this]() { MusicNowPlaying::present(this->list, 0, false); };
            return header;
        }

        auto* cell = dynamic_cast<TrackCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index - 1);
        cell->setId(item.ratingKey);

        cell->picture->clear();
        std::string art = !item.thumb.empty() ? item.thumb : (album.thumb.empty() ? item.parentThumb : album.thumb);
        if (!art.empty()) Image::load(cell->picture, art, 92);

        cell->labelIndex->setText(item.index > 0 ? std::to_string(item.index) : "");
        cell->labelTitle->setText(item.title);
        cell->labelExt->setText(item.duration > 0 ? misc::sec2Time(item.duration / 1000) : "");
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        if (index == 0) return;  // header acts through its Play button
        MusicNowPlaying::present(this->list, index - 1, false);
    }

    void clearData() override { this->list.clear(); }

private:
    media::Item album;
    std::vector<media::Item> list;
};

// ---- MediaAlbum --------------------------------------------------------------

MediaAlbum::MediaAlbum(const media::Item& item) : album(item) {
    this->inflateFromXMLRes("xml/tabs/album.xml");
    this->recycler->registerCell("Header", []() { return new AlbumHeaderCell(); });
    this->recycler->registerCell("Cell", []() { return new TrackCell(); });
    this->doRequest();
}

void MediaAlbum::doRequest() {
    ASYNC_RETAIN
    AppConfig::instance().backend().getChildren(
        this->album.ratingKey,
        [ASYNC_TOKEN](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            this->recycler->setDataSource(new AlbumTracksDataSource(this->album, r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setError(ex);
        });
}

// ---- MediaArtist -------------------------------------------------------------

MediaArtist::MediaArtist(const media::Item& item) : artist(item) {
    this->inflateFromXMLRes("xml/tabs/media.xml");
    this->recycler->itemImageRatio = 1.0f;  // square album covers
    this->recycler->registerCell("Cell", VideoCardCell::create);

    brls::View* header = brls::View::createFromXMLResource("view/artist_header.xml");
    this->labelTitle = dynamic_cast<brls::Label*>(header->getView("grid/header/title"));
    this->labelMeta = dynamic_cast<brls::Label*>(header->getView("grid/header/meta"));
    if (brls::View* btn = header->getView("artist/shuffle"))
        btn->registerClickAction([this](...) {
            this->shufflePlay();
            return true;
        });
    this->recycler->setHeaderView(header, 150);
    if (this->labelTitle) this->labelTitle->setText(item.title);

    this->doRequest();
}

void MediaArtist::shufflePlay() {
    ASYNC_RETAIN
    AppConfig::instance().backend().getArtistTracks(
        this->artist.ratingKey,
        [ASYNC_TOKEN](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            if (!r.Items.empty()) MusicNowPlaying::present(r.Items, 0, true);  // shuffle
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        });
}

void MediaArtist::doRequest() {
    ASYNC_RETAIN
    AppConfig::instance().backend().getArtistAlbums(
        this->artist.ratingKey,
        [ASYNC_TOKEN](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            if (this->labelMeta) {
                this->labelMeta->setText(fmt::format("{} {}", r.Items.size(),
                    r.Items.size() > 1 ? "main/music/albums"_i18n : "main/music/album"_i18n));
            }
            this->recycler->setDataSource(new VideoDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setError(ex);
        });
}
