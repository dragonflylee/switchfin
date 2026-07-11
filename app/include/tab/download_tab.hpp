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
    /// bar. recomputeCache re-walks the meta+art caches (expensive) — only do
    /// that on a download set change, NOT on a progress tick (reuse the cache).
    void updateStorage(bool recomputeCache = false);

    BRLS_BIND(RecyclingGrid, recycler, "downloads/list");

    /// Cached recursive size of the offline caches (meta+art), refreshed on a
    /// list rebuild; a progress tick reuses it instead of re-walking the disk.
    int64_t storageCacheBytes = 0;

    DownloadManager::StatusEvent::Subscription statusSubId;
    DownloadManager::ProgressEvent::Subscription progressSubId;
};
