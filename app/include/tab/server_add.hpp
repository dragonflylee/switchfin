/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_native_requests.hpp"
#endif

class ServerAdd : public brls::Box {
public:
    ServerAdd();
    ~ServerAdd() override;

    brls::View* getDefaultFocus() override;

private:
    bool onConnect();
#ifdef PS5_NATIVE_GPU
    ps5_native_requests::Scope requests;
#endif

    BRLS_BIND(brls::InputCell, inputUrl, "server/url");
    BRLS_BIND(brls::DetailCell, btnConnect, "server/connect");
};
