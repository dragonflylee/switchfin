/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <nlohmann/json.hpp>

namespace jellyfin {

const std::string apiPublicInfo = "/System/Info/Public";
const std::string apiAuthByName = "/Users/authenticatebyname";

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

struct UserResult {
    std::string Id;
    std::string Name;
    std::string ServerId;
    bool HasPassword;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UserResult, Id, Name, ServerId, HasPassword);

struct SessionResult {
    std::string Id;
    std::string RemoteEndPoint;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SessionResult, Id, RemoteEndPoint);

/// @brief /Users/authenticatebyname
struct AuthResult {
    std::string AccessToken;
    std::string ServerId;
    UserResult User;
    SessionResult SessionInfo;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuthResult, AccessToken, ServerId, User, SessionInfo);

}