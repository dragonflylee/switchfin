#pragma once

#include <borealis.hpp>
#include <api/jellyfin/media.hpp>
#include <memory>
#include <atomic>

class HLSDownloader;

class DownloadDialog : public brls::Box {
public:
    DownloadDialog(const std::string& itemId, const std::string& itemName, const jellyfin::PlaybackResult& playbackInfo);
    ~DownloadDialog() override;

    bool isTranslucent() override { return true; }

    View* getDefaultFocus() override { return this->content->getDefaultFocus(); }

private:
    void startDownload();
    void startDirectDownload(const std::string& url);
    void startTranscodedDownload(const std::string& transcodingUrl);
    void updateProgress(int percent, const std::string& status);
    void onDownloadComplete(bool success, const std::string& message);
    void reportPlaybackStart();
    void reportPlaybackStop();

    BRLS_BIND(brls::ScrollingFrame, content, "download/content");
    BRLS_BIND(brls::Box, cancel, "download/cancel");
    BRLS_BIND(brls::SelectorCell, qualitySelector, "download/quality");
    BRLS_BIND(brls::Header, progressHeader, "download/progress/header");
    BRLS_BIND(brls::Rectangle, progressBar, "download/progress/bar");
    BRLS_BIND(brls::Label, statusLabel, "download/status");
    BRLS_BIND(brls::Button, btnStart, "download/start");
    BRLS_BIND(brls::Button, btnCancel, "download/btn/cancel");

    std::string itemId;
    std::string itemName;
    jellyfin::PlaybackResult playbackInfo;
    std::string playSessionId;
    std::unique_ptr<HLSDownloader> downloader;
    std::shared_ptr<std::atomic_bool> downloadCancel;
    bool isDownloading = false;
    int selectedQuality = 0;

    std::vector<std::string> qualityLabels;
    std::vector<int64_t> qualityValues;
};
