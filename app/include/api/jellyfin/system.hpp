/*
    Copyright 2023 jellyfin
*/

#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiInfo = "/System/Info";
const std::string apiPublicInfo = "/System/Info/Public";
const std::string apiAuthByName = "/Users/authenticatebyname";
const std::string apiLogout = "/Sessions/Logout";
const std::string apiBranding = "/Branding/Configuration";

const std::string_view apiDevices = "/Devices?{}";
const std::string_view apiActivityLog = "/System/ActivityLog/Entries?{}";
const std::string_view apiScheduledTasks = "/ScheduledTasks";
const std::string_view apiSessionList = "/Sessions?{}";
const std::string_view apiCapabilities = "/Sessions/Capabilities/Full";
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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PublicSystemInfo, Id, ServerName, Version);

struct UserResult {
    std::string Id;
    std::string Name;
    std::string ServerId;
    bool HasPassword;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UserResult, Id, Name, ServerId, HasPassword);

/// @brief /Users/authenticatebyname
struct AuthResult {
    std::string AccessToken;
    std::string ServerId;
    UserResult User;
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
    bool IsActive;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Session, Id, UserId, UserName, Client, LastActivityDate, DeviceName,
    DeviceId, ApplicationVersion, RemoteEndPoint, IsActive);

struct Device {
    std::string Id;
    std::string Name;
    std::string LastUserName;
    std::string AppName;
    std::string AppVersion;
    std::string DateLastActivity;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Device, Id, Name, LastUserName, AppName, AppVersion, DateLastActivity);

struct ActivityLog {
    std::string Id;
    std::string Name;
    std::string ShortOverview;
    std::string Type;
    std::string Date;
    std::string UserId;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ActivityLog, Id, Name, ShortOverview, Type, Date, UserId);

}  // namespace jellyfin