/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class ServerLogin : public brls::Box {
public:
    ServerLogin(const std::string& name, const std::string& url, const std::string& user = "");
    ~ServerLogin();

    bool onSignin();

private:
    BRLS_BIND(brls::Header, hdrSigin, "server/sigin_to");
    BRLS_BIND(brls::InputCell, inputUser, "server/user");
    BRLS_BIND(brls::InputCell, inputPass, "server/pass");
    BRLS_BIND(brls::Button, btnSignin, "server/signin");
    BRLS_BIND(brls::Button, btnQuickConnect, "server/quick_connect");

    std::string url;
};