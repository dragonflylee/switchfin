/*
    Copyright 2023 dragonflylee
*/

#include "tab/live_tv.hpp"
#include "api/jellyfin.hpp"
#include "view/video_card.hpp"
#include "activity/player_view.hpp"
#include "utils/keybind.hpp"
#include <fmt/format.h>

using namespace brls::literals;  // for _i18n

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
    this->inflateFromXMLRes("xml/tabs/media.xml");
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

    this->recycler->spanCount = 3;
    this->recycler->estimatedRowHeight = 250;
    this->recycler->registerCell("Cell", MediaCardCell::create);

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