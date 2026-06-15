#include "activity/player_view.hpp"
#include "activity/gallery_activity.hpp"
#include "tab/media_collection.hpp"
#include "tab/media_series.hpp"
#include "tab/media_movie.hpp"
#include "tab/music_album.hpp"
#include "tab/song_list.hpp"
#include "tab/playlist.hpp"
#include "utils/misc.hpp"
#include "utils/keybind.hpp"
#include "view/svg_image.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/context_menu.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

class EpisodeCardCell : public BaseCardCell {
public:
    EpisodeCardCell() { this->inflateFromXMLRes("xml/view/episode_card.xml"); }

    BRLS_BIND(brls::Label, labelName, "episode/card/name");
    BRLS_BIND(brls::Label, labelOverview, "episode/card/overview");
    BRLS_BIND(SVGImage, badgeFavorite, "video/card/badge/favorite");
};

class EpisodeDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Episode>;

    explicit EpisodeDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("EpisodeDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        EpisodeCardCell* cell = dynamic_cast<EpisodeCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.Id);

        auto epimage = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (epimage != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", epimage->second}, {"fillWidth", "300"}}));
        } else if (item.SeriesId.is_string()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId.get<std::string>(),
                HTTP::encode_form({{"tag", item.SeriesPrimaryImageTag}, {"fillWidth", "300"}}));
        }

        if (item.IndexNumber > 0) {
            cell->labelName->setText(fmt::format("{}. {}", item.IndexNumber, item.Name));
        } else {
            cell->labelName->setText(item.Name);
        }
        cell->labelOverview->setText(item.Overview);

        if (item.UserData.IsFavorite) {
            cell->badgeFavorite->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->badgeFavorite->setVisibility(brls::Visibility::INVISIBLE);
        }

        if (item.UserData.Played) {
            cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
            cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
        } else if (item.UserData.PlaybackPositionTicks) {
            cell->rectProgress->setWidthPercentage(item.UserData.PlayedPercentage);
            cell->rectProgress->getParent()->setVisibility(brls::Visibility::VISIBLE);
            cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        } else {
            cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
            cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        }

        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        PlayerView* view = new PlayerView(item);
        view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        if (item.SeriesId.is_string()) view->setSeries(item.SeriesId.get<std::string>());
        brls::sync([view]() { brls::Application::giveFocus(view); });
    }

    void onContextMenu(EpisodeCardCell* cell, size_t index) {
        auto& item = this->list.at(index);
        brls::Box* menu = new ContextMenu(item, cell);
        brls::Application::pushActivity(new brls::Activity(menu));
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

class MediaSeason : public brls::Box {
public:
    MediaSeason(const jellyfin::Season& item) : seriesId(item.SeriesId), seasonId(item.Id) {
        this->inflateFromXMLRes("xml/tabs/seasons.xml");

        this->recycler->registerCell("Cell", []() {
            auto cell = new EpisodeCardCell();
            auto actionListener = [cell](brls::View*) -> bool {
                brls::Box* view = cell->getParent()->getParent();
                RecyclingView* recycler = dynamic_cast<RecyclingView*>(view);
                if (!recycler) return false;
                EpisodeDataSource* dataSrc = dynamic_cast<EpisodeDataSource*>(recycler->getDataSource());
                if (!dataSrc) return false;
                dataSrc->onContextMenu(cell, cell->getIndex());
                return true;
            };
            cell->registerAction("hints/submit"_i18n, brls::BUTTON_X, actionListener, true);
            cell->registerAction(KeyBind::getSetting(), actionListener);
            return cell;
        });

        this->doRequest();
    }

    void doRequest() {
        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"seasonId", this->seasonId},
            {"fields", "ItemCounts,PrimaryImageAspectRatio,Chapters,Overview"},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
                ASYNC_RELEASE
                this->recycler->setDataSource(new EpisodeDataSource(r.Items));
                brls::sync([this] { brls::Application::giveFocus(this->recycler); });
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->recycler->setError(ex);
            },
            jellyfin::apiShowEpisodes, this->seriesId, query);
    }

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/episodes");

    std::string seriesId;
    std::string seasonId;
};

VideoDataSource::VideoDataSource(const MediaList& r) : list(std::move(r)) {}
VideoDataSource::VideoDataSource(const MediaList& r, const std::string& parentId)
    : list(std::move(r)), parentId(parentId) {}

size_t VideoDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* VideoDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);
    cell->setId(item.Id);
    if (item.Type == jellyfin::mediaTypeEpisode) {
        if (item.SeriesName.empty()) {
            cell->labelTitle->setVisibility(brls::Visibility::GONE);
        } else {
            cell->labelTitle->setText(item.SeriesName);
        }
        cell->labelExt->setText(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));

        auto it = item.ImageTags.find(jellyfin::imageTypeThumb);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiThumbImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "325"}}));
        } else if (item.ParentThumbImageTag.size() > 0) {
            Image::load(cell->picture, jellyfin::apiThumbImage, item.ParentThumbItemId,
                HTTP::encode_form({{"tag", item.ParentThumbImageTag}, {"maxWidth", "325"}}));
        } else if (item.ParentBackdropImageTags.size() > 0) {
            Image::load(cell->picture, jellyfin::apiBackdropImage, item.ParentBackdropItemId, 0,
                HTTP::encode_form({{"tag", item.ParentBackdropImageTags.at(0)}, {"maxWidth", "325"}}));
        } else if (item.SeriesId.is_string()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId.get<std::string>(),
                HTTP::encode_form({{"tag", item.SeriesPrimaryImageTag}, {"maxWidth", "325"}}));
        }
    } else {
        cell->labelTitle->setText(item.Name);

        if (item.Type == jellyfin::mediaTypeGenre || item.Type == jellyfin::mediaTypeBook) {
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        } else if (item.Type == jellyfin::mediaTypeVideo) {
            cell->labelExt->setText(misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS));
        } else if (item.ProductionYear > 0) {
            cell->labelExt->setText(std::to_string(item.ProductionYear));
        }

        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "325"}}));
        }
    }

    if (item.UserData.IsFavorite) {
        cell->badgeFavorite->setVisibility(brls::Visibility::VISIBLE);
    } else {
        cell->badgeFavorite->setVisibility(brls::Visibility::INVISIBLE);
    }

    if (item.UserData.Played) {
        cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
        cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
        cell->labelRating->setVisibility(brls::Visibility::INVISIBLE);
    } else if (item.UserData.PlaybackPositionTicks) {
        if (item.Type == jellyfin::mediaTypeEpisode || item.Type == jellyfin::mediaTypeMovie) {
            cell->labelRating->setText(
                fmt::format("{}/{}", misc::sec2Time(item.UserData.PlaybackPositionTicks / jellyfin::PLAYTICKS),
                    misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS)));
        } else {
            cell->labelRating->setText(fmt::format("{:.2f}%", item.UserData.PlayedPercentage));
        }
        cell->labelRating->setVisibility(brls::Visibility::VISIBLE);
        cell->rectProgress->setWidthPercentage(item.UserData.PlayedPercentage);
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

    if (item.Type == jellyfin::mediaTypeSeries) {
        recycler->present(new MediaSeries(item));
    } else if (item.Type == jellyfin::mediaTypeSeason) {
        recycler->present(new MediaSeason(item));
    } else if (item.Type == jellyfin::mediaTypeMovie) {
        recycler->present(new MediaMovie(item));
    } else if (item.Type == jellyfin::mediaTypeFolder || item.Type == jellyfin::mediaTypeBoxSet ||
               item.Type == jellyfin::mediaTypePhotoAlbum) {
        recycler->present(new MediaCollection(item.Id));
    } else if (item.Type == jellyfin::mediaTypeMusicVideo || item.Type == jellyfin::mediaTypeVideo) {
        PlayerView* view = new PlayerView(item);
        view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
    } else if (item.Type == jellyfin::mediaTypeEpisode) {
        PlayerView* view = new PlayerView(item);
        view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        if (item.SeriesId.is_string()) view->setSeries(item.SeriesId.get<std::string>());
    } else if (item.Type == jellyfin::mediaTypeMusicAlbum) {
        recycler->present(new MusicAlbum(item));
    } else if (item.Type == jellyfin::mediaTypeMusicArtist) {
        recycler->present(new SongList(this->parentId, item.Id));
    } else if (item.Type == jellyfin::mediaTypePlaylist) {
        recycler->present(new Playlist(item));
    } else if (item.Type == jellyfin::mediaTypePhoto) {
        auto& conf = AppConfig::instance();
        std::string query = HTTP::encode_form({{"api_key", conf.getToken()}});
        std::string url = conf.getUrl() + fmt::format(fmt::runtime(jellyfin::apiDownload), item.Id, query);
        brls::Application::pushActivity(new GalleryActivity(url));
    } else {
        auto dialog = new brls::Dialog(fmt::format("Unsupported media type: {}", item.Type));
        dialog->addButton("hints/cancel"_i18n, []() {});
        dialog->open();
    }
}

void VideoDataSource::onContextMenu(VideoCardCell* cell, size_t index) {
    auto& item = this->list.at(index);
    brls::Box* menu = new ContextMenu(item, cell);
    brls::Application::pushActivity(new brls::Activity(menu));
}

void VideoDataSource::clearData() { this->list.clear(); }

void VideoDataSource::appendData(const MediaList& data) {
    this->list.insert(this->list.end(), data.begin(), data.end());
}

ProgramDataSource::ProgramDataSource(const MediaList& r) : list(std::move(r)) {
    brls::Logger::debug("ProgramDataSource: create {}", r.size());
}

size_t ProgramDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* ProgramDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);
    cell->labelTitle->setText(item.Name);
    cell->labelExt->setText(item.ChannelName);
    return cell;
}

void ProgramDataSource::onItemSelected(brls::Box* recycler, size_t index) {
    jellyfin::Item channel;
    auto& item = this->list.at(index);

    channel.Id = item.ChannelId;
    channel.Name = item.ChannelName;
    channel.Type = jellyfin::mediaTypeTvChannel;
    channel.RunTimeTicks = item.RunTimeTicks;
    PlayerView* view = new PlayerView(channel);
    view->setTitie(item.Name);
}

void ProgramDataSource::clearData() { this->list.clear(); }

void ProgramDataSource::appendData(const MediaList& data) {
    this->list.insert(this->list.end(), data.begin(), data.end());
}
