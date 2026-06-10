/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "api/plex.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/media_filter.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/h_recycling.hpp"
#include "tab/suggest_show.hpp"
#include "tab/suggest_movie.hpp"
#include "utils/genre_image.hpp"
#include "utils/image.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

std::map<std::string, std::string> MediaCollection::customPrefs;

/// true when the current focus lives inside `view` — a tab loading in the
/// background must not steal the focus from the sidebar
static bool hasFocusWithin(brls::View* view) {
    for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent()) {
        if (v == view) return true;
    }
    return false;
}

class GenresDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Section>;

    explicit GenresDataSource(const MediaList& r, const std::string& itemId, const std::string& itemType)
        : list(std::move(r)), itemId(itemId), itemType(itemType) {
        brls::Logger::debug("GenresDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        // le Directory de genre n'a pas de thumb (vérifié serveur
        // 2026-06-10) → poster Kometa via le transcodeur photo du serveur
        // (genre_image.cpp) ; genre inconnu → placeholder posé par
        // prepareForReuse (aucune requête, le set Kometa est embarqué)
        cell->labelTitle->setText(item.title);
        cell->labelExt->setVisibility(brls::Visibility::GONE);
        std::string poster = GenreImage::posterUrl(item.title);
        if (!poster.empty()) Image::with(cell->picture, poster);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        ui::presentDetail(recycler, new MediaCollection(this->itemId, this->itemType, item.key));
    }

    void clearData() override { this->list.clear(); }

private:
    MediaList list;
    std::string itemId;
    std::string itemType;
};

class GenresTab : public RecyclingGrid {
public:
    GenresTab(const std::string& itemId, const std::string& itemType) {
        this->setGrow(1.f);
        this->registerCell("Cell", VideoCardCell::create);
        this->spanCount = brls::getStyle().getMetric("app/grid/6");
        // affiches 2:3 + zone labels, recalculé au layout (recycling_grid.cpp)
        this->itemImageRatio = 1.5f;
        this->itemExtraHeight = 55;
        // retraits internes au scroll (collection.xml n'a plus de padding
        // racine) ; top 70 : la barre d'onglets flottante (60) passe devant
        float side = brls::getStyle()["main/content_padding_sides"];
        this->setPadding(70, side, brls::getStyle()["main/content_padding_top_bottom"], side);

        int type = itemType == plex::mediaTypeShow ? plex::typeShow : plex::typeMovie;

        ASYNC_RETAIN
        // GET /library/sections/{key}/genre?type= → Directory key/title
        plex::getJSON<plex::Container<plex::Section>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN, itemId, itemType](const plex::Container<plex::Section>& r) {
                ASYNC_RELEASE
                this->setDataSource(new GenresDataSource(r.Items, itemId, itemType));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            plex::apiSectionGenres, itemId, type);
    }
};

class CollectionsTab : public RecyclingGrid {
public:
    CollectionsTab(const std::string& sectionId) : sectionId(sectionId) {
        this->setGrow(1.f);
        this->registerCell("Cell", VideoCardCell::create);
        this->spanCount = brls::getStyle().getMetric("app/grid/6");
        this->itemImageRatio = 1.5f;
        this->itemExtraHeight = 55;
        // top 70 : même contrat que GenresTab (barre flottante au-dessus)
        float side = brls::getStyle()["main/content_padding_sides"];
        this->setPadding(70, side, brls::getStyle()["main/content_padding_top_bottom"], side);

        this->onNextPage([this] { this->doRequest(); });
        this->doRequest();
    }

    void doRequest() {
        HTTP::Form query;
        plex::addPagination(query, this->start, this->pageSize);

        ASYNC_RETAIN
        // GET /library/sections/{key}/collections (plex_client.dart:2444-2462)
        plex::getJSON<plex::Container<plex::Item>>(
            AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
            [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
                ASYNC_RELEASE
                this->start = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->setEmpty();
                } else if (r.StartIndex == 0) {
                    this->setDataSource(new VideoDataSource(r.Items));
                } else if (r.Items.size() > 0) {
                    auto dataSrc = dynamic_cast<VideoDataSource*>(this->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->notifyDataChanged();
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            plex::apiCollections, this->sectionId, HTTP::encode_form(query));
    }

private:
    std::string sectionId;
    size_t start = 0;
    size_t pageSize = 60;
};

MediaCollection::MediaCollection(const std::string& itemId, const std::string& itemType, const std::string& genresId)
    : itemId(itemId), genresId(genresId), itemType(itemType), startIndex(0) {
    brls::Logger::debug("MediaCollection: create {} type {}", itemId, itemType);
    if (genresId.size() > 0) {
        this->inflateFromXMLRes("xml/tabs/media.xml");
    } else if (itemType == plex::mediaTypeMovie || itemType == plex::mediaTypeShow) {
        this->inflateFromXMLRes("xml/tabs/collection.xml");
        // centered top tabs: home (from XML), then suggest, collections, genres

        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/suggest"_i18n);
        if (itemType == plex::mediaTypeShow) {
            this->tabFrame->addTab(item, [this]() { return new SuggestShow(this->itemId); });
        } else {
            this->tabFrame->addTab(item, [this]() { return new SuggestMovie(this->itemId); });
        }

        // Collections : films uniquement — inutile sur les bibliothèques de
        // séries (retour recette UI n°5 ; vérifié serveur 2026-06-10 :
        // /library/sections/{show}/collections → size 0)
        if (itemType == plex::mediaTypeMovie) {
            item = new AutoSidebarItem();
            item->setTabStyle(AutoTabBarStyle::ACCENT);
            item->setFontSize(18);
            item->setLabel("main/media/collections"_i18n);
            this->tabFrame->addTab(item, [this]() { return new CollectionsTab(this->itemId); });
        }

        item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/genres"_i18n);
        this->tabFrame->addTab(item, [this]() { return new GenresTab(this->itemId, this->itemType); });

        this->tabFrame->registerTabAction(this);
    } else {
        this->inflateFromXMLRes("xml/tabs/media.xml");
        // mode collection (grille seule) : en-tête scrollé « titre + N
        // éléments · durée » comme la vue playlist (retour recette UI n°5) ;
        // posé AVANT le premier layout (contrat setHeaderView)
        if (itemType == plex::mediaTypeCollection) {
            brls::View* header = brls::View::createFromXMLResource("view/grid_header.xml");
            this->labelTitle = dynamic_cast<brls::Label*>(header->getView("grid/header/title"));
            this->labelMeta = dynamic_cast<brls::Label*>(header->getView("grid/header/meta"));
            this->recycler->setHeaderView(header, 84);
            this->doMetadata();
        }
    }

    this->pageSize = this->recycler->spanCount * 3;

    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    });

    this->registerAction(KeyBind::getRefresh(), [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    });

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    if (AppConfig::SYNC) {
        // préférences de tri persistées LOCALEMENT (item LIBRARY_SORT) :
        // /DisplayPreferences n'existe pas chez Plex (PLEX_MIGRATION.md §2.5)
        if (MediaCollection::customPrefs.empty()) {
            auto saved = AppConfig::instance().getItem(AppConfig::LIBRARY_SORT, nlohmann::json::object());
            for (auto& el : saved.items()) {
                if (el.value().is_string()) {
                    MediaCollection::customPrefs[el.key()] = el.value().get<std::string>();
                }
            }
        }
        this->loadFilter();
        this->doRequest();
    } else {
        this->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
            MediaFilter* filter = new MediaFilter();
            filter->getEvent()->subscribe([this]() {
                this->startIndex = 0;
                this->recycler->showSkeleton();
                this->doRequest();
            });
            brls::Application::pushActivity(new brls::Activity(filter));
            return true;
        });

        this->doRequest();
    }
}

brls::View* MediaCollection::getDefaultFocus() {
    // mode bibliothèque : déléguer au frame d'onglets, qui résout l'onglet
    // ACTIF et sa mémoire de focus. `recycler` est la grille de l'onglet
    // Accueil : la rendre directement quand un AUTRE onglet est affiché
    // donnait le focus à un arbre DÉTACHÉ (cellules invisibles, positions
    // périmées) — bug « focus perdu au retour de la sidebar » (recette n°6).
    // Résolution par id : pas de throw en mode collection (XML sans tabFrame).
    if (brls::View* frame = this->getView("media/tabFrame")) return frame->getDefaultFocus();
    return this->recycler;
}

void MediaCollection::doMetadata() {
    ASYNC_RETAIN
    // titre de l'en-tête : GET /library/metadata/{ratingKey} — le Metadata
    // d'une collection ne porte NI duration NI leafCount, seulement childCount
    // (vérifié serveur 2026-06-10 sur /library/metadata/1024133) ; le compte
    // affiché vient donc du totalSize de la grille (doRequest)
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (this->labelTitle && !r.Items.empty()) this->labelTitle->setText(r.Items.front().title);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaCollection: metadata {}", ex);
        },
        plex::apiMetadata, this->itemId, "");
}

void MediaCollection::updateMeta(int64_t count, int64_t durationMs) {
    if (!this->labelMeta) return;  // jamais posé hors mode collection
    if (count <= 0) {
        this->labelMeta->setVisibility(brls::Visibility::GONE);
        return;
    }
    std::string meta =
        fmt::format("{} {}", count, count > 1 ? "main/playlist/items"_i18n : "main/playlist/item"_i18n);
    if (durationMs > 0) {
        // même convention que la vue playlist (playlist_view.cpp) : h/min
        int min = int(durationMs / 60000);
        meta += min >= 60 ? fmt::format("  ·  {} h {:02d}", min / 60, min % 60) : fmt::format("  ·  {} min", min);
    }
    this->labelMeta->setText(meta);
    this->labelMeta->setVisibility(brls::Visibility::VISIBLE);
}

void MediaCollection::onCreate() {
    // sidebar tab embedding: the close cross only makes sense when presented
    brls::View* close = this->getView("media/close");
    if (close) close->setVisibility(brls::Visibility::GONE);
}

void MediaCollection::loadFilter() {
    this->recycler->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        MediaFilter* filter = new MediaFilter();
        filter->getEvent()->subscribe([this]() {
            this->startIndex = 0;
            this->recycler->showSkeleton();
            this->doRequest();
            this->saveFilter();
        });
        brls::Application::pushActivity(new brls::Activity(filter));
        return true;
    });

    auto it = MediaCollection::customPrefs.find(this->itemId);
    if (it == MediaCollection::customPrefs.end()) return;

    // format "sortBy,sortOrder,filtre" (ex. "addedAt,1,0")
    std::stringstream ss(it->second);
    std::string sortBy, sortOrder, unwatched;
    std::getline(ss, sortBy, ',');
    std::getline(ss, sortOrder, ',');
    std::getline(ss, unwatched, ',');

    for (size_t i = 0; i < std::size(MediaFilter::sortList); i++) {
        if (MediaFilter::sortList[i] == sortBy) {
            MediaFilter::selectedSort = i;
        }
    }
    MediaFilter::selectedOrder = sortOrder == "1" ? 1 : 0;
    MediaFilter::selectedUnplayed = unwatched == "1";
}

void MediaCollection::saveFilter() {
    MediaCollection::customPrefs[this->itemId] =
        fmt::format("{},{},{}", MediaFilter::sortList[MediaFilter::selectedSort], MediaFilter::selectedOrder,
            MediaFilter::selectedUnplayed ? 1 : 0);

    nlohmann::json value(MediaCollection::customPrefs);
    AppConfig::instance().setItem(AppConfig::LIBRARY_SORT, value);
}

void MediaCollection::doRequest() {
    HTTP::Form query;
    // tri Plex : sort=champ[:desc] (library_query_translator.dart:51-105)
    std::string sort = MediaFilter::sortList[MediaFilter::selectedSort];
    if (MediaFilter::selectedOrder) sort += ":desc";
    query["sort"] = sort;
    if (MediaFilter::selectedUnplayed) query["unwatched"] = "1";
    if (this->genresId.size() > 0) query["genre"] = this->genresId;
    plex::addPagination(query, this->startIndex, this->pageSize);

    std::string_view path = plex::apiSectionAll;
    if (this->itemType == plex::mediaTypeCollection) {
        path = plex::apiCollectionChildren;
    } else if (this->itemType == plex::mediaTypeMovie) {
        query["type"] = std::to_string(plex::typeMovie);
    } else if (this->itemType == plex::mediaTypeShow) {
        query["type"] = std::to_string(plex::typeShow);
    }
    // photo : pas de filtre type=

    ASYNC_RETAIN
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->updateMeta(0, 0);
                this->recycler->setEmpty();
            } else if (r.StartIndex == 0) {
                // méta de l'en-tête du mode collection : compte fiable
                // (totalSize) ; durée totale UNIQUEMENT si cette page couvre
                // toute la collection — le Metadata d'une collection n'a pas
                // de durée propre (vérifié serveur 2026-06-10) et une somme
                // partielle serait fausse
                if (this->labelMeta) {
                    int64_t total = 0;
                    if (r.TotalRecordCount <= (long)this->pageSize) {
                        for (auto& it : r.Items) {
                            // seuls movie/episode/clip portent une durée pleine
                            // (un show n'expose qu'une durée d'épisode)
                            bool full = it.type == plex::mediaTypeMovie || it.type == plex::mediaTypeEpisode ||
                                        it.type == plex::mediaTypeClip;
                            if (!full || it.duration <= 0) {
                                total = 0;
                                break;
                            }
                            total += it.duration;
                        }
                    }
                    this->updateMeta(r.TotalRecordCount, total);
                }
                this->recycler->setDataSource(new VideoDataSource(r.Items));
                if (hasFocusWithin(this)) brls::Application::giveFocus(this->recycler);
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            if (this->startIndex > 0) {
                brls::Application::notify(ex);
            } else {
                this->recycler->setError(ex);
            }
        },
        path, this->itemId, HTTP::encode_form(query));
}
