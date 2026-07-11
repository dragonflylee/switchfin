#include "activity/player_view.hpp"
#include "activity/gallery_activity.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "tab/media_collection.hpp"
#include "tab/media_series.hpp"
#include "tab/media_movie.hpp"
#include "tab/media_music.hpp"
#include "tab/playlist_view.hpp"
#include "tab/hub_view.hpp"
#include "view/music_now_playing.hpp"
#include "utils/misc.hpp"
#include "view/svg_image.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/context_menu.hpp"
#include "view/auto_tab_frame.hpp"

using namespace brls::literals;  // for _i18n

VideoDataSource::VideoDataSource(const MediaList& r) : list(std::move(r)) {}
VideoDataSource::VideoDataSource(const MediaList& r, const std::string& parentId)
    : list(std::move(r)), parentId(parentId) {}

size_t VideoDataSource::getItemCount() { return this->list.size() + (this->moreKey.empty() ? 0 : 1); }

void VideoDataSource::setMore(const std::string& title, const std::string& key) {
    this->moreTitle = title;
    this->moreKey = key;
}

RecyclingGridItem* VideoDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    // end-of-row "+" card -> full hub page
    if (!this->moreKey.empty() && index == this->list.size()) return recycler->dequeueReusableCell("More");

    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);
    cell->setId(item.ratingKey);
    // recycled cell: purge the previous media's poster (otherwise an item
    // without a thumb inherits another's texture — the placeholder reappears)
    cell->picture->clear();
    // music items get a note placeholder (not the video-camera glyph) when they
    // have no cover art; reset per-cell for recycling (issue #11)
    bool isMusicCard = item.type == plex::mediaTypeArtist || item.type == plex::mediaTypeAlbum ||
                       item.type == plex::mediaTypeTrack;
    if (auto* ph = dynamic_cast<SVGImage*>(cell->getView("video/card/placeholder")))
        ph->setImageFromSVGRes(isMusicCard ? "icon/ico-audio.svg" : "icon/ico-media.svg");

    if (item.type == plex::mediaTypeEpisode) {
        if (item.grandparentTitle.empty()) {
            cell->labelTitle->setVisibility(brls::Visibility::GONE);
        } else {
            cell->labelTitle->setText(item.grandparentTitle);
        }
        cell->labelExt->setText(fmt::format("S{}E{} - {}", item.parentIndex, item.index, item.title));

        // season poster, otherwise the show's — not the episode's 16:9
        // capture (portrait cards, user feedback)
        if (!item.parentThumb.empty()) {
            Image::load(cell->picture, item.parentThumb, 325);
        } else if (!item.grandparentThumb.empty()) {
            Image::load(cell->picture, item.grandparentThumb, 325);
        } else if (!item.thumb.empty()) {
            Image::load(cell->picture, item.thumb, 325);
        }
    } else if (item.type == plex::mediaTypeSeason) {
        // a season announces itself by its show: title = show, subtitle = season
        cell->labelTitle->setText(item.parentTitle.empty() ? item.title : item.parentTitle);
        cell->labelExt->setText(item.parentTitle.empty() ? "" : item.title);
        Image::load(cell->picture, item.thumb.empty() ? item.parentThumb : item.thumb, 325);
    } else if (item.type == plex::mediaTypeArtist) {
        // artist: name + album count (square cover, cf. grid geometry)
        cell->labelTitle->setText(item.title);
        if (item.childCount > 0)
            cell->labelExt->setText(fmt::format("{} {}", item.childCount,
                item.childCount > 1 ? "main/music/albums"_i18n : "main/music/album"_i18n));
        else
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        if (!item.thumb.empty()) Image::load(cell->picture, item.thumb, 325);
    } else if (item.type == plex::mediaTypeAlbum) {
        // album: title + artist (fallback year); square cover
        cell->labelTitle->setText(item.title);
        if (!item.parentTitle.empty())
            cell->labelExt->setText(item.parentTitle);
        else if (item.year > 0)
            cell->labelExt->setText(std::to_string(item.year));
        else
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        if (!item.thumb.empty()) Image::load(cell->picture, item.thumb, 325);
    } else if (item.type == plex::mediaTypeTrack) {
        // track: title + artist (fallback duration)
        cell->labelTitle->setText(item.title);
        if (!item.grandparentTitle.empty())
            cell->labelExt->setText(item.grandparentTitle);
        else
            cell->labelExt->setText(misc::sec2Time(item.duration / 1000));
        if (!item.thumb.empty())
            Image::load(cell->picture, item.thumb, 325);
        else if (!item.parentThumb.empty())
            Image::load(cell->picture, item.parentThumb, 325);
    } else {
        cell->labelTitle->setText(item.title);

        if (item.type == plex::mediaTypePlaylist && item.leafCount > 0) {
            // same meta as the Playlists tab — and the label area keeps its
            // standard height (55), a condition of the square render in rows
            cell->labelExt->setText(fmt::format("{} {}", item.leafCount,
                item.leafCount > 1 ? "main/playlist/items"_i18n : "main/playlist/item"_i18n));
            cell->labelExt->setVisibility(brls::Visibility::VISIBLE);
        } else if (item.type == plex::mediaTypeCollection || item.type == plex::mediaTypePlaylist ||
                   item.type.empty()) {
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        } else if (item.type == plex::mediaTypeClip) {
            cell->labelExt->setText(misc::sec2Time(item.duration / 1000));
        } else if (item.year > 0) {
            cell->labelExt->setText(std::to_string(item.year));
        }

        // custom poster (thumb) takes priority; playlists fall back to
        // the composite mosaic generated by the server
        if (!item.thumb.empty()) {
            Image::load(cell->picture, item.thumb, 325);
        } else if (item.type == plex::mediaTypePlaylist && !item.composite.empty()) {
            Image::load(cell->picture, item.composite, 325);
        }
    }

    if (item.played()) {
        cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
        cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
    } else if (item.viewOffset > 0 && item.duration > 0) {
        float percent = float(item.viewOffset) / float(item.duration) * 100.f;
        cell->rectProgress->setWidthPercentage(percent);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::VISIBLE);
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
    } else {
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
    }

    // A-button hint + focus overlay reflect what selecting the card DOES: it
    // PLAYS for items that start playback on select (episode/clip — incl. the
    // continue-watching row), and OPENS a detail page otherwise.
    bool plays = (item.type == plex::mediaTypeEpisode || item.type == plex::mediaTypeClip ||
                  item.type == plex::mediaTypeTrack ||
                  (item.type == plex::mediaTypePlaylist && item.playlistType == "audio"));
    cell->setPlayOverlay(plays);
    cell->updateActionHint(brls::BUTTON_A, plays ? "main/media/play"_i18n : "main/media/open"_i18n);
    return cell;
}

void VideoDataSource::onItemSelected(brls::Box* recycler, size_t index) {
    if (!this->moreKey.empty() && index == this->list.size()) {
        ui::presentDetail(recycler, new HubView(this->moreTitle, this->moreKey));
        return;
    }
    auto& item = this->list.at(index);

    if (item.type == plex::mediaTypeShow) {
        ui::presentDetail(recycler, new MediaSeries(item));
    } else if (item.type == plex::mediaTypeMovie) {
        ui::presentDetail(recycler, new MediaMovie(item));
    } else if (item.type == plex::mediaTypeSeason) {
        ui::presentDetail(recycler, new MediaSeries(item));
    } else if (item.type == plex::mediaTypeCollection) {
        ui::presentDetail(recycler, new MediaCollection(item.ratingKey, plex::mediaTypeCollection));
    } else if (item.type == plex::mediaTypeClip) {
        PlayerView* view = new PlayerView(item);
        view->setTitie(item.year ? fmt::format("{} ({})", item.title, item.year) : item.title);
    } else if (item.type == plex::mediaTypeEpisode) {
        PlayerView* view = new PlayerView(item);
        view->setTitie(fmt::format("S{}E{} - {}", item.parentIndex, item.index, item.title));
        if (!item.grandparentRatingKey.empty()) view->setSeries(item.grandparentRatingKey);
    } else if (item.type == plex::mediaTypePlaylist) {
        // audio playlist -> music queue; video playlist -> PlaylistView (issue #11)
        ui::presentPlaylist(recycler, item);
    } else if (item.type == plex::mediaTypePhoto) {
        // photo: original file served by the Part (PLEX_MIGRATION.md §2.5)
        if (!item.media.empty() && !item.media.front().parts.empty()) {
            std::string url = AppConfig::instance().backend().imageUrl(item.media.front().parts.front().key);
            brls::Application::pushActivity(new GalleryActivity(url));
        }
    } else if (item.type == plex::mediaTypeArtist) {
        ui::presentDetail(recycler, new MediaArtist(item));
    } else if (item.type == plex::mediaTypeAlbum) {
        ui::presentDetail(recycler, new MediaAlbum(item));
    } else if (item.type == plex::mediaTypeTrack) {
        // a lone track (search/hub): play it as a one-item queue
        MusicNowPlaying::present({item}, 0, false);
    } else {
        auto dialog = new brls::Dialog(fmt::format("Unsupported media type: {}", item.type));
        dialog->addButton("hints/cancel"_i18n, []() {});
        dialog->open();
    }
}

void VideoDataSource::onContextMenu(brls::Box* recycler, size_t index) {
    if (index >= this->list.size()) return;  // "+" card: no menu
    auto& item = this->list.at(index);
    brls::Box* menu = new ContextMenu(item, recycler);
    brls::Application::pushActivity(new brls::Activity(menu));
}

int VideoDataSource::setPlayed(const std::string& itemId, bool played) {
    for (size_t i = 0; i < this->list.size(); i++) {
        if (this->list[i].ratingKey != itemId) continue;
        // played() reads viewCount; clear the resume offset when watched so
        // the progress bar gives way to the check mark
        this->list[i].viewCount = played ? 1 : 0;
        if (played) this->list[i].viewOffset = 0;
        return (int)i;
    }
    return -1;
}

void VideoDataSource::clearData() { this->list.clear(); }

void VideoDataSource::appendData(const MediaList& data) {
    this->list.insert(this->list.end(), data.begin(), data.end());
}
