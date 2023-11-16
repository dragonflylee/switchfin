/*
    Copyright 2023 dragonflylee
*/

#include "tab/home_tab.hpp"
#include "api/jellyfin.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "utils/misc.hpp"

using namespace brls::literals;  // for _i18n

class ResumeCard : public BaseVideoCard {
public:
    ResumeCard() { this->inflateFromXMLRes("xml/view/video_resume.xml"); }

    static ResumeCard* create() { return new ResumeCard(); }

    BRLS_BIND(brls::Label, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelExt, "video/card/label/ext");
    BRLS_BIND(brls::Label, labelCount, "video/card/label/count");
    BRLS_BIND(brls::Label, labelDuration, "video/card/label/duration");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
};

class ResumeDataSource : public VideoDataSource {
public:
    explicit ResumeDataSource(const MediaList& r) : VideoDataSource(r) {}

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        ResumeCard* cell = dynamic_cast<ResumeCard*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"fillWidth", "400"}}));
        } else {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId,
                HTTP::encode_form({{"tag", item.SeriesPrimaryImageTag}, {"fillWidth", "400"}}));
        }

        if (item.Type == jellyfin::mediaTypeEpisode) {
            cell->labelTitle->setText(item.SeriesName);
            cell->labelExt->setText(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        } else if (item.ProductionYear > 0) {
            cell->labelTitle->setText(item.Name);
            cell->labelExt->setText(std::to_string(item.ProductionYear));
        }

        cell->labelDuration->setText(
            fmt::format("{}/{}", misc::sec2Time(item.UserData.PlaybackPositionTicks / jellyfin::PLAYTICKS),
                misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS)));
        cell->rectProgress->setWidthPercentage(item.UserData.PlayedPercentage);
        return cell;
    }
};

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/home.xml");

    this->registerFloatXMLAttribute("latestSize", [this](float value) {
        this->latestSize = value;
        this->doRequest();
    });

    this->userResume->registerCell("Cell", ResumeCard::create);
    this->userResume->onNextPage([this]() { this->doResume(); });
    this->videoLatest->registerCell("Cell", VideoCardCell::create);
    this->musicLatest->registerCell("Cell", VideoCardCell::create);
    this->showNextup->registerCell("Cell", VideoCardCell::create);
    this->showNextup->onNextPage([this]() { this->doNextup(); });
}

HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }

void HomeTab::doRequest() {
    this->doResume();
    this->doVideoLatest();
    this->doMusicLatest();
    this->doNextup();
}

void HomeTab::onCreate() {
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->startNextup = 0;
        this->startResume = 0;
        this->doRequest();
        return true;
    });
}

brls::View* HomeTab::create() { return new HomeTab(); }

void HomeTab::doResume() {
    std::string query = HTTP::encode_form({
        {"enableImageTypes", "Primary"},
        {"mediaTypes", "Video"},
        {"fields", "BasicSyncInfo"},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->startResume)},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->startResume = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->userResume->setVisibility(brls::Visibility::GONE);
                this->headerResume->setVisibility(brls::Visibility::GONE);
            } else if (r.StartIndex == 0) {
                this->headerResume->setVisibility(brls::Visibility::VISIBLE);
                this->userResume->setVisibility(brls::Visibility::VISIBLE);
                this->userResume->setDataSource(new ResumeDataSource(r.Items));
                this->headerResume->setSubtitle(std::to_string(r.TotalRecordCount));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<ResumeDataSource*>(this->userResume->getDataSource());
                dataSrc->appendData(r.Items);
                this->userResume->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->userResume->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserResume, AppConfig::instance().getUser().id, query);
}

void HomeTab::doVideoLatest() {
    std::string query = HTTP::encode_form({
        {"enableImageTypes", "Primary"},
        {"includeItemTypes", "Series,Movie"},
        {"fields", "BasicSyncInfo"},
        {"limit", std::to_string(this->latestSize)},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const std::vector<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            if (r.empty()) {
                this->headerVideo->setVisibility(brls::Visibility::GONE);
                this->videoLatest->setVisibility(brls::Visibility::GONE);
            } else {
                this->headerVideo->setVisibility(brls::Visibility::VISIBLE);
                this->videoLatest->setVisibility(brls::Visibility::VISIBLE);
                this->videoLatest->setDataSource(new VideoDataSource(r));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->headerVideo->setVisibility(brls::Visibility::GONE);
            this->videoLatest->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserLatest, AppConfig::instance().getUser().id, query);
}

void HomeTab::doMusicLatest() {
    std::string query = HTTP::encode_form({
        {"enableImageTypes", "Primary"},
        {"includeItemTypes", "MusicAlbum"},
        {"fields", "BasicSyncInfo"},
        {"limit", std::to_string(this->latestSize)},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const std::vector<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            if (r.empty()) {
                this->headerMusic->setVisibility(brls::Visibility::GONE);
                this->musicLatest->setVisibility(brls::Visibility::GONE);
            } else {
                this->headerMusic->setVisibility(brls::Visibility::VISIBLE);
                this->musicLatest->setVisibility(brls::Visibility::VISIBLE);
                this->musicLatest->setDataSource(new VideoDataSource(r));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->headerMusic->setVisibility(brls::Visibility::GONE);
            this->musicLatest->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserLatest, AppConfig::instance().getUser().id, query);
}

void HomeTab::doNextup() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUser().id},
        {"fields", "BasicSyncInfo"},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->startNextup)},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->startNextup = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->showNextup->setVisibility(brls::Visibility::GONE);
            } else if (r.StartIndex == 0) {
                this->showNextup->setDataSource(new VideoDataSource(r.Items));
                this->headerNextup->setSubtitle(std::to_string(r.TotalRecordCount));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->showNextup->getDataSource());
                dataSrc->appendData(r.Items);
                this->showNextup->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->showNextup->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiShowNextUp, query);
}