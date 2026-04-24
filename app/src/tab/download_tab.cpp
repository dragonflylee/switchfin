#include "tab/download_tab.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_view.hpp"
#include "view/mpv_core.hpp"
#include "view/video_profile.hpp"
#include "view/player_setting.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "utils/misc.hpp"
#include "api/jellyfin/media.hpp"

#ifdef USE_BOOST_FILESYSTEM
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace brls::literals;

class DownloadCard : public RecyclingGridItem {
public:
    DownloadCard() { this->inflateFromXMLRes("xml/view/download_card.xml"); }

    void setItem(const DownloadItem& item, const std::string& downloadDir) {
        this->thumb->setImageFromRes("img/video-card-bg.png");
        std::string thumbPath = downloadDir + "/" + item.itemId + "/thumb.png";
        if (fs::exists(thumbPath)) {
            this->thumb->setImageFromFile(thumbPath);
        }


        this->name->setText(item.seriesName.empty() ? item.name
            : fmt::format("{} - S{}E{} {}", item.seriesName, item.seasonIndex, item.episodeIndex, item.name));

        std::string detail;
        if (item.runTimeTicks > 0) {
            detail = misc::sec2Time(item.runTimeTicks / jellyfin::PLAYTICKS);
        }
        if (item.productionYear > 0) {
            if (!detail.empty()) detail += " · ";
            detail += std::to_string(item.productionYear);
        }
        this->detail->setText(detail);

        switch (item.status) {
        case DownloadStatus::Queued:
            this->status->setText("main/download/queued"_i18n);
            break;
        case DownloadStatus::Downloading:
            if (item.totalBytes > 0) {
                int pct = static_cast<int>(item.downloadedBytes * 100 / item.totalBytes);
                this->status->setText(fmt::format("{}%", pct));
            } else if (item.downloadedBytes > 0 && item.quality != DownloadQuality::Original) {
                std::string size = misc::formatSize(item.downloadedBytes);
                int64_t bitrate = item.quality == DownloadQuality::Q1080p ? 4000000
                    : item.quality == DownloadQuality::Q720p ? 2000000 : 1000000;
                int64_t durationSec = item.runTimeTicks / 10000000;
                int64_t estimated = bitrate * durationSec / 8;
                if (estimated > 0) {
                    int pct = std::min(99, static_cast<int>(item.downloadedBytes * 100 / estimated));
                    this->status->setText(fmt::format("~{}% ({})", pct, size));
                } else {
                    this->status->setText(size);
                }
            } else {
                this->status->setText("main/download/downloading"_i18n);
            }
            break;
        case DownloadStatus::Completed:
            this->status->setText("main/download/completed"_i18n);
            break;
        case DownloadStatus::Failed:
            this->status->setText("main/download/failed"_i18n);
            break;
        }
    }

private:
    BRLS_BIND(brls::Image, thumb, "download/thumb");
    BRLS_BIND(brls::Label, name, "download/name");
    BRLS_BIND(brls::Label, detail, "download/detail");
    BRLS_BIND(brls::Label, status, "download/status");
};

class DownloadDataSource : public RecyclingGridDataSource {
public:
    DownloadDataSource(std::vector<DownloadItem> items)
        : items(std::move(items)), dlDir(AppConfig::instance().configDir() + "/downloads") {}

    size_t getItemCount() override { return this->items.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        DownloadCard* cell = dynamic_cast<DownloadCard*>(recycler->dequeueReusableCell("Cell"));
        cell->setItem(this->items.at(index), this->dlDir);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->items.at(index);
        auto& dm = DownloadManager::instance();

        if (item.status == DownloadStatus::Completed) {
            std::string path = dm.getLocalPath(item.itemId);
            if (!path.empty()) {
                VideoView* view = new VideoView();
                float width = brls::Application::contentWidth;
                float height = brls::Application::contentHeight;
                view->setDimensions(width, height);
                view->setWidthPercentage(100);
                view->setHeightPercentage(100);
                view->setTitie(item.name);
                view->hideVideoQuality();

                auto* profile = view->getProfile();
                auto& mpv = MPVCore::instance();
                auto subId = std::make_shared<MPVEvent::Subscription>();
                *subId = mpv.getEvent()->subscribe([profile, subId](MpvEventEnum event) {
                    if (event == MpvEventEnum::MPV_RESUME) {
                        profile->init("Local");
                    } else if (event == MpvEventEnum::MPV_STOP) {
                        MPVCore::instance().getEvent()->unsubscribe(*subId);
                    }
                });

                view->getPlayEvent()->subscribe([](int) { return VideoView::close(true); });
                view->getSettingEvent()->subscribe([]() {
                    brls::Application::pushActivity(new brls::Activity(new PlayerSetting()));
                });

                brls::Box* container = new brls::Box();
                container->setDimensions(width, height);
                container->addView(view);
                brls::Application::pushActivity(new brls::Activity(container), brls::TransitionAnimation::NONE);

                MPVCore::instance().setUrl(path);
            }
        } else if (item.status == DownloadStatus::Downloading) {
            std::string id = item.itemId;
            Dialog::cancelable("main/download/cancel"_i18n, [id]() {
                DownloadManager::instance().cancelDownload(id);
            });
        } else if (item.status == DownloadStatus::Queued) {
            DownloadManager::instance().resumeQueue();
        } else if (item.status == DownloadStatus::Failed) {
            std::string id = item.itemId;
            Dialog::cancelable("main/download/confirm_remove"_i18n, [id]() {
                DownloadManager::instance().removeDownload(id);
            });
        }
    }

    void clearData() override { this->items.clear(); }

    const std::string& getItemId(size_t index) const { return this->items.at(index).itemId; }
    size_t itemCount() const { return this->items.size(); }

private:
    std::vector<DownloadItem> items;
    std::string dlDir;
};

DownloadView::DownloadView() {
    brls::Logger::debug("DownloadView: create");

    RecyclingGrid* grid = this->newRecycler();
    this->stack.push_back(grid);
    this->setContent(grid);

    this->statusSubId = DownloadManager::instance().getStatusEvent()->subscribe(
        [this](const std::string&, DownloadStatus) {
            this->loadItems();
        });

    this->progressSubId = DownloadManager::instance().getProgressEvent()->subscribe(
        [this](const std::string&, int64_t, int64_t) {
            this->loadItems();
        });

    this->loadItems();
}

DownloadView::~DownloadView() {
    brls::Logger::debug("DownloadView: deleted");
    DownloadManager::instance().getStatusEvent()->unsubscribe(this->statusSubId);
    DownloadManager::instance().getProgressEvent()->unsubscribe(this->progressSubId);
}

brls::View* DownloadView::getDefaultFocus() { return this->recycler; }

void DownloadView::loadItems() {
    auto items = DownloadManager::instance().getItems();
    if (items.empty()) {
        this->recycler->setEmpty("main/download/no_downloads"_i18n);
    } else {
        this->recycler->setDataSource(new DownloadDataSource(std::move(items)));
    }
}

RecyclingGrid* DownloadView::newRecycler() {
    RecyclingGrid* grid = new RecyclingGrid();
    grid->spanCount = 1;
    grid->estimatedRowHeight = 130;
    grid->estimatedRowSpace = 5;
    grid->setDefaultCellFocus(1);
    grid->registerCell("Cell", []() { return new DownloadCard(); });

    auto deleteAction = [this](brls::View*) {
        auto* focus = dynamic_cast<RecyclingGridItem*>(brls::Application::getCurrentFocus());
        if (!focus) return false;
        auto* ds = dynamic_cast<DownloadDataSource*>(this->recycler->getDataSource());
        if (!ds) return false;
        size_t idx = focus->getIndex();
        if (idx >= ds->itemCount()) return false;
        std::string id = ds->getItemId(idx);
        Dialog::cancelable("main/download/confirm_remove"_i18n, [this, id]() {
            DownloadManager::instance().removeDownload(id);
            this->loadItems();
        });
        return true;
    };
    grid->registerAction("main/download/remove"_i18n, brls::BUTTON_X, deleteAction);
    grid->registerAction(brls::BRLS_KBD_KEY_BACKSPACE, deleteAction);

    grid->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](...) {
        this->dismiss();
        return true;
    });

    return grid;
}

void DownloadView::setContent(RecyclingGrid* view) {
    if (this->recycler) {
        this->removeView(this->recycler, false);
        this->recycler = nullptr;
    }
    this->recycler = view;
    this->recycler->setDimensions(brls::View::AUTO, brls::View::AUTO);
    this->recycler->setGrow(1.0f);
    this->addView(this->recycler);
    brls::Application::giveFocus(this->recycler);
}

void DownloadView::dismiss(std::function<void(void)> cb) {
    if (this->stack.size() > 1) {
        brls::View* lastView = this->recycler;
        this->stack.pop_back();
        this->setContent(this->stack.back());
        cb();
        lastView->freeView();
    } else {
        AutoTabFrame::focus2Sidebar(this);
    }
}
