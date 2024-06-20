/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <client/client.hpp>

class RecyclingGrid;

using DirList = std::vector<remote::DirEntry>;

class RemoteView : public AttachedView {
public:
    using Client = std::shared_ptr<remote::Client>;

    RemoteView(Client c);
    ~RemoteView() override;

    brls::View* getDefaultFocus() override;

    void onCreate() override;

    void push(const std::string& path);

private:
    BRLS_BIND(RecyclingGrid, recycler, "remote/list");

    void load();

    std::vector<std::string> stack;
    Client client;
};