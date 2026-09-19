#include "utils/download.hpp"
#ifdef PS5_NATIVE_GPU
#include <cstdio>
#endif
#include "utils/config.hpp"
#include "utils/thread.hpp"
#include "utils/misc.hpp"
#include "api/jellyfin.hpp"
#include "view/mpv_core.hpp"
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_download_removal.hpp"
#include "utils/ps5_download_file.hpp"
#include "utils/ps5_download_journal.hpp"
#define PS5_DOWNLOAD_NOTE(...) ps5::downloads::note(__VA_ARGS__)
#include "utils/ps5_native_http_diagnostics.hpp"
#define PS5_DOWNLOAD_NET(id, s) ps5::downloads::noteNetwork((id), (s).result, (s).os_error, \
    static_cast<unsigned>((s).request_stage), static_cast<unsigned>((s).error_stage), (s).error_buffer, \
    (s).valid, (s).http_status, static_cast<uint64_t>((s).lookup_us), \
    static_cast<uint64_t>((s).connect_us), static_cast<uint64_t>((s).total_us))
#define PS5_DOWNLOAD_FILE_FAULT(id, f) ps5::downloads::noteFile((id), (f).error, (f).stream, \
    (f).offset, (f).requested, (f).written)
#include "utils/ps5_download_index_scan.hpp"
#define PS5_DOWNLOAD_INDEX_SCAN(dir) this->recordIndexScan(dir)
#include "utils/ps5_storage_home.hpp"
#include <sys/stat.h>
// Record whether the writable download root was promoted.
#define PS5_DOWNLOAD_HOME() do { const auto& r = ps5::storage::state(); \
    ps5::downloads::noteHome(r.unjail, r.probeResult, r.probeErrno, r.elevated); } while (0)
#define PS5_DOWNLOAD_REPAIR() this->repairPermissions()
#ifndef PS5_DOWNLOAD_REPAIR
#define PS5_DOWNLOAD_REPAIR() ((void)0)
#endif
#endif

#ifdef PS5_NATIVE_GPU
std::string DownloadManager::indexDir() const { return AppConfig::instance().configDir() + "/downloads"; }

std::string DownloadManager::downloadDir() const {
    // The elevated home once /data was probed writable, else the sandbox path.
    return ps5::storage::downloadHome(this->indexDir());
}

std::string DownloadManager::itemDir(const DownloadItem& item) const {
    // An empty storageRoot is a pre-storageRoot legacy download; those live in
    // the sandbox, so resolve to the sandbox index dir (rebased when promoted)
    // rather than the active download dir, which is /data when elevated and
    // would send playback/removal to the wrong place.
    std::string root = item.storageRoot.empty() ? this->indexDir() : item.storageRoot;
    // A root stored before the sandbox was rebased is a bare /download0 path,
    // dead once promoted; map it onto the real sandbox root so old downloads
    // stay reachable (and removable) after the un-chroot.
    if (!ps5::storage::sandboxRoot().empty() && root.rfind("/download0", 0) == 0)
        root = ps5::storage::sandboxRoot() + root;
    return root + "/" + item.itemId;
}
#else
std::string DownloadManager::downloadDir() const { return AppConfig::instance().configDir() + "/downloads"; }
#endif

void DownloadManager::init() {
    auto dir = this->downloadDir();
#ifdef PS5_NATIVE_GPU
    // Directory setup must never abort startup: a read-only or vanished path
    // (a promotion that outran a rebase, say) degrades to "downloads
    // unavailable", not the fatal hold the throwing overloads would trigger.
    std::error_code dirEc;
    if (!fs::exists(dir, dirEc)) fs::create_directories(dir, dirEc);
    if (this->indexDir() != dir && !fs::exists(this->indexDir(), dirEc))
        fs::create_directories(this->indexDir(), dirEc);
    // Finish interrupted removals (.switchfin-removing-*) in both roots so a
    // failed download does not strand gigabytes; best effort, never fatal.
    {
        std::string sweepRoots[] = {this->downloadDir(), this->indexDir()};
        for (const std::string& sweepRoot : sweepRoots) {
            if (DIR* d = ::opendir(sweepRoot.c_str())) {
                std::vector<std::string> retired;
                while (const dirent* e = ::readdir(d)) {
                    const std::string n = e->d_name;
                    if (ps5::downloads::isRetiredDirectory(n)) retired.push_back(sweepRoot + "/" + n);
                }
                ::closedir(d);
                for (const auto& r : retired) ps5::downloads::removeTree(r);
            }
        }
#else
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
#endif
    }
#ifdef PS5_NATIVE_GPU
    PS5_DOWNLOAD_HOME();
#endif
    this->loadIndex();

    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& item : this->items) {
        if (item.status == DownloadStatus::Downloading) {
            item.status = DownloadStatus::Queued;
        }
    }
#ifdef PS5_NATIVE_GPU
    this->reconcileOrphans();
#endif
    this->saveIndex();
#ifdef PS5_NATIVE_GPU
    PS5_DOWNLOAD_INDEX_SCAN(dir);
    PS5_DOWNLOAD_REPAIR();
    // Diagnostic: re-scan the download dir AFTER the permission repair (root=2)
    // to learn whether chmod restores opendir in the sandbox, where the browser
    // lists empty. Best effort; the journal already bounds its own rows.
    {
        const auto postRepair = ps5::downloads::scanDirectory(dir);
        ps5::downloads::noteRoot(2, postRepair.result, postRepair.error, postRepair.entries, postRepair.directories);
    }
}

// Must be called with the mutex held. opendir on the app's own directories
// returned EPERM -- the read bit was cleared at mkdir time -- so Local Storage
// could not enumerate them although writing and stat worked. The app owns the
// directories, so chmod restores read+search. Failures are ignored: this only
// improves listing and must never disturb a download.
void DownloadManager::repairPermissions() const {
    ::chmod(AppConfig::instance().configDir().c_str(), 0755);
    ::chmod(this->indexDir().c_str(), 0755);
    ::chmod(this->downloadDir().c_str(), 0755);
    for (const auto& item : this->items) ::chmod(this->itemDir(item).c_str(), 0755);
    // Files are created 0600, so a different-uid run mode (promoted vs sandboxed)
    // cannot read them -- the download shows without title/artwork/size. Relax to
    // 0644; effective for files the current uid owns, so a no-daemon launch heals
    // the items it created and a promoted launch heals its own. New files are
    // already created 0644 by File::commit.
    ::chmod((this->indexDir() + "/index.json").c_str(), 0644);
    for (const auto& item : this->items) {
        const std::string entryDir = this->itemDir(item);
        if (DIR* dir = ::opendir(entryDir.c_str())) {
            while (const dirent* entry = ::readdir(dir)) {
                const std::string name = entry->d_name;
                if (name == "." || name == "..") continue;
                ::chmod((entryDir + "/" + name).c_str(), 0644);
            }
            ::closedir(dir);
        }
    }
}

// Must be called with the mutex held, once, from init(). Bounded by the
// journal's own row budget as well as this one: a large index must not spend
// the rows a download attempt will need later in the same session.
void DownloadManager::recordIndexScan(const std::string& dir) const {
    constexpr size_t entryLimit = 32;
    const auto config = AppConfig::instance().configDir();
    const auto configScan = ps5::downloads::scanDirectory(config);
    ps5::downloads::noteRoot(0, configScan.result, configScan.error, configScan.entries,
                             configScan.directories);
    const auto downloadScan = ps5::downloads::scanDirectory(dir);
    ps5::downloads::noteRoot(1, downloadScan.result, downloadScan.error, downloadScan.entries,
                             downloadScan.directories);
    size_t scanned = 0;
    for (const auto& item : this->items) {
        if (scanned++ >= entryLimit) break;
        const auto scan = ps5::downloads::scanIndexEntry(dir, item.itemId, item.filePath);
        ps5::downloads::noteScan(item.itemId, static_cast<int>(item.status), scan.result, scan.error,
                                 scan.size, scan.named);
    }
}

void DownloadManager::writeMetadata(const DownloadItem& item) const {
    std::string dir = this->itemDir(item);
    try {
        if (!fs::exists(dir)) fs::create_directories(dir);
        nlohmann::json j = item;
        ps5::downloads::File<> f(dir + "/metadata.json");
        f.stream() << j.dump(2);
        f.commit();
    } catch (const std::exception& e) {
        brls::Logger::error("Failed to write metadata for {}: {}", item.itemId, e.what());
    }
}

void DownloadManager::reconcileOrphans() {
    // A download directory is created and filled long before anything writes a
    // metadata.json into it, and index.json is only rewritten at status
    // transitions -- so an interrupted download used to leave a directory that
    // nothing knew about. It could not be listed, resumed or deleted from the
    // UI, only found with a file browser. One was: 1.41 GB of video.mkv with a
    // thumbnail, no metadata.json and no index entry.
    //
    // addDownload now writes metadata.json at queue time, so a directory is
    // self-describing from the moment it exists. This handles both that case and
    // the ones already on disk from before, and it is also the repair path if
    // index.json is lost while the directories survive.
    size_t adopted = 0, orphaned = 0;
    // A download made before the home moved still lives where it was written,
    // so both the current home and the sandbox index dir are scanned and each
    // adopted item is pinned to the directory it was actually found in.
    std::vector<std::string> roots = {this->downloadDir()};
    if (this->indexDir() != this->downloadDir()) roots.push_back(this->indexDir());
    for (const std::string& base : roots) {
    std::error_code ec;
    fs::directory_iterator it(base, ec), end;
    if (ec) continue;

    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_directory(ec) || ec) continue;

        std::string id = it->path().filename().string();
        if (ps5::downloads::isRetiredDirectory(id)) continue;
        bool known = false;
        for (auto& item : this->items) {
            if (item.itemId == id) {
                known = true;
                break;
            }
        }
        if (known) continue;

        DownloadItem dl;
        bool loaded = false;
        try {
            // .string() rather than handing the path straight to ifstream: under
            // -DUSE_BOOST_FILESYSTEM `fs` is boost's, whose path has no
            // conversion the stream constructors accept.
            std::ifstream f((it->path() / "metadata.json").string());
            if (f) {
                dl = nlohmann::json::parse(f).get<DownloadItem>();
                loaded = true;
            }
        } catch (const std::exception& e) {
            brls::Logger::warning("Damaged metadata for {}: {}", id, e.what());
        }

        if (loaded) {
            // The directory name is the authority on the id; metadata.json is
            // whatever happened to be written last.
            dl.itemId = id;
            if (dl.status == DownloadStatus::Downloading) dl.status = DownloadStatus::Queued;
            // Completed only survives if the file it claims is actually there.
            if (dl.status == DownloadStatus::Completed &&
                (dl.filePath.empty() || !fs::exists(it->path() / dl.filePath))) {
                dl.status = DownloadStatus::Failed;
                dl.errorMessage = "file missing";
            }
            adopted++;
        } else {
            // No usable metadata. Surface it anyway, failed, so that it can at
            // least be seen and deleted -- which is the whole point.
            dl = DownloadItem{};
            dl.itemId = id;
            dl.name = id;
            dl.status = DownloadStatus::Failed;
            dl.errorMessage = "orphaned";
            orphaned++;
        }
        dl.storageRoot = base;
        this->items.push_back(dl);
    }
    }

    if (adopted || orphaned)
        brls::Logger::info("Downloads: adopted {} directory(ies), {} without metadata", adopted, orphaned);
#endif
}

void DownloadManager::loadIndex() {
#ifdef PS5_NATIVE_GPU
    std::string path = this->indexDir() + "/index.json";
    // std::ifstream reads the sandbox index back empty once promoted (via the
    // /mnt path); read with raw stdio, which binds on this image like the logs.
    std::string data;
    if (FILE* fp = std::fopen(path.c_str(), "rb")) {
        char buf[8192];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, fp)) > 0;) data.append(buf, n);
        std::fclose(fp);
    }
    if (data.empty()) return;
#else
    std::string path = this->downloadDir() + "/index.json";
    if (!fs::exists(path)) return;

#endif
    try {
#ifdef PS5_NATIVE_GPU
        this->items = nlohmann::json::parse(data).get<std::vector<DownloadItem>>();
#else
        std::ifstream f(path);
        nlohmann::json j = nlohmann::json::parse(f);
        this->items = j.get<std::vector<DownloadItem>>();
#endif
    } catch (const std::exception& e) {
        brls::Logger::error("Failed to load download index: {}", e.what());
    }
}

void DownloadManager::saveIndex() {
#ifdef PS5_NATIVE_GPU
    std::string path = this->indexDir() + "/index.json";
#else
    std::string path = this->downloadDir() + "/index.json";
#endif
    try {
        nlohmann::json j = this->items;
#ifdef PS5_NATIVE_GPU
        // The atomic temp+rename cannot create a sibling in the sandbox downloads
        // dir once promoted (mkstemp -> EACCES), so write index.json in place with
        // raw stdio (rebuildable via reconcileOrphans, so non-atomic is acceptable).
        const std::string dump = j.dump(2);
        FILE* fp = std::fopen(path.c_str(), "wb");
        if (!fp) throw std::runtime_error("open index for write failed");
        const size_t wrote = std::fwrite(dump.data(), 1, dump.size(), fp);
        std::fclose(fp);
        if (wrote != dump.size()) throw std::runtime_error("short index write");
#else
        std::ofstream f(path);
        f << j.dump(2);
#endif
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
#ifdef PS5_NATIVE_GPU
            if (!ps5::downloads::isItemComponent(item.Id)) return;
            // Concurrent detail requests can complete after the initial check.
            for (const auto& existing : this->items)
                if (existing.itemId == item.Id) return;
#endif

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
#ifdef PS5_NATIVE_GPU
            dl.storageRoot = this->downloadDir();
            PS5_DOWNLOAD_NOTE(dl.itemId, ps5::downloads::DownloadJournal::Queued);
#endif
            if (item.SeriesId.is_string()) dl.seriesId = item.SeriesId.get<std::string>();
            for (auto& src : item.MediaSources) dl.filePath = src.Name;

            auto primaryTag = item.ImageTags.find(jellyfin::imageTypePrimary);
            if (primaryTag != item.ImageTags.end()) dl.imagePrimaryTag = primaryTag->second;

            this->items.push_back(dl);
            this->saveIndex();
#ifdef PS5_NATIVE_GPU
            // Before any bytes arrive, so the directory describes itself from
            // the moment it exists. Written again on completion, when filePath
            // and the byte counts are finally known.
            this->writeMetadata(dl);
#endif
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
        brls::sync([this, itemId]() { this->statusEvent.fire(itemId, DownloadStatus::Failed); });
    }
}

void DownloadManager::removeDownload(const std::string& itemId) {
#ifdef PS5_NATIVE_GPU
#else
    bool wasActive = false;
#endif
    bool erased = false;
#ifdef PS5_NATIVE_GPU
    bool failed = false;
    std::string failStorageRoot;
#endif
    {
        std::lock_guard<std::mutex> lock(this->mutex);
#ifdef PS5_NATIVE_GPU
#else

#endif
        for (auto& item : this->items) {
            if (item.itemId == itemId && item.status == DownloadStatus::Downloading && this->currentCancel) {
                this->currentCancel->store(true);
                item.errorMessage = "removed";
#ifdef PS5_NATIVE_GPU
                return; // The worker must close its files before retirement.
#else
                wasActive = true;
                break;
#endif
            }
        }
#ifdef PS5_NATIVE_GPU
        if (this->retirePS5Download(itemId)) {
#else

        if (!wasActive) {
#endif
            for (auto it = this->items.begin(); it != this->items.end(); ++it) {
                if (it->itemId == itemId) {
                    this->items.erase(it);
                    erased = true;
                    break;
                }
            }
            this->saveIndex();
#ifdef PS5_NATIVE_GPU
        } else {
            failed = true;
            for (const auto& item : this->items)
                if (item.itemId == itemId) { failStorageRoot = item.storageRoot; break; }
#endif
        }
    }
#ifdef PS5_NATIVE_GPU
    if (erased) brls::sync([this, itemId]() { this->statusEvent.fire(itemId, DownloadStatus::Failed); });
    if (failed) {
        // A sandbox download cannot be removed while promoted: the sandbox pfs
        // denies inode changes in downloads/ even as root (mkdir and unlink both
        // return EACCES). Say so, instead of a generic failure. /data downloads
        // (root-owned) delete normally.
        if (ps5::storage::promoted() && failStorageRoot.find("/data") == std::string::npos)
            brls::Application::notify("This download is stored in the sandbox; remove it with Switchfin launched without the unjail daemon.");
        else
            brls::Application::notify("Could not remove download");
#else

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
        brls::sync([this, itemId]() { this->statusEvent.fire(itemId, DownloadStatus::Failed); });
#endif
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
#ifdef PS5_NATIVE_GPU
            return this->itemDir(item) + "/" + item.filePath;
#else
            return this->downloadDir() + "/" + itemId + "/" + item.filePath;
#endif
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
#ifdef PS5_NATIVE_GPU
        // Jellyfin 12 rejects api_key in the query (401, empty body). Every
        // request already carries Authorization: MediaBrowser Token="...".
        (void)token;
        return server + fmt::format(fmt::runtime(jellyfin::apiDownload), item.itemId, "");
#else
        return server +
               fmt::format(fmt::runtime(jellyfin::apiDownload), item.itemId, HTTP::encode_form({{"api_key", token}}));
#endif
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
            brls::Application::getPlatform()->disableScreenDimming(true, "Downloading");
            this->doDownload(item);
            return;
        }
    }

    auto& mpv = MPVCore::instance();
    if (mpv.isStopped()) brls::Application::getPlatform()->disableScreenDimming(false, "Downloading");
}

#ifdef PS5_NATIVE_GPU
bool DownloadManager::retirePS5Download(const std::string& itemId) {
    std::string retired;
    std::string root = this->indexDir();   // empty storageRoot = legacy sandbox item
    for (const auto& item : this->items)
        if (item.itemId == itemId && !item.storageRoot.empty()) { root = item.storageRoot; break; }
    // Same rebase as itemDir(): a legacy /download0 root is dead once promoted.
    if (!ps5::storage::sandboxRoot().empty() && root.rfind("/download0", 0) == 0)
        root = ps5::storage::sandboxRoot() + root;
    const auto result = ps5::downloads::retireDirectory(root, itemId, retired);
    if (result == ps5::downloads::Retirement::Failed) {
        // Captured in application.log (rebased under the sandbox when promoted).
        const int retireErrno = errno;
        brls::Logger::error("Could not remove {}: retire failed at {} errno={}", itemId, root, retireErrno);
        // Retire needs mkdir inside downloads/, which the sandbox pfs denies when
        // promoted (EACCES even as root). Try deleting the item directory in
        // place: this needs only unlink within the item dir + rmdir of the item
        // dir. Preserve the failing syscall errno when removal is also denied.
        const std::string inPlace = root + "/" + itemId;
        errno = 0;
        if (ps5::downloads::removeTree(inPlace)) {
            brls::Logger::info("Removed {} in place (promoted; retire unavailable)", itemId);
            return true;
        }
        brls::Logger::error("In-place remove of {} failed at {} errno={}", itemId, inPlace, errno);
        return false;
    }
    if (result == ps5::downloads::Retirement::Moved) {
        try {
            brls::async([retired = std::move(retired)]() {
                try {
                    if (!ps5::downloads::removeTree(retired))
                        brls::Logger::error("Failed to clean retired download");
                } catch (const std::exception& e) {
                    brls::Logger::error("Failed to clean retired download: {}", e.what());
                }
            });
        } catch (const std::exception& e) {
            // The original path is already retired. Preserve those files if
            // cleanup cannot be scheduled; never delete a newly re-added item.
            brls::Logger::error("Could not schedule retired download cleanup: {}", e.what());
        }
    }
    return true;
}

void DownloadManager::finishPS5Download(const std::string& itemId, const std::string& fileName,
    const std::shared_ptr<std::atomic_bool>& cancel, bool success, int64_t completedBytes, const std::string& error) {
    brls::sync([this, itemId, fileName, success, error, cancel, completedBytes]() {
        DownloadStatus finalStatus = DownloadStatus::Failed;

        {
            std::lock_guard<std::mutex> lock(this->mutex);

            if (cancel->load()) {
                for (auto it = this->items.begin(); it != this->items.end(); ++it) {
                    if (it->itemId == itemId) {
                        if (it->errorMessage == "removed") {
                            if (this->retirePS5Download(itemId)) {
                                this->items.erase(it);
                            } else {
                                it->status = DownloadStatus::Failed;
                                it->errorMessage = "Could not remove download";
                            }
                        } else {
                            it->status = DownloadStatus::Failed;
                            it->errorMessage = "Cancelled";
                        }
                        break;
                    }
                }
                this->saveIndex();
            } else if (success) {
                finalStatus = DownloadStatus::Completed;
                for (auto& item : this->items) {
                    if (item.itemId == itemId) {
                        item.status = DownloadStatus::Completed;
                        item.filePath = fileName;
                        item.downloadedBytes = completedBytes;
                        item.totalBytes = completedBytes;
                        item.errorMessage.clear();
                        this->writeMetadata(item);
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

        PS5_DOWNLOAD_NOTE(itemId,
            finalStatus == DownloadStatus::Completed ? ps5::downloads::DownloadJournal::Completed
                : cancel->load() ? ps5::downloads::DownloadJournal::Cancelled
                                 : ps5::downloads::DownloadJournal::Failed,
            0, static_cast<uint64_t>(completedBytes < 0 ? 0 : completedBytes));
        this->statusEvent.fire(itemId, finalStatus);
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->processQueue();
        }
    });
}

#endif
// Must be called with mutex held. Copies what it needs, then releases via async.
void DownloadManager::doDownload(DownloadItem& item) {
    item.status = DownloadStatus::Downloading;
#ifdef PS5_NATIVE_GPU
    PS5_DOWNLOAD_NOTE(item.itemId, ps5::downloads::DownloadJournal::Started);
#endif

    std::string itemId = item.itemId;
    std::string imagePrimaryTag = item.imagePrimaryTag;
    DownloadQuality quality = item.quality;
    std::string url = this->buildDownloadUrl(item);
#ifdef PS5_NATIVE_GPU
    std::string itemDir = this->itemDir(item);
#else
    std::string itemDir = this->downloadDir() + "/" + itemId;
#endif

    this->saveIndex();

    auto cancel = std::make_shared<std::atomic_bool>(false);
    this->currentCancel = cancel;

    brls::sync([this, itemId]() { this->statusEvent.fire(itemId, DownloadStatus::Downloading); });

#ifdef PS5_NATIVE_GPU
    ThreadPool::instance().submit([this, itemId, imagePrimaryTag, quality, url, itemDir, cancel](HTTP& s) {
        auto resetQueue = [this, itemId, cancel](const std::string& error) {
            this->finishPS5Download(itemId, "", cancel, false, 0, error);
#else
    ThreadPool::instance().submit([this, itemId, imagePrimaryTag, quality, url, itemDir, cancel](HTTP&) {
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
#endif
        };

        try {
            if (!fs::exists(itemDir)) fs::create_directories(itemDir);
#ifdef PS5_NATIVE_GPU
            PS5_DOWNLOAD_NOTE(itemId, ps5::downloads::DownloadJournal::DirectoryReady);
#endif
        } catch (const std::exception& e) {
            brls::Logger::error("Failed to create download dir: {}", e.what());
#ifdef PS5_NATIVE_GPU
            PS5_DOWNLOAD_NOTE(itemId, ps5::downloads::DownloadJournal::Failed, 1);
#endif
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
#ifdef PS5_NATIVE_GPU
                HTTP::download(thumbUrl, itemDir + "/thumb.png", header, cancel, HTTP::Timeout{});
#else
                HTTP::download(thumbUrl, itemDir + "/thumb.png", HTTP::Timeout{});
#endif
            } catch (const std::exception& e) {
#ifdef PS5_NATIVE_GPU
#else
                fs::remove(itemDir + "/thumb.png");
#endif
                brls::Logger::warning("Failed to download thumbnail: {}", e.what());
            }
        }

        if (!cancel->load()) {
            try {
                std::string subUrl = fmt::format(fmt::runtime(jellyfin::apiUserItem), conf.getUserId(), itemId);
                auto resp = HTTP::get(conf.getUrl() + subUrl, header, HTTP::Timeout{});
                auto detail = nlohmann::json::parse(resp).get<jellyfin::Detail>();
                for (const auto& src : detail.MediaSources) {
                    for (const auto& stream : src.MediaStreams) {
#ifdef PS5_NATIVE_GPU
                        if (cancel->load()) break;
#endif
                        if (stream.Type != jellyfin::streamTypeSubtitle) continue;
                        std::string subUrl = misc::buildSubtitleUrl(conf.getUrl(), itemId, src.Id, stream.Index,
                            stream.Codec, stream.IsExternal, stream.DeliveryUrl);
                        if (subUrl.empty()) continue;
                        std::string subFileName = fmt::format("sub_{}.{}", stream.Index, misc::codec2Ext(stream.Codec));
                        try {
#ifdef PS5_NATIVE_GPU
                            HTTP::download(subUrl, itemDir + "/" + subFileName, header, cancel, HTTP::Timeout{});
#else
                            HTTP::download(subUrl, itemDir + "/" + subFileName, HTTP::Timeout{});
#endif
                            brls::Logger::info("Downloaded subtitle: {}", subFileName);
                        } catch (const std::exception& e) {
#ifdef PS5_NATIVE_GPU
                            brls::Logger::warning("Failed to download subtitle stream {}", stream.Index);
#else
                            brls::Logger::warning("Failed to download subtitle stream {}: {}", stream.Index, e.what());
#endif
                        }
                    }
                }
            } catch (const std::exception& e) {
#ifdef PS5_NATIVE_GPU
                brls::Logger::warning("Failed to fetch item subtitles for download");
#else
                brls::Logger::warning("Failed to fetch item subtitles for download: {}", e.what());
#endif
            }
        }

        auto lastProgress = std::make_shared<std::chrono::steady_clock::time_point>();
#ifdef PS5_NATIVE_GPU
        // Set once the try block below creates the .part file, so the progress
        // callback can pre-allocate it to the full Content-Length the first time
        // the total is known (flattens the exFAT O(filesize) append cost).
        auto downloadFile = std::make_shared<ps5::downloads::File<>*>(nullptr);
        HTTP::Progress::Callback progressCb = [this, itemId, lastProgress, downloadFile](curl_off_t total, curl_off_t now) {
#else
        HTTP::Progress::Callback progressCb = [this, itemId, lastProgress](curl_off_t total, curl_off_t now) {
#endif
            auto tp = std::chrono::steady_clock::now();
#ifdef PS5_NATIVE_GPU
            if (*downloadFile && total > 0) {
                const auto a0 = std::chrono::steady_clock::now();
                if ((*downloadFile)->preallocate(static_cast<uint64_t>(total))) {
                    const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - a0).count();
                    brls::Logger::info("[download] {} preallocate {} MiB in {:.0f} ms", itemId,
                        static_cast<long long>(total >> 20), ms);
                }
            }
            auto prev = *lastProgress;
            if (tp - prev < std::chrono::seconds(1)) return;
#else
            if (tp - *lastProgress < std::chrono::seconds(1)) return;
#endif
            *lastProgress = tp;

#ifdef PS5_NATIVE_GPU
            PS5_DOWNLOAD_NOTE(itemId, ps5::downloads::DownloadJournal::TransferProgress, 0,
                static_cast<uint64_t>(now < 0 ? 0 : now), static_cast<uint64_t>(total < 0 ? 0 : total));
#endif
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

#ifdef PS5_NATIVE_GPU
#else
        bool cancelled = false;
#endif
        bool success = false;
        std::string error;
#ifdef PS5_NATIVE_GPU
        // Not a pointer to either object: unwinding destroys both before any
        // handler runs, which is otherwise leaves a freed libcurl handle.
        long transferStatus = -1;
        int64_t completedBytes = 0;
#endif

#ifdef PS5_NATIVE_GPU
        // Records whether a bearer header is attached, never the token itself.
        const bool authorized = !header.empty() && header.front().find("Token=\"") != std::string::npos;
        PS5_DOWNLOAD_NOTE(itemId, ps5::downloads::DownloadJournal::TransferBegin, authorized ? 1 : 0);
#endif
        try {
#ifdef PS5_NATIVE_GPU
            ps5::downloads::File<> file(filePath);
            *downloadFile = &file; // let progressCb pre-allocate to Content-Length
#else
            std::ofstream of(filePath, std::ios::binary);
            if (!of) throw std::runtime_error("Failed to open file for writing");

#endif
            HTTP s;
            HTTP::set_option(s, header, cancel, progressCb);
#ifdef PS5_NATIVE_GPU
            try {
                s._get(url, &file.stream());
            } catch (...) {
                // Both objects are still alive here, and only here.
                transferStatus = s.last_status();
                const auto& fault = file.writeFault();
                if (fault.observed) PS5_DOWNLOAD_FILE_FAULT(itemId, fault);
                throw;
            }
            if (file.bytes() == 0) throw ps5::downloads::Error(ps5::downloads::Failure::Empty);
            if (file.bytes() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                throw ps5::downloads::Error(ps5::downloads::Failure::Write);
            success = file.commit(cancel.get());
            if (success) completedBytes = static_cast<int64_t>(file.bytes());
            PS5_DOWNLOAD_NOTE(itemId, ps5::downloads::DownloadJournal::TransferEnd, success ? 0 : 2, file.bytes());
        } catch (const ps5_native_http::Failure& failure) {
            // No HTTP response was produced, so the status alone says nothing.
            // The snapshot libcurl was asked for at the point of failure does.
            error = failure.what();
            PS5_DOWNLOAD_NOTE(itemId, ps5::downloads::DownloadJournal::TransferEnd,
                static_cast<int>(failure.snapshot().http_status));
            PS5_DOWNLOAD_NET(itemId, failure.snapshot());
            brls::Logger::error("Download failed: {} - {}", itemId, error);
        } catch (const ps5::downloads::Error& local) {
            // A local file outcome, not a transport one. Recorded apart from any
            // HTTP status so the two can never be read as each other.
            error = local.what();
            PS5_DOWNLOAD_NOTE(itemId, ps5::downloads::DownloadJournal::TransferEnd,
                -20 - static_cast<int>(local.reason));
            brls::Logger::error("Download failed: {} - {}", itemId, error);
#else
            s._get(url, &of);
            of.close();

            cancelled = cancel->load();
            if (!cancelled) success = true;
#endif
        } catch (const std::exception& ex) {
            error = ex.what();
#ifdef PS5_NATIVE_GPU
            // The response status names the cause: 401 is a refused request,
            // 0 means the transfer never produced one.
            PS5_DOWNLOAD_NOTE(itemId, ps5::downloads::DownloadJournal::TransferEnd,
                static_cast<int>(transferStatus));
#endif
            brls::Logger::error("Download failed: {} - {}", itemId, error);
        }

#ifdef PS5_NATIVE_GPU
        this->finishPS5Download(itemId, fileName, cancel, success, completedBytes, error);
#else
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
#endif
    });
}
