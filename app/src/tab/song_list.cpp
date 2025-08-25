#include "tab/song_list.hpp"
#include "view/music_view.hpp"
#include "view/recycling_grid.hpp"
#include "view/mpv_core.hpp"
#include "api/jellyfin.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"
#include <fmt/ranges.h>

class SongCell : public RecyclingGridItem {
public:
    SongCell() { this->inflateFromXMLRes("xml/view/playlist_item.xml"); }

    void prepareForReuse() override {
        this->picture->setImageFromRes("img/video-card-bg.png");
        this->rating->getParent()->setVisibility(brls::Visibility::GONE);
    }

    void cacheForReuse() override { Image::cancel(this->picture); }

    void setSelected(const std::string& itemId) {
        this->selected = !this->id.compare(itemId);
        this->setBackgroundColor(brls::Application::getTheme().getColor(selected ? "color/grey_3" : "color/grey_2"));
    }

    BRLS_BIND(brls::Label, name, "playlist/item/name");
    BRLS_BIND(brls::Label, misc, "playlist/item/misc");
    BRLS_BIND(brls::Label, duration, "playlist/item/duration");
    BRLS_BIND(brls::Label, rating, "playlist/item/rating");
    BRLS_BIND(brls::Box, favorite, "playlist/item/favorite");
    BRLS_BIND(brls::Image, picture, "playlist/item/picture");

private:
    bool selected = false;
};

class SongsDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Track>;

    SongsDataSource(const MediaList& r) : list(std::move(r)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        SongCell* cell = dynamic_cast<SongCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);

        if (!item.AlbumPrimaryImageTag.empty()) {
            Image::load(cell->picture, jellyfin::apiPrimaryImage, item.AlbumId,
                HTTP::encode_form({{"tag", item.AlbumPrimaryImageTag}, {"maxWidth", "50"}}));
        }

        cell->setId(item.Id);
        cell->name->setText(item.Name);
        cell->misc->setText(fmt::format("{}", fmt::join(item.Artists, " ")));
        cell->duration->setText(misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS));
        cell->favorite->setVisibility(
            item.UserData.IsFavorite ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
        cell->setSelected(MusicView::instance().currentId());
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& stats = MusicView::instance();
        stats.load(this->list, index);
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

SongList::SongList(const std::string& itemId, const std::string& artistIds) : itemId(itemId), artistIds(artistIds) {
    this->inflateFromXMLRes("xml/tabs/music_album.xml");
    brls::Logger::debug("Tab SongList: create {}", itemId);

    this->list->estimatedRowHeight = 100;
    this->list->registerCell("Cell", []() { return new SongCell(); });
    this->list->onNextPage([this] { this->doList(); });

    this->doList();

    auto& stats = MusicView::instance();
    this->prevParent = stats.getParent();
    if (this->prevParent) this->prevParent->clearViews(false);

    this->stats->addView(&stats);
    stats.registerViewAction(this);
    stats.image(this->cover);

    auto mpvce = MPVCore::instance().getCustomEvent();
    this->customEventSubscribeID = mpvce->subscribe([this](const std::string& event, void* data) {
        if (event == TRACK_START) {
            auto item = reinterpret_cast<MusicView::Track*>(data);
            for (auto i : this->list->getGridItems()) {
                auto* cell = dynamic_cast<SongCell*>(i);
                if (cell) cell->setSelected(item->Id);
            }
            if (item->ImageTag.size() > 0)
                Image::load(this->cover, jellyfin::apiPrimaryImage, item->ImageId,
                    HTTP::encode_form({
                        {"tag", item->ImageTag},
                        {"maxWidth", "240"},
                    }));
            this->title->setText(item->Album);
        }
    });
}

SongList::~SongList() {
    brls::Logger::debug("Tab SongList: delete");
    Image::cancel(this->cover);
    auto mpvce = MPVCore::instance().getCustomEvent();
    mpvce->unsubscribe(this->customEventSubscribeID);
    /// 通知 MusicView 已关闭
    this->stats->clearViews(false);
    auto& stats = MusicView::instance();
    if (this->prevParent)
        this->prevParent->addView(&stats);
    else
        MusicView::instance().setParent(nullptr);
}

void SongList::doList() {
    std::string query = HTTP::encode_form({
        {"parentId", this->itemId},
        {"artistIds", this->artistIds},
        {"enableImageTypes", "Primary"},
        {"fields", "ParentId"},
        {"includeItemTypes", jellyfin::mediaTypeAudio},
        {"recursive", "true"},
        {"limit", std::to_string(this->pageSize)},
        {"startIndex", std::to_string(this->start)},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Track>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Track>& r) {
            ASYNC_RELEASE
            this->start = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->list->clearData();
            } else if (r.StartIndex == 0) {
                this->list->setDataSource(new SongsDataSource(r.Items));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<SongsDataSource*>(this->list->getDataSource());
                dataSrc->appendData(r.Items);
                this->list->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->list->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), query);
}