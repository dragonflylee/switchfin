/*
    Copyright 2023 dragonflylee
*/

#include "tab/live_tv.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/auto_tab_frame.hpp"
#include "activity/player_view.hpp"
#include "utils/keybind.hpp"
#include <fmt/format.h>

using namespace brls::literals;  // for _i18n

class ProgramTab : public RecyclingGrid {
public:
    ProgramTab() {
        this->setGrow(1.f);
        this->registerCell("Cell", VideoCardCell::create);
        this->estimatedRowHeight = 100;
        this->spanCount = brls::getStyle().getMetric("app/grid/5");
#ifdef PS5_NATIVE_GPU
        this->onNextPage([this]() { this->doRequest(); });
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
    ~ProgramTab() override {
        if (requestCancelled) requestCancelled->store(true);
    }

    void doRequest(bool refresh = false) {
        if (refresh) {
            if (requestCancelled) requestCancelled->store(true);
            ++requestGeneration;
            requestInFlight = false;
            start = 0;
            hasMore = true;
        }
        if (requestInFlight || !hasMore) return;
        const auto offset = start;
        const auto generation = ++requestGeneration;
        // Retire any prior error dialog's retry capability as well as workers.
        if (requestCancelled) requestCancelled->store(true);
        requestCancelled = std::make_shared<std::atomic_bool>(false);
        auto context = jellyfin::RequestContext::capture();
        context.ownerCancel = requestCancelled;
#else
    void doRequest() {
#endif
        std::string query = HTTP::encode_form({
#ifdef PS5_NATIVE_GPU
            {"userId", context.user},
#else
            {"userId", AppConfig::instance().getUserId()},
#endif
            {"limit", std::to_string(this->pageSize)},
#ifdef PS5_NATIVE_GPU
            {"startIndex", std::to_string(offset)},
#else
            {"startIndex", std::to_string(this->start)},
#endif
            {"fields", "ChannelInfo"},
            {"enableImageTypes", "Primary"},
            {"isAiring", "true"},
        });
#ifdef PS5_NATIVE_GPU
        const auto path = fmt::format(fmt::runtime(jellyfin::apiProgramRecommend), query);
        auto* expectedSource = this->getDataSource();
        requestInFlight = true;
#else

#endif
        ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
        jellyfin::request<jellyfin::Result<jellyfin::ProgramInfo>>(context,
            [path](const jellyfin::RequestContext& request) {
                return jellyfin::fetchJSON<jellyfin::Result<jellyfin::ProgramInfo>>(request, path);
            },
            [ASYNC_TOKEN, generation, offset, expectedSource](const jellyfin::Result<jellyfin::ProgramInfo>& r) {
#else
        jellyfin::getJSON<jellyfin::Result<jellyfin::ProgramInfo>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::ProgramInfo>& r) {
#endif
                ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
                if (generation != requestGeneration) return;
                requestInFlight = false;
                auto* dataSrc = dynamic_cast<ProgramDataSource*>(this->getDataSource());
                if (offset && (!dataSrc || dataSrc != expectedSource)) {
                    start = 0;
                    hasMore = true;
                    return;
                }
                start = offset + r.Items.size();
                hasMore = !r.Items.empty() && (r.TotalRecordCount <= 0
                    || start < static_cast<size_t>(r.TotalRecordCount));
                if (offset == 0 && r.Items.empty()) {
                    this->setEmpty();
                } else if (offset == 0) {
#else
                this->start = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->clearData();
                } else if (r.StartIndex == 0) {
#endif
                    this->setDataSource(new ProgramDataSource(r.Items));
#ifdef PS5_NATIVE_GPU
                } else if (!r.Items.empty()) {
#else
                } else if (r.Items.size() > 0) {
                    auto dataSrc = dynamic_cast<ProgramDataSource*>(this->getDataSource());
#endif
                    dataSrc->appendData(r.Items);
                    this->notifyDataChanged();
                }
            },
#ifdef PS5_NATIVE_GPU
            [ASYNC_TOKEN, generation, offset, context](const std::string& ex) {
#else
            [ASYNC_TOKEN](const std::string& ex) {
#endif
                ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
                if (generation != requestGeneration) return;
                requestInFlight = false;
                if (context.cancelled()) return;
                if (offset == 0 && !dynamic_cast<ProgramDataSource*>(this->getDataSource()))
                    this->setError(ex);
                // The recycler latches a next-page request until data changes;
                // a short failed page may leave no way to scroll away and back.
                // Offer explicit retry of this offset without a per-frame loop.
                auto current = [context]() { return !context.cancelled(); };
                auto* dialog = new brls::Dialog(ex);
                dialog->addButton("hints/retry"_i18n, [this, current]() {
                    if (!current()) return;
                    brls::sync([this, current]() { if (current()) this->doRequest(); });
                });
                dialog->addButton("hints/cancel"_i18n, []() {});
                dialog->open();
            });
#else
                this->setError(ex);
            },
            jellyfin::apiProgramRecommend, query);
#endif
    }

private:
    size_t start = 0;
    size_t pageSize = 48;
#ifdef PS5_NATIVE_GPU
    size_t requestGeneration = 0;
    std::shared_ptr<std::atomic_bool> requestCancelled;
    bool requestInFlight = false;
    bool hasMore = true;
#endif
};

class LiveDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Channel>;

    explicit LiveDataSource(const MediaList& r) : list(std::move(r)) {
        brls::Logger::debug("LiveDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        MediaCardCell* cell = dynamic_cast<MediaCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
#ifdef PS5_NATIVE_GPU
        cell->setId(item.Id);
#endif
        cell->labelTitle->setText(item.Name);
        cell->labelExt->setText(item.CurrentProgram.Name);
        cell->picture->setScalingType(brls::ImageScalingType::FIT);

#ifdef PS5_NATIVE_GPU
        loadArtwork(cell, item);
        return cell;
    }

    static void loadArtwork(MediaCardCell* cell, const MediaList::value_type& item) {
#endif
        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "300"}}));
        }
#ifdef PS5_NATIVE_GPU
    }

    void retryArtwork(RecyclingGridItem* existing, size_t index) override {
        auto* cell = dynamic_cast<MediaCardCell*>(existing);
        if (cell && index < list.size() && cell->matchesArtworkId(list[index].Id))
            loadArtwork(cell, list[index]);
#else
        return cell;
#endif
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        PlayerView* view = new PlayerView(item);
        view->setTitie(item.Name);
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

LiveTV::LiveTV(const std::string& itemId) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/collection.xml");
    brls::Logger::debug("LiveTV: create {}", itemId);

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    });

    this->registerAction(KeyBind::getRefresh(), [this](...) {
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    });

    this->recycler->spanCount = brls::getStyle().getMetric("app/grid/5");
    this->recycler->estimatedRowHeight = 200;
    this->recycler->registerCell("Cell", MediaCardCell::create);

    // add genres tab
    auto* item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setFontSize(18);
    item->setLabel("main/tabs/program"_i18n);
    this->tabFrame->addTab(item, []() { return new ProgramTab(); });
    this->tabFrame->registerTabAction(this);

    this->doRequest();
}

brls::View* LiveTV::getDefaultFocus() { return this->recycler; }

#ifdef PS5_NATIVE_GPU
LiveTV::~LiveTV() {
    if (requestCancelled) requestCancelled->store(true);
}

#endif
void LiveTV::doRequest() {
#ifdef PS5_NATIVE_GPU
    if (requestCancelled) requestCancelled->store(true);
    const auto generation = ++requestGeneration;
    requestCancelled = std::make_shared<std::atomic_bool>(false);
    auto context = jellyfin::RequestContext::capture();
    context.ownerCancel = requestCancelled;
    HTTP::Form query = {{"userId", context.user}};
    const auto path = fmt::format(fmt::runtime(jellyfin::apiLiveChannels), HTTP::encode_form(query));
#else
    HTTP::Form query = {{"userId", AppConfig::instance().getUserId()}};
#endif

    ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
    jellyfin::request<jellyfin::Result<jellyfin::Channel>>(context,
        [path](const jellyfin::RequestContext& request) {
            return jellyfin::fetchJSON<jellyfin::Result<jellyfin::Channel>>(request, path);
        },
        [ASYNC_TOKEN, generation](const jellyfin::Result<jellyfin::Channel>& r) {
#else
    jellyfin::getJSON<jellyfin::Result<jellyfin::Channel>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Channel>& r) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration) return;
#endif
            if (r.Items.empty())
                this->recycler->setEmpty();
            else
                this->recycler->setDataSource(new LiveDataSource(r.Items));
        },
#ifdef PS5_NATIVE_GPU
        [ASYNC_TOKEN, generation, context](const std::string& ex) {
#else
        [ASYNC_TOKEN](const std::string& ex) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration || context.cancelled()) return;
#endif
            this->recycler->setError(ex);
#ifdef PS5_NATIVE_GPU
        });
}
#else
        },
        jellyfin::apiLiveChannels, HTTP::encode_form(query));
}
#endif
