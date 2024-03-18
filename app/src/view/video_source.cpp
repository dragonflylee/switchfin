#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "view/video_view.hpp"
#include "view/svg_image.hpp"
#include "tab/media_series.hpp"
#include "tab/media_collection.hpp"
#include "tab/music_album.hpp"
#include "utils/misc.hpp"

VideoDataSource::VideoDataSource(const MediaList& r) : list(std::move(r)) {}

size_t VideoDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* VideoDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);

    if (item.Type == jellyfin::mediaTypeEpisode) {
        cell->labelTitle->setText(item.SeriesName);
        cell->labelExt->setText(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));

        if (item.ParentBackdropImageTags.empty()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId,
                HTTP::encode_form({{"tag", item.SeriesPrimaryImageTag}, {"maxWidth", "400"}}));
        } else {
            Image::load(cell->picture, jellyfin::apiBackdropImage, item.ParentBackdropItemId,
                HTTP::encode_form({{"tag", item.ParentBackdropImageTags.at(0)}, {"maxWidth", "400"}}));
        }
    } else {
        cell->labelTitle->setText(item.Name);
        cell->labelExt->setText(item.ProductionYear > 0 ? std::to_string(item.ProductionYear) : "");

        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end())
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "400"}}));
    }

    if (item.UserData.Played) {
        cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
        cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
    } else if (item.UserData.PlaybackPositionTicks) {
        cell->labelRating->setText(
            fmt::format("{}/{}", misc::sec2Time(item.UserData.PlaybackPositionTicks / jellyfin::PLAYTICKS),
                misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS)));
        cell->rectProgress->setWidthPercentage(item.UserData.PlayedPercentage);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::VISIBLE);
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
    } else {
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
    }
    return cell;
}

void VideoDataSource::onItemSelected(brls::View* recycler, size_t index) {
    auto& item = this->list.at(index);

    if (item.Type == jellyfin::mediaTypeSeries) {
        recycler->present(new MediaSeries(item.Id));
    } else if (item.Type == jellyfin::mediaTypeFolder) {
        recycler->present(new MediaCollection(item.Id));
    } else if (item.Type == jellyfin::mediaTypeBoxSet) {
        recycler->present(new MediaCollection(item.Id));
    } else if (item.Type == jellyfin::mediaTypeMovie) {
        VideoView* view = new VideoView(item);
        view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
        brls::sync([view]() { brls::Application::giveFocus(view); });
    } else if (item.Type == jellyfin::mediaTypeEpisode) {
        VideoView* view = new VideoView(item);
        view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        view->setSeries(item.SeriesId);
        brls::sync([view]() { brls::Application::giveFocus(view); });
    } else if (item.Type == jellyfin::mediaTypeMusicAlbum) {
        recycler->present(new MusicAlbum(item.Id));
    } else {
        brls::Logger::warning("onItemSelected type {}", item.Type);
    }
}

void VideoDataSource::clearData() { this->list.clear(); }

void VideoDataSource::appendData(const MediaList& data) {
    this->list.insert(this->list.end(), data.begin(), data.end());
}
