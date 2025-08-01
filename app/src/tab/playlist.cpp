#include "activity/player_view.hpp"
#include "api/jellyfin.hpp"
#include "tab/playlist.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"
#include "view/music_view.hpp"
#include "view/mpv_core.hpp"
#include "view/recycling_grid.hpp"
#include <fmt/ranges.h>

class PlaylistCell : public RecyclingGridItem {
public:
    PlaylistCell() { this->inflateFromXMLRes("xml/view/playlist_item.xml"); }

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

class PlaylistDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Track>;

    PlaylistDataSource(const MediaList& r) : list(std::move(r)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        PlaylistCell* cell = dynamic_cast<PlaylistCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.Id);

        if (item.Type == jellyfin::mediaTypeEpisode) {
            cell->name->setText(fmt::format("S{}E{} {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        } else {
            cell->name->setText(item.Name);
        }

        if (item.Type == jellyfin::mediaTypeAudio) {
            if (!item.AlbumPrimaryImageTag.empty()) {
                Image::load(cell->picture, jellyfin::apiPrimaryImage, item.AlbumId,
                    HTTP::encode_form({{"tag", item.AlbumPrimaryImageTag}, {"maxWidth", "50"}}));
            }

            cell->misc->setText(fmt::format("{}", fmt::join(item.Artists, " ")));
        } else {
            auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
            if (it != item.ImageTags.end()) {
                Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
                    HTTP::encode_form({{"tag", it->second}, {"maxWidth", "50"}}));
            }

            if (item.CommunityRating > 0) {
                cell->rating->setText(fmt::format("{:.1f}", item.CommunityRating));
                cell->rating->getParent()->setVisibility(brls::Visibility::VISIBLE);
            }

            if (item.Type == jellyfin::mediaTypeEpisode) {
                cell->misc->setText(item.SeriesName);
            } else if (item.ProductionYear > 0) {
                cell->misc->setText(std::to_string(item.ProductionYear));
            } else {
                cell->misc->setText("");
            }
        }

        cell->duration->setText(misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS));
        cell->favorite->setVisibility(
            item.UserData.IsFavorite ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
        cell->setSelected(MusicView::instance().currentId());
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        auto& stats = MusicView::instance();

        if (item.Type == jellyfin::mediaTypeAudio) {
            stats.load(this->list, index);
            return;
        }

        if (item.Type == jellyfin::mediaTypeMovie || item.Type == jellyfin::mediaTypeMusicVideo) {
            PlayerView* view = new PlayerView(item);
            view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
        } else if (item.Type == jellyfin::mediaTypeEpisode) {
            PlayerView* view = new PlayerView(item);
            view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
        } else {
            brls::Logger::info("Playlist selected {}", item.Type);
        }
    }

    void clearData() override { this->list.clear(); }

private:
    MediaList list;
};

Playlist::Playlist(const jellyfin::Item& item) : itemId(item.Id) {
    this->inflateFromXMLRes("xml/tabs/music_album.xml");
    brls::Logger::debug("Tab Playlist: create {}", itemId);

    this->list->estimatedRowHeight = 100;
    this->list->registerCell("Cell", []() { return new PlaylistCell(); });

    this->title->setText(item.Name);
    this->misc->setText(misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS));

    // loading cover
    auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
    if (it != item.ImageTags.end()) {
        Image::load(this->cover, jellyfin::apiPrimaryImage, itemId,
            HTTP::encode_form({
                {"tag", it->second},
                {"maxWidth", "240"},
            }));
        this->cover->setVisibility(brls::Visibility::VISIBLE);
    }

    this->doList();

    auto& stats = MusicView::instance();
    this->stats->addView(&stats);
    stats.registerViewAction(this);

    auto mpvce = MPVCore::instance().getCustomEvent();
    this->customEventSubscribeID = mpvce->subscribe([this](const std::string& event, void* data) {
        if (event == TRACK_START) {
            auto item = reinterpret_cast<MusicView::Track*>(data);
            for (auto i : this->list->getGridItems()) {
                auto* cell = dynamic_cast<PlaylistCell*>(i);
                if (cell) cell->setSelected(item->Id);
            }
        }
    });
}

Playlist::~Playlist() {
    brls::Logger::debug("Tab Playlist: delete");
    Image::cancel(this->cover);
    auto mpvce = MPVCore::instance().getCustomEvent();
    mpvce->unsubscribe(this->customEventSubscribeID);
    /// 通知 MusicView 已关闭
    MusicView::instance().setParent(nullptr);
    this->stats->clearViews(false);
}

brls::View* Playlist::getDefaultFocus() { return this->stats; }

void Playlist::doList() {
    std::string query = HTTP::encode_form({
        {"fields", "PrimaryImageAspectRatio,Chapters,BasicSyncInfo"},
        {"EnableImageTypes", "Primary"},
        {"UserId", AppConfig::instance().getUserId()},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Track>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Track>& r) {
            ASYNC_RELEASE
            this->list->setDataSource(new PlaylistDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->list->setError(ex);
        },
        jellyfin::apiUserList, this->itemId, query);
}