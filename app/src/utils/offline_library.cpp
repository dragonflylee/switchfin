#include <borealis.hpp>
#include "utils/offline_library.hpp"
#include "utils/offline_catalog.hpp"
#include "utils/config.hpp"
#include "utils/misc.hpp"
#include "utils/download.hpp"

#include <fstream>

std::string OfflineLibrary::metaDir() const { return AppConfig::instance().configDir() + "/downloads/meta"; }

std::string OfflineLibrary::metaPath(const std::string& ratingKey) const {
    // filename is a sanitized key (real Plex keys are numeric; synthetic keys
    // never reach disk). The authoritative ratingKey is read back from the JSON
    // content, not the filename.
    std::string safe;
    for (char c : ratingKey)
        safe += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return this->metaDir() + "/" + safe + ".json";
}

void OfflineLibrary::init() {
    std::string dir = this->metaDir();
    if (!fs::exists(dir)) {
        try {
            fs::create_directories(dir);
        } catch (const std::exception& e) {
            brls::Logger::error("OfflineLibrary: cannot create {}: {}", dir, e.what());
        }
    }
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->load();
        this->rebuild();
    }
    // legacy back-fill takes the lock per putItem, so run it outside the block
    this->migrateLegacy();
}

void OfflineLibrary::load() {
    this->nodes.clear();
    std::string dir = this->metaDir();
    if (!fs::exists(dir)) return;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension().string() != ".json") continue;
        try {
            std::ifstream f(entry.path().string());
            nlohmann::json j = nlohmann::json::parse(f);
            this->nodes.push_back(j.get<plex::Item>());
        } catch (const std::exception& e) {
            brls::Logger::error("OfflineLibrary: bad meta {}: {}", entry.path().string(), e.what());
        }
    }
}

void OfflineLibrary::rebuild() { this->derived = offline::synthesizeAncestors(this->nodes); }

void OfflineLibrary::writeMeta(const plex::Item& item) const {
    try {
        nlohmann::json j = item;
        std::ofstream f(this->metaPath(item.ratingKey));
        f << j.dump(2);
    } catch (const std::exception& e) {
        brls::Logger::error("OfflineLibrary: cannot write meta {}: {}", item.ratingKey, e.what());
    }
}

void OfflineLibrary::putItem(const plex::Item& item) {
    if (item.ratingKey.empty()) return;
    std::lock_guard<std::mutex> lock(this->mutex);
    this->writeMeta(item);
    bool replaced = false;
    for (auto& n : this->nodes) {
        if (n.ratingKey == item.ratingKey) {
            n = item;
            replaced = true;
            break;
        }
    }
    if (!replaced) this->nodes.push_back(item);
    this->rebuild();
}

bool OfflineLibrary::hasItem(const std::string& ratingKey) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& n : this->nodes)
        if (n.ratingKey == ratingKey) return true;
    return false;
}

bool OfflineLibrary::getItem(const std::string& ratingKey, plex::Item& out) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    for (auto& n : this->derived) {
        if (n.ratingKey == ratingKey) {
            out = n;
            return true;
        }
    }
    return false;
}

std::vector<plex::Section> OfflineLibrary::sections() const {
    std::lock_guard<std::mutex> lock(this->mutex);
    return offline::buildSections(this->derived);
}

std::vector<plex::Item> OfflineLibrary::sectionItems(const std::string& sectionKey) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    return offline::sectionItems(this->derived, sectionKey);
}

std::vector<plex::Item> OfflineLibrary::children(const std::string& ratingKey) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    return offline::childrenOf(this->derived, ratingKey);
}

std::vector<plex::Item> OfflineLibrary::leaves(const std::string& showRatingKey) const {
    std::lock_guard<std::mutex> lock(this->mutex);
    return offline::leavesOf(this->derived, showRatingKey);
}

bool OfflineLibrary::empty() const {
    std::lock_guard<std::mutex> lock(this->mutex);
    return offline::buildSections(this->derived).empty();
}

void OfflineLibrary::removeItem(const std::string& ratingKey) {
    std::lock_guard<std::mutex> lock(this->mutex);
    try {
        std::string path = this->metaPath(ratingKey);
        if (fs::exists(path)) fs::remove(path);
    } catch (const std::exception& e) {
        brls::Logger::error("OfflineLibrary: cannot remove meta {}: {}", ratingKey, e.what());
    }
    this->nodes.erase(std::remove_if(this->nodes.begin(), this->nodes.end(),
                          [&](const plex::Item& n) { return n.ratingKey == ratingKey; }),
        this->nodes.end());
    this->rebuild();
}

void OfflineLibrary::migrateLegacy() {
    std::string index = AppConfig::instance().configDir() + "/downloads/index.json";
    if (!fs::exists(index)) return;
    std::vector<DownloadItem> items;
    try {
        std::ifstream f(index);
        nlohmann::json j = nlohmann::json::parse(f);
        items = j.get<std::vector<DownloadItem>>();
    } catch (const std::exception& e) {
        brls::Logger::error("OfflineLibrary: cannot read legacy index: {}", e.what());
        return;
    }
    for (auto& dl : items) {
        // only completed downloads are browsable; skip if already captured
        if (dl.status != DownloadStatus::Completed) continue;
        if (this->hasItem(dl.itemId)) continue;

        plex::Item it;
        it.ratingKey = dl.itemId;
        it.title = dl.name;
        it.year = dl.productionYear;
        it.duration = dl.durationMs;
        it.thumb = dl.thumb;
        if (dl.type == plex::mediaTypeEpisode) {
            it.type = plex::mediaTypeEpisode;
            it.index = dl.episodeIndex;
            it.parentIndex = dl.seasonIndex;
            it.grandparentTitle = dl.seriesName;
        } else {
            // movie or clip -> browsable as a top-level movie
            it.type = plex::mediaTypeMovie;
        }
        this->putItem(it);
    }
}
