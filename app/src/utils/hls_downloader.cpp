#include "utils/hls_downloader.hpp"
#include "utils/config.hpp"
#include "api/http.hpp"
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <filesystem>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>

HLSDownloader::HLSDownloader(const std::string& baseUrl, const std::string& playlistUrl, const std::string& outputPath)
    : baseUrl(baseUrl), playlistUrl(playlistUrl), outputPath(outputPath) {}

HLSDownloader::~HLSDownloader() {
    if (outputFile.is_open()) {
        outputFile.close();
    }
}

std::string HLSDownloader::sanitizeFilename(const std::string& filename) {
    std::string result = filename;
    const std::string invalid = "\\/:*?\"<>|";
    for (char& c : result) {
        if (invalid.find(c) != std::string::npos) {
            c = '_';
        }
    }
    if (result.length() > 200) {
        result = result.substr(0, 200);
    }
    return result;
}

std::string HLSDownloader::getDownloadPath(const std::string& filename) {
    std::string dir = AppConfig::instance().configDir() + "/downloads";
    std::filesystem::create_directories(dir);
    return dir + "/" + sanitizeFilename(filename);
}

void HLSDownloader::start() {
    brls::async([this]() {
        outputFile.open(outputPath, std::ios::binary | std::ios::trunc);
        if (!outputFile.is_open()) {
            reportComplete(false, "Failed to open output file");
            return;
        }

        reportProgress(0, "Preparing...");

        int retryCount = 0;
        const int maxRetries = 60;
        const int pollIntervalMs = 2000;

        while (!playlistComplete && retryCount < maxRetries) {
            if (cancel && *cancel) {
                reportComplete(false, "Cancelled");
                return;
            }

            if (!fetchPlaylist()) {
                retryCount++;
                std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
                continue;
            }

            while (downloadedSegments < (int)segments.size()) {
                if (cancel && *cancel) {
                    reportComplete(false, "Cancelled");
                    return;
                }

                const auto& segmentUrl = segments[downloadedSegments];
                if (!downloadSegment(segmentUrl)) {
                    reportComplete(false, "Failed to download segment");
                    return;
                }
                downloadedSegments++;

                int percent = 0;
                if (playlistComplete && totalSegments > 0) {
                    percent = (downloadedSegments * 100) / totalSegments;
                } else {
                    percent = std::min(downloadedSegments, 99);
                }
                reportProgress(percent, "Downloading...");
            }

            if (!playlistComplete) {
                std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
            }
        }

        outputFile.close();

        if (playlistComplete) {
            reportComplete(true, outputPath);
        } else {
            reportComplete(false, "Timeout waiting for playlist");
        }
    });
}

bool HLSDownloader::fetchPlaylist() {
    try {
        auto& c = AppConfig::instance();
        HTTP::Header header = {c.getAuth(c.getToken())};
        std::string url = baseUrl + playlistUrl;

        brls::Logger::debug("HLS: Fetching playlist: {}", url);
        std::string content = HTTP::get(url, header, HTTP::Timeout{10000});

        if (content.empty()) {
            brls::Logger::warning("HLS: Empty playlist response");
            return false;
        }

        return parsePlaylist(content);
    } catch (const std::exception& e) {
        brls::Logger::error("HLS: Failed to fetch playlist: {}", e.what());
        return false;
    }
}

bool HLSDownloader::parsePlaylist(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    std::vector<std::string> newSegments;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.find("#EXT-X-ENDLIST") != std::string::npos) {
            playlistComplete = true;
        }

        if (!line.empty() && line[0] != '#') {
            newSegments.push_back(line);
        }
    }

    for (const auto& seg : newSegments) {
        bool found = false;
        for (const auto& existing : segments) {
            if (existing == seg) {
                found = true;
                break;
            }
        }
        if (!found) {
            segments.push_back(seg);
        }
    }

    totalSegments = segments.size();
    if (playlistComplete) {
        brls::Logger::debug("HLS: Playlist complete with {} segments", totalSegments);
    }

    return !segments.empty();
}

bool HLSDownloader::downloadSegment(const std::string& segmentUrl) {
    try {
        auto& c = AppConfig::instance();
        HTTP::Header header = {c.getAuth(c.getToken())};

        std::string url;
        if (segmentUrl.find("://") != std::string::npos) {
            url = segmentUrl;
        } else if (segmentUrl[0] == '/') {
            url = baseUrl + segmentUrl;
        } else {
            size_t lastSlash = playlistUrl.rfind('/');
            if (lastSlash != std::string::npos) {
                url = baseUrl + playlistUrl.substr(0, lastSlash + 1) + segmentUrl;
            } else {
                url = baseUrl + "/" + segmentUrl;
            }
        }

        brls::Logger::debug("HLS: Downloading segment: {}", url);

        HTTP s;
        std::ostringstream body;
        HTTP::set_option(s, header, HTTP::Timeout{30000});
        if (cancel) {
            HTTP::set_option(s, cancel);
        }
        s._get(url, &body);

        std::string data = body.str();
        if (data.empty()) {
            brls::Logger::warning("HLS: Empty segment data");
            return false;
        }

        outputFile.write(data.c_str(), data.size());
        outputFile.flush();

        return true;
    } catch (const std::exception& e) {
        brls::Logger::error("HLS: Failed to download segment: {}", e.what());
        return false;
    }
}

void HLSDownloader::reportProgress(int percent, const std::string& status) {
    if (progressCallback) {
        brls::sync([this, percent, status]() {
            if (progressCallback) {
                progressCallback(percent, status);
            }
        });
    }
}

void HLSDownloader::reportComplete(bool success, const std::string& message) {
    if (outputFile.is_open()) {
        outputFile.close();
    }

    if (!success && !outputPath.empty()) {
        try {
            std::filesystem::remove(outputPath);
        } catch (...) {}
    }

    if (completeCallback) {
        brls::sync([this, success, message]() {
            if (completeCallback) {
                completeCallback(success, message);
            }
        });
    }
}
