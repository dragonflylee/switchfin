#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/event.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

enum class DownloadStatus { Queued, Downloading, Completed, Failed };

NLOHMANN_JSON_SERIALIZE_ENUM(DownloadStatus, {
    {DownloadStatus::Queued, "Queued"},
    {DownloadStatus::Downloading, "Downloading"},
    {DownloadStatus::Completed, "Completed"},
    {DownloadStatus::Failed, "Failed"},
})

/// Download in ORIGINAL quality only (PLEX_MIGRATION.md D2):
/// URL = {base}{partKey}?download=1&X-Plex-Token=...
struct DownloadItem {
    std::string itemId;  // ratingKey
    std::string name;
    std::string type;         // movie | episode | clip
    std::string seriesName;   // grandparentTitle
    int seasonIndex = 0;      // parentIndex
    int episodeIndex = 0;     // index
    long productionYear = 0;  // year
    int64_t durationMs = 0;   // duration (ms)
    std::string thumb;        // relative poster path
    std::string partKey;      // Part.key (original file)
    DownloadStatus status = DownloadStatus::Queued;
    std::string filePath;
    int64_t totalBytes = 0;
    int64_t downloadedBytes = 0;
    std::string errorMessage;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DownloadItem, itemId, name, type, seriesName,
    seasonIndex, episodeIndex, productionYear, durationMs, thumb, partKey, status,
    filePath, totalBytes, downloadedBytes, errorMessage);

class DownloadManager : public brls::Singleton<DownloadManager> {
public:
    // itemId, downloaded bytes, total bytes (0 = unknown), smoothed speed B/s (0 = n/a)
    using ProgressEvent = brls::Event<std::string, int64_t, int64_t, double>;
    using StatusEvent = brls::Event<std::string, DownloadStatus>;

    void init();

    void addDownload(const std::string& itemId);
    void cancelDownload(const std::string& itemId);
    void removeDownload(const std::string& itemId);
    void resumeQueue();

    bool isDownloaded(const std::string& itemId) const;
    bool isDownloading(const std::string& itemId) const;
    std::string getLocalPath(const std::string& itemId) const;
    /// Downloads root ({config}/downloads): thumbnails, real sizes and
    /// fs::space for the "Storage" header (download_tab.cpp)
    std::string getDownloadDir() const { return this->downloadDir(); }

    std::vector<DownloadItem> getItems() const;

    ProgressEvent* getProgressEvent() { return &progressEvent; }
    StatusEvent* getStatusEvent() { return &statusEvent; }

private:
    void saveIndex();
    void loadIndex();
    void processQueue();
    void doDownload(DownloadItem& item);
    std::string downloadDir() const;
    std::string buildDownloadUrl(const DownloadItem& item) const;

    mutable std::mutex mutex;
    std::vector<DownloadItem> items;
    std::shared_ptr<std::atomic_bool> currentCancel;
    bool downloading = false;

    ProgressEvent progressEvent;
    StatusEvent statusEvent;
};
