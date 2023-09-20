/*
    Copyright 2023 jellyfin
*/

#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiInfo = "/System/Info";
const std::string apiPublicInfo = "/System/Info/Public";
const std::string apiAuthByName = "/Users/authenticatebyname";

const std::string apiSessionList = "/Sessions?{}";
// apiQuickConnect
const std::string apiQuickEnabled = "/QuickConnect/Enabled";
const std::string apiQuickInitiate = "/QuickConnect/Initiate";
const std::string apiQuickConnect = "/QuickConnect/Connect?secret={}";
const std::string apiAuthWithQuickConnect = "/Users/AuthenticateWithQuickConnect";

struct PublicSystemInfo {
    std::string Id;
    std::string LocalAddress;
    std::string ServerName;
    std::string Version;
    std::string ProductName;
    std::string OperatingSystem;
    bool StartupWizardCompleted;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PublicSystemInfo, Id, ServerName, Version, OperatingSystem);

struct UserPolicy {
    bool IsAdministrator;
    std::vector<std::string> EnabledFolders;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UserPolicy, IsAdministrator, EnabledFolders);

struct UserConfiguration {
    bool PlayDefaultAudioTrack;
    std::string SubtitleLanguagePreference;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UserConfiguration, PlayDefaultAudioTrack, SubtitleLanguagePreference);

struct UserResult {
    std::string Id;
    std::string Name;
    std::string ServerId;
    bool HasPassword;
    UserPolicy Policy;
    UserConfiguration Configuration;
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

}