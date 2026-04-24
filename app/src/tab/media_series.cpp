/*
    Copyright 2023 dragonflylee
*/

#include "activity/player_view.hpp"
#include "api/jellyfin.hpp"
#include "tab/media_series.hpp"
#include "view/h_recycling.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/svg_image.hpp"
#include "view/text_box.hpp"
#include "view/video_card.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "view/presenter.hpp"
#include "view/context_menu.hpp"
#include "utils/keybind.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

class EpisodeCardCell : public BaseCardCell {
public:
    EpisodeCardCell() { this->inflateFromXMLRes("xml/view/episode_card.xml"); }

    static RecyclingGridItem* create() { return new EpisodeCardCell(); }

    BRLS_BIND(brls::Label, labelName, "episode/card/name");
    BRLS_BIND(brls::Label, labelOverview, "episode/card/overview");
    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(SVGImage, badgeFavorite, "video/card/badge/favorite");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
};

class EpisodeDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Episode>;

    explicit EpisodeDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("EpisodeDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        EpisodeCardCell* cell = dynamic_cast<EpisodeCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.Id);

        auto epimage = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (epimage != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", epimage->second}, {"fillWidth", "300"}}));
        } else if (item.SeriesId.is_string()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.SeriesId.get<std::string>(),
                HTTP::encode_form({{"tag", item.SeriesPrimaryImageTag}, {"fillWidth", "300"}}));
        }

        if (item.IndexNumber > 0) {
            cell->labelName->setText(fmt::format("{}. {}", item.IndexNumber, item.Name));
        } else {
            cell->labelName->setText(item.Name);
        }
        cell->labelOverview->setText(item.Overview);

        if (item.UserData.IsFavorite) {
            cell->badgeFavorite->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->badgeFavorite->setVisibility(brls::Visibility::INVISIBLE);
        }

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

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        PlayerView* view = new PlayerView(item);
        view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        if (item.SeriesId.is_string()) view->setSeries(item.SeriesId.get<std::string>());
        brls::sync([view]() { brls::Application::giveFocus(view); });
    }

    void onContextMenu(brls::Box* recycler, size_t index) {
        auto& item = this->list.at(index);
        brls::Box* menu = new ContextMenu(item);
        brls::Application::pushActivity(new brls::Activity(menu));
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

class MediaSeason : public AttachedView {
public:
    MediaSeason(const jellyfin::Season& item) : seriesId(item.SeriesId), seasonId(item.Id) {
        this->inflateFromXMLRes("xml/tabs/seasons.xml");

        this->recycler->registerCell("Cell", EpisodeCardCell::create);

        auto contextAction = [this](brls::View*) {
            auto* focus = dynamic_cast<RecyclingGridItem*>(brls::Application::getCurrentFocus());
            if (!focus) return false;
            auto* ds = dynamic_cast<EpisodeDataSource*>(this->recycler->getDataSource());
            if (!ds) return false;
            size_t idx = focus->getIndex();
            if (idx >= ds->getItemCount()) return false;
            ds->onContextMenu(this->recycler, idx);
            return true;
        };
        this->recycler->registerAction("hints/submit"_i18n, brls::BUTTON_X, contextAction, true);
        this->recycler->registerAction(KeyBind::getSetting(), contextAction);
    }

    void onCreate() override {
        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"seasonId", this->seasonId},
            {"fields", "ItemCounts,PrimaryImageAspectRatio,Chapters,Overview"},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
                ASYNC_RELEASE
                this->recycler->setDataSource(new EpisodeDataSource(r.Items));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->recycler->setError(ex);
            },
            jellyfin::apiShowEpisodes, this->seriesId, query);
    }

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/episodes");

    std::string seriesId;
    std::string seasonId;
};

MediaSeries::MediaSeries(const jellyfin::Item& item) : seriesId(item.Id) {
    brls::Logger::debug("Tab MediaSeries: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");

    this->headerTitle->setTitle(item.Name);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->similar->registerCell("Cell", VideoCardCell::create);
    this->nextUp->registerCell("Cell", VideoCardCell::create);
    this->special->registerCell("Cell", VideoCardCell::create);
    this->tabFrame->registerTabAction(this);

    this->doSeason();
    this->doSeries();
    this->doNextup();
    this->doSimilar();
    this->doSpecial();

    // loading Logo
    auto logo = item.ImageTags.find(jellyfin::imageTypePrimary);
    if (logo != item.ImageTags.end()) {
        Image::load(this->imageLogo, jellyfin::apiPrimaryImage, item.Id,
            HTTP::encode_form({
                {"tag", logo->second},
                {"maxWidth", "240"},
            }));
        this->imageLogo->setVisibility(brls::Visibility::VISIBLE);
    }
}

MediaSeries::~MediaSeries() {
    brls::Logger::debug("Tab MediaSeries: delete");
    Image::cancel(this->imageLogo);
}

void MediaSeries::doRequest() {
    if (this->tabFrame->isOnTop) {
        auto view = dynamic_cast<AttachedView*>(this->tabFrame->getActiveTab());
        if (view) view->onCreate();
        this->doNextup();
    }
}

void MediaSeries::doSeries() {
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
            if (r.People.size() > 0) {
                this->people->setDataSource(new PeopleDataSource(r.People));
            } else {
                this->people->setVisibility(brls::Visibility::GONE);
            }

            auto logo = r.ImageTags.find(jellyfin::imageTypePrimary);
            if (logo != r.ImageTags.end()) {
                Image::load(this->imageLogo, jellyfin::apiPrimaryImage, r.Id,
                    HTTP::encode_form({
                        {"tag", logo->second},
                        {"maxWidth", "240"},
                    }));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserItem, AppConfig::instance().getUserId(), this->seriesId);
}

void MediaSeries::doSeason() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"fields", "ItemCounts"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Season>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Season>& r) {
            ASYNC_RELEASE

            for (size_t i = 0; i < r.Items.size(); i++) {
                auto& it = r.Items.at(i);
                auto* item = new AutoSidebarItem();
                item->setTabStyle(AutoTabBarStyle::ACCENT);
                item->setFontSize(22);
                item->setLabel(it.Name);
                this->tabFrame->addTab(item, [it]() { return new MediaSeason(it); });
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("doSeason {}", ex);
        },
        jellyfin::apiShowSeanon, this->seriesId, query);
}

void MediaSeries::doNextup() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"fields", "MediaSourceCount"},
        {"seriesId", this->seriesId},
    });
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                auto items = std::move(r.Items);
                items[0].SeriesName.clear();
                this->nextUp->setDataSource(new VideoDataSource(items));
                this->nextUp->setVisibility(brls::Visibility::VISIBLE);
                this->labelNextup->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->nextUp->setVisibility(brls::Visibility::GONE);
                this->labelNextup->setVisibility(brls::Visibility::GONE);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->nextUp->setVisibility(brls::Visibility::GONE);
            this->labelNextup->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        jellyfin::apiShowNextUp, query);
}

void MediaSeries::doSimilar() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"limit", "12"},
        {"enableImageTypes", "Primary"},
        {"fields", "ItemCounts"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                this->similar->setDataSource(new VideoDataSource(r.Items));
                this->similar->setVisibility(brls::Visibility::VISIBLE);
                this->labelSimilar->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->similar->setVisibility(brls::Visibility::GONE);
                this->labelSimilar->setVisibility(brls::Visibility::GONE);
                this->similar->clearData();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->similar->setVisibility(brls::Visibility::GONE);
            this->labelSimilar->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        jellyfin::apiSimilar, this->seriesId, query);
}

void MediaSeries::doSpecial() {
    ASYNC_RETAIN
    jellyfin::getJSON<std::vector<jellyfin::Episode>>(
        [ASYNC_TOKEN](const std::vector<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.size() > 0) {
                this->special->setDataSource(new VideoDataSource(r));
                this->special->setVisibility(brls::Visibility::VISIBLE);
                this->labelSpecial->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->special->setVisibility(brls::Visibility::GONE);
                this->labelSpecial->setVisibility(brls::Visibility::GONE);
                this->special->clearData();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->special->setVisibility(brls::Visibility::GONE);
            this->labelSpecial->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        jellyfin::apiItemSpecial, AppConfig::instance().getUserId(), this->seriesId);
}