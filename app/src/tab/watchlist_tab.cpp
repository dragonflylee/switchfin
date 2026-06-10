/*
    pleNx — onglet sidebar « Watchlist » (voir watchlist_tab.hpp).
*/

#include "tab/watchlist_tab.hpp"
#include "tab/media_movie.hpp"
#include "tab/media_series.hpp"
#include "api/plex/watchlist.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_card.hpp"
#include "view/auto_tab_frame.hpp"
#include "utils/image.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

/// Affiche provider (URL absolue : tmdb, metadata-static.plex.tv — tailles
/// ORIGINALES, plusieurs Mo) proxifiée par le transcodeur photo du serveur
/// pour obtenir une vignette : /photo/:/transcode?url=<absolue>&width&height
/// (pattern plezy externalImageUrl, plex_client.dart:4043-4055).
static void loadProviderImage(brls::Image* view, const std::string& url, int width, int height) {
    if (url.empty()) return;
    auto& conf = AppConfig::instance();
    HTTP::Form form = {
        {"minSize", "1"},
        {"upscale", "1"},
        {"url", url},
        {"X-Plex-Token", conf.getToken()},
        {"width", std::to_string(width)},
        {"height", std::to_string(height)},
    };
    Image::with(view, conf.getUrl() + "/photo/:/transcode?" + HTTP::encode_form(form));
}

/// Panneau latéral tri/filtres (action Y) — même pattern que MediaFilter
/// (media_filter.cpp), mais options propres à la watchlist : seuls les tris
/// HONORÉS par discover.provider sont exposés (vérifiés en GET réel, voir
/// plex::fetchWatchlist) et le filtre Disponibilité est purement client.
class WatchlistFilter : public brls::Box {
public:
    WatchlistFilter() {
        this->inflateFromXMLRes("xml/view/watchlist_filter.xml");
        brls::Logger::debug("WatchlistFilter: create");

        this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [this](...) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [this]() { this->event.fire(); });
            return true;
        });

        this->cancel->registerClickAction([this](...) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [this]() { this->event.fire(); });
            return true;
        });
        this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

        this->sortBy->init("main/media/sort_by"_i18n,
            {
                "main/media/date_add"_i18n,
                "main/media/name"_i18n,
                "main/media/premiere_date"_i18n,
            },
            selectedSort, [](int selected) { selectedSort = selected; });

        this->sortOrder->init("main/media/order"_i18n,
            {
                "main/media/ascending"_i18n,
                "main/media/descending"_i18n,
            },
            selectedOrder, [](int selected) { selectedOrder = selected; });

        this->filterType->init("main/remote/type"_i18n,
            {
                "main/watchlist/all"_i18n,
                "main/person/movies"_i18n,
                "main/person/shows"_i18n,
            },
            selectedType, [](int selected) { selectedType = selected; });

        this->filterAvailability->init("main/watchlist/availability"_i18n,
            {
                "main/watchlist/all"_i18n,
                "main/watchlist/on_server"_i18n,
                "main/watchlist/not_on_server"_i18n,
            },
            selectedAvailability, [](int selected) { selectedAvailability = selected; });
    }

    ~WatchlistFilter() override { brls::Logger::debug("WatchlistFilter: delete"); }

    bool isTranslucent() override { return true; }

    brls::VoidEvent* getEvent() { return &this->event; }

    /// État (session) partagé avec WatchlistTab::doRequest
    inline static int selectedSort = 0;   // index dans sortList
    inline static int selectedOrder = 1;  // 0 croissant, 1 décroissant
    inline static int selectedType = 0;   // 0 tous, 1 films, 2 séries
    inline static int selectedAvailability = 0;  // 0 tous, 1 sur le serveur, 2 absents

    /// Champs de tri provider honorés, alignés sur les libellés du sélecteur
    inline static std::string sortList[] = {
        "watchlistedAt",
        "titleSort",
        "originallyAvailableAt",
    };

private:
    BRLS_BIND(brls::Box, cancel, "filter/cancel");
    BRLS_BIND(brls::SelectorCell, sortBy, "watchlist/sort/by");
    BRLS_BIND(brls::SelectorCell, sortOrder, "watchlist/sort/order");
    BRLS_BIND(brls::SelectorCell, filterType, "watchlist/filter/type");
    BRLS_BIND(brls::SelectorCell, filterAvailability, "watchlist/filter/availability");

    brls::VoidEvent event;
};

/// Cartes affiches 2:3 : poster provider + titre + année.
/// Pas un VideoDataSource : le clic principal fait la correspondance
/// provider→serveur avant d'ouvrir la fiche, et le menu contextuel
/// X/long-press de VideoCardCell (qui caste vers VideoDataSource) reste
/// inerte ici — les ratingKey provider n'existent pas sur le serveur.
class WatchlistDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Item>;
    using GuidSet = std::shared_ptr<std::unordered_set<std::string>>;

    WatchlistDataSource(const MediaList& r, GuidSet guids) : list(std::move(r)), guids(std::move(guids)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.ratingKey);
        cell->labelTitle->setText(item.title);
        if (item.year > 0) {
            cell->labelExt->setText(std::to_string(item.year));
            cell->labelExt->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        }
        // cellule recyclée : purger l'affiche du média précédent
        cell->picture->clear();
        loadProviderImage(cell->picture, item.thumb, 325, 488);
        // ni badge « vu » ni progression : états du serveur, pas du provider
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        // titre absent du serveur (cache des guid) : affiche et textes
        // atténués — alpha TOUJOURS posé (1.0 au rebind d'une cellule
        // recyclée qui affichait un absent). Pas de set (nullptr) = présence
        // inconnue : pas de grisage.
        bool present = !this->guids || this->guids->count(item.guid) > 0;
        cell->picture->setAlpha(present ? 1.0f : 0.4f);
        cell->labelTitle->setAlpha(present ? 1.0f : 0.5f);
        cell->labelExt->setAlpha(present ? 1.0f : 0.5f);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        // correspondance provider → serveur par guid (plex::matchInLibrary) ;
        // les fiches MediaMovie/MediaSeries exigent un ratingKey SERVEUR
        plex::matchInLibrary(
            item.guid,
            [recycler, item](const plex::Container<plex::Item>& r) {
                if (r.Items.empty()) {
                    brls::Application::notify("main/watchlist/not_in_library"_i18n);
                    return;
                }
                auto& found = r.Items.front();
                if (found.type == plex::mediaTypeShow) {
                    ui::presentDetail(recycler, new MediaSeries(found));
                } else {
                    ui::presentDetail(recycler, new MediaMovie(found));
                }
            },
            [](const std::string& ex) { brls::Application::notify(ex); });
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
    GuidSet guids;
};

WatchlistTab::WatchlistTab() {
    brls::Logger::debug("WatchlistTab: create");
    this->inflateFromXMLRes("xml/tabs/watchlist.xml");

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });
}

void WatchlistTab::onCreate() {
    auto actionRefresh = [this](...) {
        this->refresh(true);
        return true;
    };
    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    // panneau tri/filtres (même hint Y « Trié » que les bibliothèques) ;
    // au retour, rechargement UNIQUEMENT si un réglage a changé — le cache
    // des guid n'est pas concerné par un changement de tri/filtre
    this->recycler->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        auto before = std::make_tuple(WatchlistFilter::selectedSort, WatchlistFilter::selectedOrder,
            WatchlistFilter::selectedType, WatchlistFilter::selectedAvailability);
        WatchlistFilter* filter = new WatchlistFilter();
        filter->getEvent()->subscribe([this, before]() {
            auto after = std::make_tuple(WatchlistFilter::selectedSort, WatchlistFilter::selectedOrder,
                WatchlistFilter::selectedType, WatchlistFilter::selectedAvailability);
            if (after != before) this->refresh(false);
        });
        brls::Application::pushActivity(new brls::Activity(filter));
        return true;
    });

    this->refresh(true);
}

brls::View* WatchlistTab::getDefaultFocus() { return this->recycler; }

brls::View* WatchlistTab::create() { return new WatchlistTab(); }

void WatchlistTab::refresh(bool reloadGuids) {
    this->startIndex = 0;
    this->loaded = false;
    this->recycler->showSkeleton();
    // cache des guid à (re)charger : chargement initial/rafraîchissement, ou
    // filtre Disponibilité actif alors qu'un chargement précédent a échoué
    if (reloadGuids || (!this->libraryGuids && WatchlistFilter::selectedAvailability != 0)) {
        ASYNC_RETAIN
        plex::fetchLibraryGuids(
            [ASYNC_TOKEN](std::shared_ptr<std::unordered_set<std::string>> guids) {
                ASYNC_RELEASE
                this->libraryGuids = guids;
                this->doRequest();
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                // dégradation : pas de grisage ni de filtre Disponibilité,
                // mais la watchlist reste consultable
                brls::Logger::warning("WatchlistTab: fetchLibraryGuids {}", ex);
                this->libraryGuids = nullptr;
                this->doRequest();
            });
    } else {
        this->doRequest();
    }
}

void WatchlistTab::doRequest() {
    // tri provider : champ honoré + suffixe :asc|:desc (vérifiés, voir
    // plex::fetchWatchlist) ; filtre Type provider via type=1|2
    std::string sort = WatchlistFilter::sortList[WatchlistFilter::selectedSort];
    sort += WatchlistFilter::selectedOrder ? ":desc" : ":asc";
    int type = WatchlistFilter::selectedType == 1   ? plex::typeMovie
               : WatchlistFilter::selectedType == 2 ? plex::typeShow
                                                    : 0;

    ASYNC_RETAIN
    // GET discover.provider/library/sections/watchlist/all (token COMPTE)
    plex::fetchWatchlist(
        this->startIndex, this->pageSize, sort, type,
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            bool more = !r.Items.empty() && (long)this->startIndex < r.TotalRecordCount;

            // filtre Disponibilité : CLIENT (le provider ignore tout du
            // serveur), appuyé sur le cache des guid ; sans cache (échec de
            // chargement), tout passe — présence inconnue
            std::vector<plex::Item> items;
            int avail = WatchlistFilter::selectedAvailability;
            if (avail == 0 || !this->libraryGuids) {
                items = r.Items;
            } else {
                for (auto& item : r.Items) {
                    bool present = this->libraryGuids->count(item.guid) > 0;
                    if (present == (avail == 1)) items.push_back(item);
                }
            }

            if (!this->loaded) {
                if (!items.empty()) {
                    this->loaded = true;
                    this->recycler->setDataSource(new WatchlistDataSource(items, this->libraryGuids));
                } else if (more) {
                    // page entièrement filtrée : enchaîner tant qu'il en reste
                    this->doRequest();
                } else if (r.TotalRecordCount == 0 && avail == 0 && WatchlistFilter::selectedType == 0) {
                    this->recycler->setEmpty(
                        "main/watchlist/empty_title"_i18n, "main/watchlist/empty_sub"_i18n, "icon/ico-bookmark.svg");
                } else {
                    // vide à cause des filtres : état vide générique
                    this->recycler->setEmpty();
                }
            } else if (!items.empty()) {
                auto dataSrc = dynamic_cast<WatchlistDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(items);
                this->recycler->notifyDataChanged();
            } else if (more) {
                this->doRequest();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            if (this->loaded) {
                brls::Application::notify(ex);
            } else {
                this->recycler->setError(ex);
            }
        });
}
