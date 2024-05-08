/*
    Copyright 2023 dragonflylee
*/

#include "activity/player_view.hpp"
#include "api/jellyfin.hpp"
#include "tab/media_series.hpp"
#include "view/svg_image.hpp"
#include "view/video_card.hpp"

using namespace brls::literals;  // for _i18n

class EpisodeCardCell : public BaseVideoCard {
public:
    EpisodeCardCell() { this->inflateFromXMLRes("xml/view/episode_card.xml"); }

    static RecyclingGridItem* create() { return new EpisodeCardCell(); }

    BRLS_BIND(brls::Label, labelName, "episode/card/name");
    BRLS_BIND(brls::Label, labelOverview, "episode/card/overview");
    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
};

class EpisodeDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaEpisode>;

    explicit EpisodeDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("EpisodeDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        EpisodeCardCell* cell = dynamic_cast<EpisodeCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        auto epimage = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (epimage != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", epimage->second}, {"fillWidth", "300"}}));
        } else {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId,
                HTTP::encode_form({{"tag", item.SeriesPrimaryImageTag}, {"fillWidth", "300"}}));
        }

        if (item.IndexNumber > 0) {
            cell->labelName->setText(fmt::format("{}. {}", item.IndexNumber, item.Name));
        } else {
            cell->labelName->setText(item.Name);
        }
        cell->labelOverview->setText(item.Overview);

        if (item.UserData.Played) {
            cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
            cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
        } else if (item.UserData.PlaybackPositionTicks) {
            cell->rectProgress->setWidthPercentage(item.UserData.PlayedPercentage);
            cell->rectProgress->getParent()->setVisibility(brls::Visibility::VISIBLE);
            cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        } else {
            cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
            cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        }

        return cell;
    }

    void onItemSelected(brls::View* recycler, size_t index) override {
        auto& item = this->list.at(index);
        PlayerView* view = new PlayerView(item);
        view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        view->setSeries(item.SeriesId);
        brls::sync([view]() { brls::Application::giveFocus(view); });
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

MediaSeries::MediaSeries(const std::string& itemId) : seriesId(itemId) {
    brls::Logger::debug("Tab MediaSeries: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");
    this->imageLogo->setVisibility(brls::Visibility::GONE);

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [](...) { return true; });
    this->recyclerEpisodes->registerCell("Cell", EpisodeCardCell::create);

    this->selectorSeason->init("", {""}, 0, [this](int index) { this->doEpisodes(this->seasonIds[index]); });

    this->doSeason();
    this->doSeries();
}

MediaSeries::~MediaSeries() { brls::Logger::debug("Tab MediaSeries: delete"); }

void MediaSeries::doRequest() {
    int index = this->selectorSeason->getSelection();
    this->doEpisodes(this->seasonIds.at(index));
}

void MediaSeries::doSeries() {
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MedisSeries& r) {
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
            // loading Logo
            auto logo = r.ImageTags.find(jellyfin::imageTypeLogo);
            if (logo != r.ImageTags.end()) {
                Image::load(this->imageLogo, jellyfin::apiLogoImage, r.Id,
                    HTTP::encode_form({
                        {"tag", logo->second},
                        {"maxWidth", "300"},
                    }));
                this->imageLogo->setVisibility(brls::Visibility::VISIBLE);
            }
        },
        [](...) {}, jellyfin::apiUserItem, AppConfig::instance().getUser().id, this->seriesId);
}

void MediaSeries::doSeason() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUser().id},
        {"fields", "ItemCounts"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaSeason>& r) {
            ASYNC_RELEASE

            size_t firstSeason = 0;
            this->seasonIds.clear();

            std::vector<std::string> seasons;
            for (size_t i = 0; i < r.Items.size(); i++) {
                auto& it = r.Items.at(i);
                seasons.push_back(it.Name);
                this->seasonIds.push_back(it.Id);
                if (it.IndexNumber == 1) firstSeason = i;
            }
            this->selectorSeason->setData(seasons);
            this->selectorSeason->setSelection(firstSeason);
            brls::sync([this]() { brls::Application::giveFocus(this->selectorSeason); });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerEpisodes->setError(ex);
        },
        jellyfin::apiShowSeanon, this->seriesId, query);
}

void MediaSeries::doEpisodes(const std::string& seasonId) {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUser().id},
        {"seasonId", seasonId},
        {"fields", "ItemCounts,PrimaryImageAspectRatio,Chapters,Overview"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->recyclerEpisodes->setDataSource(new EpisodeDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerEpisodes->setError(ex);
        },
        jellyfin::apiShowEpisodes, this->seriesId, query);
}