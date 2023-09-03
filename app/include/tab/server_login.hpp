/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#include "utils/config.hpp"

class ServerLogin : public brls::Box {
public:
    ServerLogin(const AppServer& s);
    ~ServerLogin();

    bool onSignin();

private:
    BRLS_BIND(brls::Header, hdrSigin, "server/sigin_to");
    BRLS_BIND(brls::InputCell, inputUser, "server/user");
    BRLS_BIND(brls::InputCell, inputPass, "server/pass");
    BRLS_BIND(brls::Button, btnSignin, "server/signin");

    std::string url;
};