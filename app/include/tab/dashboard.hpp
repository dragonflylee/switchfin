/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>

class AutoTabFrame;
class RecyclingGrid;

class Dashboard : public brls::Box {
public:
    Dashboard();
    ~Dashboard() override;

private:
    BRLS_BIND(AutoTabFrame, tabFrame, "dashboard/tabFrame");
    BRLS_BIND(brls::Label, labelServer, "dashboard/server/version");
    BRLS_BIND(brls::Label, labelName, "dashboard/server/name");
    BRLS_BIND(brls::Label, labelAddr, "dashboard/server/addr");
    BRLS_BIND(brls::Label, labelMovie, "dashboard/movie/count");
    BRLS_BIND(brls::Label, labelSeries, "dashboard/series/count");
    BRLS_BIND(brls::Label, labelEpisode, "dashboard/episode/count");
    BRLS_BIND(brls::Label, labelSong, "dashboard/song/count");
    BRLS_BIND(brls::Button, btnRestart, "dashboard/restart");
    BRLS_BIND(brls::Button, btnScan, "dashboard/scan");
    BRLS_BIND(RecyclingGrid, activity, "dashboard/activity");
    BRLS_BIND(RecyclingGrid, sess, "dashboard/session");

    std::unordered_map<std::string, std::string> taskMap;

    void doItemCount();
    void doSystemInfo();
    void doActivityWarn();
    void doSession();
    void doRestart();
    void doListTask();
    void doRunTask(const std::string& id);
};
