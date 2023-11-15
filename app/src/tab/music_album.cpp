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

    ~MusicTrackCell() override {}

    static RecyclingGridItem* create() { return new MusicTrackCell(); }

    BRLS_BIND(brls::Label, trackIndex, "music/track/index");
    BRLS_BIND(brls::Label, trackName, "music/track/name");
    BRLS_BIND(brls::Label, trackArtists, "music/track/artists");
    BRLS_BIND(brls::Label, trackDuration, "music/track/duration");
    BRLS_BIND(brls::Box, trackFavorite, "music/track/favorite");
};

class TracksDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MusicTrack>;

    TracksDataSource(const MediaList& r) : list(std::move(r)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        MusicTrackCell* cell = dynamic_cast<MusicTrackCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->trackIndex->setText(std::to_string(item.IndexNumber));
        cell->trackName->setText(item.Name);
        cell->trackArtists->setText(fmt::format("{}", fmt::join(item.Artists, " ")));
        cell->trackDuration->setText(misc::sec2Time(item.RunTimeTicks / jellyfin::PLAYTICKS));
        cell->trackFavorite->setVisibility(
            item.UserData.IsFavorite ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
        return cell;
    }

    void onItemSelected(brls::View* recycler, size_t index) override {
        std::string i = std::to_string(index);
        MusicView::load(this->list);
        MPVCore::instance().command("playlist-play-index", i.c_str());
    }

    void clearData() override { this->list.clear(); }

private:
    MediaList list;
};

MusicAlbum::MusicAlbum(const std::string& itemId) : albumId(itemId) {
    this->inflateFromXMLRes("xml/tabs/music_album.xml");
    brls::Logger::debug("Tab MusicAlbum: create {}", itemId);

    this->albumTracks->registerCell("Cell", MusicTrackCell::create);

    this->doAlbum();
    this->doTracks();
}

MusicAlbum::~MusicAlbum() { brls::Logger::debug("Tab MusicAlbum: delete"); }

void MusicAlbum::doAlbum() {
    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MusicAlbum& r) {
            ASYNC_RELEASE
            this->albumTitle->setText(r.Name);
            this->albumAritst->setText(r.AlbumArtist);
            if (r.ProductionYear) this->albumYear->setText(std::to_string(r.ProductionYear));
            // loading cover
            auto cover = r.ImageTags.find(jellyfin::imageTypePrimary);
            if (cover != r.ImageTags.end()) {
                Image::load(this->imageCover, jellyfin::apiPrimaryImage, r.Id,
                    HTTP::encode_form({
                        {"tag", cover->second},
                        {"maxWidth", "300"},
                    }));
                this->imageCover->setVisibility(brls::Visibility::VISIBLE);
            }
        },
        [](...) {}, jellyfin::apiUserItem, AppConfig::instance().getUser().id, this->albumId);
}

void MusicAlbum::doTracks() {
    std::string query = HTTP::encode_form({
        {"parentId", this->albumId},
        {"fields", "ItemCounts,BasicSyncInfo"},
        {"sortBy", "ParentIndexNumber,IndexNumber,SortName"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON(
        [ASYNC_TOKEN](const jellyfin::MediaQueryResult<jellyfin::MusicTrack>& r) {
            ASYNC_RELEASE
            this->albumTracks->setDataSource(new TracksDataSource(r.Items));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->albumTracks->setError(ex);
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUser().id, query);
}