#include "activity/player_view.hpp"
#include "tab/media_movie.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/text_box.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "view/mpv_core.hpp"
#include "api/jellyfin.hpp"
#include "utils/misc.hpp"
#include "utils/download.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

MediaMovie::MediaMovie(const jellyfin::Item& item) : itemId(item.Id) {
    brls::Logger::debug("Tab MediaMovie: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/movie.xml");

    this->headerTitle->setTitle(item.Name);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->similar->registerCell("Cell", VideoCardCell::create);

    this->btnPlay->registerClickAction([this, item](...) {
        PlayerView* view = new PlayerView(item, this->playTicks, this->sourceId);
        view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
        return true;
    });

    auto& dm = DownloadManager::instance();
    if (dm.isDownloaded(item.Id)) {
        this->btnDownload->setText("main/download/completed"_i18n);
    } else if (dm.isDownloading(item.Id)) {
        this->btnDownload->setText("main/download/downloading"_i18n);
    }
    this->btnDownload->registerClickAction([this, item](...) {
        auto& dm = DownloadManager::instance();
        if (dm.isDownloaded(item.Id)) {
            brls::Application::notify("main/download/completed"_i18n);
        } else if (dm.isDownloading(item.Id)) {
            brls::Application::notify("main/download/downloading"_i18n);
        } else {
            int qi = AppConfig::instance().getValueIndex(AppConfig::DOWNLOAD_QUALITY);
            dm.addDownload(item, static_cast<DownloadQuality>(qi));
            this->btnDownload->setText("main/download/queued"_i18n);
            brls::Application::notify("main/download/queued"_i18n);
        }
        return true;
    });

    this->doMovie();
    this->doSimilar();

    auto logo = item.ImageTags.find(jellyfin::imageTypePrimary);
    if (logo != item.ImageTags.end()) {
        Image::load(this->imageLogo, jellyfin::apiPrimaryImage, item.Id,
            HTTP::encode_form({
                {"tag", logo->second},
                {"maxWidth", "240"},
            }));
        this->imageLogo->setVisibility(brls::Visibility::VISIBLE);
    }
}

MediaMovie::~MediaMovie() {
    brls::Logger::debug("Tab MediaMovie: delete");
    Image::cancel(this->imageLogo);
}

void MediaMovie::doRequest() {
    int64_t ticks = MPVCore::instance().playback_time;
    this->playTicks = ticks * jellyfin::PLAYTICKS;
    this->btnPlay->setText(ticks > 0 ? misc::sec2Time(ticks) : "main/media/play"_i18n);
}

void MediaMovie::doMovie() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Detail>(
        [ASYNC_TOKEN](const jellyfin::Detail& r) {
            ASYNC_RELEASE
            this->headerTitle->setTitle(r.Name);
            this->labelYear->setText(std::to_string(r.ProductionYear));
            if (r.OfficialRating.empty()) {
                this->parentalRating->getParent()->setVisibility(brls::Visibility::GONE);
            } else {
                this->parentalRating->setText(r.OfficialRating);
                this->parentalRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }
            if (r.CommunityRating == 0.f) {
                this->labelRating->getParent()->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelRating->setText(fmt::format("{:.1f}", r.CommunityRating));
                this->labelRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }
            this->labelOverview->setText(r.Overview);

            if (r.Genres.empty()) {
                this->labelGenres->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelGenres->setText(fmt::format("{}", fmt::join(r.Genres, ", ")));
                this->labelGenres->setVisibility(brls::Visibility::VISIBLE);
            }
            if (r.People.size() > 0) {
                this->people->setDataSource(new PeopleDataSource(r.People));
            } else {
                this->people->setVisibility(brls::Visibility::GONE);
            }

            auto logo = r.ImageTags.find(jellyfin::imageTypePrimary);
            if (logo != r.ImageTags.end()) {
                Image::load(this->imageLogo, jellyfin::apiPrimaryImage, r.Id,
                    HTTP::encode_form({
                        {"tag", logo->second},
                        {"maxWidth", "240"},
                    }));
            }

            if (r.MediaSources.size() > 1) {
                std::vector<std::string> names, ids;
                for (auto& it : r.MediaSources) {
                    names.push_back(it.Name);
                    ids.push_back(it.Id);
                }
                this->btnSource->init(
                    "main/setting/version"_i18n, names, 0, [this, ids](int index) { this->sourceId = ids[index]; });
                this->btnSource->setVisibility(brls::Visibility::VISIBLE);
            }

            this->playTicks = r.UserData.PlaybackPositionTicks;
            this->btnPlay->setText(
                this->playTicks > 0 ? misc::sec2Time(this->playTicks / jellyfin::PLAYTICKS) : "main/media/play"_i18n);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserItem, AppConfig::instance().getUserId(), this->itemId);
}

void MediaMovie::doSimilar() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"limit", "12"},
        {"fields", "ItemCounts"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                this->similar->setDataSource(new VideoDataSource(r.Items));
                this->similar->setVisibility(brls::Visibility::VISIBLE);
                this->labelSimilar->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->similar->setVisibility(brls::Visibility::GONE);
                this->labelSimilar->setVisibility(brls::Visibility::GONE);
                this->similar->clearData();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->similar->setVisibility(brls::Visibility::GONE);
            this->labelSimilar->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        jellyfin::apiSimilar, this->itemId, query);
}