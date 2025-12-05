/*
    Copyright 2023 jellyfin
*/

#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiSystemInfo = "/System/Info";
const std::string apiPublicInfo = "/System/Info/Public";
const std::string apiAuthByName = "/Users/authenticatebyname";
const std::string apiLogout = "/Sessions/Logout";
const std::string apiBranding = "/Branding/Configuration";

const std::string_view apiUsers = "/Users";
const std::string_view apiDevices = "/Devices";
const std::string_view apiStorage = "/System/Info/Storage";
const std::string_view apiActivityLog = "/System/ActivityLog/Entries?{}";
const std::string_view apiRestart = "/System/Restart";
const std::string_view apiScheduledTasks = "/ScheduledTasks?isHidden=false";
const std::string_view apiRunTask = "/ScheduledTasks/Running/{}";
const std::string_view apiSessionList = "/Sessions?{}";
const std::string_view apiCapabilities = "/Sessions/Capabilities/Full";
const std::string_view apiItemCount = "/Items/Counts";
// apiQuickConnect
const std::string apiQuickEnabled = "/QuickConnect/Enabled";
const std::string apiQuickInitiate = "/QuickConnect/Initiate";
const std::string apiQuickConnect = "/QuickConnect/Connect?secret={}";
const std::string apiAuthWithQuickConnect = "/Users/AuthenticateWithQuickConnect";
const std::string apiUserSetting = "/DisplayPreferences/usersettings?userId={}&client=emby";

const std::string_view apiPlugins = "/Plugins";

struct PublicSystemInfo {
    std::string Id;
    std::string ServerName;
    std::string Version;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PublicSystemInfo, Id, ServerName, Version);

struct SystemInfo : public PublicSystemInfo {
    std::string LocalAddress;
    std::string SystemArchitecture;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SystemInfo, Id, ServerName, Version, LocalAddress, SystemArchitecture);

struct UserPolicy {
    bool IsAdministrator = false;
    bool IsDisabled = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UserPolicy, IsAdministrator, IsDisabled);

struct UserConfig {
    bool EnableNextEpisodeAutoPlay = false;
    bool HidePlayedInLatest = false;
    bool RememberAudioSelections = false;
    bool RememberSubtitleSelections = false;
    bool DisplayCollectionsView = false;
    std::vector<std::string> LatestItemsExcludes;
    std::string AudioLanguagePreference;
    std::string SubtitleLanguagePreference;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UserConfig, EnableNextEpisodeAutoPlay, HidePlayedInLatest,
    RememberAudioSelections, RememberSubtitleSelections, DisplayCollectionsView, LatestItemsExcludes,
    AudioLanguagePreference, SubtitleLanguagePreference);

struct UserInfo {
    std::string Id;
    std::string Name;
    std::string ServerId;
    std::string PrimaryImageTag;
    bool HasPassword = false;
    UserPolicy Policy;
    UserConfig Configuration;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    UserInfo, Id, Name, ServerId, PrimaryImageTag, HasPassword, Policy, Configuration);

/// @brief /Users/authenticatebyname
struct AuthResult {
    std::string AccessToken;
    std::string ServerId;
    UserInfo User;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuthResult, AccessToken, ServerId, User);

struct QuickConnect {
    bool Authenticated;
    std::string Code;
    std::string DateAdded;
    std::string Secret;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(QuickConnect, Authenticated, Code, DateAdded, Secret);

struct PlayStateInfo {
    std::string PlayMethod;
    std::string RepeatMode;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlayStateInfo, PlayMethod, RepeatMode)

struct TranscodeInfo {
    std::string AudioCodec;
    std::string VideoCodec;
    double CompletionPercentage;
    bool IsVideoDirect;
    bool IsAudioDirect;
    std::vector<std::string> TranscodeReasons;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    TranscodeInfo, AudioCodec, VideoCodec, CompletionPercentage, IsVideoDirect, IsAudioDirect, TranscodeReasons);

struct SessionInfo {
    std::string Id;
    PlayStateInfo PlayState;
    TranscodeInfo TranscodingInfo;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SessionInfo, Id, PlayState, TranscodingInfo);

struct DisplayPreferences {
    std::string Id;
    nlohmann::json CustomPrefs;
    std::string SortBy;
    std::string SortOrder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DisplayPreferences, Id, CustomPrefs, SortBy, SortOrder);

struct BrandingConfig {
    std::string LoginDisclaimer;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BrandingConfig, LoginDisclaimer);

struct PluginInfo {
    std::string Name;
    std::string Version;
    std::string Id;
    std::string Status;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PluginInfo, Name, Version, Id, Status);

typedef std::vector<PluginInfo> PluginList;

struct Device {
    std::string Id;
    std::string Name;
    std::string LastUserName;
    std::string LastUserId;
    std::string AppName;
    std::string AppVersion;
    std::string DateLastActivity;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    Device, Id, Name, LastUserName, LastUserId, AppName, AppVersion, DateLastActivity);

struct ActivityLog {
    std::string Name;
    std::string ShortOverview;
    std::string Type;
    std::string Date;
    std::string UserId;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ActivityLog, Name, ShortOverview, Type, Date, UserId);

struct ItemCount {
    int MovieCount;
    int SeriesCount;
    int EpisodeCount;
    int SongCount;
    int AlbumCount;
    int MusicVideoCount;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    ItemCount, MovieCount, SeriesCount, EpisodeCount, SongCount, AlbumCount, MusicVideoCount);

struct FolderInfo {
    std::string Path;
    int64_t FreeSpace;
    int64_t UsedSpace;
    std::string StorageType;
    std::string DeviceId;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FolderInfo, Path, FreeSpace, UsedSpace, StorageType, DeviceId);

struct StorageInfo {
    FolderInfo ProgramDataFolder;
    FolderInfo WebFolder;
    FolderInfo ImageCacheFolder;
    FolderInfo CacheFolder;
    FolderInfo LogFolder;
    FolderInfo InternalMetadataFolder;
    FolderInfo TranscodingTempFolder;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StorageInfo, ProgramDataFolder, WebFolder, ImageCacheFolder, CacheFolder, LogFolder,
    InternalMetadataFolder, TranscodingTempFolder);

struct TaskInfo {
    std::string Id;
    std::string Name;
    std::string State;
    std::string Key;
    bool IsHidden;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TaskInfo, Id, Name, State, Key, IsHidden);

}  // namespace jellyfin