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
        this->doRequest();
    }

    void doRequest() {
        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"limit", std::to_string(this->pageSize)},
            {"startIndex", std::to_string(this->start)},
            {"fields", "ChannelInfo"},
            {"enableImageTypes", "Primary"},
            {"isAiring", "true"},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::ProgramInfo>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::ProgramInfo>& r) {
                ASYNC_RELEASE
                this->start = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->clearData();
                } else if (r.StartIndex == 0) {
                    this->setDataSource(new ProgramDataSource(r.Items));
                } else if (r.Items.size() > 0) {
                    auto dataSrc = dynamic_cast<ProgramDataSource*>(this->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->notifyDataChanged();
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            jellyfin::apiProgramRecommend, query);
    }

private:
    size_t start = 0;
    size_t pageSize = 48;
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
        cell->labelTitle->setText(item.Name);
        cell->labelExt->setText(item.CurrentProgram.Name);
        cell->picture->setScalingType(brls::ImageScalingType::FIT);

        auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
        if (it != item.ImageTags.end()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                HTTP::encode_form({{"tag", it->second}, {"maxWidth", "300"}}));
        }
        return cell;
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

void LiveTV::doRequest() {
    HTTP::Form query = {{"userId", AppConfig::instance().getUserId()}};

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Channel>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Channel>& r) {
            ASYNC_RELEASE
            if (r.Items.empty())
                this->recycler->setEmpty();
            else
                this->recycler->setDataSource(new LiveDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setError(ex);
        },
        jellyfin::apiLiveChannels, HTTP::encode_form(query));
}