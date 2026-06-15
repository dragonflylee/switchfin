#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string_view apiUserViews = "/Users/{}/Views";
const std::string_view apiUserLibrary = "/Users/{}/Items?{}";
const std::string_view apiUserList = "/Playlists/{}/Items?{}";
const std::string_view apiUserItem = "/Users/{}/Items/{}";
const std::string_view apiItemSpecial = "/Users/{}/Items/{}/SpecialFeatures";
const std::string_view apiUserResume = "/Users/{}/Items/Resume?{}";
const std::string_view apiUserLatest = "/Users/{}/Items/Latest?{}";
const std::string_view apiShowNextUp = "/Shows/NextUp?{}";
const std::string_view apiShowSeanon = "/Shows/{}/Seasons?{}";
const std::string_view apiShowEpisodes = "/Shows/{}/Episodes?{}";
const std::string_view apiSimilar = "/Items/{}/Similar?{}";
const std::string_view apiLiveChannels = "/LiveTv/Channels?{}";
const std::string_view apiProgramRecommend = "/LiveTv/Programs/Recommended?{}";
const std::string_view apiGenres = "/Genres?{}";
const std::string_view apiArtists = "/Artists?{}";
const std::string_view apiMovieRecommend = "/Movies/Recommendations?{}";
const std::string_view apiPlayedItems = "/Users/{}/PlayedItems/{}";
const std::string_view apiFavoriteItems = "/Users/{}/FavoriteItems/{}";
#ifdef USE_WEBP
const std::string_view apiUserImage = "/Users/{}/Images/Primary?format=Webp&{}";
const std::string_view apiPrimaryImage = "/Items/{}/Images/Primary?format=Webp&{}";
const std::string_view apiThumbImage = "/Items/{}/Images/Thumb?format=Webp&{}";
const std::string_view apiLogoImage = "/Items/{}/Images/Logo?format=Webp&{}";
const std::string_view apiBackdropImage = "/Items/{}/Images/Backdrop/{}?format=Webp&{}";
#else
const std::string_view apiUserImage = "/Users/{}/Images/Primary?format=Png&{}";
const std::string_view apiPrimaryImage = "/Items/{}/Images/Primary?format=Png&{}";
const std::string_view apiThumbImage = "/Items/{}/Images/Thumb?format=Png&{}";
const std::string_view apiLogoImage = "/Items/{}/Images/Logo?format=Png&{}";
const std::string_view apiBackdropImage = "/Items/{}/Images/Backdrop/{}?format=Png&{}";
#endif

// danmu plugin
const std::string_view apiDanmuku = "/api/danmu/{}/raw";

const std::string_view apiDownload = "/Items/{}/Download?{}";
const std::string_view apiPlayback = "/Items/{}/PlaybackInfo";
const std::string_view apiStream = "/Videos/{}/stream?{}";
const std::string_view apiAudio = "/Audio/{}/stream?{}";
const std::string_view apiPlayStart = "/Sessions/Playing";
const std::string_view apiPlayStop = "/Sessions/Playing/Stopped";
const std::string_view apiPlaying = "/Sessions/Playing/Progress";

const std::string imageTypePrimary = "Primary";
const std::string imageTypeLogo = "Logo";
const std::string imageTypeThumb = "Thumb";
const std::string imageTypeBackdrop = "Backdrop";

const std::string mediaTypeFolder = "Folder";
const std::string mediaTypeSeries = "Series";
const std::string mediaTypeSeason = "Season";
const std::string mediaTypeEpisode = "Episode";
const std::string mediaTypeMovie = "Movie";
const std::string mediaTypeBoxSet = "BoxSet";
const std::string mediaTypeGenre = "Genre";
const std::string mediaTypeAudio = "Audio";
const std::string mediaTypeVideo = "Video";
const std::string mediaTypePhoto = "Photo";
const std::string mediaTypeBook = "Book";
const std::string mediaTypePhotoAlbum = "PhotoAlbum";
const std::string mediaTypeMusicAlbum = "MusicAlbum";
const std::string mediaTypeMusicVideo = "MusicVideo";
const std::string mediaTypeMusicArtist = "MusicArtist";
const std::string mediaTypePlaylist = "Playlist";
const std::string mediaTypeProgram = "Program";
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

struct Genres {
    std::string Id;
    std::string Name;
    std::map<std::string, std::string> ImageTags;
    int ChildCount;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Genres, Id, Name, ImageTags, ChildCount);

struct Item {
    std::string Id;
    std::string Name;
    std::string Type;
    std::map<std::string, std::string> ImageTags;
    long ProductionYear = 0;
    uint64_t RunTimeTicks = 0;
    UserDataResult UserData;
    std::vector<MediaChapter> Chapters;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Item, Id, Name, Type, ImageTags, ProductionYear, RunTimeTicks, UserData, Chapters);

struct Collection : public Item {
    bool IsFolder;
    std::string CollectionType;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Collection, Id, Name, Type, ImageTags, IsFolder, CollectionType);

struct MediaPeople {
    std::string Id;
    std::string Name;
    std::string PrimaryImageTag;
    std::string Role;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaPeople, Id, Name, PrimaryImageTag, Role);

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
    std::string Path;
    int DefaultAudioStreamIndex;
    int DefaultSubtitleStreamIndex;
    bool SupportsDirectPlay;
    bool SupportsTranscoding;
    bool IsRemote;
    bool IsInfiniteStream;
    std::string ETag;
    std::string DirectStreamUrl;
    std::string TranscodingUrl;
    std::vector<Stream> MediaStreams;
    std::vector<Attachment> MediaAttachments;
    int64_t Bitrate;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Source, Id, Name, Path, DefaultAudioStreamIndex,
    DefaultSubtitleStreamIndex, SupportsDirectPlay, SupportsTranscoding, IsRemote, IsInfiniteStream, ETag,
    DirectStreamUrl, TranscodingUrl, MediaStreams, MediaAttachments, Bitrate);

struct Detail : public Item {
    std::string OriginalTitle;
    std::string Overview;
    std::string OfficialRating;
    float CommunityRating = 0.0f;
    std::vector<std::string> BackdropImageTags;
    std::vector<std::string> Genres;
    std::vector<MediaPeople> People;
    std::vector<Source> MediaSources;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Detail, Id, Name, Type, ImageTags, ProductionYear, OriginalTitle,
    Overview, OfficialRating, CommunityRating, BackdropImageTags, Genres, People, MediaSources, UserData);

struct Season : public Item {
    long IndexNumber = 0;
    nlohmann::json SeriesId;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Season, Id, Name, Type, ImageTags, SeriesId, IndexNumber);

struct PeopleItem {
    std::string Id;
    std::string Name;
    std::string Overview;
    std::vector<std::string> ProductionLocations;
    std::map<std::string, std::string> ImageTags;
    int MovieCount;
    int SeriesCount;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PeopleItem, Id, Name, Overview, ProductionLocations, ImageTags)

struct PlaybackResult {
    std::vector<Source> MediaSources;
    std::string PlaySessionId;
    std::string ErrorCode;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlaybackResult, MediaSources, PlaySessionId, ErrorCode);

struct Episode : public Season {
    int ParentIndexNumber = 0;
    std::string Overview;
    std::string ParentThumbImageTag;
    std::string ParentThumbItemId;
    std::string SeriesName;
    std::string SeriesPrimaryImageTag;
    std::string ParentBackdropItemId;
    std::vector<std::string> ParentBackdropImageTags;
    std::vector<Source> MediaSources;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Episode, Id, Name, Type, ImageTags, ProductionYear, UserData, Chapters,
    RunTimeTicks, IndexNumber, ParentIndexNumber, Overview, ParentThumbImageTag, ParentThumbItemId, SeriesId,
    SeriesName, SeriesPrimaryImageTag, ParentBackdropItemId, ParentBackdropImageTags, MediaSources);

struct Recommend {
    std::string BaselineItemName;
    std::string CategoryId;
    std::string RecommendationType;
    std::vector<Episode> Items;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Recommend, BaselineItemName, CategoryId, RecommendationType, Items);

using Recommends = std::vector<Recommend>;

struct Album : public Item {
    std::string AlbumArtist;
    long RecursiveItemCount = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Album, Id, Name, Type, ImageTags, ProductionYear, AlbumArtist, RunTimeTicks, RecursiveItemCount);

struct Track : public Item {
    long IndexNumber = 0;
    long ParentIndexNumber = 0;
    float CommunityRating = 0.0f;
    std::string Album;
    std::string AlbumId;
    std::string AlbumPrimaryImageTag;
    std::vector<std::string> Artists;
    std::string SeriesName;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Track, Id, Name, Type, IndexNumber, ParentIndexNumber, RunTimeTicks,
    ProductionYear, Chapters, CommunityRating, SeriesName, Album, AlbumId, AlbumPrimaryImageTag, Artists, UserData);

struct Program {
    std::string Name;
    uint64_t RunTimeTicks = 0;
    std::string StartDate;
    std::string EndDate;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Program, Name, RunTimeTicks, StartDate, EndDate);

struct ProgramInfo : public Program {
    std::string ChannelId;
    std::string ChannelName;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProgramInfo, Name, ChannelId, ChannelName, RunTimeTicks, StartDate, EndDate);

struct Channel : public Item {
    std::string ChannelType;
    Program CurrentProgram;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Channel, Id, Name, Type, ImageTags, ChannelType, CurrentProgram);

struct Session {
    std::string Id;
    std::string UserId;
    std::string UserName;
    std::string Client;
    std::string LastActivityDate;
    std::string DeviceName;
    std::string DeviceId;
    std::string ApplicationVersion;
    std::string RemoteEndPoint;
    std::string UserPrimaryImageTag;
    Episode NowPlayingItem;
    bool IsActive;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Session, Id, UserId, UserName, Client, LastActivityDate, DeviceName,
    DeviceId, ApplicationVersion, RemoteEndPoint, UserPrimaryImageTag, NowPlayingItem, IsActive);

}  // namespace jellyfin
