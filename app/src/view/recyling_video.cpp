#include "view/recyling_video.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "view/more_card.hpp"
#include "api/plex.hpp"

const std::string recylingVideoContentXML = R"xml(
    <brls:Box
        width="auto"
        height="auto"
        axis="column"
        marginBottom="36">

        <brls:Header
            marginBottom="6"
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

    this->registerFloatXMLAttribute("sidePadding", [this](float value) { this->setSidePadding(value); });

    this->registerFloatXMLAttribute("pageSize", [this](float value) { this->setPageSize(value); });

    this->registerAutoXMLAttribute(
        "nextPage", [this]() { this->recycler->onNextPage([this]() { this->doRequest(); }); });

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->registerCell("More", MoreCardCell::create);
}

RecylingVideo::~RecylingVideo() {}

void RecylingVideo::setTitle(const std::string& text) { this->title->setTitle(text); }

void RecylingVideo::setSidePadding(float padding) {
    this->title->setMarginLeft(padding);
    this->title->setMarginRight(padding);
    // padding inside the HRecyclerFrame: the cards slide up to the edges
    this->recycler->setPaddingLeft(padding);
    this->recycler->setPaddingRight(padding);
}

void RecylingVideo::setFrameHeight(float height) { this->recycler->setHeight(height); }

void RecylingVideo::setItemWidth(float width) {
    this->recycler->estimatedRowWidth = width;
    this->recycler->reloadData();
}

void RecylingVideo::setPageSize(size_t pageSize) { this->pageSize = pageSize; }

void RecylingVideo::onQuery(const Callback& callback) { this->queryCallback = callback; }

void RecylingVideo::setItems(const std::vector<plex::Item>& items) { this->setItems(items, "", ""); }

void RecylingVideo::setItems(
    const std::vector<plex::Item>& items, const std::string& moreTitle, const std::string& moreKey) {
    if (items.empty()) {
        this->setVisibility(brls::Visibility::GONE);
        this->recycler->clearData();
    } else {
        this->setVisibility(brls::Visibility::VISIBLE);
        auto* source = new VideoDataSource(items);
        if (!moreKey.empty()) source->setMore(moreTitle, moreKey);
        this->recycler->setDataSource(source);
    }
}

void RecylingVideo::doRequest(bool refresh) {
    // row fed by setItems (hubs): the nextPage XML attribute can trigger
    // doRequest without a queryCallback
    if (!this->queryCallback) return;
    if (refresh) {
        this->start = 0;
        this->recycler->showSkeleton(this->pageSize);
    }
    auto& conf = AppConfig::instance();
    ASYNC_RETAIN
    // the relative path is already formatted by queryCallback -> fmt "{}"
    plex::getJSON<plex::Container<plex::Item>>(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            this->start = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else if (r.StartIndex == 0) {
                this->setVisibility(brls::Visibility::VISIBLE);
                this->recycler->setDataSource(new VideoDataSource(r.Items));
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
        "{}", this->queryCallback(this->start, this->pageSize));
}

void RecylingVideo::doLatest(bool refresh) {
    if (!this->queryCallback) return;
    if (refresh) {
        this->start = 0;
        this->recycler->showSkeleton(this->pageSize);
    }
    auto& conf = AppConfig::instance();
    ASYNC_RETAIN
    // the response is also a MediaContainer (no more bare array on the Plex side)
    plex::getJSON<plex::Container<plex::Item>>(
        conf.getUrl(), conf.getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else {
                this->setVisibility(brls::Visibility::VISIBLE);
                this->recycler->setDataSource(new VideoDataSource(r.Items));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setVisibility(brls::Visibility::GONE);
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
        },
        "{}", this->queryCallback(0, this->pageSize));
}
