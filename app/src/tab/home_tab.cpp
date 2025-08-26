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
            {"enableResumable", "false"},
            {"enableRewatching", "false"},
            {"limit", std::to_string(pageSize)},
            {"startIndex", std::to_string(start)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiShowNextUp), query);
    });

    this->movieLatest->onQuery([](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary"},
            {"includeItemTypes", jellyfin::mediaTypeMovie},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserLatest), AppConfig::instance().getUserId(), query);
    });

    this->seriesLatest->onQuery([](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary"},
            {"includeItemTypes", jellyfin::mediaTypeSeries},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserLatest), AppConfig::instance().getUserId(), query);
    });

    this->musicLatest->onQuery([](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary"},
            {"includeItemTypes", jellyfin::mediaTypeMusicAlbum},
            {"fields", "BasicSyncInfo"},
            {"limit", std::to_string(pageSize)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserLatest), AppConfig::instance().getUserId(), query);
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
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
        this->userResume->doRequest(true);
        this->showNextup->doRequest(true);
        this->movieLatest->doLatest(true);
        this->seriesLatest->doLatest(true);
        this->musicLatest->doLatest(true);
        return true;
    });

    this->registerAction(KeyBind::getRefresh(), [this](...) {
        this->userResume->doRequest(true);
        this->showNextup->doRequest(true);
        this->movieLatest->doLatest(true);
        this->seriesLatest->doLatest(true);
        this->musicLatest->doLatest(true);
        return true;
    });

    this->userResume->doRequest();
    this->showNextup->doRequest();
    this->movieLatest->doLatest();
    this->seriesLatest->doLatest();
    this->musicLatest->doLatest();
}