#include "activity/player_view.hpp"
#include "tab/media_movie.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "api/jellyfin.hpp"
#include <fmt/ranges.h>

MediaMovie::MediaMovie(const jellyfin::Item& item) {
    brls::Logger::debug("Tab MediaMovie: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/movie.xml");

    this->headerTitle->setTitle(item.Name);
    this->people->registerCell("Cell", VideoCardCell::create);
    this->similar->registerCell("Cell", VideoCardCell::create);

    this->btnPlay->registerClickAction([this, item](...) {
        PlayerView* view = new PlayerView(item);
        view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
        return true;
    });

    this->doMovie(item.Id);
    this->doSimilar(item.Id);
}

MediaMovie::~MediaMovie() { brls::Logger::debug("Tab MediaMovie: delete"); }

void MediaMovie::doMovie(const std::string& itemId) {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Detail>(
        [ASYNC_TOKEN](const jellyfin::Detail& r) {
            ASYNC_RELEASE
            this->headerTitle->setTitle(r.Name);
            this->labelYear->setText(std::to_string(r.ProductionYear));
            if (r.OfficialRating.empty()) {
                this->parentalRating->getParent()->setVisibility(brls::Visibility::GONE);
            } else {
                this->parentalRating->setText(r.OfficialRating);
                this->parentalRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }
            if (r.CommunityRating == 0.f) {
                this->labelRating->getParent()->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelRating->setText(fmt::format("{:.1f}", r.CommunityRating));
                this->labelRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }
            this->labelOverview->setText(r.Overview);

            if (r.Genres.empty()) {
                this->labelGenres->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelGenres->setText(fmt::format("{}", fmt::join(r.Genres, ", ")));
                this->labelGenres->setVisibility(brls::Visibility::VISIBLE);
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

            if (r.People.size() > 0) {
                this->people->setDataSource(new PeopleDataSource(r.People));
            } else {
                this->people->setVisibility(brls::Visibility::GONE);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserItem, AppConfig::instance().getUserId(), itemId);
}

void MediaMovie::doSimilar(const std::string& itemId) {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"limit", "12"},
        {"fields", "ItemCounts"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                this->similar->setDataSource(new VideoDataSource(r.Items));
            } else {
                this->similar->setVisibility(brls::Visibility::GONE);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->similar->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiSimilar, itemId, query);
}