#pragma once

#include <nlohmann/json.hpp>
#ifdef PS5_NATIVE_GPU
#include <optional>
#include <stdexcept>
#include <limits>
#include <cmath>
#include <type_traits>
#endif

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
    int UnplayedItemCount = 0;
    int64_t PlaybackPositionTicks = 0;
    float PlayedPercentage = 0;
    bool Played = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    UserDataResult, IsFavorite, PlayCount, UnplayedItemCount, PlaybackPositionTicks, PlayedPercentage, Played);

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
#ifdef PS5_NATIVE_GPU
    long Index = -1;
#else
    long Index = 0;
#endif
    bool IsDefault = false;
    bool IsExternal = false;
#ifdef PS5_NATIVE_GPU
    bool IsForced = false;
    std::string Language;
    std::string Title;
    std::string DeliveryMethod;
#endif
    std::string DeliveryUrl;
#ifdef PS5_NATIVE_GPU
    std::optional<int64_t> Width, Height, BitDepth, Channels, RefFrames;
    std::optional<double> AverageFrameRate, RealFrameRate, Level;
    std::string Profile, VideoRangeType, ColorTransfer, ColorPrimaries, ColorSpace;
#endif
};
#ifdef PS5_NATIVE_GPU

template <typename T>
inline T mediaValue(const nlohmann::json& json, const char* key, T fallback = {}) {
    auto value = json.find(key);
    return value == json.end() || value->is_null() ? fallback : value->get<T>();
}

inline std::optional<int64_t> mediaStreamIndex(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || it->is_null()) return std::nullopt;
    // The DTO's identity is int32, not a float/string coercion or a boolean.
    if (!it->is_number_integer() ||
        (it->is_number_unsigned() && it->get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())))
        throw std::invalid_argument("Invalid media stream index");
    const auto index = it->get<int64_t>();
    if (index < -1 || index > std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("Invalid media stream index");
    return index;
}

template <typename T>
inline std::optional<T> mediaPositiveNumber(const nlohmann::json& json, const char* key) {
    auto it = json.find(key);
    if (it == json.end() || it->is_null()) return std::nullopt;
    if (!it->is_number() || (std::is_integral_v<T> && !it->is_number_integer()))
        throw std::invalid_argument("Invalid numeric media metadata");
    const double number = it->get<double>();
    if (!std::isfinite(number) || number < 0 || number > std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("Invalid numeric media metadata");
    if (number == 0) return std::nullopt; // Unprobed server metadata.
    return it->get<T>();
}

inline void from_json(const nlohmann::json& json, Stream& value) {
    value = {};
    value.Codec = mediaValue<std::string>(json, "Codec");
    value.DisplayTitle = mediaValue<std::string>(json, "DisplayTitle");
    value.Type = mediaValue<std::string>(json, "Type");
    value.Index = mediaStreamIndex(json, "Index").value_or(-1);
    value.IsDefault = mediaValue<bool>(json, "IsDefault");
    value.IsExternal = mediaValue<bool>(json, "IsExternal");
    value.IsForced = mediaValue<bool>(json, "IsForced");
    value.Language = mediaValue<std::string>(json, "Language");
    value.Title = mediaValue<std::string>(json, "Title");
    value.DeliveryMethod = mediaValue<std::string>(json, "DeliveryMethod");
    value.DeliveryUrl = mediaValue<std::string>(json, "DeliveryUrl");
    value.Width = mediaPositiveNumber<int64_t>(json, "Width");
    value.Height = mediaPositiveNumber<int64_t>(json, "Height");
    value.BitDepth = mediaPositiveNumber<int64_t>(json, "BitDepth");
    value.Channels = mediaPositiveNumber<int64_t>(json, "Channels");
    value.RefFrames = mediaPositiveNumber<int64_t>(json, "RefFrames");
    value.AverageFrameRate = mediaPositiveNumber<double>(json, "AverageFrameRate");
    value.RealFrameRate = mediaPositiveNumber<double>(json, "RealFrameRate");
    value.Level = mediaPositiveNumber<double>(json, "Level");
    value.Profile = mediaValue<std::string>(json, "Profile");
    value.VideoRangeType = mediaValue<std::string>(json, "VideoRangeType");
    value.ColorTransfer = mediaValue<std::string>(json, "ColorTransfer");
    value.ColorPrimaries = mediaValue<std::string>(json, "ColorPrimaries");
    value.ColorSpace = mediaValue<std::string>(json, "ColorSpace");
}
inline void to_json(nlohmann::json& json, const Stream& value) {
    json = {{"Codec", value.Codec}, {"DisplayTitle", value.DisplayTitle}, {"Type", value.Type},
        {"Index", value.Index}, {"IsDefault", value.IsDefault}, {"IsExternal", value.IsExternal},
        {"IsForced", value.IsForced}, {"Language", value.Language}, {"Title", value.Title},
        {"DeliveryMethod", value.DeliveryMethod}, {"DeliveryUrl", value.DeliveryUrl}};
    auto number = [&](const char* key, const auto& value) { if (value) json[key] = *value; };
    number("Width", value.Width); number("Height", value.Height); number("BitDepth", value.BitDepth);
    number("Channels", value.Channels); number("RefFrames", value.RefFrames);
    number("AverageFrameRate", value.AverageFrameRate); number("RealFrameRate", value.RealFrameRate);
    number("Level", value.Level);
    json["Profile"] = value.Profile; json["VideoRangeType"] = value.VideoRangeType;
    json["ColorTransfer"] = value.ColorTransfer; json["ColorPrimaries"] = value.ColorPrimaries;
    json["ColorSpace"] = value.ColorSpace;
}
#else
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Stream, Codec, DisplayTitle, Type, Index, IsDefault, IsExternal, DeliveryUrl);
#endif

struct Source {
    std::string Id;
    std::string Name;
    std::string Path;
#ifdef PS5_NATIVE_GPU
    std::optional<int64_t> DefaultAudioStreamIndex;
    std::optional<int64_t> DefaultSubtitleStreamIndex;
    bool HasDefaultAudioStreamIndex = false;
    bool HasDefaultSubtitleStreamIndex = false;
    bool SupportsDirectPlay = false;
    bool SupportsDirectStream = false;
    bool SupportsTranscoding = false;
    bool IsRemote = false;
    bool IsInfiniteStream = false;
#else
    int DefaultAudioStreamIndex;
    int DefaultSubtitleStreamIndex;
    bool SupportsDirectPlay;
    bool SupportsTranscoding;
    bool IsRemote;
    bool IsInfiniteStream;
#endif
    std::string ETag;
    std::string DirectStreamUrl;
    std::string TranscodingUrl;
    std::vector<Stream> MediaStreams;
    std::vector<Attachment> MediaAttachments;
#ifdef PS5_NATIVE_GPU
    int64_t Bitrate = 0;
#else
    int64_t Bitrate;
#endif
};
#ifdef PS5_NATIVE_GPU

inline void from_json(const nlohmann::json& json, Source& value) {
    value = {};
    value.Id = mediaValue<std::string>(json, "Id");
    value.Name = mediaValue<std::string>(json, "Name");
    value.Path = mediaValue<std::string>(json, "Path");
    value.HasDefaultAudioStreamIndex = json.contains("DefaultAudioStreamIndex");
    value.HasDefaultSubtitleStreamIndex = json.contains("DefaultSubtitleStreamIndex");
    value.DefaultAudioStreamIndex = mediaStreamIndex(json, "DefaultAudioStreamIndex");
    value.DefaultSubtitleStreamIndex = mediaStreamIndex(json, "DefaultSubtitleStreamIndex");
    value.SupportsDirectPlay = mediaValue<bool>(json, "SupportsDirectPlay");
    value.SupportsDirectStream = mediaValue<bool>(json, "SupportsDirectStream");
    value.SupportsTranscoding = mediaValue<bool>(json, "SupportsTranscoding");
    value.IsRemote = mediaValue<bool>(json, "IsRemote");
    value.IsInfiniteStream = mediaValue<bool>(json, "IsInfiniteStream");
    value.ETag = mediaValue<std::string>(json, "ETag");
    value.DirectStreamUrl = mediaValue<std::string>(json, "DirectStreamUrl");
    value.TranscodingUrl = mediaValue<std::string>(json, "TranscodingUrl");
    value.MediaStreams = mediaValue<std::vector<Stream>>(json, "MediaStreams");
    value.MediaAttachments = mediaValue<std::vector<Attachment>>(json, "MediaAttachments");
    value.Bitrate = mediaValue<int64_t>(json, "Bitrate");
}
inline void to_json(nlohmann::json& json, const Source& value) {
    json = {{"Id", value.Id}, {"Name", value.Name}, {"Path", value.Path},
        {"SupportsDirectPlay", value.SupportsDirectPlay}, {"SupportsDirectStream", value.SupportsDirectStream},
        {"SupportsTranscoding", value.SupportsTranscoding},
        {"IsRemote", value.IsRemote}, {"IsInfiniteStream", value.IsInfiniteStream}, {"ETag", value.ETag},
        {"DirectStreamUrl", value.DirectStreamUrl}, {"TranscodingUrl", value.TranscodingUrl},
        {"MediaStreams", value.MediaStreams}, {"MediaAttachments", value.MediaAttachments}, {"Bitrate", value.Bitrate}};
    auto writeIndex = [&](const char* key, bool present, const std::optional<int64_t>& index) {
        if (index) json[key] = *index;
        else if (present) json[key] = nullptr;
    };
    writeIndex("DefaultAudioStreamIndex", value.HasDefaultAudioStreamIndex, value.DefaultAudioStreamIndex);
    writeIndex("DefaultSubtitleStreamIndex", value.HasDefaultSubtitleStreamIndex, value.DefaultSubtitleStreamIndex);
}
#else
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Source, Id, Name, Path, DefaultAudioStreamIndex,
    DefaultSubtitleStreamIndex, SupportsDirectPlay, SupportsTranscoding, IsRemote, IsInfiniteStream, ETag,
    DirectStreamUrl, TranscodingUrl, MediaStreams, MediaAttachments, Bitrate);
#endif

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
#ifdef PS5_NATIVE_GPU
// RunTimeTicks and Chapters are inherited from Item but have to be listed here
// too: the macro serialises exactly the fields named, so leaving them out meant
// a movie's chapters were parsed away even though the server sends them.
#endif
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Detail, Id, Name, Type, ImageTags, ProductionYear, OriginalTitle,
#ifdef PS5_NATIVE_GPU
    Overview, OfficialRating, CommunityRating, BackdropImageTags, Genres, People, MediaSources, UserData,
    RunTimeTicks, Chapters);
#else
    Overview, OfficialRating, CommunityRating, BackdropImageTags, Genres, People, MediaSources, UserData);
#endif

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
#ifdef PS5_NATIVE_GPU
inline void from_json(const nlohmann::json& json, Program& value) {
    if (!json.is_object()) throw std::invalid_argument("Invalid program metadata");
    value = {};
    value.Name = mediaValue<std::string>(json, "Name");
    value.StartDate = mediaValue<std::string>(json, "StartDate");
    value.EndDate = mediaValue<std::string>(json, "EndDate");
    const auto ticks = json.find("RunTimeTicks");
    if (ticks != json.end() && !ticks->is_null()) {
        // BaseItemDto uses nullable Int64 ticks; do not wrap negatives into a
        // huge unsigned duration or coerce fractional/string/bool metadata.
        if (!ticks->is_number_integer() ||
            (ticks->is_number_unsigned() && ticks->get<uint64_t>() > uint64_t(std::numeric_limits<int64_t>::max())) ||
            ticks->get<int64_t>() < 0)
            throw std::invalid_argument("Invalid program duration");
        value.RunTimeTicks = ticks->get<uint64_t>();
    }
}
inline void to_json(nlohmann::json& json, const Program& value) {
    json = {{"Name", value.Name}, {"RunTimeTicks", value.RunTimeTicks},
        {"StartDate", value.StartDate}, {"EndDate", value.EndDate}};
}
#else
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Program, Name, RunTimeTicks, StartDate, EndDate);
#endif

struct ProgramInfo : public Program {
    std::string ChannelId;
    std::string ChannelName;
};
#ifdef PS5_NATIVE_GPU
inline void from_json(const nlohmann::json& json, ProgramInfo& value) {
    value = {};
    static_cast<Program&>(value) = json.get<Program>();
    value.ChannelId = mediaValue<std::string>(json, "ChannelId");
    value.ChannelName = mediaValue<std::string>(json, "ChannelName");
}
inline void to_json(nlohmann::json& json, const ProgramInfo& value) {
    json = static_cast<const Program&>(value);
    json["ChannelId"] = value.ChannelId;
    json["ChannelName"] = value.ChannelName;
}
#else
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProgramInfo, Name, ChannelId, ChannelName, RunTimeTicks, StartDate, EndDate);
#endif

struct Channel : public Item {
    std::string ChannelType;
    Program CurrentProgram;
};
#ifdef PS5_NATIVE_GPU
inline void from_json(const nlohmann::json& json, Channel& value) {
    if (!json.is_object()) throw std::invalid_argument("Invalid channel metadata");
    value = {};
    value.Id = mediaValue<std::string>(json, "Id");
    value.Name = mediaValue<std::string>(json, "Name");
    value.Type = mediaValue<std::string>(json, "Type");
    value.ImageTags = mediaValue<std::map<std::string, std::string>>(json, "ImageTags");
    value.ChannelType = mediaValue<std::string>(json, "ChannelType");
    // Guide data is absent when no current program is available or requested.
    value.CurrentProgram = mediaValue<Program>(json, "CurrentProgram");
}
inline void to_json(nlohmann::json& json, const Channel& value) {
    json = {{"Id", value.Id}, {"Name", value.Name}, {"Type", value.Type},
        {"ImageTags", value.ImageTags}, {"ChannelType", value.ChannelType},
        {"CurrentProgram", value.CurrentProgram}};
}
#else
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Channel, Id, Name, Type, ImageTags, ChannelType, CurrentProgram);
#endif

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
