#pragma once

#include <view/auto_tab_frame.hpp>
#include "utils/download.hpp"

class RecyclingGrid;

/// Downloads tab (xml/tabs/downloads.xml): a single flow-mode list whose row 0
/// is the "Storage" card (segmented disk bar + legend), scrolling with the
/// sectioned "In progress" / "Downloaded" rows below it.
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
    /// Refreshes the storage card (row 0) in place: disk figures + segmented
    /// bar (fs::space on the downloads folder + bytes occupied by pleNx).
    void updateStorage();

    BRLS_BIND(RecyclingGrid, recycler, "downloads/list");

    DownloadManager::StatusEvent::Subscription statusSubId;
    DownloadManager::ProgressEvent::Subscription progressSubId;
};
