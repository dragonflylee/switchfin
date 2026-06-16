#include "activity/player_view.hpp"
#include "tab/media_movie.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/text_box.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "view/mpv_core.hpp"
#include "view/icon_button.hpp"
#include "api/jellyfin.hpp"
#include "utils/misc.hpp"
#include "utils/dialog.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

MediaMovie::MediaMovie(const jellyfin::Item& item) : itemId(item.Id) {
    brls::Logger::debug("Tab MediaMovie: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/movie.xml");

    this->labelTitle->setText(item.Name);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->similar->registerCell("Cell", VideoCardCell::create);

    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT) {
        this->imageFade->setImageFromRes("img/fade-bottom-light.png");
    }
    // the buttons and the cast row have no geometric overlap: D-pad nav
    // cannot find it. Explicit route — the row materializes its first cell
    // if needed (HRecyclerFrame::getDefaultFocus) and the "centered" scroll
    // follows the focus
    this->btnPlay->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");
    this->btnDownload->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");

    this->btnPlay->registerClickAction([this, item](...) {
        PlayerView* view = new PlayerView(item, this->playTicks, this->sourceId);
        view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
        return true;
    });

    auto& dm = DownloadManager::instance();
    this->updateDownloadButton();
    // live progress on the button (events emitted on the UI thread)
    this->progressSub =
        dm.getProgressEvent()->subscribe([this](const std::string& id, int64_t downloaded, int64_t total) {
            if (id != this->itemId || total <= 0) return;
            // "Downloading... (42%)" — a bare percentage does not say what
            // the button does; completion goes back through updateDownloadButton
            this->btnDownload->setText(
                fmt::format("{} ({:.0f}%)", "main/download/downloading"_i18n, downloaded * 100.0 / total));
        });
    this->statusSub = dm.getStatusEvent()->subscribe([this](const std::string& id, DownloadStatus status) {
        if (id == this->itemId) this->updateDownloadButton();
    });
    this->btnDownload->registerClickAction([this](...) {
        auto& dm = DownloadManager::instance();
        switch (dm.findItem(this->itemId)) {
        case DownloadStatus::Queued:
        case DownloadStatus::Downloading:
            Dialog::cancelable("main/download/confirm_cancel"_i18n, [this]() {
                DownloadManager::instance().cancelDownload(this->itemId);
                this->updateDownloadButton();
            });
            break;
        case DownloadStatus::Completed:
            brls::Application::notify("main/download/completed"_i18n);
            break;
        default:
            int qi = AppConfig::instance().getValueIndex(AppConfig::DOWNLOAD_QUALITY);
            dm.addDownload(this->itemId, static_cast<DownloadQuality>(qi));
            this->updateDownloadButton();
        }
        return true;
    });

    this->doMovie();
    this->doSimilar();
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
    auto& dm = DownloadManager::instance();
    switch (dm.findItem(this->itemId)) {
    case DownloadStatus::Completed:
        this->btnDownload->setText("main/download/completed"_i18n);
        break;
    case DownloadStatus::Queued:
    case DownloadStatus::Downloading:
        this->btnDownload->setText("main/download/downloading"_i18n);
        break;
    default:
        this->btnDownload->setText("main/download/start"_i18n);
    }
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