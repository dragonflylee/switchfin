#include <borealis.hpp>
#include "utils/download.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "api/plex.hpp"

using namespace brls::literals;  // for _i18n

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

void DownloadManager::addDownload(const std::string& itemId) {
    {
        std::lock_guard<std::mutex> lock(this->mutex);

        for (auto& existing : this->items) {
            if (existing.itemId == itemId) {
                brls::Logger::info("Already exists: {}", itemId);
                return;
            }
        }
    }

    auto& conf = AppConfig::instance();
    // métadonnées : GET /library/metadata/{ratingKey} (plex_client.dart:1607-1626)
    plex::getJSON<plex::Container<plex::Item>>(
        conf.getUrl(), conf.getToken(),
        [this](const plex::Container<plex::Item>& r) {
            if (r.Items.empty()) {
                brls::Application::notify("main/download/failed"_i18n);
                return;
            }
            auto& item = r.Items.front();

            std::lock_guard<std::mutex> lock(this->mutex);

            DownloadItem dl;
            dl.itemId = item.ratingKey;
            dl.name = item.title;
            dl.type = item.type;
            dl.seriesName = item.grandparentTitle;
            dl.seasonIndex = (int)item.parentIndex;
            dl.episodeIndex = (int)item.index;
            dl.productionYear = (long)item.year;
            dl.durationMs = item.duration;
            // affiche (poster) : pour un épisode, celle de la saison/série
            dl.thumb = item.type == plex::mediaTypeEpisode
                           ? (!item.parentThumb.empty() ? item.parentThumb : item.grandparentThumb)
                           : item.thumb;
            // première Part accessible : qualité originale (PLEX_MIGRATION.md D2)
            for (auto& media : item.media) {
                for (auto& part : media.parts) {
                    if (!part.key.empty()) {
                        dl.partKey = part.key;
                        break;
                    }
                }
                if (!dl.partKey.empty()) break;
            }
            if (dl.partKey.empty()) {
                brls::Application::notify("main/download/failed"_i18n);
                return;
            }
            dl.status = DownloadStatus::Queued;

            this->items.push_back(dl);
            this->saveIndex();
            brls::Logger::info("Download queued: {}", item.title);
            this->processQueue();
        },
        [](const std::string& ex) { brls::Application::notify(ex); }, plex::apiMetadata, itemId, "");
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
    // fichier original : {base}{Part.key}?download=1&X-Plex-Token=…
    // (PLEX_MIGRATION.md D2 — pas de téléchargement transcodé en v1)
    return plex::withToken(conf.getUrl() + item.partKey + "?download=1", conf.getToken());
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
    std::string thumb = item.thumb;
    std::string partKey = item.partKey;
    std::string url = this->buildDownloadUrl(item);
    std::string itemDir = this->downloadDir() + "/" + itemId;

    this->saveIndex();

    auto cancel = std::make_shared<std::atomic_bool>(false);
    this->currentCancel = cancel;

    brls::sync([this, itemId]() { this->statusEvent.fire(itemId, DownloadStatus::Downloading); });

    brls::async([this, itemId, thumb, partKey, url, itemDir, cancel]() {
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
        HTTP::Header header = plex::headers(conf.getToken());

        if (cancel->load()) {
            resetQueue("Cancelled");
            return;
        }
        // extension du fichier original, lue depuis Part.key
        // (ex. /library/parts/{id}/{ts}/file.mkv)
        std::string ext = "mp4";
        auto dot = partKey.find_last_of('.');
        if (dot != std::string::npos && dot > partKey.find_last_of('/')) {
            ext = partKey.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
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

        if (!thumb.empty() && !cancel->load()) {
            try {
                // vignette : {base}{thumb}?X-Plex-Token=… (PLEX_MIGRATION.md §2.5)
                std::string thumbUrl = plex::withToken(conf.getUrl() + thumb, conf.getToken());
                HTTP::download(thumbUrl, itemDir + "/thumb.png", HTTP::Timeout{});
            } catch (const std::exception& e) {
                brls::Logger::warning("Failed to download thumbnail: {}", e.what());
            }
        }

        auto lastProgress = std::make_shared<std::chrono::steady_clock::time_point>();
        HTTP::Progress::Callback progressCb = [this, itemId, lastProgress](curl_off_t total, curl_off_t now) {
            auto tp = std::chrono::steady_clock::now();
            if (tp - *lastProgress < std::chrono::milliseconds(500)) return;
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
