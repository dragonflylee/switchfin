#include "view/context_menu.hpp"
#include "view/svg_image.hpp"
#include "view/mpv_core.hpp"
#include "view/video_card.hpp"
#include "api/jellyfin.hpp"
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
            marginRight="14" />

        <brls:Label
            id="menu_item/title"
            fontSize="16"
            grow="1.0" />

        <SVGImage
            id="menu_item/check"
            width="18"
            height="18"
            visibility="invisible"
            svg="@res/icon/ico-checkmark.svg" />

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

void MenuItem::setSelected(bool selected) {
    this->selected = selected;
    this->check->setVisibility(selected ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
}

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
        if (this->btnFavorite->getSelected())
            return this->unFavorite();
        else
            return this->doFavorite();
    });
    this->btnFavorite->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnFavorite));
    this->btnFavorite->setSelected(item.UserData.IsFavorite);

    this->btnMarkPlay->registerClickAction([this](brls::View* view) {
        if (this->btnMarkPlay->getSelected())
            return this->unPlayed();
        else
            return this->doPlayed();
    });
    this->btnMarkPlay->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnMarkPlay));
    this->btnMarkPlay->setSelected(item.UserData.Played);

    if (item.Type == jellyfin::mediaTypeMovie || item.Type == jellyfin::mediaTypeEpisode ||
        item.Type == jellyfin::mediaTypeVideo) {
        auto& dm = DownloadManager::instance();
        switch (dm.findItem(this->itemId)) {
        case DownloadStatus::Completed:
            this->btnDownload->title->setText("main/download/completed"_i18n);
            this->btnDownload->setSelected(true);
            break;
        case DownloadStatus::Queued:
        case DownloadStatus::Downloading:
            this->btnDownload->title->setText("main/download/downloading"_i18n);
            this->btnDownload->setSelected(true);
            break;
        default:;
        }
        this->btnDownload->registerClickAction([this](brls::View* view) {
            auto& dm = DownloadManager::instance();
            switch (dm.findItem(this->itemId)) {
            case DownloadStatus::Completed:
                brls::Application::notify("main/download/completed"_i18n);
                break;
            case DownloadStatus::Queued:
            case DownloadStatus::Downloading:
                brls::Application::notify("main/download/downloading"_i18n);
                break;
            default:
                int qi = AppConfig::instance().getValueIndex(AppConfig::DOWNLOAD_QUALITY);
                dm.addDownload(this->itemId, static_cast<DownloadQuality>(qi));
                brls::Application::notify("main/download/queued"_i18n);
                this->btnDownload->setSelected(true);
            }
            return true;
        });
        this->btnDownload->addGestureRecognizer(new brls::TapGestureRecognizer(this->btnDownload));
    } else {
        this->btnDownload->setVisibility(brls::Visibility::GONE);
    }
}

bool ContextMenu::doPlayed() {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"itemId", this->itemId},
            {"played", this->btnMarkPlay->getSelected()},
        },
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->btnMarkPlay->setSelected(r.Played);
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
            {"isFavorite", this->btnFavorite->getSelected()},
        },
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->btnFavorite->setSelected(r.IsFavorite);
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
            this->btnMarkPlay->setSelected(r.IsFavorite);
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
            this->btnFavorite->setSelected(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [ex]() { brls::Application::notify(ex); });
        },
        jellyfin::apiFavoriteItems, AppConfig::instance().getUserId(), this->itemId);

    return true;
}
