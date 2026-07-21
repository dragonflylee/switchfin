#include "view/context_menu.hpp"
#include "view/svg_image.hpp"
#include "view/mpv_core.hpp"
#include "view/video_card.hpp"
#include "api/jellyfin.hpp"
#include "utils/dialog.hpp"
#include "utils/download.hpp"

using namespace brls::literals;

const std::string menuItemXML = R"xml(
    <brls:Box
        height="50"
        axis="row"
        focusable="true"
        cornerRadius="10"
        highlightCornerRadius="10"
        alignItems="center"
        paddingLeft="14"
        paddingRight="14">

        <SVGImage
            id="menu_item/icon"
            width="20"
            height="20"
            marginRight="16" />

        <brls:Label
            id="menu_item/title"
            fontSize="16"
            grow="1.0" />
    </brls:Box>
)xml";

MenuItem::MenuItem() {
    this->inflateFromXMLString(menuItemXML);

    this->registerStringXMLAttribute("icon", [this](std::string value) { this->setIcon(value); });
    this->registerStringXMLAttribute("title", [this](std::string value) { this->setTitle(value); });
}

void MenuItem::setIcon(const std::string& res) {
    std::string path = res;
    const std::string prefix = "@res/";
    if (path.rfind(prefix, 0) == 0) path = path.substr(prefix.size());
    this->icon->setImageFromSVGRes(path);
}

void MenuItem::setTitle(const std::string& text) { this->title->setText(text); }

brls::View* MenuItem::create() { return new MenuItem(); }

ContextMenu::ContextMenu(const jellyfin::Item& item, BaseCardCell* view) : itemId(item.Id), cell(view) {
    this->inflateFromXMLRes("xml/view/context_menu.xml");
    brls::Logger::debug("ContextMenu: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    this->labelTitle->setText(item.Name);

    this->btnFavorite->registerClickAction([this](brls::View* view) {
        if (this->isFavorite)
            return this->unFavorite();
        else
            return this->doFavorite();
    });
    this->btnFavorite->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnFavorite));
    this->updateFavoriteButton(item.UserData.IsFavorite);

    this->btnMarkPlay->registerClickAction([this](brls::View* view) {
        if (this->isPlayed)
            return this->unPlayed();
        else
            return this->doPlayed();
    });
    this->btnMarkPlay->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnMarkPlay));
    this->updatePlayedButton(item.UserData.Played);

    if (item.Type == jellyfin::mediaTypeMovie || item.Type == jellyfin::mediaTypeEpisode ||
        item.Type == jellyfin::mediaTypeVideo) {
        auto& dm = DownloadManager::instance();
        this->updateDownloadButton();
        this->btnDownload->registerClickAction([this](brls::View* view) {
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
        this->btnDownload->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnDownload));
        this->statusSub = dm.getStatusEvent()->subscribe([this](const std::string& id, DownloadStatus status) {
            if (id == this->itemId) this->updateDownloadButton();
        });
        this->progressSub =
            dm.getProgressEvent()->subscribe([this](const std::string& id, int64_t downloaded, int64_t total) {
                if (id != this->itemId || total <= 0) return;
                this->btnDownload->setTitle(
                    fmt::format("{} ({:.0f}%)", "main/download/downloading"_i18n, downloaded * 100.0 / total));
            });
    } else {
        this->btnDownload->setVisibility(brls::Visibility::GONE);
    }
}

ContextMenu::~ContextMenu() {
    if (this->btnDownload->getVisibility() == brls::Visibility::VISIBLE) {
        auto& dm = DownloadManager::instance();
        dm.getProgressEvent()->unsubscribe(this->progressSub);
        dm.getStatusEvent()->unsubscribe(this->statusSub);
    }
}

bool ContextMenu::doPlayed() {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"itemId", this->itemId},
            {"played", this->isPlayed},
        },
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->updatePlayedButton(r.Played);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiPlayedItems, AppConfig::instance().getUserId(), this->itemId);

    this->cell->setWatched(true);
    return true;
}

bool ContextMenu::doFavorite() {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"itemId", this->itemId},
            {"isFavorite", this->isFavorite},
        },
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->updateFavoriteButton(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiFavoriteItems, AppConfig::instance().getUserId(), this->itemId);

    return true;
}

bool ContextMenu::unPlayed() {
    ASYNC_RETAIN
    jellyfin::deleteJSON<jellyfin::UserDataResult>(
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->updatePlayedButton(r.Played);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiPlayedItems, AppConfig::instance().getUserId(), this->itemId);

    this->cell->setWatched(false);
    return true;
}

bool ContextMenu::unFavorite() {
    ASYNC_RETAIN
    jellyfin::deleteJSON<jellyfin::UserDataResult>(
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->updateFavoriteButton(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiFavoriteItems, AppConfig::instance().getUserId(), this->itemId);

    return true;
}

void ContextMenu::updatePlayedButton(bool played) {
    this->isPlayed = played;
    if (played) {
        this->btnMarkPlay->setIcon("icon/ico-checkmark.svg");
        this->btnMarkPlay->setTitle("main/media/mark_unplayed"_i18n);
    } else {
        this->btnMarkPlay->setIcon("icon/ico-unchecked.svg");
        this->btnMarkPlay->setTitle("main/media/mark_played"_i18n);
    }
}

void ContextMenu::updateFavoriteButton(bool favorite) {
    this->isFavorite = favorite;
    if (favorite) {
        this->btnFavorite->setIcon("icon/ico-heart.svg");
        this->btnFavorite->setTitle("main/media/del_favorite"_i18n);
    } else {
        this->btnFavorite->setIcon("icon/ico-heart-gray.svg");
        this->btnFavorite->setTitle("main/media/add_favorite"_i18n);
    }
}

void ContextMenu::updateDownloadButton() {
    auto& dm = DownloadManager::instance();
    switch (dm.findItem(this->itemId)) {
    case DownloadStatus::Completed:
        this->btnDownload->setTitle("main/download/completed"_i18n);
        break;
    case DownloadStatus::Queued:
    case DownloadStatus::Downloading:
        this->btnDownload->setTitle("main/download/downloading"_i18n);
        break;
    default:
        this->btnDownload->setTitle("main/download/start"_i18n);
    }
}