#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "view/video_view.hpp"
#include "tab/media_series.hpp"

VideoDataSource::VideoDataSource(const MediaList& r) : list(std::move(r)) {}

size_t VideoDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* VideoDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);

    if (item.Type == jellyfin::mediaTypeEpisode) {
        cell->labelTitle->setText(item.SeriesName);
        cell->labelExt->setText(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));

        Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId,
            HTTP::encode_form({{"tag", item.SeriesPrimaryImageTag}, {"maxWidth", "240"}}));
    } else {
        cell->labelTitle->setText(item.Name);
        cell->labelExt->setText(item.ProductionYear > 0 ? std::to_string(item.ProductionYear) : "");

        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end())
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "200"}}));
    }
    return cell;
}

void VideoDataSource::onItemSelected(brls::View* recycler, size_t index) {
    auto& item = this->list.at(index);

    if (item.Type == jellyfin::mediaTypeSeries) {
        brls::View* view = dynamic_cast<brls::View*>(recycler);
        view->present(new MediaSeries(item));
    } else if (item.Type == jellyfin::mediaTypeMovie) {
        VideoView* view = new VideoView(item);
        view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
        brls::sync([view]() { brls::Application::giveFocus(view); });
    } else if (item.Type == jellyfin::mediaTypeEpisode) {
        VideoView* view = new VideoView(item);
        view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        view->setSeries(item.SeriesId);
        brls::sync([view]() { brls::Application::giveFocus(view); });
    } else {
        brls::Logger::debug("unsupport type {}", item.Type);
    }
}

void VideoDataSource::clearData() { this->list.clear(); }

void VideoDataSource::appendData(const MediaList& data) {
    this->list.insert(this->list.end(), data.begin(), data.end());
}
