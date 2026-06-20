/*
    pleNx — full page of a hub (see hub_view.hpp).
    Reuses VideoDataSource: movies/shows -> detail page, episodes -> playback,
    X/long-press context menu included (video_card.cpp).
*/

#include "tab/hub_view.hpp"
#include "api/plex.hpp"
#include "api/backend.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

HubView::HubView(const std::string& title, const std::string& key) : hubKey(key) {
    brls::Logger::debug("HubView: create {} ({})", title, key);
    this->inflateFromXMLRes("xml/tabs/hub.xml");

    // header scrolled WITH the grid (title + meta): it belongs to the
    // recycler, which offsets all its cells by the given height
    brls::View* header = brls::View::createFromXMLResource("view/grid_header.xml");
    this->labelTitle = dynamic_cast<brls::Label*>(header->getView("grid/header/title"));
    this->labelMeta = dynamic_cast<brls::Label*>(header->getView("grid/header/meta"));
    this->recycler->setHeaderView(header, 84);

    this->labelTitle->setText(title);
    this->labelMeta->setVisibility(brls::Visibility::GONE);

    this->pageSize = this->recycler->spanCount * 3;
    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    auto actionRefresh = [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    };
    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->doRequest();
}

brls::View* HubView::getDefaultFocus() { return this->recycler; }

void HubView::doRequest() {
    ASYNC_RETAIN
    // requested offset, not r.StartIndex: Jellyfin/Emby omit StartIndex on an
    // empty past-the-end page (it parses to 0) and would wipe a filled grid
    size_t reqStart = this->startIndex;
    AppConfig::instance().backend().getHubPage(this->hubKey, this->startIndex, this->pageSize,
        [ASYNC_TOKEN, reqStart](const media::Container<media::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = reqStart + this->pageSize;
            if (r.TotalRecordCount == 0 && reqStart == 0) {
                this->recycler->setEmpty();
            } else if (reqStart == 0) {
                int64_t count = r.TotalRecordCount;
                this->labelMeta->setText(fmt::format(
                    "{} {}", count, count > 1 ? "main/playlist/items"_i18n : "main/playlist/item"_i18n));
                this->labelMeta->setVisibility(brls::Visibility::VISIBLE);
                this->recycler->setDataSource(new VideoDataSource(r.Items));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<VideoDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN, reqStart](const std::string& ex) {
            ASYNC_RELEASE
            if (reqStart > 0) {
                brls::Application::notify(ex);
            } else {
                this->recycler->setError(ex);
            }
        });
}
