/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class ServerAdd : public brls::Box {
public:
    ServerAdd(std::function<void(void)> cb);
    ~ServerAdd();

private:
    bool onConnect();
    std::function<void(void)> cbConnected;

    BRLS_BIND(brls::InputCell, inputUrl, "server/url");
    BRLS_BIND(brls::DetailCell, btnConnect, "server/connect");
};
