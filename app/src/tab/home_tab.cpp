/*
    Copyright 2023 dragonflylee
*/

#include "tab/home_tab.hpp"
#include "view/recyling_video.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/home.xml");

    this->userResume->onQuery([this](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary,Backdrop,Thumb"},
            {"mediaTypes", "Video"},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
            {"startIndex", std::to_string(start)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserResume), AppConfig::instance().getUserId(), query);
    });

    this->showNextup->onQuery([this](size_t start, size_t pageSize) {
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

    this->movieLatest->onQuery([this](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary"},
            {"includeItemTypes", "Movie"},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserLatest), AppConfig::instance().getUserId(), query);
    });

    this->seriesLatest->onQuery([this](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary"},
            {"includeItemTypes", "Series"},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserLatest), AppConfig::instance().getUserId(), query);
    });

    this->musicLatest->onQuery([this](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary"},
            {"includeItemTypes", "MusicAlbum"},
            {"fields", "BasicSyncInfo"},
            {"limit", std::to_string(pageSize)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserLatest), AppConfig::instance().getUserId(), query);
    });
}

HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }

brls::View* HomeTab::create() { return new HomeTab(); }

void HomeTab::doRequest() {
    this->userResume->doRequest(true);
    this->showNextup->doRequest(true);
}

void HomeTab::onCreate() {
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->doRequest();
        this->movieLatest->doLatest(true);
        this->seriesLatest->doLatest(true);
        this->musicLatest->doLatest(true);
        return true;
    });

    this->doRequest();
    this->movieLatest->doLatest();
    this->seriesLatest->doLatest();
    this->musicLatest->doLatest();
}