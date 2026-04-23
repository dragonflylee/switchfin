#pragma once

#include <borealis.hpp>
#include "view/auto_tab_frame.hpp"
#include "utils/download.hpp"

class RecyclingGrid;

class DownloadTab : public AttachedView {
public:
    DownloadTab();
    ~DownloadTab() override;
    void onCreate() override;
    static brls::View* create();

private:
    BRLS_BIND(RecyclingGrid, recycler, "download/grid");

    void doRequest();

    DownloadManager::StatusEvent::Subscription statusSubId;
    DownloadManager::ProgressEvent::Subscription progressSubId;
};
