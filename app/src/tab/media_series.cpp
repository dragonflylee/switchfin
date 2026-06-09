/*
    Copyright 2023 dragonflylee
*/

#include "activity/player_view.hpp"
#include "api/plex.hpp"
#include "tab/media_series.hpp"
#include "view/h_recycling.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/svg_image.hpp"
#include "view/text_box.hpp"
#include "view/video_card.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "view/recyling_video.hpp"
#include "view/presenter.hpp"
#include "view/context_menu.hpp"
#include "utils/keybind.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

class EpisodeCardCell : public BaseCardCell {
public:
    EpisodeCardCell() { this->inflateFromXMLRes("xml/view/episode_card.xml"); }

    BRLS_BIND(brls::Label, labelName, "episode/card/name");
    BRLS_BIND(brls::Label, labelOverview, "episode/card/overview");
    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
};

class EpisodeDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Item>;

    explicit EpisodeDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("EpisodeDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        EpisodeCardCell* cell = dynamic_cast<EpisodeCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.ratingKey);

        // vignette d'épisode, sinon celle de la série
        if (!item.thumb.empty()) {
            Image::load(cell->picture, item.thumb, 300);
        } else if (!item.grandparentThumb.empty()) {
            Image::load(cell->picture, item.grandparentThumb, 300);
        }

        if (item.index > 0) {
            cell->labelName->setText(fmt::format("{}. {}", item.index, item.title));
        } else {
            cell->labelName->setText(item.title);
        }
        cell->labelOverview->setText(item.summary);

        if (item.played()) {
            cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
            cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
        } else if (item.viewOffset > 0 && item.duration > 0) {
            cell->rectProgress->setWidthPercentage(float(item.viewOffset) / float(item.duration) * 100.f);
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
        view->setTitie(fmt::format("S{}E{} - {}", item.parentIndex, item.index, item.title));
        if (!item.grandparentRatingKey.empty()) view->setSeries(item.grandparentRatingKey);
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
    MediaSeason(const plex::Item& item) : seasonId(item.ratingKey) {
        this->inflateFromXMLRes("xml/tabs/seasons.xml");

        this->recycler->registerCell("Cell", []() {
            auto cell = new EpisodeCardCell();
            auto actionListener = [cell](brls::View*) -> bool {
                brls::Box* view = cell->getParent()->getParent();
                RecyclingView* recycler = dynamic_cast<RecyclingView*>(view);
                if (!recycler) return false;
                EpisodeDataSource* dataSrc = dynamic_cast<EpisodeDataSource*>(recycler->getDataSource());
                if (!dataSrc) return false;
                dataSrc->onContextMenu(view, cell->getIndex());
                return true;
            };
            cell->registerAction("hints/submit"_i18n, brls::BUTTON_X, actionListener, true);
            cell->registerAction(KeyBind::getSetting(), actionListener);
            return cell;
        });
    }

    void onCreate() override {
        ASYNC_RETAIN
        // épisodes de la saison : GET /library/metadata/{seasonKey}/children
        // (plex_client.dart:1485-1493)
        plex::getJSON<plex::Container<plex::Item>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
                ASYNC_RELEASE
                this->recycler->setDataSource(new EpisodeDataSource(r.Items));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->recycler->setError(ex);
            },
            plex::apiChildren, this->seasonId, "");
    }

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/episodes");

    std::string seasonId;
};

MediaSeries::MediaSeries(const plex::Item& item)
    : seriesId(item.type == plex::mediaTypeSeason && !item.parentRatingKey.empty() ? item.parentRatingKey
                                                                                   : item.ratingKey) {
    brls::Logger::debug("Tab MediaSeries: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");

    this->headerTitle->setTitle(item.title);
    // affiche de la série (ou de la saison sélectionnée)
    Image::load(this->imagePoster, item.thumb.empty() ? item.parentThumb : item.thumb, 300);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->nextUp->registerCell("Cell", VideoCardCell::create);
    this->special->registerCell("Cell", VideoCardCell::create);
    this->tabFrame->registerTabAction(this);

    this->doSeason();
    this->doSeries();
    this->doNextup();
    this->doRelated();
    this->doSpecial();
}

MediaSeries::~MediaSeries() {
    brls::Logger::debug("Tab MediaSeries: delete");
    Image::cancel(this->imageLogo);
    Image::cancel(this->imagePoster);
    Image::cancel(this->imageBackdrop);
}

void MediaSeries::doRequest() {
    if (this->tabFrame->isOnTop) {
        auto view = dynamic_cast<AttachedView*>(this->tabFrame->getActiveTab());
        if (view) view->onCreate();
        this->doNextup();
    }
}

void MediaSeries::doSeries() {
    std::string query = HTTP::encode_form({
        {"includeChapters", "1"},
        {"includeMarkers", "1"},
        {"includeStreams", "1"},
        {"checkFiles", "1"},
    });

    ASYNC_RETAIN
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->people->setVisibility(brls::Visibility::GONE);
                return;
            }
            auto& item = r.Items.front();
            this->headerTitle->setTitle(item.title);
            Image::load(this->imagePoster, item.thumb, 300);
            // bannière : fond (art) + logo détouré centré ; le bloc affiche
            // chevauche la bannière pour garder le contenu accessible
            if (!item.art.empty()) {
                Image::load(this->imageBackdrop, item.art, 1080, 608);
                if (!item.clearLogo.empty()) Image::load(this->imageLogo, item.clearLogo, 420, 120);
                this->bannerBox->setVisibility(brls::Visibility::VISIBLE);
                this->contentRow->setMarginTop(-60);
                this->contentInfo->setMarginTop(80);
            }
            this->labelYear->setText(std::to_string(item.year));
            if (item.contentRating.empty()) {
                this->parentalRating->getParent()->setVisibility(brls::Visibility::GONE);
            } else {
                this->parentalRating->setText(item.contentRating);
                this->parentalRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }
            if (item.rating == 0.0) {
                this->labelRating->getParent()->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelRating->setText(fmt::format("{:.1f}", item.rating));
                this->labelRating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }
            this->labelOverview->setText(item.summary);

            if (item.genres.empty()) {
                this->labelGenres->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelGenres->setText(fmt::format("{}", fmt::join(item.genres, ", ")));
                this->labelGenres->setVisibility(brls::Visibility::VISIBLE);
            }
            if (item.roles.size() > 0) {
                this->people->setDataSource(new PeopleDataSource(item.roles));
            } else {
                this->people->setVisibility(brls::Visibility::GONE);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        plex::apiMetadata, this->seriesId, query);
}

void MediaSeries::doSeason() {
    ASYNC_RETAIN
    // saisons : GET /library/metadata/{showKey}/children (plex_client.dart:1470-1480)
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE

            for (size_t i = 0; i < r.Items.size(); i++) {
                auto& it = r.Items.at(i);
                auto* item = new AutoSidebarItem();
                item->setTabStyle(AutoTabBarStyle::ACCENT);
                item->setFontSize(22);
                item->setLabel(it.title);
                this->tabFrame->addTab(item, [it]() { return new MediaSeason(it); });
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("doSeason {}", ex);
        },
        plex::apiChildren, this->seriesId, "");
}

void MediaSeries::doNextup() {
    auto& conf = AppConfig::instance();
    std::string url = conf.getUrl() + fmt::format("/library/metadata/{}?includeOnDeck=1", this->seriesId);
    // « token » est réservé par ASYNC_RETAIN (borealis/core/view.hpp:60)
    std::string accessToken = conf.getToken();

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url, accessToken]() {
        std::vector<plex::Item> items;
        try {
            // « À suivre » par série : Metadata[0].OnDeck.Metadata est un OBJET,
            // pas un tableau → extraction manuelle (plex_client.dart:1035-1094)
            nlohmann::json j = plex::getSync(url, accessToken);
            auto& meta = j.at("MediaContainer").at("Metadata").at(0);
            if (meta.contains("OnDeck") && meta["OnDeck"].contains("Metadata")) {
                items.push_back(meta["OnDeck"]["Metadata"].get<plex::Item>());
            }
        } catch (const std::exception& ex) {
            brls::Logger::warning("doNextup {}", ex.what());
        }
        brls::sync([ASYNC_TOKEN, items]() {
            ASYNC_RELEASE
            if (!items.empty()) {
                this->nextUp->setDataSource(new VideoDataSource(items));
                this->nextUp->setVisibility(brls::Visibility::VISIBLE);
                this->labelNextup->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->nextUp->setVisibility(brls::Visibility::GONE);
                this->labelNextup->setVisibility(brls::Visibility::GONE);
            }
        });
    });
}

void MediaSeries::doRelated() {
    std::string query = HTTP::encode_form({{"count", "12"}});

    ASYNC_RETAIN
    // toutes les rangées « related » du serveur, titres localisés
    // (plex_client.dart:1981-2006)
    plex::getJSON<plex::Container<plex::Hub>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Hub>& r) {
            ASYNC_RELEASE
            this->boxRelated->clearViews();
            for (auto& hub : r.Items) {
                if (hub.items.empty()) continue;
                RecylingVideo* row = new RecylingVideo();
                row->setTitle(hub.title);
                row->setFrameHeight(300);
                row->setItemWidth(175);
                row->setItems(hub.items);
                this->boxRelated->addView(row);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        plex::apiHubRelated, this->seriesId, query);
}

void MediaSeries::doSpecial() {
    ASYNC_RETAIN
    // bonus : GET /library/metadata/{key}/extras (plex_client.dart:1512-1522) ;
    // type "clip" → lecture directe gérée par VideoDataSource
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                this->special->setDataSource(new VideoDataSource(r.Items));
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
        plex::apiExtras, this->seriesId);
}
