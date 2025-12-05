/*
    Copyright 2023 dragonflylee
*/

#include "tab/home_tab.hpp"
#include "view/recyling_video.hpp"
#include "api/jellyfin.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/home.xml");

    this->userResume->onQuery([](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary,Backdrop,Thumb"},
            {"mediaTypes", "Video"},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
            {"startIndex", std::to_string(start)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserResume), AppConfig::instance().getUserId(), query);
    });

    this->showNextup->onQuery([](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"fields", "BasicSyncInfo,Chapters"},
            {"enableImageTypes", "Primary,Backdrop,Thumb"},
            {"enableResumable", "false"},
            {"enableRewatching", "false"},
            {"limit", std::to_string(pageSize)},
            {"startIndex", std::to_string(start)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiShowNextUp), query);
    });
}

HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }

brls::View* HomeTab::create() { return new HomeTab(); }

void HomeTab::doRequest() {
    this->userResume->reset();
    this->showNextup->reset();
    this->userResume->doRequest();
    this->showNextup->doRequest();
}

void HomeTab::onCreate() {
    auto actionRefresh = [this](brls::View* view) {
        this->userResume->doRequest(true);
        this->showNextup->doRequest(true);
        for (auto recyler : this->latest) {
            recyler->doLatest(true);
        }
        return true;
    };

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Collection>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Collection>& r) {
            ASYNC_RELEASE

            auto& excludes = AppConfig::instance().userConfig().LatestItemsExcludes;
            for (auto& item : r.Items) {
                auto it = std::find(excludes.begin(), excludes.end(), item.Id);
                if (it != excludes.end()) continue;

                RecylingVideo* recyler = new RecylingVideo();
                if (item.CollectionType == "music") {
                    recyler->setFrameHeight(225);
                } else if (item.CollectionType != "livetv") {
                    recyler->setFrameHeight(300);
                } else {
                    continue;
                }
                recyler->setItemWidth(175);
                recyler->setTitle(item.Name);
                recyler->onQuery([&item](size_t start, size_t pageSize) {
                    std::string query = HTTP::encode_form({
                        {"enableImageTypes", "Primary"},
                        {"parentId", item.Id},
                        {"fields", "BasicSyncInfo,Chapters"},
                        {"limit", std::to_string(pageSize)},
                    });
                    return fmt::format(fmt::runtime(jellyfin::apiUserLatest), AppConfig::instance().getUserId(), query);
                });
                recyler->doLatest();
                boxHome->addView(recyler);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiUserViews, AppConfig::instance().getUserId());

    this->userResume->doRequest();
    this->showNextup->doRequest();
}