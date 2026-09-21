#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/event.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

enum class DownloadStatus { Queued, Downloading, Completed, Failed, NotFound };
enum class DownloadQuality { Original, Q1080p, Q720p, Q480p };

NLOHMANN_JSON_SERIALIZE_ENUM(DownloadStatus, {
    {DownloadStatus::Queued, "Queued"},
    {DownloadStatus::Downloading, "Downloading"},
    {DownloadStatus::Completed, "Completed"},
    {DownloadStatus::Failed, "Failed"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(DownloadQuality, {
    {DownloadQuality::Original, "Original"},
    {DownloadQuality::Q1080p, "1080p"},
    {DownloadQuality::Q720p, "720p"},
    {DownloadQuality::Q480p, "480p"},
})

struct DownloadItem {
    std::string itemId;
    std::string name;
    std::string type;
    std::string seriesId;
    std::string seriesName;
    int seasonIndex = 0;
    int episodeIndex = 0;
    long productionYear = 0;
    uint64_t runTimeTicks = 0;
    std::string imagePrimaryTag;
    DownloadQuality quality = DownloadQuality::Original;
    DownloadStatus status = DownloadStatus::Queued;
    std::string filePath;
    int64_t totalBytes = 0;
    int64_t downloadedBytes = 0;
    std::string errorMessage;
#ifdef PS5_NATIVE_GPU
    // The media directory that holds this item's folder. Empty means the
    // current downloadDir(); a value pins an item to where it was written, so a
    // download made in the sandbox keeps playing after the home moves to /data.
    std::string storageRoot;
#endif
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DownloadItem, itemId, name, type, seriesId, seriesName,
    seasonIndex, episodeIndex, productionYear, runTimeTicks, imagePrimaryTag, quality, status,
#ifdef PS5_NATIVE_GPU
    filePath, totalBytes, downloadedBytes, errorMessage, storageRoot);
#else
    filePath, totalBytes, downloadedBytes, errorMessage);
#endif

class DownloadManager : public brls::Singleton<DownloadManager> {
public:
    using ProgressEvent = brls::Event<std::string, int64_t, int64_t>;
    using StatusEvent = brls::Event<std::string, DownloadStatus>;

    void init();

    void addDownload(const std::string& itemId, DownloadQuality quality);
    void cancelDownload(const std::string& itemId);
    void removeDownload(const std::string& itemId);
    void resumeQueue();

    DownloadStatus findItem(const std::string& itemId) const;
    std::pair<size_t, size_t> findSeries(const std::string& seriesId) const;
    std::string getLocalPath(const std::string& itemId) const;

    std::vector<DownloadItem> getItems() const;
    std::string downloadDir() const;
#ifdef PS5_NATIVE_GPU
    // Where index.json and metadata live, always in the sandbox so the index is
    // found regardless of where media currently goes.
    std::string indexDir() const;
    // The directory that holds one item's files, honouring its storageRoot.
    std::string itemDir(const DownloadItem& item) const;
    // chmod the download directories so opendir can enumerate them.
    void repairPermissions() const;
    void recordIndexScan(const std::string& dir) const;
#endif

    ProgressEvent* getProgressEvent() { return &progressEvent; }
    StatusEvent* getStatusEvent() { return &statusEvent; }

private:
    void saveIndex();
    void loadIndex();
#ifdef PS5_NATIVE_GPU
    /// Adopt download directories that index.json does not know about.
    /// Caller must hold `mutex`.
    void reconcileOrphans();
    /// Write a directory's own copy of its item, so the directory is
    /// self-describing even if index.json is lost or never updated.
    void writeMetadata(const DownloadItem& item) const;
#endif
    void processQueue();
    void doDownload(DownloadItem& item);
#ifdef PS5_NATIVE_GPU
    // Caller holds mutex; deferred deletion owns only a retired directory.
    bool retirePS5Download(const std::string& itemId);
    void finishPS5Download(const std::string& itemId, const std::string& fileName,
        const std::shared_ptr<std::atomic_bool>& cancel, bool success, int64_t completedBytes, const std::string& error);
#endif
    std::string buildDownloadUrl(const DownloadItem& item) const;

    mutable std::mutex mutex;
    std::vector<DownloadItem> items;
    std::shared_ptr<std::atomic_bool> currentCancel;
    bool downloading = false;

    ProgressEvent progressEvent;
    StatusEvent statusEvent;
};
