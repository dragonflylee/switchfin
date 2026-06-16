/*
    Copyright 2023 dragonflylee
*/

#include "activity/player_view.hpp"
#include "api/jellyfin.hpp"
#include "tab/media_series.hpp"
#include "view/h_recycling.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/icon_button.hpp"
#include "view/svg_image.hpp"
#include "view/text_box.hpp"
#include "view/video_card.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "view/presenter.hpp"
#include "view/context_menu.hpp"
#include "utils/keybind.hpp"
#include "utils/dialog.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

MediaSeries::MediaSeries(const jellyfin::Episode& item) {
    brls::Logger::debug("Tab MediaSeries: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");

    if (item.Type == jellyfin::mediaTypeSeries) {
        this->seriesId = item.Id;
    } else if (item.SeriesId.is_string()) {
        this->seriesId = item.SeriesId.get<std::string>();
    }

    this->labelTitle->setText(item.Name);
    this->seasons->registerCell("Cell", VideoCardCell::create);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->similar->registerCell("Cell", VideoCardCell::create);
    this->special->registerCell("Cell", VideoCardCell::create);

    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT) {
        this->imageFade->setImageFromRes("img/fade-bottom-light.png");
    }
    // the buttons and the seasons row have no geometric overlap:
    // explicit route (cf. media_movie.cpp)
    this->btnPlay->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");
    this->btnDownload->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");

    this->btnPlay->registerClickAction([this](...) {
        this->doPlay();
        return true;
    });
    this->btnDownload->registerClickAction([this](...) {
        this->doDownloadSeries();
        return true;
    });

    auto& dm = DownloadManager::instance();
    this->updateDownloadButton();
    this->statusSub = dm.getStatusEvent()->subscribe(
        [this](const std::string& id, DownloadStatus status) { this->updateDownloadButton(); });

    this->doSeason();
    this->doSeries();
    this->doSimilar();
    this->doSpecial();
}

MediaSeries::~MediaSeries() {
    brls::Logger::debug("Tab MediaSeries: delete");
    auto& dm = DownloadManager::instance();
    dm.getStatusEvent()->unsubscribe(this->statusSub);
    Image::cancel(this->imageLogo);
    Image::cancel(this->imagePoster);
    Image::cancel(this->imageBackdrop);
}

void MediaSeries::doRequest() {
    // after playback: next episode (Play button) + watched states of the
    // season cards; the episodes are refreshed by MediaSeason itself
    this->doSeason();
}

void MediaSeries::doSeries() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Detail>(
        [ASYNC_TOKEN](const jellyfin::Detail& r) {
            ASYNC_RELEASE
            this->labelTitle->setText(r.Name);
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

            auto poster = r.ImageTags.find(jellyfin::imageTypePrimary);
            if (poster != r.ImageTags.end()) {
                Image::load(this->imagePoster, jellyfin::apiPrimaryImage, r.Id,
                    HTTP::encode_form({
                        {"tag", poster->second},
                        {"maxWidth", "325"},
                    }));
            }

            auto logo = r.ImageTags.find(jellyfin::imageTypeLogo);
            if (logo != r.ImageTags.end()) {
                Image::load(this->imageLogo, jellyfin::apiLogoImage, r.Id,
                    HTTP::encode_form({
                        {"tag", logo->second},
                        {"maxWidth", "440"},
                    }));
            }

            if (r.BackdropImageTags.size() > 0) {
                Image::load(this->imageBackdrop, jellyfin::apiBackdropImage, r.Id, 0,
                    HTTP::encode_form({{"tag", r.BackdropImageTags.at(0)}}));
            } else {
                this->bannerBox->setVisibility(brls::Visibility::GONE);
                this->contentRow->setMarginTop(0);
                this->contentInfo->setMarginTop(0);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserItem, AppConfig::instance().getUserId(), this->seriesId);
}

void MediaSeries::doSeason() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"fields", "ItemCounts"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->labelSeasons->setVisibility(brls::Visibility::GONE);
                this->seasons->setVisibility(brls::Visibility::GONE);
                return;
            }
            this->seasons->setDataSource(new VideoDataSource(r.Items, this->seriesId));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("doSeason {}", ex);
        },
        jellyfin::apiShowSeanon, this->seriesId, query);
}

void MediaSeries::doSimilar() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"limit", "12"},
        {"enableImageTypes", "Primary"},
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
        jellyfin::apiSimilar, this->seriesId, query);
}

void MediaSeries::doSpecial() {
    ASYNC_RETAIN
    jellyfin::getJSON<std::vector<jellyfin::Episode>>(
        [ASYNC_TOKEN](const std::vector<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.size() > 0) {
                this->special->setDataSource(new VideoDataSource(r));
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
        jellyfin::apiItemSpecial, AppConfig::instance().getUserId(), this->seriesId);
}

void MediaSeries::doPlay() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"fields", "MediaSourceCount"},
        {"seriesId", this->seriesId},
    });
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                PlayerView* view = new PlayerView(r.Items[0]);
                view->setTitie(
                    fmt::format("S{}E{} - {}", r.Items[0].ParentIndexNumber, r.Items[0].IndexNumber, r.Items[0].Name));
                brls::sync([view]() { brls::Application::giveFocus(view); });
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiShowNextUp, query);
}

void MediaSeries::doDownloadSeries() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            auto& dm = DownloadManager::instance();
            std::vector<std::string> wanted;
            for (auto& item : r.Items) {
                if (dm.findItem(item.Id) > DownloadStatus::Completed) wanted.push_back(item.Id);
            }
            if (wanted.empty()) {
                brls::Application::notify("main/download/completed"_i18n);
                return;
            }
            Dialog::cancelable(
                fmt::format(fmt::runtime("main/download/confirm_season"_i18n), wanted.size()), [wanted]() {
                    auto& dm = DownloadManager::instance();
                    int qi = AppConfig::instance().getValueIndex(AppConfig::DOWNLOAD_QUALITY);
                    for (auto& key : wanted) dm.addDownload(key, static_cast<DownloadQuality>(qi));
                    brls::Application::notify("main/download/queued"_i18n);
                });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiShowEpisodes, this->seriesId, "");
}

void MediaSeries::updateDownloadButton() {
    auto& dm = DownloadManager::instance();
    auto it = dm.findSeries(this->seriesId);
    if (it.first == 0) {
        this->btnDownload->setText("main/download/start"_i18n);
    } else if (it.first == it.second) {
        this->btnDownload->setText("main/download/completed"_i18n);
    } else {
        this->btnDownload->setText(fmt::format("{} {}/{}", "main/download/downloading"_i18n, it.second, it.first));
    }
}
