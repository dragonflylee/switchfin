/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_series.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/video_view.hpp"

using namespace brls::literals;  // for _i18n

class EpisodeCardCell : public BaseVideoCard {
public:
    EpisodeCardCell() { this->inflateFromXMLRes("xml/view/episode_card.xml"); }

    static EpisodeCardCell* create() { return new EpisodeCardCell(); }

    BRLS_BIND(brls::Label, labelName, "episode/card/name");
    BRLS_BIND(brls::Label, labelOverview, "episode/card/overview");
};

class SeasonDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaEpisode>;

    explicit SeasonDataSource(const std::string& id, const MediaList& r) : seriesId(id), list(std::move(r)) {
        brls::Logger::debug("SeasonDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        EpisodeCardCell* cell = dynamic_cast<EpisodeCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        auto epimage = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (epimage != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_query({{"tag", epimage->second}, {"fillWidth", "300"}}));
        } else {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, this->seriesId,
                HTTP::encode_query({{"tag", item.SeriesPrimaryImageTag}, {"fillWidth", "300"}}));
        }

        cell->labelName->setText(fmt::format("{}. {}", item.IndexNumber, item.Name));
        cell->labelOverview->setText(item.Overview);

        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        auto& item = this->list.at(index);
        VideoView* view = new VideoView(item);
        brls::sync([view]() { brls::Application::giveFocus(view); });
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    std::string seriesId;
    MediaList list;
};

MediaSeries::MediaSeries(const jellyfin::MediaItem& item) : seriesId(item.Id) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");
    brls::Logger::debug("MediaSeries: create");

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) { return true; });
    this->recyclerEpisodes->registerCell("Cell", &EpisodeCardCell::create);

    this->doSeasons();

    // 加载 Logo
    auto logo = item.ImageTags.find(jellyfin::imageTypeLogo);
    if (logo != item.ImageTags.end()) {
        Image::load(this->imageLogo, jellyfin::apiLogoImage, item.Id,
            HTTP::encode_query({
                {"tag", logo->second},
                {"maxWidth", "300"},
            }));
    } else {
        this->imageLogo->setVisibility(brls::Visibility::GONE);
    }
}

void MediaSeries::doSeasons() {
    std::string query = HTTP::encode_query({
        {"userId", AppConfig::instance().getUserId()},
        {"Fields", "ItemCounts,PrimaryImageAspectRatio"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaItem>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                this->seasonIds.clear();
                std::vector<std::string> seasons;
                for (auto& it : r.Items) {
                    seasons.push_back(it.Name);
                    this->seasonIds.push_back(it.Id);
                }
                this->selectorSeason->init(
                    "", seasons, 0, [this](int index) { this->doEpisodes(this->seasonIds[index]); });
                this->selectorSeason->setSelection(0);
                brls::Application::giveFocus(this->selectorSeason);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerEpisodes->setError(ex);
        },
        jellyfin::apiShowSeanon, this->seriesId, query);
}

void MediaSeries::doEpisodes(const std::string& seasonId) {
    std::string query = HTTP::encode_query({
        {"userId", AppConfig::instance().getUserId()},
        {"seasonId", seasonId},
        {"Fields", "ItemCounts,PrimaryImageAspectRatio,Overview"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaEpisode>& r) {
            ASYNC_RELEASE
            this->recyclerEpisodes->setDataSource(new SeasonDataSource(this->seriesId, r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recyclerEpisodes->setError(ex);
        },
        jellyfin::apiShowEpisodes, this->seriesId, query);
}