/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_series.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"

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

        std::string query = HTTP::encode_query({
            {"tag", item.ImageTags[jellyfin::imageTypePrimary]},
            {"maxWidth", "300"},
        });
        Image::load(cell->picture, jellyfin::apiPrimaryImage, AppConfig::instance().getServerUrl(), item.Id, query);

        cell->labelName->setText(fmt::format("{}. {}", item.IndexNumber, item.Name));
        cell->labelOverview->setText(item.Overview);

        return cell;
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {}

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    std::string seriesId;
    MediaList list;
};

MediaSeries::MediaSeries(const std::string& id) : seriesId(id) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");
    brls::Logger::debug("MediaSeries: create");

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) { return true; });
    this->recyclerEpisodes->registerCell("Cell", &EpisodeCardCell::create);

    this->doSeasons();
}

void MediaSeries::doSeasons() {
    std::string query = HTTP::encode_query({
        {"userId", AppConfig::instance().getUserId()},
        {"Fields", "ItemCounts,PrimaryImageAspectRatio,Overview"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MediaSeason>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                this->seasonIds.clear();
                std::vector<std::string> seasons;
                for (auto& it : r.Items) {
                    seasons.push_back(it.Name);
                    this->seasonIds.push_back(it.Id);
                }
                this->selectorSeason->init("", seasons, 0, [this](int index) {
                    this->doEpisodes(this->seasonIds[index]);
                    return true;
                });
                this->labelSeason->setText(r.Items[0].SeriesName);
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