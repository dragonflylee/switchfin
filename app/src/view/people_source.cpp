#include "view/people_source.hpp"
#include "view/video_card.hpp"
#include "view/recyling_video.hpp"
#include "utils/image.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

class PeopleView : public brls::Box {
public:
    PeopleView(const jellyfin::MediaPeople& item) : peopleId(item.Id) {
        brls::Logger::debug("Tab PeopleView: create");
        this->inflateFromXMLRes("xml/view/people.xml");
        this->headerTitle->setTitle(item.Name);

        this->movie->onQuery([this](size_t start, size_t pageSize) {
            std::string query = HTTP::encode_form({
                {"PersonIds", this->peopleId},
                {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
                {"EnableImageTypes", "Primary"},
                {"Recursive", "true"},
                {"IncludeItemTypes", jellyfin::mediaTypeMovie},
                {"limit", std::to_string(pageSize)},
                {"startIndex", std::to_string(start)},
            });

            return fmt::format(fmt::runtime(jellyfin::apiUserLibrary), AppConfig::instance().getUserId(), query);
        });

        this->series->onQuery([this](size_t start, size_t pageSize) {
            std::string query = HTTP::encode_form({
                {"PersonIds", this->peopleId},
                {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
                {"EnableImageTypes", "Primary"},
                {"Recursive", "true"},
                {"IncludeItemTypes", jellyfin::mediaTypeSeries},
                {"limit", std::to_string(pageSize)},
                {"startIndex", std::to_string(start)},
            });

            return fmt::format(fmt::runtime(jellyfin::apiUserLibrary), AppConfig::instance().getUserId(), query);
        });

        this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
            this->doPeople();
            this->movie->doRequest(true);
            this->series->doRequest(true);
            return true;
        });

        this->doPeople();
        this->movie->doRequest();
        this->series->doRequest();
        ;

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

private:
    BRLS_BIND(brls::Image, imageLogo, "people/image/logo");
    BRLS_BIND(brls::Header, headerTitle, "people/header/title");
    BRLS_BIND(brls::Label, labelOverview, "people/label/overview");
    BRLS_BIND(brls::Label, labelLocation, "people/label/location");
    BRLS_BIND(RecylingVideo, movie, "people/movie");
    BRLS_BIND(RecylingVideo, series, "people/series");

    std::string peopleId;
};

PeopleDataSource::PeopleDataSource(const MediaList& r) : list(std::move(r)) {}

size_t PeopleDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* PeopleDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    MediaCardCell* cell = dynamic_cast<MediaCardCell*>(recycler->dequeueReusableCell("Cell"));
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
