#include "activity/player_view.hpp"
#include "tab/media_movie.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/text_box.hpp"
#include "view/people_source.hpp"
#include "view/recyling_video.hpp"
#include "view/mpv_core.hpp"
#include "api/plex.hpp"
#include "utils/misc.hpp"
#include "utils/dialog.hpp"
#include "utils/download.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

MediaMovie::MediaMovie(const plex::Item& item) : itemId(item.ratingKey) {
    brls::Logger::debug("Tab MediaMovie: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/movie.xml");

    this->headerTitle->setTitle(item.title);
    Image::load(this->imagePoster, item.thumb, 325);
    this->people->registerCell("Cell", MediaCardCell::create);

    this->btnPlay->registerClickAction([this, item](...) {
        PlayerView* view = new PlayerView(item, this->viewOffsetMs);
        view->setTitie(item.year ? fmt::format("{} ({})", item.title, item.year) : item.title);
        return true;
    });

    auto& dm = DownloadManager::instance();
    this->updateDownloadButton();
    // progression en direct sur le bouton (événements émis sur le thread UI)
    this->progressSub = dm.getProgressEvent()->subscribe(
        [this](const std::string& id, int64_t downloaded, int64_t total) {
            if (id != this->itemId || total <= 0) return;
            this->btnDownload->setText(fmt::format("{:.0f} %", downloaded * 100.0 / total));
        });
    this->statusSub = dm.getStatusEvent()->subscribe([this](const std::string& id, DownloadStatus status) {
        if (id == this->itemId) this->updateDownloadButton();
    });
    this->btnDownload->registerClickAction([this](...) {
        auto& dm = DownloadManager::instance();
        if (dm.isDownloading(this->itemId)) {
            Dialog::cancelable("main/download/confirm_cancel"_i18n, [this]() {
                DownloadManager::instance().cancelDownload(this->itemId);
                this->updateDownloadButton();
            });
        } else if (dm.isDownloaded(this->itemId)) {
            brls::Application::notify("main/download/completed"_i18n);
        } else {
            dm.addDownload(this->itemId);
            this->updateDownloadButton();
        }
        return true;
    });

    this->doMovie();
    this->doRelated();
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
    if (dm.isDownloaded(this->itemId)) {
        this->btnDownload->setText("main/download/completed"_i18n);
    } else if (dm.isDownloading(this->itemId)) {
        this->btnDownload->setText("main/download/downloading"_i18n);
    } else {
        this->btnDownload->setText("main/download/start"_i18n);
    }
}

void MediaMovie::doRequest() {
    int64_t seconds = MPVCore::instance().playback_time;
    this->viewOffsetMs = seconds * 1000;
    this->btnPlay->setText(seconds > 0 ? misc::sec2Time(seconds) : "main/media/play"_i18n);
}

void MediaMovie::doMovie() {
    std::string query = HTTP::encode_form({
        {"includeChapters", "1"},
        {"includeMarkers", "1"},
        {"includeStreams", "1"},
        {"checkFiles", "1"},
    });

    ASYNC_RETAIN
    // détail : GET /library/metadata/{ratingKey} (plex_client.dart:1607-1626)
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->people->setVisibility(brls::Visibility::GONE);
                return;
            }
            auto& item = r.Items.front();
            this->headerTitle->setTitle(item.title);
            Image::load(this->imagePoster, item.thumb, 325);
            // bannière : fond (art) + logo détouré centré ; le bloc affiche
            // chevauche la bannière pour garder les boutons visibles
            if (!item.art.empty()) {
                Image::load(this->imageBackdrop, item.art, 1080, 608);
                if (!item.clearLogo.empty()) Image::load(this->imageLogo, item.clearLogo, 440, 130);
                this->bannerBox->setVisibility(brls::Visibility::VISIBLE);
                this->contentRow->setMarginTop(-70);
                this->contentInfo->setMarginTop(90);
            }
            this->labelYear->setText(std::to_string(item.year));
            if (item.contentRating.empty()) {
                this->parentalRating->getParent()->setVisibility(brls::Visibility::GONE);
            } else {
                this->parentalRating->setText(item.contentRating);
                this->parentalRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }
            if (item.rating == 0.0) {
                this->labelRating->getParent()->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelRating->setText(fmt::format("{:.1f}", item.rating));
                this->labelRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }
            this->labelOverview->setText(item.summary);

            if (item.genres.empty()) {
                this->labelGenres->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelGenres->setText(fmt::format("{}", fmt::join(item.genres, ", ")));
                this->labelGenres->setVisibility(brls::Visibility::VISIBLE);
            }
            if (item.roles.size() > 0) {
                this->people->setDataSource(new PeopleDataSource(item.roles));
            } else {
                this->people->setVisibility(brls::Visibility::GONE);
            }

            // versions multiples (item.media[]) : le sélecteur mémorise le choix
            // mais la lecture v1 utilise toujours la première version accessible
            if (item.media.size() > 1) {
                std::vector<std::string> names;
                for (auto& m : item.media) {
                    names.push_back(
                        fmt::format("{} {} ({} kbps)", m.videoResolution, m.videoCodec, m.bitrate));
                }
                this->btnSource->init(
                    "main/setting/version"_i18n, names, 0, [this](int index) { this->selectedVersion = index; });
                this->btnSource->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->btnSource->setVisibility(brls::Visibility::GONE);
            }

            this->viewOffsetMs = item.viewOffset;
            this->btnPlay->setText(
                this->viewOffsetMs > 0 ? misc::sec2Time(this->viewOffsetMs / 1000) : "main/media/play"_i18n);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        plex::apiMetadata, this->itemId, query);
}

void MediaMovie::doRelated() {
    std::string query = HTTP::encode_form({{"count", "12"}});

    ASYNC_RETAIN
    // toutes les rangées « related » du serveur, titres localisés
    // (plex_client.dart:1981-2006)
    plex::getJSON<plex::Container<plex::Hub>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Hub>& r) {
            ASYNC_RELEASE
            this->boxRelated->clearViews();
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                RecylingVideo* row = new RecylingVideo();
                row->setTitle(hub.title);
                row->setFrameHeight(300);
                row->setItemWidth(175);
                row->setItems(hub.items);
                this->boxRelated->addView(row);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        plex::apiHubRelated, this->itemId, query);
}
