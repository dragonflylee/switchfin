#pragma once

#include <view/auto_tab_frame.hpp>
#include "utils/download.hpp"

class RecyclingGrid;
class SegmentedBar;

class DownloadView : public AttachedView {
public:
    DownloadView();
    ~DownloadView() override;

    brls::View* getDefaultFocus() override;
    void dismiss(std::function<void(void)> cb = [] {}) override;
    /// The tab is cached by AutoSidebarItem: refreshes list and storage on
    /// every return (queueing an item does not emit a StatusEvent)
    void willAppear(bool resetState = false) override;

private:
    void loadItems();
    /// Recomputes the header bar and figures: fs::space on the downloads
    /// folder + bytes occupied by pleNx
    void updateStorage();

    BRLS_BIND(RecyclingGrid, recycler, "downloads/list");
    BRLS_BIND(brls::Box, storageBarBox, "downloads/storage/bar");
    BRLS_BIND(brls::Label, storageApp, "downloads/storage/app");
    BRLS_BIND(brls::Label, storageFree, "downloads/storage/free");

    SegmentedBar* storageBar = nullptr;

    DownloadManager::StatusEvent::Subscription statusSubId;
    DownloadManager::ProgressEvent::Subscription progressSubId;
};
