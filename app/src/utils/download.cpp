#include "utils/download.hpp"
#include "utils/config.hpp"
#include "utils/thread.hpp"
#include "utils/misc.hpp"
#include "api/jellyfin.hpp"
#include "view/mpv_core.hpp"

std::string DownloadManager::downloadDir() const { return AppConfig::instance().configDir() + "/downloads"; }

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

void DownloadManager::addDownload(const std::string& itemId, DownloadQuality quality) {
    std::lock_guard<std::mutex> lock(this->mutex);

    for (auto& existing : this->items) {
        if (existing.itemId == itemId) {
            brls::Logger::info("Already exists: {}", itemId);
            return;
        }
    }

    jellyfin::getJSON<jellyfin::Episode>(
        [this, quality](const jellyfin::Episode& item) {
            std::lock_guard<std::mutex> lock(this->mutex);

            DownloadItem dl;
            dl.itemId = item.Id;
            dl.name = item.Name;
            dl.type = item.Type;
            dl.seriesName = item.SeriesName;
            dl.seasonIndex = item.ParentIndexNumber;
            dl.episodeIndex = item.IndexNumber;
            dl.productionYear = item.ProductionYear;
            dl.runTimeTicks = item.RunTimeTicks;
            dl.quality = quality;
            dl.status = DownloadStatus::Queued;
            if (item.SeriesId.is_string()) dl.seriesId = item.SeriesId.get<std::string>();
            for (auto& src : item.MediaSources) dl.filePath = src.Name;

            auto primaryTag = item.ImageTags.find(jellyfin::imageTypePrimary);
            if (primaryTag != item.ImageTags.end()) dl.imagePrimaryTag = primaryTag->second;

            this->items.push_back(dl);
            this->saveIndex();
            brls::Logger::info("Download queued: {}", item.Name);
            this->processQueue();
        },
        [](const std::string& ex) { brls::Application::notify(ex); }, jellyfin::apiUserItem,
        AppConfig::instance().getUserId(), itemId);
}

void DownloadManager::resumeQueue() {
    std::lock_guard<std::mutex> lock(this->mutex);
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

    if (!wasActive) {
        std::string dir = this->downloadDir() + "/" + itemId;
        brls::async([dir]() {
            try {
                if (fs::exists(dir)) fs::remove_all(dir);
            } catch (const std::exception& e) {
                brls::Logger::error("Failed to remove download dir: {}", e.what());
            }
        });
    }

    if (erased) {
        brls::sync([this, itemId]() {
            this->statusEvent.fire(itemId, DownloadStatus::Failed);
        });
    }
}

DownloadStatus DownloadManager::findItem(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& item : this->items) {
        if (item.itemId == itemId) return item.status;
    }
    return DownloadStatus::NotFound;
}

std::pair<size_t, size_t> DownloadManager::findSeries(const std::string& seriesId) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    size_t count = 0, done = 0;
    for (auto& item : this->items) {
        if (item.seriesId == seriesId) {
            if (item.status == DownloadStatus::Completed) done++;
            ++count;
        }
    }
    return std::make_pair(count, done);
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
    case DownloadQuality::Q1080p:
        return server + fmt::format(fmt::runtime(jellyfin::apiStream), item.itemId,
            HTTP::encode_form({
                {"static", "false"},
                {"mediaSourceId", item.itemId},
                {"videoCodec", MPVCore::VIDEO_CODEC},
                {"audioCodec", "aac"},
                {"maxStreamingBitrate", "4000000"},
                {"maxHeight", "1080"},
                {"api_key", token},
            }));
    case DownloadQuality::Q720p:
        return server + fmt::format(fmt::runtime(jellyfin::apiStream), item.itemId,
            HTTP::encode_form({
                {"static", "false"},
                {"mediaSourceId", item.itemId},
                {"videoCodec", MPVCore::VIDEO_CODEC},
                {"audioCodec", "aac"},
                {"maxStreamingBitrate", "2000000"},
                {"maxHeight", "720"},
                {"api_key", token},
            }));
    case DownloadQuality::Q480p:
        return server + fmt::format(fmt::runtime(jellyfin::apiStream), item.itemId,
            HTTP::encode_form({
                {"static", "false"},
                {"mediaSourceId", item.itemId},
                {"videoCodec", MPVCore::VIDEO_CODEC},
                {"audioCodec", "aac"},
                {"maxStreamingBitrate", "1000000"},
                {"maxHeight", "480"},
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
    std::string imagePrimaryTag = item.imagePrimaryTag;
    DownloadQuality quality = item.quality;
    std::string url = this->buildDownloadUrl(item);
    std::string itemDir = this->downloadDir() + "/" + itemId;

    this->saveIndex();

    auto cancel = std::make_shared<std::atomic_bool>(false);
    this->currentCancel = cancel;

    brls::sync([this, itemId]() { this->statusEvent.fire(itemId, DownloadStatus::Downloading); });

    ThreadPool::instance().submit([this, itemId, imagePrimaryTag, quality, url, itemDir, cancel](HTTP& s) {
        auto resetQueue = [this, itemId](const std::string& error) {
            brls::sync([this, itemId, error]() {
                {
                    std::lock_guard<std::mutex> lock(this->mutex);
                    for (auto& item : this->items) {
                        if (item.itemId == itemId) {
                            item.status = DownloadStatus::Failed;
                            item.errorMessage = error;
                            break;
                        }
                    }
                    this->downloading = false;
                    this->currentCancel.reset();
                    this->saveIndex();
                }
                this->statusEvent.fire(itemId, DownloadStatus::Failed);
                {
                    std::lock_guard<std::mutex> lock(this->mutex);
                    this->processQueue();
                }
            });
        };

        try {
            if (!fs::exists(itemDir)) fs::create_directories(itemDir);
        } catch (const std::exception& e) {
            brls::Logger::error("Failed to create download dir: {}", e.what());
            resetQueue(e.what());
            return;
        }

        auto& conf = AppConfig::instance();
        HTTP::Header header = {conf.getAuth(conf.getToken())};

        std::string ext = "mp4";
        if (cancel->load()) {
            resetQueue("Cancelled");
            return;
        }
        if (quality == DownloadQuality::Original) {
            try {
                auto resp = HTTP::get(
                    conf.getUrl() + fmt::format(fmt::runtime(jellyfin::apiUserItem), conf.getUserId(), itemId), header,
                    HTTP::Timeout{});
                if (!resp.empty()) {
                    auto detail = nlohmann::json::parse(resp).get<jellyfin::Detail>();
                    if (!detail.MediaSources.empty()) {
                        auto& path = detail.MediaSources[0].Path;
                        auto dot = path.find_last_of('.');
                        if (dot != std::string::npos) {
                            ext = path.substr(dot + 1);
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        }
                    }
                }
            } catch (const std::exception& e) {
                brls::Logger::warning("Failed to fetch item detail for extension: {}", e.what());
            }
        }

        std::string fileName = "video." + ext;
        std::string filePath = itemDir + "/" + fileName;

        {
            std::lock_guard<std::mutex> lock(this->mutex);
            for (auto& it : this->items) {
                if (it.itemId == itemId) {
                    it.filePath = fileName;
                    break;
                }
            }
            this->saveIndex();
        }

        if (!imagePrimaryTag.empty() && !cancel->load()) {
            try {
                std::string thumbUrl = fmt::format("{}/Items/{}/Images/Primary?format=Png&{}", conf.getUrl(), itemId,
                    HTTP::encode_form({{"tag", imagePrimaryTag}, {"maxWidth", "300"}}));
                HTTP::download(thumbUrl, itemDir + "/thumb.png", HTTP::Timeout{});
            } catch (const std::exception& e) {
                fs::remove(itemDir + "/thumb.png");
                brls::Logger::warning("Failed to download thumbnail: {}", e.what());
            }
        }

        auto lastProgress = std::make_shared<std::chrono::steady_clock::time_point>();
        HTTP::Progress::Callback progressCb = [this, itemId, lastProgress](curl_off_t total, curl_off_t now) {
            auto tp = std::chrono::steady_clock::now();
            if (tp - *lastProgress < std::chrono::seconds(1)) return;
            *lastProgress = tp;

            brls::sync([this, itemId, total, now]() {
                {
                    std::lock_guard<std::mutex> lock(this->mutex);
                    for (auto& item : this->items) {
                        if (item.itemId == itemId) {
                            item.totalBytes = total;
                            item.downloadedBytes = now;
                            break;
                        }
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
            DownloadStatus finalStatus = DownloadStatus::Failed;

            {
                std::lock_guard<std::mutex> lock(this->mutex);

                if (cancelled) {
                    bool removed = false;
                    for (auto it = this->items.begin(); it != this->items.end(); ++it) {
                        if (it->itemId == itemId) {
                            if (it->errorMessage == "removed") {
                                this->items.erase(it);
                                removed = true;
                            } else {
                                it->status = DownloadStatus::Failed;
                                it->errorMessage = "Cancelled";
                            }
                            break;
                        }
                    }
                    this->saveIndex();
                    if (removed) {
                        std::string dir = this->downloadDir() + "/" + itemId;
                        brls::async([dir]() {
                            try {
                                if (fs::exists(dir)) fs::remove_all(dir);
                            } catch (const std::exception& e) {
                                brls::Logger::error("Failed to remove download dir: {}", e.what());
                            }
                        });
                    }
                } else if (success) {
                    finalStatus = DownloadStatus::Completed;
                    for (auto& item : this->items) {
                        if (item.itemId == itemId) {
                            item.status = DownloadStatus::Completed;
                            item.filePath = fileName;

                            std::string metaPath = this->downloadDir() + "/" + itemId + "/metadata.json";
                            try {
                                nlohmann::json j = item;
                                std::ofstream f(metaPath);
                                f << j.dump(2);
                            } catch (...) {
                            }
                            break;
                        }
                    }
                    this->saveIndex();
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
                }

                this->downloading = false;
                this->currentCancel.reset();
            }

            this->statusEvent.fire(itemId, finalStatus);
            {
                std::lock_guard<std::mutex> lock(this->mutex);
                this->processQueue();
            }
        });
    });
}
