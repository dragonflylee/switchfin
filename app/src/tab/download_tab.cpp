#include "tab/download_tab.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_view.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "utils/misc.hpp"
#include "api/jellyfin/media.hpp"

using namespace brls::literals;

class DownloadCard : public RecyclingGridItem {
public:
    DownloadCard() { this->inflateFromXMLRes("xml/view/download_card.xml"); }

    void setItem(const DownloadItem& item) {
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
    BRLS_BIND(SVGImage, icon, "download/icon");
    BRLS_BIND(brls::Label, name, "download/name");
    BRLS_BIND(brls::Label, detail, "download/detail");
    BRLS_BIND(brls::Label, status, "download/status");
};

class DownloadDataSource : public RecyclingGridDataSource {
public:
    DownloadDataSource(std::vector<DownloadItem> items) : items(std::move(items)) {}

    size_t getItemCount() override { return this->items.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        DownloadCard* cell = dynamic_cast<DownloadCard*>(recycler->dequeueReusableCell("Cell"));
        cell->setItem(this->items.at(index));
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
        } else if (item.status == DownloadStatus::Downloading || item.status == DownloadStatus::Queued) {
            std::string id = item.itemId;
            Dialog::cancelable("main/download/cancel"_i18n, [id]() {
                DownloadManager::instance().cancelDownload(id);
            });
        } else if (item.status == DownloadStatus::Failed) {
            std::string id = item.itemId;
            Dialog::cancelable("main/download/confirm_remove"_i18n, [id]() {
                DownloadManager::instance().removeDownload(id);
            });
        }
    }

    void clearData() override { this->items.clear(); }

    std::vector<DownloadItem> items;
};

DownloadTab::DownloadTab() {
    this->inflateFromXMLRes("xml/tabs/downloads.xml");
    this->recycler->spanCount = 1;
    this->recycler->estimatedRowHeight = 70;
    this->recycler->estimatedRowSpace = 5;
    this->recycler->registerCell("Cell", []() { return new DownloadCard(); });
}

DownloadTab::~DownloadTab() {
    DownloadManager::instance().getStatusEvent()->unsubscribe(this->statusSubId);
}

void DownloadTab::onCreate() {
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, [this](...) {
        this->doRequest();
        return true;
    });

    this->statusSubId = DownloadManager::instance().getStatusEvent()->subscribe(
        [this](const std::string&, DownloadStatus) {
            this->doRequest();
        });

    this->doRequest();
}

void DownloadTab::doRequest() {
    auto items = DownloadManager::instance().getItems();
    if (items.empty()) {
        this->recycler->setEmpty("main/download/no_downloads"_i18n);
    } else {
        this->recycler->setDataSource(new DownloadDataSource(std::move(items)));
    }
}

brls::View* DownloadTab::create() { return new DownloadTab(); }
