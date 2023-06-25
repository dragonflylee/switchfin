/*
    Copyright 2023 dragonflylee
*/

#include "tab/home_tab.hpp"
#include "tab/media_series.hpp"
#include "api/jellyfin.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/video_view.hpp"

using namespace brls::literals;  // for _i18n

class VideoDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaEpisode>;

    explicit VideoDataSource(const MediaList& r) : list(std::move(r)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        if (item.Type == jellyfin::mediaTypeEpisode) {
            cell->labelTitle->setText(item.SeriesName);
            cell->labelExt->setText(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));

            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId,
                HTTP().encode_form({{"tag", item.SeriesPrimaryImageTag}, {"maxWidth", "240"}}));
        } else {
            cell->labelTitle->setText(item.Name);
            cell->labelExt->setText(item.ProductionYear > 0 ? std::to_string(item.ProductionYear) : "");

            auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
            if (it != item.ImageTags.end())
                Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                    HTTP().encode_form({{"tag", it->second}, {"maxWidth", "200"}}));
        }
        return cell;
    }

    void onItemSelected(brls::View* recycler, size_t index) override {
        auto& item = this->list.at(index);

        if (item.Type == jellyfin::mediaTypeSeries) {
            brls::View* view = dynamic_cast<brls::View*>(recycler);
            view->present(new MediaSeries(item));
        } else if (item.Type == jellyfin::mediaTypeMovie) {
            VideoView* view = new VideoView(item);
            view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
            brls::sync([view]() { brls::Application::giveFocus(view); });
        } else if (item.Type == jellyfin::mediaTypeEpisode) {
            VideoView* view = new VideoView(item);
            view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
            brls::sync([view]() { brls::Application::giveFocus(view); });
        } else {
            brls::Logger::debug("unsupport type {}", item.Type);
        }
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

protected:
    MediaList list;
};

class ResumeDataSource : public VideoDataSource {
public:
    explicit ResumeDataSource(const MediaList& r) : VideoDataSource(r) {}

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end())
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP().encode_form({{"tag", it->first}, {"maxWidth", "300"}}));
        else
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId,
                HTTP().encode_form({{"tag", item.SeriesPrimaryImageTag}, {"fillWidth", "300"}}));

        if (item.Type == jellyfin::mediaTypeEpisode) {
            cell->labelTitle->setText(item.SeriesName);
            cell->labelExt->setText(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        } else if (item.ProductionYear > 0) {
            cell->labelTitle->setText(item.Name);
            cell->labelExt->setText(std::to_string(item.ProductionYear));
        }
        return cell;
    }
};

HomeTab::HomeTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/home.xml");

    this->userResume->registerCell("Cell", &VideoCardCell::create);
    this->userLatest->registerCell("Cell", &VideoCardCell::create);
    this->showNextup->registerCell("Cell", &VideoCardCell::create);

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->doResume();
        this->doLatest();
        this->doNextup();
        return true;
    });

    this->doResume();
    this->doLatest();
    this->doNextup();
}

void HomeTab::onCreate() {}

brls::View* HomeTab::create() { return new HomeTab(); }

void HomeTab::doResume() {
    std::string query = HTTP().encode_form({
        {"enableImageTypes", "Primary"},
        {"mediaTypes", "Video"},
        {"fields", "BasicSyncInfo"},
        {"limit", "12"},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->userResume->setVisibility(brls::Visibility::GONE);
                this->headerResume->setVisibility(brls::Visibility::GONE);
            } else {
                this->headerResume->setVisibility(brls::Visibility::VISIBLE);
                this->userResume->setVisibility(brls::Visibility::VISIBLE);
                this->userResume->setDataSource(new ResumeDataSource(r.Items));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->userResume->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserResume, AppConfig::instance().getUser().id, query);
}

void HomeTab::doLatest() {
    std::string query = HTTP().encode_form({
        {"enableImageTypes", "Primary"},
        {"fields", "BasicSyncInfo"},
        {"limit", "16"},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const std::vector<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->userLatest->setDataSource(new VideoDataSource(r));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->userLatest->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserLatest, AppConfig::instance().getUser().id, query);
}

void HomeTab::doNextup() {
    std::string query = HTTP().encode_form({
        {"userId", AppConfig::instance().getUser().id},
        {"fields", "PrimaryImageAspectRatio"},
        {"enableTotalRecordCount", "false"},
        {"startIndex", "0"},
        {"limit", "16"},
    });
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->showNextup->setDataSource(new VideoDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->showNextup->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiShowNextUp, query);
}