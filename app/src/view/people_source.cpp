#include "view/people_source.hpp"
#include "view/video_card.hpp"
#include "view/h_recycling.hpp"
#include "view/video_source.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

class PeopleView : public brls::Box {
public:
    PeopleView(const jellyfin::MediaPeople& item) : peopleId(item.Id) {
        brls::Logger::debug("Tab PeopleView: create");
        this->inflateFromXMLRes("xml/view/people.xml");
        this->headerTitle->setTitle(item.Name);

        this->movie->registerCell("Cell", VideoCardCell::create);
        this->series->registerCell("Cell", VideoCardCell::create);
        this->movie->onNextPage([this]() { this->doMovie(); });
        this->series->onNextPage([this]() { this->doSeries(); });

        this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
            this->startMovie = 0;
            this->startSeries = 0;

            this->movie->showSkeleton();
            this->series->showSkeleton();
            this->doRequest();
            return true;
        });

        this->doRequest();

        Image::load(this->imageLogo, jellyfin::apiPrimaryImage, item.Id,
            HTTP::encode_form({
                {"tag", item.PrimaryImageTag},
                {"maxWidth", "350"},
            }));
    }

    ~PeopleView() override {
        brls::Logger::debug("Tab PeopleView: delete");
        Image::cancel(this->imageLogo);
    }

    void doRequest() {
        this->doPeople();
        this->doMovie();
        this->doSeries();
    }

    void doPeople() {
        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::PeopleItem>(
            [ASYNC_TOKEN](const jellyfin::PeopleItem& r) {
                ASYNC_RELEASE
                this->headerTitle->setTitle(r.Name);
                this->labelOverview->setText(r.Overview);

                if (r.ProductionLocations.size() > 0) {
                    this->labelLocation->setText(r.ProductionLocations.front());
                } else {
                    this->labelLocation->setVisibility(brls::Visibility::GONE);
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                brls::Application::notify(ex);
            },
            jellyfin::apiUserItem, AppConfig::instance().getUserId(), this->peopleId);
    }

    void doMovie() {
        std::string query = HTTP::encode_form({
            {"PersonIds", this->peopleId},
            {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
            {"EnableImageTypes", "Primary"},
            {"Recursive", "true"},
            {"IncludeItemTypes", jellyfin::mediaTypeMovie},
            {"limit", std::to_string(this->pageSize)},
            {"startIndex", std::to_string(this->startMovie)},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
                ASYNC_RELEASE
                this->startMovie = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->movie->setVisibility(brls::Visibility::GONE);
                    this->titleMovie->setVisibility(brls::Visibility::GONE);
                } else if (r.StartIndex == 0) {
                    this->titleMovie->setVisibility(brls::Visibility::VISIBLE);
                    this->movie->setVisibility(brls::Visibility::VISIBLE);
                    this->movie->setDataSource(new VideoDataSource(r.Items));
                    this->titleMovie->setSubtitle(std::to_string(r.TotalRecordCount));
                } else if (r.Items.size() > 0) {
                    auto dataSrc = dynamic_cast<VideoDataSource*>(this->movie->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->movie->notifyDataChanged();
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->movie->setVisibility(brls::Visibility::GONE);
                this->titleMovie->setSubtitle(ex);
            },
            jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), query);
    }

    void doSeries() {
        std::string query = HTTP::encode_form({
            {"PersonIds", this->peopleId},
            {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
            {"EnableImageTypes", "Primary"},
            {"Recursive", "true"},
            {"IncludeItemTypes", jellyfin::mediaTypeSeries},
            {"limit", std::to_string(this->pageSize)},
            {"startIndex", std::to_string(this->startSeries)},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
                ASYNC_RELEASE
                this->startSeries = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->series->setVisibility(brls::Visibility::GONE);
                    this->titleSeries->setVisibility(brls::Visibility::GONE);
                } else if (r.StartIndex == 0) {
                    this->titleSeries->setVisibility(brls::Visibility::VISIBLE);
                    this->series->setVisibility(brls::Visibility::VISIBLE);
                    this->series->setDataSource(new VideoDataSource(r.Items));
                    this->titleSeries->setSubtitle(std::to_string(r.TotalRecordCount));
                } else if (r.Items.size() > 0) {
                    auto dataSrc = dynamic_cast<VideoDataSource*>(this->series->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->series->notifyDataChanged();
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->series->setVisibility(brls::Visibility::GONE);
                this->titleSeries->setSubtitle(ex);
            },
            jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), query);
    }

private:
    BRLS_BIND(brls::Image, imageLogo, "people/image/logo");
    BRLS_BIND(brls::Header, headerTitle, "people/header/title");
    BRLS_BIND(brls::Label, labelOverview, "people/label/overview");
    BRLS_BIND(brls::Label, labelLocation, "people/label/location");
    BRLS_BIND(brls::Header, titleMovie, "people/movie/title");
    BRLS_BIND(brls::Header, titleSeries, "people/series/title");
    BRLS_BIND(HRecyclerFrame, movie, "people/movie");
    BRLS_BIND(HRecyclerFrame, series, "people/series");

    std::string peopleId;
    size_t pageSize = 10;
    size_t startMovie = 0;
    size_t startSeries = 0;
};

PeopleDataSource::PeopleDataSource(const MediaList& r) : list(std::move(r)) {}

size_t PeopleDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* PeopleDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);

    cell->setId(item.Id);
    cell->labelTitle->setText(item.Name);
    cell->labelExt->setText(item.Role);

    if (!item.PrimaryImageTag.empty()) {
        Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
            HTTP::encode_form({{"tag", item.PrimaryImageTag}, {"maxWidth", "350"}}));
    }
    return cell;
}

void PeopleDataSource::onItemSelected(brls::Box* recycler, size_t index) {
    recycler->present(new PeopleView(this->list.at(index)));
}

void PeopleDataSource::clearData() { this->list.clear(); }
