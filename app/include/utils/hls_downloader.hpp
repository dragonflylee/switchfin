#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <memory>
#include <fstream>

class HLSDownloader {
public:
    using ProgressCallback = std::function<void(int percent, const std::string& status)>;
    using CompleteCallback = std::function<void(bool success, const std::string& message)>;
    using Cancel = std::shared_ptr<std::atomic_bool>;

    HLSDownloader(const std::string& baseUrl, const std::string& playlistUrl, const std::string& outputPath);
    ~HLSDownloader();

    void setProgressCallback(ProgressCallback cb) { progressCallback = std::move(cb); }
    void setCompleteCallback(CompleteCallback cb) { completeCallback = std::move(cb); }
    void setCancel(Cancel c) { cancel = std::move(c); }

    void start();

    static std::string sanitizeFilename(const std::string& filename);
    static std::string getDownloadPath(const std::string& filename);

private:
    bool fetchPlaylist();
    bool parsePlaylist(const std::string& content);
    bool downloadSegment(const std::string& url);
    void reportProgress(int percent, const std::string& status);
    void reportComplete(bool success, const std::string& message);

    std::string baseUrl;
    std::string playlistUrl;
    std::string outputPath;
    std::vector<std::string> segments;
    std::ofstream outputFile;
    Cancel cancel;
    ProgressCallback progressCallback;
    CompleteCallback completeCallback;
    int downloadedSegments = 0;
    int totalSegments = 0;
    bool playlistComplete = false;
};
