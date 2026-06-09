#include "activity/player_view.hpp"
#include "activity/gallery_activity.hpp"
#include "api/plex.hpp"
#include "tab/media_collection.hpp"
#include "tab/media_series.hpp"
#include "tab/media_movie.hpp"
#include "utils/misc.hpp"
#include "view/svg_image.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/context_menu.hpp"

using namespace brls::literals;  // for _i18n

VideoDataSource::VideoDataSource(const MediaList& r) : list(std::move(r)) {}
VideoDataSource::VideoDataSource(const MediaList& r, const std::string& parentId)
    : list(std::move(r)), parentId(parentId) {}

size_t VideoDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* VideoDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);
    cell->setId(item.ratingKey);
    if (item.type == plex::mediaTypeEpisode) {
        if (item.grandparentTitle.empty()) {
            cell->labelTitle->setVisibility(brls::Visibility::GONE);
        } else {
            cell->labelTitle->setText(item.grandparentTitle);
        }
        cell->labelExt->setText(fmt::format("S{}E{} - {}", item.parentIndex, item.index, item.title));

        // affiche de la saison, sinon de la série — pas la capture 16:9 de
        // l'épisode (cartes portrait, retour utilisateur)
        if (!item.parentThumb.empty()) {
            Image::load(cell->picture, item.parentThumb, 325);
        } else if (!item.grandparentThumb.empty()) {
            Image::load(cell->picture, item.grandparentThumb, 325);
        } else if (!item.thumb.empty()) {
            Image::load(cell->picture, item.thumb, 325);
        }
    } else {
        cell->labelTitle->setText(item.title);

        if (item.type == plex::mediaTypeCollection || item.type.empty()) {
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        } else if (item.type == plex::mediaTypeClip) {
            cell->labelExt->setText(misc::sec2Time(item.duration / 1000));
        } else if (item.year > 0) {
            cell->labelExt->setText(std::to_string(item.year));
        }

        Image::load(cell->picture, item.thumb, 325);
    }

    if (item.played()) {
        cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
        cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
        cell->labelRating->setVisibility(brls::Visibility::INVISIBLE);
    } else if (item.viewOffset > 0 && item.duration > 0) {
        float percent = float(item.viewOffset) / float(item.duration) * 100.f;
        if (item.type == plex::mediaTypeEpisode || item.type == plex::mediaTypeMovie) {
            cell->labelRating->setText(
                fmt::format("{}/{}", misc::sec2Time(item.viewOffset / 1000), misc::sec2Time(item.duration / 1000)));
        } else {
            cell->labelRating->setText(fmt::format("{:.2f}%", percent));
        }
        cell->labelRating->setVisibility(brls::Visibility::VISIBLE);
        cell->rectProgress->setWidthPercentage(percent);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::VISIBLE);
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
    } else {
        cell->labelRating->setVisibility(brls::Visibility::INVISIBLE);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
    }
    return cell;
}

void VideoDataSource::onItemSelected(brls::Box* recycler, size_t index) {
    auto& item = this->list.at(index);

    if (item.type == plex::mediaTypeShow) {
        recycler->present(new MediaSeries(item));
    } else if (item.type == plex::mediaTypeMovie) {
        recycler->present(new MediaMovie(item));
    } else if (item.type == plex::mediaTypeSeason) {
        recycler->present(new MediaSeries(item));
    } else if (item.type == plex::mediaTypeCollection) {
        recycler->present(new MediaCollection(item.ratingKey, plex::mediaTypeCollection));
    } else if (item.type == plex::mediaTypeClip) {
        PlayerView* view = new PlayerView(item);
        view->setTitie(item.year ? fmt::format("{} ({})", item.title, item.year) : item.title);
    } else if (item.type == plex::mediaTypeEpisode) {
        PlayerView* view = new PlayerView(item);
        view->setTitie(fmt::format("S{}E{} - {}", item.parentIndex, item.index, item.title));
        if (!item.grandparentRatingKey.empty()) view->setSeries(item.grandparentRatingKey);
    } else if (item.type == plex::mediaTypePhoto) {
        // photo : fichier original servi par la Part (PLEX_MIGRATION.md §2.5)
        if (!item.media.empty() && !item.media.front().parts.empty()) {
            auto& conf = AppConfig::instance();
            std::string url = plex::withToken(conf.getUrl() + item.media.front().parts.front().key, conf.getToken());
            brls::Application::pushActivity(new GalleryActivity(url));
        }
    } else {
        auto dialog = new brls::Dialog(fmt::format("Unsupported media type: {}", item.type));
        dialog->addButton("hints/cancel"_i18n, []() {});
        dialog->open();
    }
}

void VideoDataSource::onContextMenu(brls::Box* recycler, size_t index) {
    auto& item = this->list.at(index);
    brls::Box* menu = new ContextMenu(item);
    brls::Application::pushActivity(new brls::Activity(menu));
}

void VideoDataSource::clearData() { this->list.clear(); }

void VideoDataSource::appendData(const MediaList& data) {
    this->list.insert(this->list.end(), data.begin(), data.end());
}
