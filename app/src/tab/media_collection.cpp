/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/media_filter.hpp"
#include "view/auto_tab_frame.hpp"
#include "tab/suggest_show.hpp"
#include "tab/suggest_movie.hpp"
#include "tab/song_list.hpp"
#include "utils/keybind.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

std::map<std::string, std::string> MediaCollection::customPrefs;

#ifdef PS5_NATIVE_GPU
static bool collectionInitialFocus(RecyclingGrid* grid, brls::View* initialFocus) {
    if (brls::Application::getCurrentFocus() != initialFocus) return false;
    const auto activities = brls::Application::getActivitiesStack();
    if (activities.empty() || grid->getParentActivity() != activities.back()) return false;
    auto* applet = grid->getAppletFrame();
    auto* content = applet ? applet->getContentView() : activities.back()->getContentView();
    bool currentContent = false;
    for (auto* view = static_cast<brls::View*>(grid); view; view = view->getParent()) {
        if (view->isHidden() || view->getVisibility() != brls::Visibility::VISIBLE) return false;
        if (view == content) currentContent = true;
    }
    return currentContent;
}

// Read live selection when applying a response, after intervening navigation.
// RecyclingGrid accepts a default index but does not preserve item identity.
static void replaceCollectionPage(RecyclingGrid* grid, const std::vector<jellyfin::Episode>& items,
    const std::string& parentId = "", brls::View* initialFocus = nullptr) {
    bool ownsFocus = false;
    for (auto* focus = brls::Application::getCurrentFocus(); focus; focus = focus->getParent())
        if (focus == grid) ownsFocus = true;
    if (!dynamic_cast<VideoDataSource*>(grid->getDataSource()) && collectionInitialFocus(grid, initialFocus))
        ownsFocus = true;
    auto* selection = ownsFocus ? brls::Application::getCurrentFocus() : grid->getDefaultFocus();
    size_t selected = grid->getDefaultCellFocus();
    for (auto* cell : grid->getGridItems()) {
        for (auto* focus = selection; focus; focus = focus->getParent())
            if (focus == cell) selected = cell->getIndex();
    }
    auto* source = grid->getDataSource();
    const auto key = source && selected < source->getItemCount() ?
        grid->getDataSource()->getItemKey(selected) : std::string();
    if (!key.empty()) {
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].Id == key) { selected = i; break; }
    }
    grid->setDefaultCellFocus(items.empty() ? 0 : std::min(selected, items.size() - 1));
    if (items.empty()) grid->setEmpty();
    else grid->setDataSource(new VideoDataSource(items, parentId));
    if (ownsFocus) {
        if (!items.empty()) brls::Application::giveFocus(grid);
        else {
            for (auto* parent = grid->getParent(); parent; parent = parent->getParent()) {
                auto* next = parent->getDefaultFocus();
                if (next && next != grid) { brls::Application::giveFocus(next); break; }
            }
        }
    }
}

#endif
class GenresDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Genres>;

    explicit GenresDataSource(const MediaList& r, const std::string& itemId, const std::string& itemType)
        : list(std::move(r)), itemId(itemId), itemType(itemType) {
        brls::Logger::debug("GenresDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
#ifdef PS5_NATIVE_GPU
        cell->setId(item.Id);
#endif
        cell->labelTitle->setText(item.Name);
        cell->labelExt->setVisibility(brls::Visibility::GONE);
#ifdef PS5_NATIVE_GPU
        loadArtwork(cell, item);
        return cell;
    }

    static void loadArtwork(VideoCardCell* cell, const MediaList::value_type& item) {
#endif
        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "300"}}));
        }
#ifdef PS5_NATIVE_GPU
    }

    void retryArtwork(RecyclingGridItem* existing, size_t index) override {
        auto* cell = dynamic_cast<VideoCardCell*>(existing);
        if (cell && index < list.size() && cell->matchesArtworkId(list[index].Id))
            loadArtwork(cell, list[index]);
#else
        return cell;
#endif
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        recycler->present(new MediaCollection(this->itemId, this->itemType, item.Id));
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
        this->estimatedRowHeight = brls::getStyle().getMetric("app/album/height");
        this->spanCount = brls::getStyle().getMetric("app/grid/6");

        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"parentId", itemId},
            {"includeItemTypes", itemType},
            {"enableImageTypes", "Primary"},
            {"recursive", "true"},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Genres>>(
            [ASYNC_TOKEN, itemId, itemType](const jellyfin::Result<jellyfin::Genres>& r) {
                ASYNC_RELEASE
                this->setDataSource(new GenresDataSource(r.Items, itemId, itemType));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            jellyfin::apiGenres, query);
    }
};

class ArtistsTab : public RecyclingGrid {
public:
    ArtistsTab(const std::string& itemId) : itemId(itemId) {
        this->setGrow(1.f);
        this->registerCell("Cell", VideoCardCell::create);
        this->estimatedRowHeight = brls::getStyle().getMetric("app/album/height");
        this->spanCount = brls::getStyle().getMetric("app/grid/6");

        this->onNextPage([this] { this->doRequest(); });
#ifdef PS5_NATIVE_GPU
        this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
            this->doRequest(true);
            return true;
        });
        this->registerAction(KeyBind::getRefresh(), [this](...) {
            this->doRequest(true);
            return true;
        });
#endif
        this->doRequest();
    }

#ifdef PS5_NATIVE_GPU
    ~ArtistsTab() override { if (requestCancelled) requestCancelled->store(true); }

    void doRequest(bool refresh = false) {
        const bool accountChanged = accountCancelled && accountCancelled->load();
        if (refresh || accountChanged) {
            if (requestCancelled) requestCancelled->store(true);
            ++requestGeneration;
            requestInFlight = false;
            hasMore = true;
            start = 0;
            accountCancelled.reset();
            if (accountChanged) replaceCollectionPage(this, {});
        }
        if (requestInFlight || !hasMore) return;
        const auto offset = start;
        auto* initialFocus = brls::Application::getCurrentFocus();
        const auto generation = ++requestGeneration;
        requestCancelled = std::make_shared<std::atomic_bool>(false);
        auto context = jellyfin::RequestContext::capture();
        context.ownerCancel = requestCancelled;
        accountCancelled = context.cancel;
#else
    void doRequest() {
#endif
        std::string query = HTTP::encode_form({
#ifdef PS5_NATIVE_GPU
            {"userId", context.user},
#else
            {"userId", AppConfig::instance().getUserId()},
#endif
            {"parentId", this->itemId},
            {"limit", std::to_string(this->pageSize)},
#ifdef PS5_NATIVE_GPU
            {"startIndex", std::to_string(offset)},
#else
            {"startIndex", std::to_string(this->start)},
#endif
            {"enableImageTypes", "Primary"},
            {"recursive", "true"},
        });

#ifdef PS5_NATIVE_GPU
        const auto path = fmt::format(fmt::runtime(jellyfin::apiArtists), query);
        requestInFlight = true;
#endif
        ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
        jellyfin::request<jellyfin::Result<jellyfin::Episode>>(context,
            [path](const jellyfin::RequestContext& request) {
                return jellyfin::fetchJSON<jellyfin::Result<jellyfin::Episode>>(request, path);
            },
            [ASYNC_TOKEN, generation, offset, initialFocus](const jellyfin::Result<jellyfin::Episode>& r) {
#else
        jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
#endif
                ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
                if (generation != requestGeneration) return;
                start = offset + r.Items.size();
                hasMore = !r.Items.empty() && (r.TotalRecordCount <= 0 || start < static_cast<size_t>(r.TotalRecordCount));
                if (offset == 0) replaceCollectionPage(this, r.Items, itemId, initialFocus);
                else if (!r.Items.empty()) {
                    if (auto* source = dynamic_cast<VideoDataSource*>(getDataSource())) {
                        source->appendData(r.Items);
                        notifyDataChanged();
                    } else { start = 0; hasMore = true; }
                }
                if (generation == requestGeneration) {
                    requestInFlight = false;
                    forceRequestNextPage();
#else
                this->start = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->clearData();
                } else if (r.StartIndex == 0) {
                    this->setDataSource(new VideoDataSource(r.Items, this->itemId));
                } else if (r.Items.size() > 0) {
                    auto dataSrc = dynamic_cast<VideoDataSource*>(this->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->notifyDataChanged();
#endif
                }
            },
#ifdef PS5_NATIVE_GPU
            [ASYNC_TOKEN, generation, context](const std::string& ex) {
#else
            [ASYNC_TOKEN](const std::string& ex) {
#endif
                ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
                if (generation != requestGeneration) return;
                requestInFlight = false;
                if (context.cancelled()) return;
                if (dynamic_cast<VideoDataSource*>(getDataSource())) brls::Application::notify(ex);
                else this->setError(ex);
            });
#else
                this->setError(ex);
            },
            jellyfin::apiArtists, query);
#endif
    }

private:
    std::string itemId;
    size_t start = 0;
    size_t pageSize = 60;
#ifdef PS5_NATIVE_GPU
    size_t requestGeneration = 0;
    bool requestInFlight = false, hasMore = true;
    std::shared_ptr<std::atomic_bool> requestCancelled, accountCancelled;
#endif
};

MediaCollection::MediaCollection(const std::string& itemId, const std::string& itemType, const std::string& genresId)
    : itemId(itemId), genresId(genresId), itemType(itemType), startIndex(0) {
    brls::Logger::debug("MediaCollection: create {} type {}", itemId, itemType);
    if (genresId.size() > 0) {
        this->inflateFromXMLRes("xml/tabs/media.xml");
    } else if (itemType == jellyfin::mediaTypeMovie || itemType == jellyfin::mediaTypeSeries ||
               itemType == jellyfin::mediaTypeMusicAlbum) {
        this->inflateFromXMLRes("xml/tabs/collection.xml");
        // add genres tab
        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/genres"_i18n);
        this->tabFrame->addTab(item, [this]() { return new GenresTab(this->itemId, this->itemType); });

        this->tabFrame->registerTabAction(this);

        // add suggest tab
        item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        if (itemType == jellyfin::mediaTypeSeries) {
            item->setLabel("main/tabs/suggest"_i18n);
            this->tabFrame->addTab(item, [this]() { return new SuggestShow(this->itemId); });
        } else if (itemType == jellyfin::mediaTypeMovie) {
            item->setLabel("main/tabs/suggest"_i18n);
            this->tabFrame->addTab(item, [this]() { return new SuggestMovie(this->itemId); });
        } else if (itemType == jellyfin::mediaTypeMusicAlbum) {
            item->setLabel("main/tabs/artists"_i18n);
            this->tabFrame->addTab(item, [this]() { return new ArtistsTab(this->itemId); });

            item = new AutoSidebarItem();
            item->setTabStyle(AutoTabBarStyle::ACCENT);
            item->setFontSize(18);
            item->setLabel("main/tabs/songs"_i18n);
            this->tabFrame->addTab(item, [this]() { return new SongList(this->itemId); });
        }
    } else {
        this->inflateFromXMLRes("xml/tabs/media.xml");
    }

    this->pageSize = this->recycler->spanCount * 3;
    if (itemType == jellyfin::mediaTypeMusicAlbum) {
        this->recycler->estimatedRowHeight = brls::getStyle().getMetric("app/album/height");
    } else if (itemType == jellyfin::mediaTypeBook) {
        this->recycler->estimatedRowHeight = brls::getStyle().getMetric("app/books/height");
    }

    std::string serverUrl = AppConfig::instance().getUrl();
    this->prefKey = fmt::format("{}/web/index.html{}", serverUrl, itemType);
    std::transform(this->prefKey.begin(), this->prefKey.end(), this->prefKey.begin(), ::tolower);

    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
#ifdef PS5_NATIVE_GPU
        this->doRequest(true);
#else
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
#endif
        return true;
    });

    this->registerAction(KeyBind::getRefresh(), [this](...) {
#ifdef PS5_NATIVE_GPU
        this->doRequest(true);
#else
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
#endif
        return true;
    });

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    if (this->itemType == jellyfin::mediaTypePlaylist) {
        this->doRequest();
    } else if (AppConfig::SYNC) {
        if (MediaCollection::customPrefs.empty()) {
            this->doPreferences();
        } else {
            this->loadFilter();
            this->doRequest();
        }
    } else {
        this->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
            MediaFilter* filter = new MediaFilter();
            filter->getEvent()->subscribe([this]() {
#ifdef PS5_NATIVE_GPU
                this->doRequest(true);
#else
                this->startIndex = 0;
                this->recycler->showSkeleton();
                this->doRequest();
#endif
            });
            brls::Application::pushActivity(new brls::Activity(filter));
            return true;
        });

        this->doRequest();
    }
}

brls::View* MediaCollection::getDefaultFocus() { return this->recycler; }

#ifdef PS5_NATIVE_GPU
MediaCollection::~MediaCollection() {
    if (requestCancelled) requestCancelled->store(true);
}

void MediaCollection::resetRequest() {
    if (requestCancelled) requestCancelled->store(true);
    ++requestGeneration;
    requestInFlight = false;
    hasMore = true;
    startIndex = 0;
    queryParameters.clear();
}

#endif
void MediaCollection::doPreferences() {
#ifdef PS5_NATIVE_GPU
    resetRequest();
    awaitingPreferences = true;
    const auto generation = requestGeneration;
    requestCancelled = std::make_shared<std::atomic_bool>(false);
    auto context = jellyfin::RequestContext::capture();
    context.ownerCancel = requestCancelled;
    accountCancelled = context.cancel;
    const auto path = fmt::format(fmt::runtime(jellyfin::apiUserSetting), context.user);
    requestInFlight = true;
#endif
    ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
    jellyfin::request<jellyfin::DisplayPreferences>(context,
        [path](const jellyfin::RequestContext& request) {
            return jellyfin::fetchJSON<jellyfin::DisplayPreferences>(request, path);
        },
        [ASYNC_TOKEN, generation](const jellyfin::DisplayPreferences& r) {
#else
    jellyfin::getJSON<jellyfin::DisplayPreferences>(
        [ASYNC_TOKEN](const jellyfin::DisplayPreferences& r) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration) return;
            requestInFlight = false;
            awaitingPreferences = false;
#endif
            this->prefId = std::move(r.Id);
            for (const auto& item : r.CustomPrefs.items()) {
                if (item.value().is_string()) {
                    MediaCollection::customPrefs[item.key()] = item.value().get<std::string>();
                }
            }
            this->loadFilter();
            this->doRequest();
        },
#ifdef PS5_NATIVE_GPU
        [ASYNC_TOKEN, generation, context](const std::string& ex) {
#else
        [ASYNC_TOKEN](const std::string& ex) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration) return;
            requestInFlight = false;
            if (context.cancelled()) return;
#endif
            this->recycler->setError(ex);
#ifdef PS5_NATIVE_GPU
        });
#else
        },
        jellyfin::apiUserSetting, AppConfig::instance().getUserId());
#endif
}

struct DisplaySort {
    std::string SortBy;
    std::string SortOrder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DisplaySort, SortBy, SortOrder);

void MediaCollection::loadFilter() {
    this->recycler->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        MediaFilter* filter = new MediaFilter();
        filter->getEvent()->subscribe([this]() {
#ifdef PS5_NATIVE_GPU
            this->doRequest(true);
#else
            this->startIndex = 0;
            this->recycler->showSkeleton();
            this->doRequest();
#endif
            this->saveFilter();
        });
        brls::Application::pushActivity(new brls::Activity(filter));
        return true;
    });

    auto it = MediaCollection::customPrefs.find(this->prefKey);
    if (it == MediaCollection::customPrefs.end()) return;

    try {
        DisplaySort s = nlohmann::json::parse(it->second);
        MediaFilter::selectedOrder = s.SortOrder == "Ascending" ? 0 : 1;
        for (size_t i = 0; i < std::size(MediaFilter::sortList); i++) {
            if (MediaFilter::sortList[i] == s.SortBy) {
                MediaFilter::selectedSort = i;
            }
        }
    } catch (const std::exception& ex) {
        brls::Application::notify(ex.what());
    }
}

void MediaCollection::saveFilter() {
    nlohmann::json value = {
        {"SortBy", MediaFilter::sortList[MediaFilter::selectedSort]},
        {"SortOrder", MediaFilter::selectedOrder ? "Descending" : "Ascending"},
    };
    MediaCollection::customPrefs[this->prefKey] = value.dump();

    jellyfin::postJSON(
        {
            {"Id", this->prefId},
            {"CustomPrefs", MediaCollection::customPrefs},
            {"Client", "emby"},
        },
        [](...) {}, nullptr, jellyfin::apiUserSetting, AppConfig::instance().getUserId());
}

#ifdef PS5_NATIVE_GPU
void MediaCollection::doRequest(bool refresh) {
    if (accountCancelled && accountCancelled->load()) {
        refresh = true;
        accountCancelled.reset();
        replaceCollectionPage(this->recycler, {});
#else
void MediaCollection::doRequest() {
    std::vector<std::string> filters;
    if (MediaFilter::selectedPlayed) filters.push_back("IsPlayed");
    if (MediaFilter::selectedUnplayed) filters.push_back("IsUnplayed");

    HTTP::Form query = {
        {"parentId", this->itemId},
        {"sortBy", MediaFilter::sortList[MediaFilter::selectedSort]},
        {"sortOrder", MediaFilter::selectedOrder ? "Descending" : "Ascending"},
        {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
        {"enableImageTypes", "Primary"},
        {"filters", fmt::format("{}", fmt::join(filters, ","))},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->startIndex)},
    };
    if (this->genresId.size() > 0) {
        query["genreIds"] = this->genresId;
#endif
    }
#ifdef PS5_NATIVE_GPU
    // Initial refresh must still install the saved ordering and sort action.
    // Retry failed preferences here too; only a current success finishes this phase.
    if (awaitingPreferences) {
        if (refresh || !requestInFlight) doPreferences();
        return;
#else
    if (this->itemType.size() > 0) {
        query["includeItemTypes"] = this->itemType;
        query["recursive"] = "true";
#endif
    }
#ifdef PS5_NATIVE_GPU
    if (refresh) resetRequest();
    if (requestInFlight || !hasMore || !pageSize) return;
    if (queryParameters.empty()) {
        std::vector<std::string> filters;
        if (MediaFilter::selectedPlayed) filters.push_back("IsPlayed");
        if (MediaFilter::selectedUnplayed) filters.push_back("IsUnplayed");
        // Freeze sort/filter settings for the entire pagination generation.
        // Another open collection can change the global filter controls.
        queryParameters = {
            {"parentId", this->itemId},
            {"sortBy", MediaFilter::sortList[MediaFilter::selectedSort]},
            {"sortOrder", MediaFilter::selectedOrder ? "Descending" : "Ascending"},
            {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
            {"enableImageTypes", "Primary"},
            {"filters", fmt::format("{}", fmt::join(filters, ","))},
        };
        if (!genresId.empty()) queryParameters["genreIds"] = genresId;
        if (!itemType.empty()) {
            queryParameters["includeItemTypes"] = itemType;
            queryParameters["recursive"] = "true";
        }
    }
    HTTP::Form query(queryParameters.begin(), queryParameters.end());
    const auto offset = startIndex;
    auto* initialFocus = brls::Application::getCurrentFocus();
    query["limit"] = std::to_string(pageSize);
    query["startIndex"] = std::to_string(offset);
    const auto generation = ++requestGeneration;
    requestCancelled = std::make_shared<std::atomic_bool>(false);
    auto context = jellyfin::RequestContext::capture();
    context.ownerCancel = requestCancelled;
    accountCancelled = context.cancel;
    const auto path = fmt::format(fmt::runtime(jellyfin::apiUserLibrary), context.user, HTTP::encode_form(query));
    requestInFlight = true;
#else

#endif
    ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
    jellyfin::request<jellyfin::Result<jellyfin::Episode>>(context,
        [path](const jellyfin::RequestContext& request) {
            return jellyfin::fetchJSON<jellyfin::Result<jellyfin::Episode>>(request, path);
        },
        [ASYNC_TOKEN, generation, offset, initialFocus](const jellyfin::Result<jellyfin::Episode>& r) {
#else
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration) return;
            this->startIndex = offset + r.Items.size();
            hasMore = !r.Items.empty() && (r.TotalRecordCount <= 0
                || startIndex < static_cast<size_t>(r.TotalRecordCount));
            if (offset == 0) replaceCollectionPage(this->recycler, r.Items, "", initialFocus);
            else if (!r.Items.empty()) {
                if (auto* source = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource())) {
                    source->appendData(r.Items);
                    this->recycler->notifyDataChanged();
                } else { resetRequest(); }
            }
            // Grid replacement can trigger paging while rebuilding cells. Keep
            // single-flight set until that finishes, then allow the next draw
            // to request the following page instead of losing its paging latch.
            if (generation == requestGeneration) {
                requestInFlight = false;
                this->recycler->forceRequestNextPage();
#else
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->recycler->setEmpty();
            } else if (r.StartIndex == 0) {
                this->recycler->setDataSource(new VideoDataSource(r.Items));
                brls::Application::giveFocus(this->recycler);
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
#endif
            }
        },
#ifdef PS5_NATIVE_GPU
        [ASYNC_TOKEN, generation, context](const std::string& ex) {
#else
        [ASYNC_TOKEN](const std::string& ex) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration) return;
            requestInFlight = false;
            if (context.cancelled()) return;
            if (dynamic_cast<VideoDataSource*>(this->recycler->getDataSource()))
#else
            if (this->startIndex > 0) {
#endif
                brls::Application::notify(ex);
#ifdef PS5_NATIVE_GPU
            else this->recycler->setError(ex);
        });
}
#else
            } else {
                this->recycler->setError(ex);
            }
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), HTTP::encode_form(query));
}
#endif
