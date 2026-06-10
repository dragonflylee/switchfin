/*
    Copyright 2023 dragonflylee
*/

#include "activity/player_view.hpp"
#include "api/plex.hpp"
#include "api/plex/watchlist.hpp"
#include "tab/media_series.hpp"
#include "view/h_recycling.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/icon_button.hpp"
#include "view/svg_image.hpp"
#include "view/text_box.hpp"
#include "view/video_card.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "view/recyling_video.hpp"
#include "view/presenter.hpp"
#include "view/context_menu.hpp"
#include "utils/keybind.hpp"
#include "utils/download.hpp"
#include "utils/dialog.hpp"
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

/// Cellule d'en-tête de la vue saison (pochette + infos + bouton de
/// téléchargement) : PREMIÈRE cellule du flow, elle défile avec les
/// épisodes. Non focusable elle-même : la navigation atteint le bouton via
/// getDefaultFocus (cf. DownloadSectionHeader).
class SeasonHeaderCell : public RecyclingGridItem {
public:
    SeasonHeaderCell() {
        this->inflateFromXMLRes("xml/view/season_header.xml");
        this->setFocusable(false);
        // le bouton est câblé UNE fois ; cellForRow ne fait que remplacer
        // onDownload (registerClickAction à chaque bind cumulerait les actions)
        this->btnDownload->registerClickAction([this](...) {
            if (this->onDownload) this->onDownload();
            return true;
        });
    }

    void setItem(const plex::Item& item, const std::string& fallbackSummary) {
        this->picture->clear();
        const std::string& thumb = item.thumb.empty() ? item.parentThumb : item.thumb;
        if (!thumb.empty()) Image::load(this->picture, thumb, 225);
        this->labelTitle->setText(item.title);
        std::string meta;
        if (item.leafCount > 0) {
            meta = fmt::format("{} {}", item.leafCount,
                item.leafCount > 1 ? "main/media/episodes"_i18n : "main/media/episode"_i18n);
            if (item.year > 0) meta = fmt::format("{}  ·  {}", item.year, meta);
        } else if (item.year > 0) {
            meta = std::to_string(item.year);
        }
        this->labelMeta->setText(meta);
        this->labelMeta->setVisibility(meta.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        this->labelOverview->setText(item.summary.empty() ? fallbackSummary : item.summary);
    }

    std::function<void()> onDownload = nullptr;

private:
    BRLS_BIND(brls::Image, picture, "season/image/poster");
    BRLS_BIND(brls::Label, labelTitle, "season/label/title");
    BRLS_BIND(brls::Label, labelMeta, "season/label/meta");
    BRLS_BIND(TextBox, labelOverview, "season/label/overview");
    BRLS_BIND(IconButton, btnDownload, "season/download");
};

/// Source de la vue saison : index 0 = en-tête, 1..N = épisodes. Hauteurs
/// FIXES via heightForRow — aucune mesure yoga (un TextBox au texte vide
/// mesurait NaN et dégénérait le layout : bug « Scrubs S1-8 », saisons sans
/// résumé propre, cf. textBoxMeasureFunc).
class SeasonEpisodesDataSource : public RecyclingGridDataSource {
public:
    static constexpr float HEADER_HEIGHT = 255;  // pochette 225 + air 30
    static constexpr float CARD_HEIGHT = 190;    // vignette 180 + padding 2x5

    using MediaList = std::vector<plex::Item>;

    SeasonEpisodesDataSource(const plex::Item& season, const std::string& fallback, const MediaList& episodes)
        : season(season), fallbackSummary(fallback), list(episodes) {}

    size_t getItemCount() override { return this->list.size() + 1; }

    float heightForRow(brls::View* recycler, size_t index) override {
        return index == 0 ? HEADER_HEIGHT : CARD_HEIGHT;
    }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        if (index == 0) {
            auto* header = dynamic_cast<SeasonHeaderCell*>(recycler->dequeueReusableCell("Header"));
            header->setItem(this->season, this->fallbackSummary);
            header->onDownload = [this]() { this->downloadRemaining(); };
            return header;
        }

        EpisodeCardCell* cell = dynamic_cast<EpisodeCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index - 1);
        cell->setId(item.ratingKey);

        // vignette d'épisode, sinon celle de la série (cellule purgée d'abord)
        cell->picture->clear();
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
            cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
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
        if (index == 0) return;  // l'en-tête n'agit que par son bouton
        auto& item = this->list.at(index - 1);
        PlayerView* view = new PlayerView(item);
        view->setTitie(item.grandparentTitle.empty()
                           ? fmt::format("S{}E{} — {}", item.parentIndex, item.index, item.title)
                           : fmt::format("{} · S{}E{} — {}", item.grandparentTitle, item.parentIndex, item.index,
                                 item.title));
        if (!item.grandparentRatingKey.empty()) view->setSeries(item.grandparentRatingKey);
        brls::sync([view]() { brls::Application::giveFocus(view); });
    }

    void onContextMenu(brls::Box* recycler, size_t index) {
        if (index == 0) return;
        auto& item = this->list.at(index - 1);
        brls::Box* menu = new ContextMenu(item, recycler);
        brls::Application::pushActivity(new brls::Activity(menu));
    }

    /// résumé série arrivé après coup (chemin « aller à la saison ») :
    /// utilisé au prochain bind de l'en-tête
    void setFallbackSummary(const std::string& s) { this->fallbackSummary = s; }

    const plex::Item& getSeason() const { return this->season; }

    void clearData() override { this->list.clear(); }

private:
    /// file le téléchargement de tous les épisodes pas encore récupérés
    void downloadRemaining() {
        auto& dm = DownloadManager::instance();
        std::vector<std::string> wanted;
        for (auto& ep : this->list) {
            if (!dm.isDownloaded(ep.ratingKey) && !dm.isDownloading(ep.ratingKey)) wanted.push_back(ep.ratingKey);
        }
        if (wanted.empty()) {
            brls::Application::notify("main/download/completed"_i18n);
            return;
        }
        Dialog::cancelable(fmt::format(fmt::runtime("main/download/confirm_season"_i18n), wanted.size()), [wanted]() {
            auto& dm = DownloadManager::instance();
            for (auto& key : wanted) dm.addDownload(key);
            brls::Application::notify("main/download/season_queued"_i18n);
        });
    }

    plex::Item season;
    std::string fallbackSummary;
    MediaList list;
};

/// Vue de détail d'une saison : TOUT défile — l'en-tête (pochette + infos +
/// bouton) est la première cellule du RecyclingGrid (flow), suivie des
/// épisodes. Empilée par-dessus la fiche série via ui::presentDetail — le
/// rafraîchissement après lecture passe par Presenter (VIDEO_CLOSE →
/// doRequest).
class MediaSeason : public brls::Box, public Presenter {
public:
    MediaSeason(const plex::Item& item, const std::string& fallbackSummary)
        : season(item), fallbackSummary(fallbackSummary) {
        this->inflateFromXMLRes("xml/tabs/seasons.xml");

        this->recycler->registerCell("Header", []() { return new SeasonHeaderCell(); });
        this->recycler->registerCell("Cell", []() {
            auto cell = new EpisodeCardCell();
            auto actionListener = [cell](brls::View*) -> bool {
                // même remontée robuste que VideoCardCell (video_card.cpp)
                brls::Box* view = cell->getParent();
                RecyclingView* recycler = nullptr;
                while (view && !(recycler = dynamic_cast<RecyclingView*>(view))) view = view->getParent();
                if (!recycler) return false;
                auto* dataSrc = dynamic_cast<SeasonEpisodesDataSource*>(recycler->getDataSource());
                if (!dataSrc) return false;
                dataSrc->onContextMenu(view, cell->getIndex());
                return true;
            };
            // hint visible (« X Options »), cf. video_card.cpp
            cell->registerAction("hints/option"_i18n, brls::BUTTON_X, actionListener);
            cell->registerAction(KeyBind::getSetting(), actionListener);
            return cell;
        });

        // résumé de la saison absent ET résumé série pas encore connu (chemin
        // « aller à la saison » : doSeries n'a pas répondu) → le récupérer
        // nous-même, requête légère (metadata sans checkFiles)
        if (item.summary.empty() && fallbackSummary.empty() && !item.parentRatingKey.empty()) this->doSummary();

        this->doRequest();
    }

    /// (re)charge les épisodes — aussi déclenché à la fermeture du player
    /// pour rafraîchir badges vus / progression (Presenter)
    void doRequest() override {
        ASYNC_RETAIN
        // épisodes de la saison : GET /library/metadata/{seasonKey}/children
        // (plex_client.dart:1485-1493)
        plex::getJSON<plex::Container<plex::Item>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
                ASYNC_RELEASE
                this->recycler->setDataSource(
                    new SeasonEpisodesDataSource(this->season, this->fallbackSummary, r.Items));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->recycler->setError(ex);
            },
            plex::apiChildren, this->season.ratingKey, "");
    }

private:
    /// résumé de repli (celui de la série) quand la saison n'en a pas
    void doSummary() {
        ASYNC_RETAIN
        plex::getJSON<plex::Container<plex::Item>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
                ASYNC_RELEASE
                if (r.Items.empty() || r.Items.front().summary.empty()) return;
                this->fallbackSummary = r.Items.front().summary;
                // données déjà affichées : mettre à jour la source et
                // l'en-tête s'il est attaché (sinon le prochain bind suffit)
                auto* src = dynamic_cast<SeasonEpisodesDataSource*>(this->recycler->getDataSource());
                if (!src) return;
                src->setFallbackSummary(this->fallbackSummary);
                auto* header = dynamic_cast<SeasonHeaderCell*>(this->recycler->getGridItemByIndex(0));
                if (header) header->setItem(src->getSeason(), this->fallbackSummary);
            },
            [ASYNC_TOKEN](const std::string& ex) { ASYNC_RELEASE },
            plex::apiMetadata, this->season.parentRatingKey, "");
    }

    BRLS_BIND(RecyclingGrid, recycler, "media/episodes");

    plex::Item season;
    std::string fallbackSummary;
};

/// Rangée « Saisons » de la fiche série : cartes poster (titre de saison
/// localisé par le serveur + « N épisodes »). Clic → vue saison empilée.
class SeasonDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Item>;

    /// `fallbackSummary` pointe sur MediaSeries::seriesSummary (adresse
    /// stable, durée de vie commune : la source meurt avec le recycler de la
    /// fiche) — le résumé n'est connu qu'après doSeries.
    SeasonDataSource(const MediaList& r, const std::string* fallbackSummary)
        : list(std::move(r)), fallbackSummary(fallbackSummary) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.ratingKey);

        cell->labelTitle->setText(item.title);
        if (item.leafCount > 0) {
            cell->labelExt->setText(fmt::format("{} {}", item.leafCount,
                item.leafCount > 1 ? "main/media/episodes"_i18n : "main/media/episode"_i18n));
            cell->labelExt->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        }

        // affiche de la saison, repli sur celle de la série (cellule purgée :
        // un item sans affiche garderait celle du précédent occupant)
        cell->picture->clear();
        if (!item.thumb.empty()) {
            Image::load(cell->picture, item.thumb, 325);
        } else if (!item.parentThumb.empty()) {
            Image::load(cell->picture, item.parentThumb, 325);
        }

        // saison entièrement vue → pastille ; pas de barre de progression
        if (item.leafCount > 0 && item.viewedLeafCount >= item.leafCount) {
            cell->badgeTopRight->setImageFromSVGRes("icon/ico-checkmark.svg");
            cell->badgeTopRight->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        }
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        ui::presentDetail(recycler, new MediaSeason(item, *this->fallbackSummary));
    }

    void clearData() override { this->list.clear(); }

private:
    MediaList list;
    const std::string* fallbackSummary;
};

MediaSeries::MediaSeries(const plex::Item& item)
    : seriesId(item.type == plex::mediaTypeSeason && !item.parentRatingKey.empty() ? item.parentRatingKey
                                                                                   : item.ratingKey) {
    brls::Logger::debug("Tab MediaSeries: create");
    if (item.type == plex::mediaTypeSeason) this->wantedSeason = item.ratingKey;
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");

    this->labelTitle->setText(item.title);
    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
        this->imageFade->setImageFromRes("img/fade-bottom-light.png");
    // affiche de la série (ou de la saison sélectionnée)
    Image::load(this->imagePoster, item.thumb.empty() ? item.parentThumb : item.thumb, 325);
    this->seasons->registerCell("Cell", VideoCardCell::create);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->special->registerCell("Cell", VideoCardCell::create);

    // les boutons et la rangée de saisons n'ont pas de chevauchement
    // géométrique : route explicite (cf. media_movie.cpp)
    this->btnPlay->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");
    this->btnDownload->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");
    this->btnWatchlist->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");

    this->btnPlay->registerClickAction([this](...) {
        this->doPlay();
        return true;
    });
    this->btnDownload->registerClickAction([this](...) {
        this->doDownloadSeries();
        return true;
    });

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
    // après une lecture : prochain épisode (bouton Lire) + états vus des
    // cartes saisons ; les épisodes sont rafraîchis par MediaSeason lui-même
    this->doNextup();
    this->doSeason();
}

void MediaSeries::doPlay() {
    if (this->onDeck.ratingKey.empty()) return;
    // copie : « Relancer » (série terminée) force le départ à 0 — PlayerView
    // reprendrait sinon au viewOffset résiduel de l'épisode (player_view.cpp:101)
    plex::Item item = this->onDeck;
    if (this->replay) item.viewOffset = 0;
    PlayerView* view = new PlayerView(item);
    view->setTitie(item.grandparentTitle.empty()
                       ? fmt::format("S{}E{} — {}", item.parentIndex, item.index, item.title)
                       : fmt::format(
                             "{} · S{}E{} — {}", item.grandparentTitle, item.parentIndex, item.index, item.title));
    if (!item.grandparentRatingKey.empty()) view->setSeries(item.grandparentRatingKey);
    brls::sync([view]() { brls::Application::giveFocus(view); });
}

void MediaSeries::doDownloadSeries() {
    ASYNC_RETAIN
    // série entière : GET /library/metadata/{showId}/allLeaves → filtre des
    // épisodes ni téléchargés ni en cours → confirmation → file d'attente
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            auto& dm = DownloadManager::instance();
            std::vector<std::string> wanted;
            for (auto& ep : r.Items) {
                if (!dm.isDownloaded(ep.ratingKey) && !dm.isDownloading(ep.ratingKey))
                    wanted.push_back(ep.ratingKey);
            }
            if (wanted.empty()) {
                brls::Application::notify("main/download/completed"_i18n);
                return;
            }
            Dialog::cancelable(
                fmt::format(fmt::runtime("main/download/confirm_season"_i18n), wanted.size()), [wanted]() {
                    auto& dm = DownloadManager::instance();
                    for (auto& key : wanted) dm.addDownload(key);
                    brls::Application::notify("main/download/season_queued"_i18n);
                });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        plex::apiAllLeaves, this->seriesId);
}

void MediaSeries::initWatchlist(const std::string& guid) {
    this->seriesGuid = guid;
    // ancien agent (guid non plex://…) : titre non adressable sur le provider
    if (plex::providerRatingKey(guid).empty()) return;

    this->btnWatchlist->registerClickAction([this](...) {
        this->toggleWatchlist();
        return true;
    });

    ASYNC_RETAIN
    // état : metadata.provider + includeUserState=1 (api/plex/watchlist.hpp) ;
    // le bouton reste caché tant que l'état n'est pas connu
    plex::fetchWatchlistState(
        guid,
        [ASYNC_TOKEN](bool state) {
            ASYNC_RELEASE
            this->watchlisted = state;
            this->updateWatchlistButton();
            this->btnWatchlist->setVisibility(brls::Visibility::VISIBLE);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaSeries watchlist state: {}", ex);
        });
}

void MediaSeries::toggleWatchlist() {
    bool add = !this->watchlisted;
    ASYNC_RETAIN
    // PUT discover.provider/actions/addToWatchlist|removeFromWatchlist
    plex::setWatchlisted(
        this->seriesGuid, add,
        [ASYNC_TOKEN, add]() {
            ASYNC_RELEASE
            this->watchlisted = add;
            this->updateWatchlistButton();
            brls::Application::notify(add ? "main/watchlist/added"_i18n : "main/watchlist/removed"_i18n);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        });
}

void MediaSeries::updateWatchlistButton() {
    // signet plein = déjà dans la Watchlist (convention Plex)
    this->btnWatchlist->setIcon(
        this->watchlisted ? "@res/icon/ico-bookmark-fill-light.svg" : "@res/icon/ico-bookmark-light.svg");
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
            this->labelTitle->setText(item.title);
            Image::load(this->imagePoster, item.thumb, 325);
            // bannière : fond (art) + logo détouré niché dans le bas du
            // fondu ; le titre texte reste TOUJOURS affiché (sous la
            // bannière). La bannière reste affichée pendant le chargement
            // (placeholder sombre) : pas de saut de layout à l'arrivée de
            // l'image — et le gone→visible déclenchait un bug de premier
            // rendu (dégradé + image fill).
            if (!item.art.empty()) {
                Image::load(this->imageBackdrop, item.art, 1080, 608);
                if (!item.clearLogo.empty()) {
                    Image::load(this->imageLogo, item.clearLogo, 440, 120);
                }
            } else {
                this->bannerBox->setVisibility(brls::Visibility::GONE);
                this->contentRow->setMarginTop(0);
                this->contentInfo->setMarginTop(0);
                this->invalidate();
            }
            // pill « année · N saisons » (childCount = nb de saisons du show)
            if (item.childCount > 0) {
                this->labelYear->setText(fmt::format(
                    "{}  ·  {} {}", item.year, item.childCount,
                    item.childCount > 1 ? "main/media/seasons"_i18n : "main/media/season"_i18n));
            } else {
                this->labelYear->setText(std::to_string(item.year));
            }
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
            this->seriesSummary = item.summary;

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

            // la watchlist porte sur la SÉRIE : guid du show (seriesId),
            // valable aussi quand la fiche a été ouverte depuis une saison
            this->initWatchlist(item.guid);
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
            if (r.Items.empty()) {
                this->labelSeasons->setVisibility(brls::Visibility::GONE);
                this->seasons->setVisibility(brls::Visibility::GONE);
                return;
            }
            this->seasons->setDataSource(new SeasonDataSource(r.Items, &this->seriesSummary));

            // « aller à la saison » : la saison voulue s'ouvre par-dessus la
            // fiche série (B revient à la fiche)
            if (!this->wantedSeason.empty()) {
                for (auto& it : r.Items) {
                    if (it.ratingKey != this->wantedSeason) continue;
                    ui::presentDetail(this, new MediaSeason(it, this->seriesSummary));
                    break;
                }
                this->wantedSeason.clear();
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
    // série entièrement vue (pas d'OnDeck) : premier épisode toutes saisons
    // confondues, via allLeaves paginé 0-1 → bouton « Relancer »
    HTTP::Form firstQuery;
    plex::addPagination(firstQuery, 0, 1);
    std::string firstUrl = conf.getUrl() + fmt::format(fmt::runtime(plex::apiAllLeaves), this->seriesId) + "?" +
                           HTTP::encode_form(firstQuery);
    // « token » est réservé par ASYNC_RETAIN (borealis/core/view.hpp:60)
    std::string accessToken = conf.getToken();

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url, firstUrl, accessToken]() {
        std::vector<plex::Item> items;
        bool fromStart = false;
        try {
            // prochain épisode : Metadata[0].OnDeck.Metadata est un OBJET,
            // pas un tableau → extraction manuelle (plex_client.dart:1035-1094)
            nlohmann::json j = plex::getSync(url, accessToken);
            auto& meta = j.at("MediaContainer").at("Metadata").at(0);
            if (meta.contains("OnDeck") && meta["OnDeck"].contains("Metadata")) {
                items.push_back(meta["OnDeck"]["Metadata"].get<plex::Item>());
            } else {
                // vérifié serveur 2026-06-10 (série 1024863 vue 9/9) :
                // Metadata[0] = S1E1 avec grandparent*/index/parentIndex
                auto first = plex::getSync(firstUrl, accessToken).get<plex::Container<plex::Item>>();
                if (!first.Items.empty()) {
                    items.push_back(first.Items.front());
                    fromStart = true;
                }
            }
        } catch (const std::exception& ex) {
            brls::Logger::warning("doNextup {}", ex.what());
        }
        brls::sync([ASYNC_TOKEN, items, fromStart]() {
            ASYNC_RELEASE
            // alimente le bouton « Lire »/« Relancer » (qui remplace la
            // rangée « À suivre ») ; série sans épisode ou erreur → masqué
            this->replay = fromStart;
            if (!items.empty()) {
                this->onDeck = items.front();
                this->btnPlay->setText(fromStart ? "main/media/replay"_i18n : "main/media/play"_i18n);
                this->btnPlay->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->onDeck = plex::Item();
                this->btnPlay->setVisibility(brls::Visibility::GONE);
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
                row->setFrameHeight(brls::getStyle()["app/card/poster/row"]);
                row->setItemWidth(brls::getStyle()["app/card/poster/width"]);
                row->setSidePadding(brls::getStyle()["main/content_padding_sides"]);
                // hub tronqué (more=1) : carte « + » vers la page complète
                if (hub.more && !hub.key.empty()) {
                    row->setItems(hub.items, hub.title, hub.key);
                } else {
                    row->setItems(hub.items);
                }
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
