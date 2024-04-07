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

const std::string apiSessionList = "/Sessions?{}";
// apiQuickConnect
const std::string apiQuickEnabled = "/QuickConnect/Enabled";
const std::string apiQuickInitiate = "/QuickConnect/Initiate";
const std::string apiQuickConnect = "/QuickConnect/Connect?secret={}";
const std::string apiAuthWithQuickConnect = "/Users/AuthenticateWithQuickConnect";
const std::string apiUserSetting = "/DisplayPreferences/usersettings?userId={}&client=emby";

const std::string apiBranding = "/Branding/Configuration";

struct PublicSystemInfo {
    std::string Id;
    std::string ServerName;
    std::string Version;
    std::string OperatingSystem;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PublicSystemInfo, Id, ServerName, Version, OperatingSystem);

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

}