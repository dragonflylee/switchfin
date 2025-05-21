/*
    Copyright 2023 dragonflylee
*/

#include "tab/media_collection.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/media_filter.hpp"
#include "view/auto_tab_frame.hpp"
#include "view/h_recycling.hpp"
#include "tab/suggest_show.hpp"
#include "tab/suggest_movie.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

class GenresDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Genres>;

    explicit GenresDataSource(const MediaList& r, const std::string& itemId) : list(std::move(r)), itemId(itemId) {
        brls::Logger::debug("GenresDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->labelTitle->setText(item.Name);
        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "400"}}));
        }
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        recycler->present(new MediaCollection(this->itemId, jellyfin::mediaTypeGenre, item.Id));
    }

    void clearData() override { this->list.clear(); }

private:
    MediaList list;
    std::string itemId;
};

class GenresTab : public RecyclingGrid {
public:
    GenresTab(const std::string& itemId, const std::string& itemType) {
        this->setGrow(1.f);
        this->registerCell("Cell", VideoCardCell::create);
        this->spanCount = 6;

        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"parentId", itemId},
            {"includeItemTypes", itemType},
            {"enableImageTypes", "Primary"},
            {"recursive", "true"},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Genres>>(
            [ASYNC_TOKEN, itemId](const jellyfin::Result<jellyfin::Genres>& r) {
                ASYNC_RELEASE
                this->setDataSource(new GenresDataSource(r.Items, itemId));
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            jellyfin::apiGenres, query);
    }
};

MediaCollection::MediaCollection(const std::string& itemId, const std::string& itemType, const std::string& genresId)
    : itemId(itemId), genresId(genresId), itemType(itemType), startIndex(0) {
    brls::Logger::debug("MediaCollection: create {} type {}", itemId, itemType);
    if (itemType == jellyfin::mediaTypeMovie || itemType == jellyfin::mediaTypeSeries) {
        this->inflateFromXMLRes("xml/tabs/collection.xml");
        // add genres tab
        auto* item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/genres"_i18n);
        this->tabFrame->addTab(item, [this]() { return new GenresTab(this->itemId, this->itemType); });

        this->registerAction(
            "main/play/next"_i18n, brls::BUTTON_LB,
            [this](brls::View* view) {
                tabFrame->focus2LastTab();
                return true;
            },
            true);

        this->registerAction(
            "main/play/pref"_i18n, brls::BUTTON_RB,
            [this](brls::View* view) {
                tabFrame->focus2NextTab();
                return true;
            },
            true);

        // add suggest tab
        item = new AutoSidebarItem();
        item->setTabStyle(AutoTabBarStyle::ACCENT);
        item->setFontSize(18);
        item->setLabel("main/tabs/suggest"_i18n);
        if (itemType == jellyfin::mediaTypeSeries) {
            this->tabFrame->addTab(item, [this]() { return new SuggestShow(this->itemId); });
        } else if (itemType == jellyfin::mediaTypeMovie) {
            this->tabFrame->addTab(item, [this]() { return new SuggestMovie(this->itemId); });
        }
    } else {
        this->inflateFromXMLRes("xml/tabs/media.xml");
    }

    this->pageSize = this->recycler->spanCount * 3;
    if (itemType == jellyfin::mediaTypeMusicAlbum) {
        this->recycler->estimatedRowHeight = 240;
    }

    std::string serverUrl = AppConfig::instance().getUrl();
    this->prefKey = fmt::format("{}/web/index.html{}", serverUrl, itemType);
    std::transform(this->prefKey.begin(), this->prefKey.end(), this->prefKey.begin(),
        [](unsigned char c) { return std::tolower(c); });

    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    });

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    if (this->itemType == jellyfin::mediaTypePlaylist) {
        this->doRequest();
    } else if (AppConfig::SYNC) {
        this->doPreferences();
    } else {
        this->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
            MediaFilter* filter = new MediaFilter();
            filter->getEvent()->subscribe([this]() {
                this->startIndex = 0;
                this->doRequest();
            });
            brls::Application::pushActivity(new brls::Activity(filter));
            return true;
        });

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
                    this->customPrefs[item.key()] = item.value().get<std::string>();
                }
            }
            this->loadFilter();
            this->doRequest();
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setError(ex);
        },
        jellyfin::apiUserSetting, AppConfig::instance().getUserId());

    this->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        MediaFilter* filter = new MediaFilter();
        filter->getEvent()->subscribe([this]() {
            this->startIndex = 0;
            this->doRequest();
            this->saveFilter();
        });
        brls::Application::pushActivity(new brls::Activity(filter));
        return true;
    });
}

struct DisplaySort {
    std::string SortBy;
    std::string SortOrder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DisplaySort, SortBy, SortOrder);

void MediaCollection::loadFilter() {
    auto it = this->customPrefs.find(this->prefKey);
    if (it == this->customPrefs.end()) return;

    try {
        DisplaySort s = nlohmann::json::parse(it->second);
        MediaFilter::selectedOrder = s.SortOrder == "Ascending" ? 0 : 1;
        for (size_t i = 0; i < std::size(MediaFilter::sortList); i++) {
            if (MediaFilter::sortList[i] == s.SortBy) {
                MediaFilter::selectedSort = i;
            }
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("MediaCollection loadFilter: {}", ex.what());
    }
}

void MediaCollection::saveFilter() {
    nlohmann::json value = {
        {"SortBy", MediaFilter::sortList[MediaFilter::selectedSort]},
        {"SortOrder", MediaFilter::selectedOrder ? "Descending" : "Ascending"},
    };
    this->customPrefs[this->prefKey] = value.dump();

    jellyfin::postJSON(
        {
            {"Id", this->prefId},
            {"CustomPrefs", this->customPrefs},
            {"Client", "emby"},
        },
        [](...) {}, [](const std::string& ex) { brls::Logger::warning("usersettings upload: {}", ex); },
        jellyfin::apiUserSetting, AppConfig::instance().getUserId());
}

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
        query["recursive"] = "true";
    } else if (this->itemType.size() > 0) {
        query["includeItemTypes"] = this->itemType;
        query["recursive"] = "true";
    }

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
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
        jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), HTTP::encode_form(query));
}