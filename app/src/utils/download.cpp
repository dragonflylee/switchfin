#include "utils/download.hpp"
#include "utils/config.hpp"
#include "utils/thread.hpp"
#include "utils/misc.hpp"
#include "api/jellyfin.hpp"
#include "view/mpv_core.hpp"

namespace {

void removeDirAsync(const std::string& dir) {
    brls::async([dir]() {
        try {
            if (fs::exists(dir)) fs::remove_all(dir);
        } catch (const std::exception& e) {
            brls::Logger::error("Failed to remove download dir: {}", e.what());
        }
    });
}

std::string extensionOf(const std::string& path, const std::string& fallback) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return fallback;
    std::string ext = path.substr(dot + 1);
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return ext;
}

}  // namespace

std::string DownloadManager::downloadDir() const { return AppConfig::instance().configDir() + "/downloads"; }

void DownloadManager::init() {
    this->loadIndex();

    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& item : this->items) {
        if (item.status == DownloadStatus::Downloading) item.status = DownloadStatus::Queued;
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
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        for (auto& existing : this->items) {
            if (existing.itemId == itemId && existing.status != DownloadStatus::Failed &&
                existing.status != DownloadStatus::Cancelled) {
                brls::Logger::info("Download already queued: {}", itemId);
                return;
            }
        }
    }

    jellyfin::getJSON<jellyfin::Episode>(
        [this, quality](const jellyfin::Episode& item) {
            {
                std::lock_guard<std::mutex> lock(this->mutex);

                auto dl = std::find_if(this->items.begin(), this->items.end(),
                    [&item](const DownloadItem& existing) { return existing.itemId == item.Id; });
                if (dl == this->items.end()) {
                    this->items.emplace_back();
                    dl = std::prev(this->items.end());
                }

                dl->itemId = item.Id;
                dl->name = item.Name;
                dl->type = item.Type;
                dl->seriesName = item.SeriesName;
                dl->seasonIndex = item.ParentIndexNumber;
                dl->episodeIndex = item.IndexNumber;
                dl->productionYear = item.ProductionYear;
                dl->runTimeTicks = item.RunTimeTicks;
                dl->quality = quality;
                dl->status = DownloadStatus::Queued;
                dl->errorMessage.clear();
                dl->filePath.clear();
                dl->downloadedBytes = 0;
                dl->totalBytes = 0;
                dl->pendingRemove = false;
                if (item.SeriesId.is_string()) dl->seriesId = item.SeriesId.get<std::string>();
                if (!item.MediaSources.empty()) dl->mediaSourceId = item.MediaSources.front().Id;

                auto primaryTag = item.ImageTags.find(jellyfin::imageTypePrimary);
                if (primaryTag != item.ImageTags.end()) dl->imagePrimaryTag = primaryTag->second;

                this->saveIndex();
            }
            brls::Logger::info("Download queued: {}", item.Name);
            this->processQueue();
        },
        [](const std::string& ex) { brls::Application::notify(ex); }, jellyfin::apiUserItem,
        AppConfig::instance().getUserId(), itemId);
}

void DownloadManager::resumeQueue() { this->processQueue(); }

void DownloadManager::cancelDownload(const std::string& itemId) {
    DownloadStatus result = DownloadStatus::NotFound;
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(this->mutex);

        auto it = this->cancels.find(itemId);
        if (it != this->cancels.end()) {
            it->second->store(true);
            active = true;
        }

        for (auto d = this->items.begin(); d != this->items.end(); ++d) {
            if (d->itemId == itemId && d->status == DownloadStatus::Queued) {
                d = this->items.erase(d);
                this->saveIndex();
                result = DownloadStatus::Cancelled;
                break;
            }
        }
    }

    // Fire outside the lock: subscribers re-enter findItem(), which locks it.
    if (!active) this->statusEvent.fire(itemId, result);
}

void DownloadManager::removeDownload(const std::string& itemId) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(this->mutex);

        auto it = this->cancels.find(itemId);
        if (it != this->cancels.end()) {
            for (auto& item : this->items) {
                if (item.itemId == itemId) {
                    item.pendingRemove = true;
                    break;
                }
            }
            it->second->store(true);
        } else {
            for (auto d = this->items.begin(); d != this->items.end(); ++d) {
                if (d->itemId == itemId) {
                    this->items.erase(d);
                    this->saveIndex();
                    removed = true;
                    break;
                }
            }
        }
    }

    if (removed) {
        removeDirAsync(this->downloadDir() + "/" + itemId);
        this->statusEvent.fire(itemId, DownloadStatus::NotFound);
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
    const std::string& server = conf.getUrl();
    const std::string& token = conf.getToken();

    if (item.quality == DownloadQuality::Original) {
        std::string query = HTTP::encode_form({{"api_key", token}});
        return server + fmt::format(fmt::runtime(jellyfin::apiDownload), item.itemId, query);
    }

    struct Preset {
        DownloadQuality quality;
        const char* bitrate;
        const char* height;
    };
    static const Preset presets[] = {
        {DownloadQuality::Q1080p, "4000000", "1080"},
        {DownloadQuality::Q720p, "2000000", "720"},
        {DownloadQuality::Q480p, "1000000", "480"},
    };

    for (const auto& p : presets) {
        if (p.quality == item.quality) {
            std::string query = HTTP::encode_form({
                {"static", "false"},
                {"mediaSourceId", item.mediaSourceId.empty() ? item.itemId : item.mediaSourceId},
                {"videoCodec", MPVCore::VIDEO_CODEC},
                {"audioCodec", "aac"},
                {"maxStreamingBitrate", p.bitrate},
                {"maxHeight", p.height},
                {"api_key", token},
            });
            return server + fmt::format(fmt::runtime(jellyfin::apiStream), item.itemId, query);
        }
    }
    return "";
}

// The event bus and the screen-dim API must run outside `mutex`: subscribers
// re-enter findItem() and would otherwise self-deadlock the caller's thread.
void DownloadManager::processQueue() {
    std::string itemId;
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (!this->cancels.empty()) return;

        for (auto& item : this->items) {
            if (item.status != DownloadStatus::Queued) continue;

            item.status = DownloadStatus::Downloading;
            this->cancels[item.itemId] = std::make_shared<std::atomic_bool>(false);
            itemId = item.itemId;
            this->saveIndex();
            break;
        }
    }

    if (itemId.empty()) {
        if (MPVCore::instance().isStopped()) {
            brls::Application::getPlatform()->disableScreenDimming(false, "Downloading");
        }
        return;
    }

    brls::Application::getPlatform()->disableScreenDimming(true, "Downloading");
    this->statusEvent.fire(itemId, DownloadStatus::Downloading);
    this->doDownload(itemId);
}

void DownloadManager::doDownload(const std::string& itemId) {
    std::string imagePrimaryTag;
    DownloadQuality quality = DownloadQuality::Original;
    std::string url;
    HTTP::Cancel cancel;
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        auto it = std::find_if(this->items.begin(), this->items.end(),
            [&itemId](const DownloadItem& item) { return item.itemId == itemId; });
        if (it == this->items.end()) return;

        imagePrimaryTag = it->imagePrimaryTag;
        quality = it->quality;
        url = this->buildDownloadUrl(*it);

        auto c = this->cancels.find(itemId);
        cancel = c == this->cancels.end() ? std::make_shared<std::atomic_bool>(false) : c->second;
    }
    std::string itemDir = this->downloadDir() + "/" + itemId;

    ThreadPool::instance().submit([this, itemId, imagePrimaryTag, quality, url, itemDir, cancel](HTTP&) {
        auto& conf = AppConfig::instance();
        std::string server = conf.getUrl();
        std::string detailUrl = server + fmt::format(fmt::runtime(jellyfin::apiUserItem), conf.getUserId(), itemId);
        auto finish = [this, itemId, itemDir](DownloadStatus status, const std::string& error) {
            brls::sync([this, itemId, itemDir, status, error]() {
                bool removed = false;
                {
                    std::lock_guard<std::mutex> lock(this->mutex);

                    for (auto it = this->items.begin(); it != this->items.end(); ++it) {
                        if (it->itemId == itemId) {
                            if (it->pendingRemove) {
                                removed = true;
                                this->items.erase(it);
                            } else {
                                it->status = status;
                                it->errorMessage = error;
                            }
                            break;
                        }
                    }
                    this->cancels.erase(itemId);
                    this->saveIndex();
                }

                // Fire outside the lock: subscribers re-enter findItem().
                this->statusEvent.fire(itemId, removed ? DownloadStatus::NotFound : status);
                if (removed) removeDirAsync(itemDir);

                this->processQueue();
            });
        };

        if (!cancel->load()) {
            try {
                if (!fs::exists(itemDir)) fs::create_directories(itemDir);
            } catch (const std::exception& e) {
                brls::Logger::error("Failed to create download dir: {}", e.what());
                finish(DownloadStatus::Failed, e.what());
                return;
            }
        }

        HTTP::Header header = {AppConfig::instance().getAuth(AppConfig::instance().getToken())};

        jellyfin::Detail detail;
        if (!cancel->load()) {
            try {
                auto resp = HTTP::get(detailUrl, header, HTTP::Timeout{});
                if (!resp.empty()) detail = nlohmann::json::parse(resp).get<jellyfin::Detail>();
            } catch (const std::exception& e) {
                brls::Logger::warning("Failed to fetch item detail for {}: {}", itemId, e.what());
            }
        }

        std::string ext = "mp4";
        if (quality == DownloadQuality::Original && !detail.MediaSources.empty()) {
            ext = extensionOf(detail.MediaSources.front().Path, "mp4");
        }

        std::string fileName = "video." + ext;
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
                std::string thumbUrl = fmt::format("{}/Items/{}/Images/Primary?format=Png&{}", server, itemId,
                    HTTP::encode_form({{"tag", imagePrimaryTag}, {"maxWidth", "300"}}));
                HTTP::download(thumbUrl, itemDir + "/thumb.png", HTTP::Timeout{}, cancel);
            } catch (const std::exception& e) {
                std::error_code ec;
                fs::remove(itemDir + "/thumb.png", ec);
                brls::Logger::warning("Failed to download thumbnail: {}", e.what());
            }
        }

        if (!cancel->load()) {
            for (const auto& src : detail.MediaSources) {
                if (cancel->load()) break;
                for (const auto& stream : src.MediaStreams) {
                    if (stream.Type != jellyfin::streamTypeSubtitle) continue;
                    std::string subUrl = misc::buildSubtitleUrl(
                        server, itemId, src.Id, stream.Index, stream.Codec, stream.IsExternal, stream.DeliveryUrl);
                    if (subUrl.empty()) continue;

                    std::string subFileName = fmt::format("sub_{}.{}", stream.Index, misc::codec2Ext(stream.Codec));
                    try {
                        HTTP::download(subUrl, itemDir + "/" + subFileName, HTTP::Timeout{}, cancel);
                        brls::Logger::info("Downloaded subtitle: {}", subFileName);
                    } catch (const std::exception& e) {
                        brls::Logger::warning("Failed to download subtitle stream {}: {}", stream.Index, e.what());
                    }
                }
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

        std::string error;
        {
            try {
                std::ofstream of(itemDir + "/" + fileName, std::ios::binary);
                if (!of) throw std::runtime_error("Failed to open file for writing");

                HTTP s;
                HTTP::set_option(s, header, cancel, progressCb);
                s._get(url, &of);
                of.close();
            } catch (const std::exception& ex) {
                error = ex.what();
                brls::Logger::error("Download failed: {} - {}", itemId, error);
            }
        }

        if (cancel->load()) {
            finish(DownloadStatus::Cancelled, "");
            return;
        }
        if (!error.empty()) {
            finish(DownloadStatus::Failed, error);
            return;
        }

        brls::Logger::info("Download completed: {}", itemId);
        finish(DownloadStatus::Completed, "");
    });
}
