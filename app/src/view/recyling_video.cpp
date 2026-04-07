#include "view/recyling_video.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "api/jellyfin.hpp"

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

RecylingVideo::~RecylingVideo() {}

void RecylingVideo::setTitle(const std::string& text) { this->title->setTitle(text); }

void RecylingVideo::setFrameHeight(float height) { this->recycler->setHeight(height); }

void RecylingVideo::setItemWidth(float width) {
    this->recycler->estimatedRowWidth = width;
    this->recycler->reloadData();
}

void RecylingVideo::setPageSize(size_t pageSize) { this->pageSize = pageSize; }

void RecylingVideo::onQuery(const Callback& callback) { this->queryCallback = callback; }

void RecylingVideo::doRequest(bool refresh) {
    if (refresh) {
        this->start = 0;
        this->recycler->showSkeleton(this->pageSize);
    }
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
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
            if (r.empty()) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else {
                this->setVisibility(brls::Visibility::VISIBLE);
                this->recycler->setDataSource(new VideoDataSource(r));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setVisibility(brls::Visibility::GONE);
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        this->queryCallback(0, this->pageSize));
}

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