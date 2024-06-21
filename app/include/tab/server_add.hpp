/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class ServerAdd : public brls::Box {
public:
    ServerAdd();
    ~ServerAdd() override;

private:
    bool onConnect();

    BRLS_BIND(brls::InputCell, inputUrl, "server/url");
    BRLS_BIND(brls::DetailCell, btnConnect, "server/connect");
};
