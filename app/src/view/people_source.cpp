#include "view/people_source.hpp"
#include "view/video_card.hpp"
#include "view/h_recycling.hpp"
#include "view/video_source.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

class PeopleView : public brls::Box {
public:
    PeopleView(const jellyfin::MediaPeople& item) {
        this->inflateFromXMLRes("xml/view/people.xml");
        this->headerTitle->setTitle(item.Name);
        this->doPeople(item.Id);

        this->doMedia(item.Id, jellyfin::mediaTypeMovie, this->movie, this->titleMovie);
        this->doMedia(item.Id, jellyfin::mediaTypeSeries, this->series, this->titleSeries);
    }

    void doPeople(const std::string& itemId) {
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
                // loading Logo
                auto logo = r.ImageTags.find(jellyfin::imageTypePrimary);
                if (logo != r.ImageTags.end()) {
                    Image::load(this->imageLogo, jellyfin::apiPrimaryImage, r.Id,
                        HTTP::encode_form({
                            {"tag", logo->second},
                            {"maxWidth", "300"},
                        }));
                    this->imageLogo->setVisibility(brls::Visibility::VISIBLE);
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                brls::Application::notify(ex);
            },
            jellyfin::apiUserItem, AppConfig::instance().getUserId(), itemId);
    }

    void doMedia(const std::string& itemId, const std::string& type, HRecyclerFrame* recyler, brls::Header* title) {
        std::string query = HTTP::encode_form({
            {"PersonIds", itemId},
            {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
            {"EnableImageTypes", "Primary"},
            {"limit", "10"},
            {"Recursive", "true"},
            {"IncludeItemTypes", type},
        });

        recyler->registerCell("Cell", VideoCardCell::create);

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
            [ASYNC_TOKEN, recyler, title](const jellyfin::Result<jellyfin::Episode>& r) {
                ASYNC_RELEASE
                if (r.Items.size() > 0) {
                    recyler->setDataSource(new VideoDataSource(r.Items));
                } else {
                    recyler->setVisibility(brls::Visibility::GONE);
                    title->setVisibility(brls::Visibility::GONE);
                }
            },
            [ASYNC_TOKEN, recyler, title](const std::string& ex) {
                ASYNC_RELEASE
                recyler->setVisibility(brls::Visibility::GONE);
                title->setVisibility(brls::Visibility::GONE);
            },
            jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), query);
    }

private:
    BRLS_BIND(brls::Image, imageLogo, "people/image/logo");
    BRLS_BIND(brls::Header, headerTitle, "people/header/title");
    BRLS_BIND(brls::Label, labelOverview, "people/label/overview");
    BRLS_BIND(brls::Label, labelLocation, "people/label/location");
    BRLS_BIND(brls::Header, titleSeries, "people/series/title");
    BRLS_BIND(brls::Header, titleMovie, "people/movie/title");
    BRLS_BIND(HRecyclerFrame, movie, "people/movie");
    BRLS_BIND(HRecyclerFrame, series, "people/series");
};

PeopleDataSource::PeopleDataSource(const MediaList& r) : list(std::move(r)) {}

size_t PeopleDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* PeopleDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);

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
