#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiUserViews = "/Users/{}/Views";
const std::string apiUserLibrary = "/Users/{}/Items?{}";
const std::string apiUserList = "/Playlists/{}/Items?{}";
const std::string apiUserItem = "/Users/{}/Items/{}";
const std::string apiItemSpecial = "/Users/{}/Items/{}/SpecialFeatures";
const std::string apiUserResume = "/Users/{}/Items/Resume?{}";
const std::string apiUserLatest = "/Users/{}/Items/Latest?{}";
const std::string apiShowNextUp = "/Shows/NextUp?{}";
const std::string apiShowSeanon = "/Shows/{}/Seasons?{}";
const std::string apiShowEpisodes = "/Shows/{}/Episodes?{}";
const std::string apiLiveChannels = "/LiveTv/Channels?{}";
#ifdef USE_WEBP
const std::string apiUserImage = "/Users/{}/Images/Primary?format=Webp&{}";
const std::string apiPrimaryImage = "/Items/{}/Images/Primary?format=Webp&{}";
const std::string apiLogoImage = "/Items/{}/Images/Logo?format=Webp&{}";
const std::string apiBackdropImage = "/Items/{}/Images/Backdrop?format=Webp&{}";
#else
const std::string apiUserImage = "/Users/{}/Images/Primary?format=Png&{}";
const std::string apiPrimaryImage = "/Items/{}/Images/Primary?format=Png&{}";
const std::string apiLogoImage = "/Items/{}/Images/Logo?format=Png&{}";
const std::string apiBackdropImage = "/Items/{}/Images/Backdrop?format=Png&{}";
#endif

// danmu plugin
const std::string apiDanmuku = "/api/danmu/{}/raw";

const std::string apiPlayback = "/Items/{}/PlaybackInfo";
const std::string apiStream = "/Videos/{}/stream?{}";
const std::string apiAudio = "/Audio/{}/stream?{}";
const std::string apiPlayStart = "/Sessions/Playing";
const std::string apiPlayStop = "/Sessions/Playing/Stopped";
const std::string apiPlaying = "/Sessions/Playing/Progress";

const std::string imageTypePrimary = "Primary";
const std::string imageTypeLogo = "Logo";

const std::string mediaTypeFolder = "Folder";
const std::string mediaTypeSeries = "Series";
const std::string mediaTypeSeason = "Season";
const std::string mediaTypeEpisode = "Episode";
const std::string mediaTypeMovie = "Movie";
const std::string mediaTypeBoxSet = "BoxSet";
const std::string mediaTypeAudio = "Audio";
const std::string mediaTypeMusicAlbum = "MusicAlbum";
const std::string mediaTypeMusicVideo = "MusicVideo";
const std::string mediaTypePlaylist = "Playlist";
const std::string mediaTypeTvChannel = "TvChannel";

const std::string streamTypeVideo = "Video";
const std::string streamTypeAudio = "Audio";
const std::string streamTypeSubtitle = "Subtitle";

const std::string methodDirectPlay = "Directplay";
const std::string methodTranscode = "Transcode";

// The position, in ticks, where playback stopped. 1 tick = 10000 ms
const uint64_t PLAYTICKS = 10000000;

struct UserDataResult {
    bool IsFavorite = false;
    int PlayCount = 0;
    int64_t PlaybackPositionTicks = 0;
    float PlayedPercentage = 0;
    bool Played = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    UserDataResult, IsFavorite, PlayCount, PlaybackPositionTicks, PlayedPercentage, Played);

struct MediaChapter {
    std::string Name;
    uint64_t StartPositionTicks = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaChapter, Name, StartPositionTicks);

struct Item {
    std::string Id;
    std::string Name;
    std::string Type;
    std::map<std::string, std::string> ImageTags;
    bool IsFolder = false;
    long ProductionYear = 0;
    float CommunityRating = 0.0f;
    uint64_t RunTimeTicks = 0;
    UserDataResult UserData;
    std::vector<MediaChapter> Chapters;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Item, Id, Name, Type, ImageTags, IsFolder, ProductionYear, CommunityRating, RunTimeTicks, UserData, Chapters);

struct Collection : public Item {
    std::string CollectionType;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Collection, Id, Name, Type, ImageTags, IsFolder, CollectionType);

struct Series : public Item {
    std::string OriginalTitle;
    std::string Overview;
    std::string OfficialRating;
    std::vector<std::string> Genres;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Series, Id, Name, Type, ImageTags, IsFolder, ProductionYear,
    OriginalTitle, Overview, OfficialRating, CommunityRating, Genres, UserData);

struct Season : public Item {
    long IndexNumber = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Season, Id, Name, Type, ImageTags, IsFolder, IndexNumber);

struct Attachment {
    std::string Codec;
    std::string Name;
    long Index = 0;
    std::string DeliveryUrl;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Attachment, Codec, Name, Index, DeliveryUrl);

struct Stream {
    std::string Codec;
    std::string DisplayTitle;
    std::string Type;
    long Index = 0;
    bool IsDefault = false;
    bool IsExternal = false;
    std::string DeliveryUrl;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Stream, Codec, DisplayTitle, Type, Index, IsDefault, IsExternal, DeliveryUrl);

struct Source {
    std::string Id;
    std::string Name;
    std::string Container;
    std::string Protocol;
    int DefaultAudioStreamIndex;
    int DefaultSubtitleStreamIndex;
    bool SupportsDirectPlay;
    bool SupportsTranscoding;
    std::string DirectStreamUrl;
    std::string TranscodingUrl;
    std::string ETag;
    std::vector<Stream> MediaStreams;
    std::vector<Attachment> MediaAttachments;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Source, Id, Name, Container, DefaultAudioStreamIndex,
    DefaultSubtitleStreamIndex, SupportsDirectPlay, SupportsTranscoding, DirectStreamUrl, TranscodingUrl, ETag,
    MediaStreams, MediaAttachments, Protocol);

struct PlaybackResult {
    std::vector<Source> MediaSources;
    std::string PlaySessionId;
    std::string ErrorCode;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlaybackResult, MediaSources, PlaySessionId, ErrorCode);

struct Episode : public Season {
    int ParentIndexNumber = 0;
    std::string Overview;
    std::string SeriesId;
    std::string SeriesName;
    std::string SeriesPrimaryImageTag;
    std::string ParentBackdropItemId;
    std::vector<std::string> ParentBackdropImageTags;
    std::vector<Source> MediaSources;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Episode, Id, Name, Type, ImageTags, IsFolder, ProductionYear,
    UserData, Chapters, RunTimeTicks, IndexNumber, ParentIndexNumber, Overview, SeriesId, SeriesName,
    SeriesPrimaryImageTag, ParentBackdropItemId, ParentBackdropImageTags, MediaSources);

struct Album : public Item {
    std::string AlbumArtist;
    long RecursiveItemCount = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Album, Id, Name, Type, ImageTags, IsFolder, ProductionYear, AlbumArtist, RunTimeTicks, RecursiveItemCount);

struct Track : public Item {
    long IndexNumber = 0;
    long ParentIndexNumber = 0;
    std::vector<std::string> Artists;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Track, Id, Name, Type, IndexNumber, ParentIndexNumber, RunTimeTicks, ProductionYear, Artists, UserData);

struct Playlist : public Item {
    long IndexNumber = 0;
    long ParentIndexNumber = 0;
    std::string AlbumId;
    std::string AlbumPrimaryImageTag;
    std::vector<std::string> Artists;
    std::string SeriesName;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Playlist, Id, Name, Type, IndexNumber, ParentIndexNumber,
    RunTimeTicks, ProductionYear, Chapters, CommunityRating, SeriesName, ImageTags, AlbumId, AlbumPrimaryImageTag,
    Artists, UserData);

struct Program {
    std::string Name;
    uint64_t RunTimeTicks = 0;
    std::string StartDate;
    std::string EndDate;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Program, Name, RunTimeTicks, StartDate, EndDate);

struct Channel : public Item {
    std::string ChannelType;
    Program CurrentProgram;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Channel, Id, Name, Type, ImageTags, ChannelType, CurrentProgram);

template <typename T>
struct Result {
    std::vector<T> Items;
    size_t TotalRecordCount = 0;
};

template <typename T>
inline void to_json(nlohmann::json& nlohmann_json_j, const Result<T>& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, Items, TotalRecordCount))
}

template <typename T>
inline void from_json(const nlohmann::json& nlohmann_json_j, Result<T>& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, Items, TotalRecordCount))
}

using EpisodeResult = Result<Episode>;

}  // namespace jellyfin
