#include "tab/suggest_show.hpp"
#include "view/recyling_video.hpp"
#include "api/jellyfin.hpp"

SuggestShow::SuggestShow(const std::string& id) : itemId(id) {
    this->inflateFromXMLRes("xml/tabs/suggest_show.xml");
    brls::Logger::debug("Tab SuggestShow: create");

    this->resume->onQuery([this](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary,Backdrop,Thumb"},
            {"parentId", this->itemId},
            {"includeItemTypes", jellyfin::mediaTypeEpisode},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
            {"startIndex", std::to_string(start)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserResume), AppConfig::instance().getUserId(), query);
    });

    this->latest->onQuery([this](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary"},
            {"parentId", this->itemId},
            {"includeItemTypes", jellyfin::mediaTypeEpisode},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserLatest), AppConfig::instance().getUserId(), query);
    });

    this->nextUp->onQuery([this](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"parentId", this->itemId},
            {"fields", "BasicSyncInfo,Chapters"},
            {"enableResumable", "false"},
            {"enableRewatching", "false"},
            {"limit", std::to_string(pageSize)},
            {"startIndex", std::to_string(start)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiShowNextUp), query);
    });
}

SuggestShow::~SuggestShow() { brls::Logger::debug("Tab SuggestShow: delete"); }

void SuggestShow::onCreate() {
    this->resume->doRequest();
    this->latest->doLatest();
    this->nextUp->doRequest();
}

void SuggestShow::doRequest() {
    this->resume->reset();
    this->nextUp->reset();
    this->resume->doRequest();
    this->nextUp->doRequest();
}