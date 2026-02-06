#include "view/download_dialog.hpp"
#include "utils/hls_downloader.hpp"
#include "utils/config.hpp"
#include "api/jellyfin.hpp"
#include <filesystem>
#include <fstream>
#include <fmt/format.h>

using namespace brls::literals;

DownloadDialog::DownloadDialog(const std::string& itemId, const std::string& itemName,
    const jellyfin::PlaybackResult& playbackInfo)
    : itemId(itemId), itemName(itemName), playbackInfo(playbackInfo) {
    this->inflateFromXMLRes("xml/view/download_dialog.xml");
    brls::Logger::debug("DownloadDialog: create");

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [this](brls::View* view) {
        if (isDownloading && downloadCancel) {
            *downloadCancel = true;
        }
        brls::Application::popActivity();
        return true;
    });

    this->cancel->registerClickAction([this](...) {
        if (isDownloading && downloadCancel) {
            *downloadCancel = true;
        }
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    qualityLabels.push_back("main/download/original"_i18n);
    qualityValues.push_back(-1);

    qualityLabels.push_back("20 Mbps");
    qualityValues.push_back(20000000);
    qualityLabels.push_back("15 Mbps");
    qualityValues.push_back(15000000);
    qualityLabels.push_back("10 Mbps");
    qualityValues.push_back(10000000);
    qualityLabels.push_back("8 Mbps");
    qualityValues.push_back(8000000);
    qualityLabels.push_back("6 Mbps");
    qualityValues.push_back(6000000);
    qualityLabels.push_back("4 Mbps");
    qualityValues.push_back(4000000);
    qualityLabels.push_back("3 Mbps");
    qualityValues.push_back(3000000);
    qualityLabels.push_back("1.5 Mbps");
    qualityValues.push_back(1500000);
    qualityLabels.push_back("720 kbps");
    qualityValues.push_back(720000);
    qualityLabels.push_back("420 kbps");
    qualityValues.push_back(420000);

    qualitySelector->init("main/download/quality"_i18n, qualityLabels, 0, [this](int selected) {
        selectedQuality = selected;
    });
    qualitySelector->detail->setVisibility(brls::Visibility::GONE);

    btnStart->registerClickAction([this](brls::View* view) {
        if (!isDownloading) {
            startDownload();
        }
        return true;
    });
    btnStart->addGestureRecognizer(new brls::TapGestureRecognizer(btnStart));

    btnCancel->registerClickAction([this](brls::View* view) {
        if (isDownloading && downloadCancel) {
            *downloadCancel = true;
        } else {
            brls::Application::popActivity();
        }
        return true;
    });
    btnCancel->addGestureRecognizer(new brls::TapGestureRecognizer(btnCancel));
}

DownloadDialog::~DownloadDialog() {
    brls::Logger::debug("DownloadDialog: delete");
    if (isDownloading && downloadCancel) {
        *downloadCancel = true;
    }
    if (!playSessionId.empty()) {
        reportPlaybackStop();
    }
}

void DownloadDialog::startDownload() {
    if (playbackInfo.MediaSources.empty()) {
        statusLabel->setText("main/download/failed"_i18n);
        statusLabel->setVisibility(brls::Visibility::VISIBLE);
        return;
    }

    isDownloading = true;
    downloadCancel = std::make_shared<std::atomic_bool>(false);
    btnStart->setVisibility(brls::Visibility::GONE);
    qualitySelector->setVisibility(brls::Visibility::GONE);

    progressHeader->setTitle("main/download/progress"_i18n);
    progressHeader->setVisibility(brls::Visibility::VISIBLE);
    progressBar->getParent()->setVisibility(brls::Visibility::VISIBLE);
    statusLabel->setText("main/download/preparing"_i18n);
    statusLabel->setVisibility(brls::Visibility::VISIBLE);

    int64_t selectedBitrate = qualityValues[selectedQuality];

    if (selectedBitrate == -1) {
        auto& c = AppConfig::instance();
        std::string query = HTTP::encode_form({{"api_key", c.getToken()}});
        std::string url = c.getUrl() + fmt::format(fmt::runtime(jellyfin::apiDownload), itemId, query);
        startDirectDownload(url);
    } else {
        playSessionId = playbackInfo.PlaySessionId;
        reportPlaybackStart();

        auto& source = playbackInfo.MediaSources[0];
        if (source.TranscodingUrl.empty()) {
            nlohmann::json profile = {
                {"MaxStreamingBitrate", selectedBitrate},
                {"DirectPlayProfiles", nlohmann::json::array()},
                {
                    "TranscodingProfiles",
                    {
                        {{"Type", "Audio"}},
                        {
                            {"Container", "ts"},
                            {"Type", "Video"},
                            {"VideoCodec", "h264,hevc"},
                            {"AudioCodec", "aac,mp3,ac3"},
                            {"Protocol", "hls"},
                        },
                    },
                },
            };

            ASYNC_RETAIN
            jellyfin::postJSON(
                {
                    {"UserId", AppConfig::instance().getUserId()},
                    {"MediaSourceId", itemId},
                    {"DeviceProfile", profile},
                },
                [ASYNC_TOKEN](const jellyfin::PlaybackResult& r) {
                    ASYNC_RELEASE
                    if (r.MediaSources.empty() || r.MediaSources[0].TranscodingUrl.empty()) {
                        this->onDownloadComplete(false, "No transcoding URL available");
                        return;
                    }
                    this->playSessionId = r.PlaySessionId;
                    this->startTranscodedDownload(r.MediaSources[0].TranscodingUrl);
                },
                [ASYNC_TOKEN](const std::string& ex) {
                    ASYNC_RELEASE
                    this->onDownloadComplete(false, ex);
                },
                jellyfin::apiPlayback, itemId);
        } else {
            startTranscodedDownload(source.TranscodingUrl);
        }
    }
}

void DownloadDialog::startDirectDownload(const std::string& url) {
    std::string ext = ".mkv";
    size_t dotPos = itemName.rfind('.');
    if (dotPos != std::string::npos) {
        ext = "";
    }
    std::string outputPath = HLSDownloader::getDownloadPath(itemName + ext);
    auto cancelPtr = downloadCancel;

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url, outputPath, cancelPtr]() {
        bool success = false;
        std::string resultMsg;

        try {
            auto& c = AppConfig::instance();
            HTTP::Header header = {c.getAuth(c.getToken())};

            std::ofstream outFile(outputPath, std::ios::binary);
            if (!outFile.is_open()) {
                resultMsg = "Failed to open output file";
            } else {
                HTTP s;
                std::shared_ptr<int> lastPercent = std::make_shared<int>(0);
                HTTP::Progress::Callback progressCb = [ASYNC_TOKEN, lastPercent](curl_off_t total, curl_off_t now) {
                    if (total > 0) {
                        int percent = (int)((now * 100) / total);
                        if (percent != *lastPercent) {
                            *lastPercent = percent;
                            brls::sync([ASYNC_TOKEN, percent]() {
                                this->updateProgress(percent, "Downloading...");
                            });
                        }
                    }
                };

                HTTP::set_option(s, header, HTTP::Timeout{600000}, cancelPtr, progressCb);

                std::ostringstream body;
                s._get(url, &body);

                if (cancelPtr && *cancelPtr) {
                    outFile.close();
                    std::filesystem::remove(outputPath);
                    resultMsg = "Cancelled";
                } else {
                    outFile << body.str();
                    outFile.close();
                    success = true;
                    resultMsg = outputPath;
                }
            }
        } catch (const std::exception& e) {
            resultMsg = e.what();
            std::filesystem::remove(outputPath);
        }

        brls::sync([ASYNC_TOKEN, success, resultMsg]() {
            ASYNC_RELEASE
            this->onDownloadComplete(success, resultMsg);
        });
    });
}

void DownloadDialog::startTranscodedDownload(const std::string& transcodingUrl) {
    std::string outputPath = HLSDownloader::getDownloadPath(itemName + ".ts");

    auto& c = AppConfig::instance();
    downloader = std::make_unique<HLSDownloader>(c.getUrl(), transcodingUrl, outputPath);
    downloader->setCancel(downloadCancel);

    downloader->setProgressCallback([this](int percent, const std::string& status) {
        updateProgress(percent, status);
    });

    downloader->setCompleteCallback([this](bool success, const std::string& message) {
        onDownloadComplete(success, message);
    });

    downloader->start();
}

void DownloadDialog::updateProgress(int percent, const std::string& status) {
    progressBar->setWidthPercentage(percent);
    statusLabel->setText(fmt::format(fmt::runtime("main/download/progress"_i18n), percent));
}

void DownloadDialog::onDownloadComplete(bool success, const std::string& message) {
    isDownloading = false;

    if (!playSessionId.empty()) {
        reportPlaybackStop();
        playSessionId.clear();
    }

    if (success) {
        progressBar->setWidthPercentage(100);
        statusLabel->setText("main/download/complete"_i18n);
        brls::Application::notify(fmt::format(fmt::runtime("main/download/path"_i18n), message));
    } else {
        if (message == "Cancelled") {
            statusLabel->setText("main/download/cancelled"_i18n);
        } else {
            statusLabel->setText(fmt::format(fmt::runtime("main/download/failed"_i18n), message));
        }
    }

    btnCancel->setText("hints/ok"_i18n);
}

void DownloadDialog::reportPlaybackStart() {
    if (playSessionId.empty()) return;

    jellyfin::postJSON(
        {
            {"ItemId", itemId},
            {"PlayMethod", jellyfin::methodTranscode},
            {"PlaySessionId", playSessionId},
            {"PositionTicks", 0},
            {"MediaSourceId", itemId},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStart);
}

void DownloadDialog::reportPlaybackStop() {
    if (playSessionId.empty()) return;

    jellyfin::postJSON(
        {
            {"ItemId", itemId},
            {"PlayMethod", jellyfin::methodTranscode},
            {"PlaySessionId", playSessionId},
            {"PositionTicks", 0},
        },
        [](...) {}, nullptr, jellyfin::apiPlayStop);

    brls::Logger::debug("DownloadDialog: reported playback stop");
}
