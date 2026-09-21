#include "view/recyling_video.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "api/jellyfin.hpp"
#ifdef PS5_NATIVE_GPU
#include "api/jellyfin_series.hpp"
#endif

const std::string recylingVideoContentXML = R"xml(
    <brls:Box
        width="auto"
        height="auto"
        axis="column">

        <brls:Header
            height="40"
            id="recycler/title" />

        <HRecyclerFrame
            id="recycler/videos" />

    </brls:Box>
)xml";

brls::View* RecylingVideo::create() { return new RecylingVideo(); }

RecylingVideo::RecylingVideo() {
    this->inflateFromXMLString(recylingVideoContentXML);

    this->registerStringXMLAttribute("title", [this](std::string value) { this->setTitle(value); });

    this->registerFloatXMLAttribute("frameHeight", [this](float value) { this->setFrameHeight(value); });

    this->registerFloatXMLAttribute("itemWidth", [this](float value) { this->setItemWidth(value); });

    this->registerFloatXMLAttribute("itemSpace", [this](float value) {
        this->recycler->estimatedRowSpace = value;
        this->recycler->reloadData();
    });

    this->registerFloatXMLAttribute("pageSize", [this](float value) { this->setPageSize(value); });

    this->registerAutoXMLAttribute(
        "nextPage", [this]() { this->recycler->onNextPage([this]() { this->doRequest(); }); });

    this->recycler->registerCell("Cell", VideoCardCell::create);
}

#ifdef PS5_NATIVE_GPU
RecylingVideo::~RecylingVideo() {
    if (requestCancelled) requestCancelled->store(true);
}

void RecylingVideo::reset() {
    if (requestCancelled) requestCancelled->store(true);
    ++requestGeneration;
    requestInFlight = false;
    hasMore = true;
    start = 0;
    // Keep current cards until replacement arrives, preserving the live selection.
}
#else
RecylingVideo::~RecylingVideo() {}
#endif

void RecylingVideo::setTitle(const std::string& text) { this->title->setTitle(text); }

void RecylingVideo::setFrameHeight(float height) { this->recycler->setHeight(height); }

void RecylingVideo::setItemWidth(float width) {
    this->recycler->estimatedRowWidth = width;
    this->recycler->reloadData();
}

void RecylingVideo::setPageSize(size_t pageSize) { this->pageSize = pageSize; }

#ifdef PS5_NATIVE_GPU
void RecylingVideo::onQuery(const Callback& callback) {
    reset();
    this->queryCallback = callback;
}
#else
void RecylingVideo::onQuery(const Callback& callback) { this->queryCallback = callback; }
#endif

#ifdef PS5_NATIVE_GPU
template <typename Item, typename Source>
void RecylingVideo::doPagedRequest(bool refresh) {
    if (refresh) {
        reset();
        this->recycler->showSkeleton(this->pageSize);
    }
    if (!queryCallback || !pageSize || requestInFlight || !hasMore) return;
    const auto offset = start;
    const auto path = queryCallback(offset, pageSize);
    const auto generation = ++requestGeneration;
    requestCancelled = std::make_shared<std::atomic_bool>(false);
    auto context = jellyfin::RequestContext::capture();
    context.ownerCancel = requestCancelled;
    requestInFlight = true;
    ASYNC_RETAIN
    jellyfin::request<jellyfin::Result<Item>>(context,
        [path](const jellyfin::RequestContext& request) {
            return jellyfin::fetchJSON<jellyfin::Result<Item>>(request, path);
        },
        [ASYNC_TOKEN, generation, offset](const jellyfin::Result<Item>& r) {
            ASYNC_RELEASE
            if (generation != requestGeneration) return;
            requestInFlight = false;
            // Advance by the actual page, not the configured limit. A short
            // server page must not skip items, and an empty page must terminate.
            this->start = offset + r.Items.size();
            // Home endpoints request totals by default. If a server reports
            // a nonpositive total, a nonempty page is still useful; stop at the
            // first empty page rather than truncating the row after page one.
            hasMore = !r.Items.empty() && (r.TotalRecordCount <= 0
                || this->start < static_cast<size_t>(r.TotalRecordCount));
            this->title->setSubtitle(r.TotalRecordCount > 0 ? std::to_string(r.TotalRecordCount) : "");
            if (offset == 0 && r.Items.empty()) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else if (offset == 0) {
                this->setVisibility(brls::Visibility::VISIBLE);
                this->recycler->setDataSource(new Source(r.Items));
            } else if (!r.Items.empty()) {
                if (auto* dataSrc = dynamic_cast<Source*>(this->recycler->getDataSource())) {
                    dataSrc->appendData(r.Items);
                    this->recycler->notifyDataChanged();
                } else {
                    // A replaced source cannot accept a page from another row type.
                    reset();
                }
            }
        },
        [ASYNC_TOKEN, generation, offset, context](const std::string& ex) {
            ASYNC_RELEASE
            if (generation != requestGeneration) return;
            requestInFlight = false;
            if (context.cancelled()) return;
            if (offset == 0 && !dynamic_cast<Source*>(this->recycler->getDataSource()))
                this->recycler->clearData();
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
        });
}

void RecylingVideo::doRequest(bool refresh) { doPagedRequest<jellyfin::Episode, VideoDataSource>(refresh); }

void RecylingVideo::doLatest(bool refresh) {
    reset();
    if (!queryCallback || !pageSize) return;
    requestCancelled = std::make_shared<std::atomic_bool>(false);
    const auto generation = requestGeneration;
    auto context = jellyfin::RequestContext::capture();
    context.ownerCancel = requestCancelled;
    const auto path = this->queryCallback(0, this->pageSize);
    const bool series = latestAsSeries;
    requestInFlight = true;
    hasMore = false; // Latest is an unpaged endpoint.
#else
void RecylingVideo::doRequest(bool refresh) {
#endif
    if (refresh) {
        this->start = 0;
        this->recycler->showSkeleton(this->pageSize);
    }
    ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
    jellyfin::request<std::vector<jellyfin::Episode>>(context,
        [path, series](const jellyfin::RequestContext& request) {
            auto items = jellyfin::fetchJSON<std::vector<jellyfin::Episode>>(request, path);
            if (!series) return items;
            return jellyfin::latestSeries(std::move(items), [&request](const std::vector<std::string>& ids) {
                std::string joined;
                for (const auto& id : ids) {
                    if (!joined.empty()) joined += ',';
                    joined += id;
                }
                const auto query = HTTP::encode_form({{"ids", joined}, {"enableImageTypes", "Primary"},
                    {"enableUserData", "true"}, {"fields", "BasicSyncInfo,SeriesPrimaryImage"},
                    {"recursive", "true"}});
                return jellyfin::fetchJSON<jellyfin::Result<jellyfin::Episode>>(request,
                    fmt::format(fmt::runtime(jellyfin::apiUserLibrary), request.user, query)).Items;
            });
        },
        [ASYNC_TOKEN, generation](const std::vector<jellyfin::Episode>& r) {
#else
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration) return;
            requestInFlight = false;
            this->title->setSubtitle("");
            this->recycler->setVisibility(brls::Visibility::VISIBLE);
#else
            this->start = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else if (r.StartIndex == 0) {
                this->setVisibility(brls::Visibility::VISIBLE);
                this->recycler->setDataSource(new VideoDataSource(r.Items));
                this->title->setSubtitle(std::to_string(r.TotalRecordCount));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        this->queryCallback(this->start, this->pageSize));
}

void RecylingVideo::doLatest(bool refresh) {
    if (refresh) {
        this->start = 0;
        this->recycler->showSkeleton(this->pageSize);
    }
    ASYNC_RETAIN
    jellyfin::getJSON<std::vector<jellyfin::Episode>>(
        [ASYNC_TOKEN](const std::vector<jellyfin::Episode>& r) {
            ASYNC_RELEASE
#endif
            if (r.empty()) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else {
                this->setVisibility(brls::Visibility::VISIBLE);
                this->recycler->setDataSource(new VideoDataSource(r));
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
            this->recycler->setVisibility(brls::Visibility::VISIBLE);
            this->recycler->clearData();
#else
            this->recycler->setVisibility(brls::Visibility::GONE);
#endif
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
#ifdef PS5_NATIVE_GPU
        });
#else
        },
        this->queryCallback(0, this->pageSize));
#endif
}

#ifdef PS5_NATIVE_GPU
void RecylingVideo::doLiveTV(bool refresh) { doPagedRequest<jellyfin::ProgramInfo, ProgramDataSource>(refresh); }
#else
void RecylingVideo::doLiveTV(bool refresh) {
    if (refresh) {
        this->start = 0;
        this->recycler->showSkeleton(this->pageSize);
    }
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::ProgramInfo>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::ProgramInfo>& r) {
            ASYNC_RELEASE
            this->start = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else if (r.StartIndex == 0) {
                this->setVisibility(brls::Visibility::VISIBLE);
                this->recycler->setDataSource(new ProgramDataSource(r.Items));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<ProgramDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        this->queryCallback(this->start, this->pageSize));
}
#endif
