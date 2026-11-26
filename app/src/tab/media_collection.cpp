/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/svg_image.hpp"
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
bool MediaCollection::prefsLoaded = false;

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
        cell->labelTitle->setText(item.Name);
        cell->labelExt->setVisibility(brls::Visibility::GONE);
        cell->labelRating->setVisibility(brls::Visibility::GONE);
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        cell->badgeFavorite->setVisibility(brls::Visibility::GONE);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, 300, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{ "tag", it->second }}));
        }
        return cell;
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
                if (r.Items.empty()) {
                    this->setEmpty();
                    return;
                }
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
        this->doRequest();
    }

    void doRequest() {
        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"parentId", this->itemId},
            {"limit", std::to_string(this->pageSize)},
            {"startIndex", std::to_string(this->start)},
            {"enableImageTypes", "Primary"},
            {"recursive", "true"},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
                ASYNC_RELEASE
                this->start = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->clearData();
                } else if (r.StartIndex == 0) {
                    this->setDataSource(new VideoDataSource(r.Items, this->itemId));
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
            jellyfin::apiArtists, query);
    }

private:
    std::string itemId;
    size_t start = 0;
    size_t pageSize = 60;
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

    auto doRefresh = [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    };

    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, doRefresh);
    this->registerAction(KeyBind::getRefresh(), doRefresh);

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    if (this->itemType == jellyfin::mediaTypePlaylist) {
        this->doRequest();
    } else if (AppConfig::SYNC) {
        if (!MediaCollection::prefsLoaded) {
            this->doPreferences();
        } else {
            this->loadFilter();
            this->doRequest();
        }
    } else {
        this->openFilter(false);
        this->doRequest();
    }
}

brls::View* MediaCollection::getDefaultFocus() { return this->recycler; }

void MediaCollection::doPreferences() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::DisplayPreferences>(
        [ASYNC_TOKEN](const jellyfin::DisplayPreferences& r) {
            ASYNC_RELEASE
            this->prefId = std::move(r.Id);
            for (const auto& item : r.CustomPrefs.items()) {
                if (item.value().is_string()) {
                    MediaCollection::customPrefs[item.key()] = item.value().get<std::string>();
                }
            }
            MediaCollection::prefsLoaded = true;
            this->loadFilter();
            this->doRequest();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaCollection doPreferences: {}", ex);
            MediaCollection::prefsLoaded = true;
            this->openFilter(false);
            this->doRequest();
        },
        jellyfin::apiUserSetting, AppConfig::instance().getUserId());
}

struct DisplaySort {
    std::string SortBy;
    std::string SortOrder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DisplaySort, SortBy, SortOrder);

void MediaCollection::openFilter(bool save) {
    MediaFilter* dialog = new MediaFilter(this->filter);
    dialog->getEvent()->subscribe([this, save, dialog]() {
        this->filter = dialog->getState();
        this->startIndex = 0;
        this->recycler->showSkeleton();
        if (save) {
            this->saveFilter();
            return;
        }
        this->doRequest();
    });
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaCollection::loadFilter() {
    this->recycler->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        this->openFilter(true);
        return true;
    });

    auto it = MediaCollection::customPrefs.find(this->prefKey);
    if (it == MediaCollection::customPrefs.end()) return;

    try {
        DisplaySort s = nlohmann::json::parse(it->second);
        this->filter.order = s.SortOrder == "Ascending" ? 0 : 1;
        for (size_t i = 0; i < std::size(FilterState::sortList); i++) {
            if (FilterState::sortList[i] == s.SortBy) {
                this->filter.sort = (int)i;
            }
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("MediaCollection parse sort: {}", ex.what());
        brls::Application::notify(fmt::format("sort: {}", ex.what()));
    }
}

void MediaCollection::saveFilter() {
    nlohmann::json value = {
        {"SortBy", this->filter.getSortBy()},
        {"SortOrder", this->filter.getSortOrder()},
    };
    MediaCollection::customPrefs[this->prefKey] = value.dump();

    jellyfin::postJSON(
        {
            {"Id", this->prefId},
            {"CustomPrefs", MediaCollection::customPrefs},
            {"Client", "emby"},
        },
        [this](...) { this->doRequest(); }, nullptr, jellyfin::apiUserSetting, AppConfig::instance().getUserId());
}

void MediaCollection::doRequest() {
    const unsigned int id = ++this->requestId;

    std::vector<std::string> filters;
    if (this->filter.played) filters.push_back("IsPlayed");
    if (this->filter.unplayed) filters.push_back("IsUnplayed");
    if (this->filter.favorite) filters.push_back("IsFavorite");

    HTTP::Form query = {
        {"parentId", this->itemId},
        {"sortBy", this->filter.getSortBy()},
        {"sortOrder", this->filter.getSortOrder()},
        {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
        {"enableImageTypes", "Primary"},
        {"filters", fmt::format("{}", fmt::join(filters, ","))},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->startIndex)},
    };
    if (this->genresId.size() > 0) {
        query["genreIds"] = this->genresId;
    }
    if (this->itemType.size() > 0) {
        query["includeItemTypes"] = this->itemType;
        query["recursive"] = "true";
    }

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN, id](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (id != this->requestId) return;
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->recycler->setEmpty();
            } else if (r.StartIndex == 0) {
                this->recycler->setDataSource(new VideoDataSource(r.Items));
                brls::Application::giveFocus(this->recycler);
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                if (dataSrc) {
                    dataSrc->appendData(r.Items);
                    this->recycler->notifyDataChanged();
                }
            }
        },
        [ASYNC_TOKEN, id](const std::string& ex) {
            ASYNC_RELEASE
            if (id != this->requestId) return;
            if (this->startIndex > 0) {
                brls::Logger::warning("MediaCollection doRequest next: {}", ex);
                brls::Application::notify(fmt::format("paging: {}", ex));
            } else {
                this->recycler->setError(ex);
            }
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), HTTP::encode_form(query));
}