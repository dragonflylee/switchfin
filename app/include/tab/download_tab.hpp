#pragma once

#include <view/auto_tab_frame.hpp>
#include "utils/download.hpp"

class RecyclingGrid;

class DownloadView : public AttachedView {
public:
    DownloadView();
    ~DownloadView() override;

    brls::View* getDefaultFocus() override;
    void dismiss(std::function<void(void)> cb = [] {}) override;

private:
    void loadItems();
    RecyclingGrid* newRecycler();
    void setContent(RecyclingGrid* view);

    std::vector<RecyclingGrid*> stack;
    RecyclingGrid* recycler = nullptr;

    DownloadManager::StatusEvent::Subscription statusSubId;
    DownloadManager::ProgressEvent::Subscription progressSubId;
};
