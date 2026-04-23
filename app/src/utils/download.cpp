#include "utils/download.hpp"
#include "utils/config.hpp"
#include "api/http.hpp"
#include "api/jellyfin/media.hpp"

#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <fstream>

#ifdef USE_BOOST_FILESYSTEM
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#endif

std::string DownloadManager::downloadDir() const {
    return AppConfig::instance().configDir() + "/downloads";
}

void DownloadManager::init() {
    auto dir = this->downloadDir();
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    this->loadIndex();

    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& item : this->items) {
        if (item.status == DownloadStatus::Downloading) {
            item.status = DownloadStatus::Queued;
        }
    }
    this->saveIndex();
}

void DownloadManager::loadIndex() {
    std::string path = this->downloadDir() + "/index.json";
    if (!fs::exists(path)) return;

    try {
        std::ifstream f(path);
        nlohmann::json j = nlohmann::json::parse(f);
        this->items = j.get<std::vector<DownloadItem>>();
    } catch (const std::exception& e) {
        brls::Logger::error("Failed to load download index: {}", e.what());
    }
}

void DownloadManager::saveIndex() {
    std::string path = this->downloadDir() + "/index.json";
    try {
        nlohmann::json j = this->items;
        std::ofstream f(path);
        f << j.dump(2);
    } catch (const std::exception& e) {
        brls::Logger::error("Failed to save download index: {}", e.what());
    }
}

void DownloadManager::addDownload(const jellyfin::Item& item, DownloadQuality quality) {
    std::lock_guard<std::mutex> lock(this->mutex);

    for (auto& existing : this->items) {
        if (existing.itemId == item.Id) {
            brls::Logger::info("Download already exists: {}", item.Name);
            return;
        }
    }

    DownloadItem dl;
    dl.itemId = item.Id;
    dl.name = item.Name;
    dl.type = item.Type;
    dl.productionYear = item.ProductionYear;
    dl.runTimeTicks = item.RunTimeTicks;
    dl.quality = quality;
    dl.status = DownloadStatus::Queued;

    auto primaryTag = item.ImageTags.find(jellyfin::imageTypePrimary);
    if (primaryTag != item.ImageTags.end()) {
        dl.imagePrimaryTag = primaryTag->second;
    }

    this->items.push_back(dl);
    this->saveIndex();
    brls::Logger::info("Download queued: {}", item.Name);
    this->processQueue();
}

void DownloadManager::addEpisodeDownload(const jellyfin::Episode& ep, DownloadQuality quality) {
    std::lock_guard<std::mutex> lock(this->mutex);

    for (auto& existing : this->items) {
        if (existing.itemId == ep.Id) {
            brls::Logger::info("Download already exists: {}", ep.Name);
            return;
        }
    }

    DownloadItem dl;
    dl.itemId = ep.Id;
    dl.name = ep.Name;
    dl.type = ep.Type;
    dl.seriesName = ep.SeriesName;
    dl.seasonIndex = ep.ParentIndexNumber;
    dl.episodeIndex = ep.IndexNumber;
    dl.productionYear = ep.ProductionYear;
    dl.runTimeTicks = ep.RunTimeTicks;
    dl.quality = quality;
    dl.status = DownloadStatus::Queued;

    auto primaryTag = ep.ImageTags.find(jellyfin::imageTypePrimary);
    if (primaryTag != ep.ImageTags.end()) {
        dl.imagePrimaryTag = primaryTag->second;
    }

    this->items.push_back(dl);
    this->saveIndex();
    brls::Logger::info("Download queued: {} - {}", ep.SeriesName, ep.Name);
    this->processQueue();
}

void DownloadManager::cancelDownload(const std::string& itemId) {
    bool erased = false;
    {
        std::lock_guard<std::mutex> lock(this->mutex);

        for (auto& item : this->items) {
            if (item.itemId == itemId && item.status == DownloadStatus::Downloading && this->currentCancel) {
                this->currentCancel->store(true);
                return;
            }
        }

        for (auto it = this->items.begin(); it != this->items.end(); ++it) {
            if (it->itemId == itemId && it->status == DownloadStatus::Queued) {
                this->items.erase(it);
                this->saveIndex();
                erased = true;
                break;
            }
        }
    }

    if (erased) {
        brls::sync([this, itemId]() {
            this->statusEvent.fire(itemId, DownloadStatus::Failed);
        });
    }
}

void DownloadManager::removeDownload(const std::string& itemId) {
    bool wasActive = false;
    bool erased = false;
    {
        std::lock_guard<std::mutex> lock(this->mutex);

        for (auto& item : this->items) {
            if (item.itemId == itemId && item.status == DownloadStatus::Downloading && this->currentCancel) {
                this->currentCancel->store(true);
                item.status = DownloadStatus::Failed;
                item.errorMessage = "removed";
                wasActive = true;
                break;
            }
        }

        if (!wasActive) {
            for (auto it = this->items.begin(); it != this->items.end(); ++it) {
                if (it->itemId == itemId) {
                    this->items.erase(it);
                    erased = true;
                    break;
                }
            }
            this->saveIndex();
        }
    }

    std::string dir = this->downloadDir() + "/" + itemId;
    brls::async([dir]() {
        if (fs::exists(dir)) {
            fs::remove_all(dir);
        }
    });

    if (erased) {
        brls::sync([this, itemId]() {
            this->statusEvent.fire(itemId, DownloadStatus::Failed);
        });
    }
}

bool DownloadManager::isDownloaded(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& item : this->items) {
        if (item.itemId == itemId && item.status == DownloadStatus::Completed) return true;
    }
    return false;
}

bool DownloadManager::isDownloading(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& item : this->items) {
        if (item.itemId == itemId &&
            (item.status == DownloadStatus::Downloading || item.status == DownloadStatus::Queued))
            return true;
    }
    return false;
}

bool DownloadManager::hasDownload(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& item : this->items) {
        if (item.itemId == itemId) return true;
    }
    return false;
}

std::string DownloadManager::getLocalPath(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& item : this->items) {
        if (item.itemId == itemId && item.status == DownloadStatus::Completed) {
            return this->downloadDir() + "/" + itemId + "/" + item.filePath;
        }
    }
    return "";
}

std::vector<DownloadItem> DownloadManager::getItems() const {
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->items;
}

std::string DownloadManager::buildDownloadUrl(const DownloadItem& item) const {
    auto& conf = AppConfig::instance();
    std::string server = conf.getUrl();
    std::string token = conf.getToken();

    switch (item.quality) {
    case DownloadQuality::Original:
        return server + fmt::format(fmt::runtime(jellyfin::apiDownload), item.itemId,
            HTTP::encode_form({{"api_key", token}}));
    case DownloadQuality::HQ:
        return server + fmt::format(fmt::runtime(jellyfin::apiStream), item.itemId,
            HTTP::encode_form({
                {"static", "false"},
                {"mediaSourceId", item.itemId},
                {"videoCodec", "h264"},
                {"audioCodec", "aac"},
                {"maxStreamingBitrate", "8000000"},
                {"api_key", token},
            }));
    case DownloadQuality::LQ:
        return server + fmt::format(fmt::runtime(jellyfin::apiStream), item.itemId,
            HTTP::encode_form({
                {"static", "false"},
                {"mediaSourceId", item.itemId},
                {"videoCodec", "h264"},
                {"audioCodec", "aac"},
                {"maxStreamingBitrate", "1500000"},
                {"maxHeight", "720"},
                {"api_key", token},
            }));
    }
    return "";
}

// Must be called with mutex held
void DownloadManager::processQueue() {
    if (this->downloading) return;

    for (auto& item : this->items) {
        if (item.status == DownloadStatus::Queued) {
            this->downloading = true;
            this->doDownload(item);
            return;
        }
    }
}

// Must be called with mutex held. Copies what it needs, then releases via async.
void DownloadManager::doDownload(DownloadItem& item) {
    item.status = DownloadStatus::Downloading;

    std::string itemId = item.itemId;
    std::string url = this->buildDownloadUrl(item);
    std::string itemDir = this->downloadDir() + "/" + itemId;
    std::string ext = (item.quality == DownloadQuality::Original) ? "mkv" : "mp4";
    std::string fileName = "video." + ext;
    std::string filePath = itemDir + "/" + fileName;

    item.filePath = fileName;
    this->saveIndex();

    auto cancel = std::make_shared<std::atomic_bool>(false);
    this->currentCancel = cancel;

    brls::sync([this, itemId]() {
        this->statusEvent.fire(itemId, DownloadStatus::Downloading);
    });

    brls::async([this, itemId, url, filePath, fileName, itemDir, cancel]() {
        if (!fs::exists(itemDir)) {
            fs::create_directories(itemDir);
        }

        auto& conf = AppConfig::instance();
        HTTP::Header header = {conf.getAuth(conf.getToken())};

        HTTP::Progress::Callback progressCb = [this, itemId](curl_off_t total, curl_off_t now) {
            brls::sync([this, itemId, total, now]() {
                std::lock_guard<std::mutex> lock(this->mutex);
                for (auto& item : this->items) {
                    if (item.itemId == itemId) {
                        item.totalBytes = total;
                        item.downloadedBytes = now;
                        break;
                    }
                }
                this->progressEvent.fire(itemId, now, total);
            });
        };

        bool cancelled = false;
        bool success = false;
        std::string error;

        try {
            std::ofstream of(filePath, std::ios::binary);
            if (!of) throw std::runtime_error("Failed to open file for writing");

            HTTP s;
            HTTP::set_option(s, header, cancel, progressCb);
            s._get(url, &of);
            of.close();

            cancelled = cancel->load();
            if (!cancelled) success = true;
        } catch (const std::exception& ex) {
            error = ex.what();
            brls::Logger::error("Download failed: {} - {}", itemId, error);
        }

        brls::sync([this, itemId, fileName, cancelled, success, error]() {
            std::lock_guard<std::mutex> lock(this->mutex);

            if (cancelled) {
                for (auto it = this->items.begin(); it != this->items.end(); ++it) {
                    if (it->itemId == itemId) {
                        if (it->errorMessage == "removed") {
                            this->items.erase(it);
                        } else {
                            it->status = DownloadStatus::Failed;
                            it->errorMessage = "Cancelled";
                        }
                        break;
                    }
                }
                this->saveIndex();
                this->statusEvent.fire(itemId, DownloadStatus::Failed);
            } else if (success) {
                for (auto& item : this->items) {
                    if (item.itemId == itemId) {
                        item.status = DownloadStatus::Completed;
                        item.filePath = fileName;

                        std::string metaPath = this->downloadDir() + "/" + itemId + "/metadata.json";
                        try {
                            nlohmann::json j = item;
                            std::ofstream f(metaPath);
                            f << j.dump(2);
                        } catch (...) {}
                        break;
                    }
                }
                this->saveIndex();
                this->statusEvent.fire(itemId, DownloadStatus::Completed);
                brls::Logger::info("Download completed: {}", itemId);
            } else {
                for (auto& item : this->items) {
                    if (item.itemId == itemId) {
                        item.status = DownloadStatus::Failed;
                        item.errorMessage = error;
                        break;
                    }
                }
                this->saveIndex();
                this->statusEvent.fire(itemId, DownloadStatus::Failed);
            }

            this->downloading = false;
            this->currentCancel.reset();
            this->processQueue();
        });
    });
}
