/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class AutoTabFrame;
class RecyclingGrid;
class HRecyclerFrame;

class Dashboard : public brls::Box {
public:
    Dashboard();
    ~Dashboard() override;

private:
    BRLS_BIND(AutoTabFrame, tabFrame, "dashboard/tabFrame");
    BRLS_BIND(brls::Label, labelServer, "dashboard/server/version");
    BRLS_BIND(brls::Label, labelName, "dashboard/server/name");
    BRLS_BIND(brls::Label, labelAddr, "dashboard/server/addr");
    BRLS_BIND(brls::Button, btnRestart, "dashboard/restart");
    BRLS_BIND(brls::Button, btnScan, "dashboard/scan");
    BRLS_BIND(RecyclingGrid, activity, "dashboard/activity");
    BRLS_BIND(RecyclingGrid, sess, "dashboard/session");
    BRLS_BIND(HRecyclerFrame, itemCount, "dashboard/count");
    BRLS_BIND(brls::Box, mainBox, "dashboard/main/box");
    BRLS_BIND(brls::Box, storage, "dashboard/storage/box");

    std::unordered_map<std::string, std::string> taskMap;

    void doItemCount();
    void doSystemInfo();
    void doActivityWarn();
    void doSession();
    void doRestart();
    void doListTask();
    void doRunTask(const std::string& id);
    void doStorage();
};
