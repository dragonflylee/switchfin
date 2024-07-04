#include "tab/music_album.hpp"
#include "view/mpv_core.hpp"
#include "view/recycling_grid.hpp"
#include "view/music_view.hpp"
#include "api/jellyfin.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"
#include <fmt/ranges.h>

class MusicTrackCell : public RecyclingGridItem {
public:
    MusicTrackCell() { this->inflateFromXMLRes("xml/view/music_track.xml"); }
    ~MusicTrackCell() override = default;

    void setSelected(bool value) {
        this->selected = value;
        if (value) {
            this->trackIndex->setMarginLeft(20);
            this->setBackgroundColor(brls::Application::getTheme().getColor("color/grey_2"));
        } else {
            this->trackIndex->setMarginLeft(0);
            this->setBackgroundColor(RGBA(0, 0, 0, 0));
        }
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override {
        Box::draw(vg, x, y, width, height, style, ctx);
        if (this->selected) {
            // h1-3 周期 666ms，最大幅度 666*0.015 ≈ 10
            int h1 = ((brls::getCPUTimeUsec() >> 10) % 666) * 0.015;
            int h2 = (h1 + 3) % 10;
            int h3 = (h1 + 7) % 10;
            if (h1 > 5) h1 = 10 - h1;
            if (h2 > 5) h2 = 10 - h2;
            if (h3 > 5) h3 = 10 - h3;

            float base_y = y + height / 2 - 2;
            nvgBeginPath(vg);
            nvgFillColor(vg, a(ctx->theme.getColor("color/app")));
            nvgRect(vg, x + 10, base_y - h1, 2, h1 + h1 + 4);
            nvgRect(vg, x + 15, base_y - h2, 2, h2 + h2 + 4);
            nvgRect(vg, x + 20, base_y - h3, 2, h3 + h3 + 4);
            nvgFill(vg);
        }
    }

    BRLS_BIND(brls::Label, trackIndex, "music/track/index");
    BRLS_BIND(brls::Label, trackName, "music/track/name");
    BRLS_BIND(brls::Label, trackArtists, "music/track/artists");
    BRLS_BIND(brls::Label, trackDuration, "music/track/duration");
    BRLS_BIND(brls::Box, trackFavorite, "music/track/favorite");

    std::string itemId;
    bool selected = false;
};

class TracksDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Track>;

    TracksDataSource(const MediaList& r) : list(std::move(r)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        MusicTrackCell* cell = dynamic_cast<MusicTrackCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->itemId = item.Id;
        cell->trackIndex->setText(std::to_string(item.IndexNumber));
        cell->trackName->setText(item.Name);
        cell->trackArtists->setText(fmt::format("{}", fmt::join(item.Artists, " ")));
        cell->trackDuration->setText(misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS));
        cell->trackFavorite->setVisibility(
            item.UserData.IsFavorite ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
        cell->setSelected(MusicView::instance().currentId() == item.Id);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        MusicView::instance().load(this->list, index);
    }

    void clearData() override { this->list.clear(); }

private:
    MediaList list;
};

MusicAlbum::MusicAlbum(const jellyfin::Item& item) : itemId(item.Id) {
    this->inflateFromXMLRes("xml/tabs/music_album.xml");
    brls::Logger::debug("Tab MusicAlbum: create {}", itemId);

    this->albumTracks->estimatedRowHeight = 60;
    this->albumTracks->registerCell("Cell", []() { return new MusicTrackCell(); });

    this->albumTitle->setText(item.Name);
    if (item.ProductionYear) this->albumYear->setText(std::to_string(item.ProductionYear));
    // loading cover
    auto it = item.ImageTags.find(jellyfin::imageTypePrimary);
    if (it != item.ImageTags.end()) {
        Image::load(this->imageCover, jellyfin::apiPrimaryImage, itemId,
            HTTP::encode_form({
                {"tag", it->second},
                {"maxWidth", "300"},
            }));
        this->imageCover->setVisibility(brls::Visibility::VISIBLE);
    }

    this->doAlbum();
    this->doTracks();

    auto& stats = MusicView::instance();
    this->albumStats->addView(&stats);
    stats.registerViewAction(this);

    auto mpvce = MPVCore::instance().getCustomEvent();
    this->customEventSubscribeID = mpvce->subscribe([this](const std::string& event, void* data) {
        if (event == TRACK_START) {
            auto item = reinterpret_cast<jellyfin::Track*>(data);
            for (auto i : this->albumTracks->getGridItems()) {
                auto* cell = dynamic_cast<MusicTrackCell*>(i);
                if (cell) cell->setSelected(cell->itemId == item->Id);
            }
        }
    });
}

MusicAlbum::~MusicAlbum() {
    brls::Logger::debug("Tab MusicAlbum: delete");

    auto mpvce = MPVCore::instance().getCustomEvent();
    mpvce->unsubscribe(this->customEventSubscribeID);
    /// 通知 MusicView 已关闭
    MusicView::instance().setParent(nullptr);
    this->albumStats->clearViews(false);
}

void MusicAlbum::doAlbum() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Album>(
        [ASYNC_TOKEN](const jellyfin::Album& r) {
            ASYNC_RELEASE
            this->albumAritst->setText(r.AlbumArtist);
        },
        nullptr, jellyfin::apiUserItem, AppConfig::instance().getUser().id, this->itemId);
}

void MusicAlbum::doTracks() {
    std::string query = HTTP::encode_form({
        {"parentId", this->itemId},
        {"fields", "ItemCounts,BasicSyncInfo"},
        {"sortBy", "ParentIndexNumber,IndexNumber,SortName"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Track>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Track>& r) {
            ASYNC_RELEASE
            this->albumTracks->setDataSource(new TracksDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->albumTracks->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, query);
}