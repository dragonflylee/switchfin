#include "tab/download_tab.hpp"
#include "tab/remote_view.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_view.hpp"
#include "view/mpv_core.hpp"
#include "view/video_profile.hpp"
#include "view/player_setting.hpp"
#include "view/presenter.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "utils/misc.hpp"

#include <algorithm>

using namespace brls::literals;

/// Segmented bar drawn in nvg: track = color/pill (free space), cumulative
/// segments clipped by scissor on the same rounded-rect (rounded ends,
/// clean junctions). Used by the "Storage" header
/// ([rest of disk | pleNx gold | free]) and by the card progress
/// (a single gold segment). No yoga relayout per tick: just a redraw.
class SegmentedBar : public brls::Rectangle {
public:
    SegmentedBar() : brls::Rectangle(nvgRGBA(0, 0, 0, 0)) {
        this->track = brls::Application::getTheme().getColor("color/pill");
    }

    /// [0..1] fractions cumulative left to right, in draw order
    void setSegments(std::vector<std::pair<float, NVGcolor>> segs) { this->segments = std::move(segs); }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
        brls::FrameContext* ctx) override {
        float r = this->getCornerRadius();

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, r);
        nvgFillColor(vg, a(this->track));
        nvgFill(vg);

        float cur = 0;
        for (auto& [frac, color] : this->segments) {
            float f = std::clamp(frac, 0.0f, 1.0f - cur);
            if (f <= 0) continue;
            nvgSave(vg);
            nvgIntersectScissor(vg, x + w * cur, y, w * f, h);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w, h, r);
            nvgFillColor(vg, a(color));
            nvgFill(vg);
            nvgRestore(vg);
            cur += f;
        }
    }

private:
    NVGcolor track;
    std::vector<std::pair<float, NVGcolor>> segments;
};

/// "Storage" card, row 0 of the list (non-focusable) so it scrolls with the
/// content. Self-contained: update() recomputes the disk figures + bar from
/// DownloadManager. NOT a RecyclingGrid scrolled header (setHeaderView), which
/// aborts Yoga when the Downloads pill is the RemoteTab's default tab.
class StorageCard : public RecyclingGridItem {
public:
    StorageCard() {
        this->setFocusable(false);
        this->inflateFromXMLRes("xml/view/storage_card.xml");
        this->bar = new SegmentedBar();
        this->bar->setCornerRadius(6);
        this->bar->setGrow(1.0f);
        this->barBox->addView(this->bar);
    }

    void update() {
        auto& dm = DownloadManager::instance();
        const std::string dir = dm.getDownloadDir();
        auto items = dm.getItems();

        // pleNx bytes: completed = known size (or real file for an inherited
        // index), in progress/failed = bytes already written to disk
        int64_t appBytes = 0;
        for (auto& it : items) {
            if (it.status == DownloadStatus::Completed) {
                if (it.totalBytes > 0) {
                    appBytes += it.totalBytes;
                } else if (!it.filePath.empty()) {
                    try {
                        appBytes += (int64_t)fs::file_size(dir + "/" + it.itemId + "/" + it.filePath);
                    } catch (const std::exception&) {
                    }
                }
            } else if (it.downloadedBytes > 0) {
                appBytes += it.downloadedBytes;
            }
        }

        // offline catalog caches (fiche snapshots + artwork) also live under the
        // downloads folder and count towards pleNx' footprint
        auto dirBytes = [](const std::string& d) -> int64_t {
            int64_t total = 0;
            try {
                if (fs::exists(d))
                    for (auto& e : fs::recursive_directory_iterator(d))
                        if (fs::is_regular_file(e.path())) total += (int64_t)fs::file_size(e.path());
            } catch (const std::exception&) {
            }
            return total;
        };
        appBytes += dirBytes(dir + "/meta") + dirBytes(dir + "/art");

        size_t count = items.size();
        this->app->setText(fmt::format("{}: {} · {} {}", AppVersion::getPackageName(),
            appBytes > 0 ? misc::formatSize(appBytes) : "0GB", count,
            count == 1 ? "main/playlist/item"_i18n : "main/playlist/items"_i18n));

        // capacity/available: std::filesystem::space and boost::filesystem::space
        // expose the same fields (macOS/Linux/Windows/Switch)
        bool spaceOk = false;
        fs::space_info space{};
        try {
            space = fs::space(dir);
            spaceOk = space.capacity > 0 && space.capacity != (uintmax_t)-1;
        } catch (const std::exception&) {
        }

        if (!spaceOk) {
            this->free->setText("");
            this->bar->setSegments({});
            return;
        }

        double total = (double)space.capacity;
        int64_t othersBytes = (int64_t)(space.capacity - space.available) - appBytes;
        if (othersBytes < 0) othersBytes = 0;

        float appFrac = (float)(appBytes / total);
        float othersFrac = (float)(othersBytes / total);
        // gold sliver always discernible as soon as there is at least one download
        if (appBytes > 0 && appFrac < 0.006f) appFrac = 0.006f;

        auto theme = brls::Application::getTheme();
        this->bar->setSegments({
            {othersFrac, theme.getColor("color/grey_3")},
            {appFrac, theme.getColor("color/app")},
        });

        this->free->setText(fmt::format(fmt::runtime("main/download/storage_free"_i18n),
            misc::formatSize((uint64_t)space.available), misc::formatSize((uint64_t)space.capacity)));
    }

private:
    BRLS_BIND(brls::Box, barBox, "downloads/storage/bar");
    BRLS_BIND(brls::Label, app, "downloads/storage/app");
    BRLS_BIND(brls::Label, free, "downloads/storage/free");
    SegmentedBar* bar = nullptr;
};

/// Empty placeholder as a list row (non-focusable): icon + title + subtitle,
/// centered — shown under the storage card when there is no download.
class DownloadEmptyCell : public RecyclingGridItem {
public:
    DownloadEmptyCell() {
        this->setFocusable(false);
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);

        auto* icon = new SVGImage();
        icon->setDimensions(56, 56);
        icon->setImageFromSVGRes("icon/ico-download.svg");
        this->addView(icon);

        auto* title = new brls::Label();
        title->setFontSize(17);
        title->setMarginTop(12);
        title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        title->setText("main/download/no_downloads"_i18n);
        this->addView(title);

        auto* sub = new brls::Label();
        sub->setFontSize(14);
        sub->setMarginTop(6);
        sub->setWidth(460);
        sub->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        sub->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        sub->setText("main/download/empty_sub"_i18n);
        this->addView(sub);
    }
};

/// Section header ("In progress", "Downloaded"): non-focusable row,
/// skipped by navigation (getDefaultFocus -> nullptr).
class DownloadSectionHeader : public RecyclingGridItem {
public:
    DownloadSectionHeader() {
        this->setFocusable(false);
        this->setAxis(brls::Axis::COLUMN);
        this->setJustifyContent(brls::JustifyContent::FLEX_END);
        this->title = new brls::Label();
        this->title->setFontSize(20);
        this->addView(this->title);
    }

    void setTitle(const std::string& text) { this->title->setText(text); }

private:
    brls::Label* title = nullptr;
};

class DownloadCard : public RecyclingGridItem {
public:
    DownloadCard() {
        this->inflateFromXMLRes("xml/view/download_card.xml");
        this->progressBar = new SegmentedBar();
        this->progressBar->setCornerRadius(3);
        this->progressBar->setGrow(1.0f);
        this->progressTrack->addView(this->progressBar);
    }

    void setItem(const DownloadItem& item, const std::string& downloadDir) {
        auto theme = brls::Application::getTheme();

        this->thumb->setImageFromRes("img/video-card-bg.png");
        std::string thumbPath = downloadDir + "/" + item.itemId + "/thumb.png";
        if (fs::exists(thumbPath)) {
            this->thumb->setImageFromFile(thumbPath);
        }

        // title = episode or movie name; subtitle = "Show · SxEy" or year,
        // completed with the duration
        this->name->setText(item.name);
        std::string detail;
        if (!item.seriesName.empty()) {
            detail = fmt::format("{} · S{}E{}", item.seriesName, item.seasonIndex, item.episodeIndex);
        } else if (item.productionYear > 0) {
            detail = std::to_string(item.productionYear);
        }
        if (item.durationMs > 0) {
            if (!detail.empty()) detail += " · ";
            detail += misc::sec2Time(item.durationMs / 1000);
        }
        this->detail->setText(detail);

        // reset all variants (recycled cells)
        this->badge->setVisibility(brls::Visibility::VISIBLE);
        this->percent->setVisibility(brls::Visibility::GONE);
        this->progressRow->setVisibility(brls::Visibility::GONE);
        this->size->setVisibility(brls::Visibility::GONE);
        this->status->setTextColor(theme.getColor("font/grey"));

        switch (item.status) {
        case DownloadStatus::Queued:
            this->status->setText("main/download/queued"_i18n);
            break;
        case DownloadStatus::Downloading:
            this->badge->setVisibility(brls::Visibility::GONE);
            this->progressRow->setVisibility(brls::Visibility::VISIBLE);
            this->setProgress(item.downloadedBytes, item.totalBytes);
            break;
        case DownloadStatus::Completed:
            this->status->setText("main/download/completed"_i18n);
            this->status->setTextColor(theme.getColor("brls/text"));
            if (item.totalBytes > 0) {
                this->size->setText(misc::formatSize(item.totalBytes));
                this->size->setVisibility(brls::Visibility::VISIBLE);
            }
            break;
        case DownloadStatus::Failed:
            this->status->setText("main/download/failed"_i18n);
            this->status->setTextColor(theme.getColor("color/danger"));
            break;
        }
    }

    /// In-place update from ProgressEvent (no list reload). speed (B/s,
    /// 0 = n/a) appends "· x/s · mm:ss left"; it is absent on a plain
    /// reload (setItem), where only the stored byte counts are known.
    void setProgress(int64_t downloaded, int64_t total, double speed = 0.0) {
        if (total > 0) {
            float frac = (float)downloaded / (float)total;
            this->progressBar->setSegments({{frac, brls::Application::getTheme().getColor("color/app")}});
            this->percent->setText(fmt::format("{}%", (int)(frac * 100)));
            this->percent->setVisibility(brls::Visibility::VISIBLE);
            std::string info = fmt::format("{} / {}", misc::formatSize(downloaded), misc::formatSize(total));
            if (speed > 0) {
                info += fmt::format(" · {}/s", misc::formatSize((uint64_t)speed));
                int64_t eta = (int64_t)((total - downloaded) / speed);
                if (eta > 0) info += " · " + brls::getStr("main/download/eta", misc::sec2Time(eta));
            }
            this->progressInfo->setText(info);
        } else {
            // original file without Content-Length: indeterminate progress
            this->progressBar->setSegments({});
            this->percent->setVisibility(brls::Visibility::GONE);
            std::string text = "main/download/downloading"_i18n;
            if (downloaded > 0) text += fmt::format(" · {}", misc::formatSize(downloaded));
            if (speed > 0) text += fmt::format(" · {}/s", misc::formatSize((uint64_t)speed));
            this->progressInfo->setText(text);
        }
    }

private:
    BRLS_BIND(brls::Image, thumb, "download/thumb");
    BRLS_BIND(brls::Label, name, "download/name");
    BRLS_BIND(brls::Label, detail, "download/detail");
    BRLS_BIND(brls::Box, progressRow, "download/progress/row");
    BRLS_BIND(brls::Box, progressTrack, "download/progress/track");
    BRLS_BIND(brls::Label, progressInfo, "download/progress/info");
    BRLS_BIND(brls::Box, badge, "download/badge");
    BRLS_BIND(brls::Label, status, "download/status");
    BRLS_BIND(brls::Label, percent, "download/percent");
    BRLS_BIND(brls::Label, size, "download/size");

    SegmentedBar* progressBar = nullptr;
};

/// Sectioned list: "In progress" (Downloading + Queued, queue order)
/// then "Downloaded" (Completed + Failed). Per-row heights via
/// heightForRow (flow mode).
class DownloadDataSource : public RecyclingGridDataSource {
public:
    static constexpr float STORAGE_HEIGHT = 150;     // the "Storage" card (row 0)
    static constexpr float EMPTY_HEIGHT = 260;       // centered empty placeholder
    static constexpr float HEADER_FIRST_HEIGHT = 42;
    static constexpr float HEADER_NEXT_HEIGHT = 64;  // +22 of air between sections
    static constexpr float CARD_HEIGHT = 140;        // poster 114 + padding 2x13

    struct Row {
        enum class Kind { Storage, Header, Item, Empty };
        Kind kind = Kind::Item;
        float height = CARD_HEIGHT;
        std::string title;
        DownloadItem item;
    };

    explicit DownloadDataSource(std::vector<DownloadItem> all)
        : dlDir(DownloadManager::instance().getDownloadDir()) {
        // row 0: the "Storage" card, always present so it scrolls with the list
        Row storage;
        storage.kind = Row::Kind::Storage;
        storage.height = STORAGE_HEIGHT;
        this->rows.push_back(std::move(storage));

        std::vector<DownloadItem> active, done;
        for (auto& it : all) {
            if (it.status == DownloadStatus::Completed && it.totalBytes <= 0 && !it.filePath.empty()) {
                // inherited index without Content-Length: real file size
                try {
                    it.totalBytes = (int64_t)fs::file_size(this->dlDir + "/" + it.itemId + "/" + it.filePath);
                } catch (const std::exception&) {
                }
            }
            bool isActive = it.status == DownloadStatus::Downloading || it.status == DownloadStatus::Queued;
            (isActive ? active : done).push_back(std::move(it));
        }
        this->addSection("main/download/section_active"_i18n, active);
        this->addSection("main/download/section_done"_i18n, done);

        // nothing downloaded: an empty placeholder row under the storage card
        if (active.empty() && done.empty()) {
            Row empty;
            empty.kind = Row::Kind::Empty;
            empty.height = EMPTY_HEIGHT;
            this->rows.push_back(std::move(empty));
        }
    }

    size_t getItemCount() override { return this->rows.size(); }

    float heightForRow(brls::View* recycler, size_t index) override { return this->rows.at(index).height; }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        auto& row = this->rows.at(index);
        if (row.kind == Row::Kind::Storage) {
            auto* cell = dynamic_cast<StorageCard*>(recycler->dequeueReusableCell("Storage"));
            cell->update();
            return cell;
        }
        if (row.kind == Row::Kind::Empty) {
            return recycler->dequeueReusableCell("Empty");
        }
        if (row.kind == Row::Kind::Header) {
            auto* cell = dynamic_cast<DownloadSectionHeader*>(recycler->dequeueReusableCell("Header"));
            cell->setTitle(row.title);
            return cell;
        }
        auto* cell = dynamic_cast<DownloadCard*>(recycler->dequeueReusableCell("Cell"));
        cell->setItem(row.item, this->dlDir);
        return cell;
    }

    /// True if there is at least one real download row (not just storage/empty).
    bool hasItems() const {
        for (auto& r : this->rows)
            if (r.kind == Row::Kind::Item) return true;
        return false;
    }

    /// Row index of the storage card (0), for in-place refresh.
    static constexpr size_t STORAGE_ROW = 0;

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& row = this->rows.at(index);
        if (row.kind != Row::Kind::Item) return;
        auto& item = row.item;
        auto& dm = DownloadManager::instance();

        if (item.status == DownloadStatus::Completed) {
            std::string path = dm.getLocalPath(item.itemId);
            if (path.empty()) return;

            std::string detail;
            if (!item.seriesName.empty()) {
                detail = fmt::format("{} - S{}E{}", item.seriesName, item.seasonIndex, item.episodeIndex);
            } else if (item.productionYear > 0) {
                detail = fmt::format("{} ({})", item.name, item.productionYear);
            } else {
                detail = item.name;
            }
            RemoteView::play(path, detail, "Local");
        } else if (item.status == DownloadStatus::Downloading) {
            std::string id = item.itemId;
            Dialog::cancelable("main/download/confirm_cancel"_i18n, [id]() {
                DownloadManager::instance().cancelDownload(id);
            });
        } else if (item.status == DownloadStatus::Queued) {
            DownloadManager::instance().resumeQueue();
        } else if (item.status == DownloadStatus::Failed) {
            // tap a failed row to retry it; X still removes it
            DownloadManager::instance().retryDownload(item.itemId);
        }
    }

    void clearData() override { this->rows.clear(); }

    /// Item of the row (nullptr for a section header)
    const DownloadItem* itemAt(size_t index) const {
        if (index >= this->rows.size()) return nullptr;
        auto& row = this->rows.at(index);
        return row.kind == Row::Kind::Item ? &row.item : nullptr;
    }

    /// Progress tick: updates the row, returns its index
    bool updateProgress(const std::string& itemId, int64_t downloaded, int64_t total, size_t& index) {
        for (size_t i = 0; i < this->rows.size(); i++) {
            auto& row = this->rows[i];
            if (row.kind != Row::Kind::Item || row.item.itemId != itemId) continue;
            if (row.item.status != DownloadStatus::Downloading) return false;
            row.item.downloadedBytes = downloaded;
            row.item.totalBytes = total;
            index = i;
            return true;
        }
        return false;
    }

private:
    void addSection(const std::string& title, std::vector<DownloadItem>& items) {
        if (items.empty()) return;
        Row header;
        header.kind = Row::Kind::Header;
        header.title = title;
        // only the storage card precedes the first section -> tighter header
        header.height = this->rows.size() <= 1 ? HEADER_FIRST_HEIGHT : HEADER_NEXT_HEIGHT;
        this->rows.push_back(std::move(header));
        for (auto& it : items) {
            Row row;
            row.kind = Row::Kind::Item;
            row.height = CARD_HEIGHT;
            row.item = std::move(it);
            this->rows.push_back(std::move(row));
        }
    }

    std::vector<Row> rows;
    std::string dlDir;
};

DownloadView::DownloadView() {
    this->inflateFromXMLRes("xml/tabs/downloads.xml");
    brls::Logger::debug("DownloadView: create");

    // The "Storage" card is row 0 of the list (StorageCard cell), so it scrolls
    // WITH the downloads instead of being pinned above. It is NOT a scrolled
    // header (RecyclingGrid::setHeaderView): that aborts Yoga when the Downloads
    // pill is the RemoteTab's default tab (nested frame laid out off-tree with an
    // indefinite height). A cell goes through the detached-positioning path and
    // is safe.
    this->recycler->registerCell("Storage", []() { return new StorageCard(); });
    this->recycler->registerCell("Empty", []() { return new DownloadEmptyCell(); });
    this->recycler->registerCell("Cell", []() { return new DownloadCard(); });
    this->recycler->registerCell("Header", []() { return new DownloadSectionHeader(); });
    // row 0 is the non-focusable storage card: the focus falls back to the
    // first focusable card (Box::getDefaultFocus)
    this->recycler->setDefaultCellFocus(0);

    auto deleteAction = [this](brls::View*) {
        auto* focus = dynamic_cast<RecyclingGridItem*>(brls::Application::getCurrentFocus());
        if (!focus) return false;
        auto* ds = dynamic_cast<DownloadDataSource*>(this->recycler->getDataSource());
        if (!ds) return false;
        const DownloadItem* item = ds->itemAt(focus->getIndex());
        if (!item) return false;
        std::string id = item->itemId;
        Dialog::cancelable("main/download/confirm_remove"_i18n, [this, id]() {
            DownloadManager::instance().removeDownload(id);
            this->loadItems();
            this->updateStorage();
        });
        return true;
    };
    this->recycler->registerAction("main/download/remove"_i18n, brls::BUTTON_X, deleteAction);
    this->recycler->registerAction(brls::BRLS_KBD_KEY_BACKSPACE, deleteAction);

    this->recycler->registerAction("main/download/remove_all"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        auto items = DownloadManager::instance().getItems();
        if (items.empty()) return false;
        Dialog::cancelable("main/download/confirm_remove_all"_i18n, [this, items]() {
            for (auto& it : items) DownloadManager::instance().removeDownload(it.itemId);
            this->loadItems();
            this->updateStorage();
        });
        return true;
    });

    this->recycler->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](...) {
        this->dismiss();
        return true;
    });

    this->statusSubId = DownloadManager::instance().getStatusEvent()->subscribe(
        [this](const std::string&, DownloadStatus) {
            this->loadItems();
            this->updateStorage();
        });

    // tick (<= 2 Hz): IN-PLACE update of the visible card — rebuilding the
    // list here reset the scroll and the focus on every tick
    this->progressSubId = DownloadManager::instance().getProgressEvent()->subscribe(
        [this](const std::string& itemId, int64_t downloaded, int64_t total, double speed) {
            auto* ds = dynamic_cast<DownloadDataSource*>(this->recycler->getDataSource());
            size_t index = 0;
            if (ds && ds->updateProgress(itemId, downloaded, total, index)) {
                auto* cell = dynamic_cast<DownloadCard*>(this->recycler->getGridItemByIndex(index));
                if (cell) cell->setProgress(downloaded, total, speed);
            } else {
                // row not yet materialized (safety): rebuild
                this->loadItems();
            }
            this->updateStorage();
        });

    this->loadItems();
    this->updateStorage();
}

DownloadView::~DownloadView() {
    brls::Logger::debug("DownloadView: deleted");
    DownloadManager::instance().getStatusEvent()->unsubscribe(this->statusSubId);
    DownloadManager::instance().getProgressEvent()->unsubscribe(this->progressSubId);
}

brls::View* DownloadView::getDefaultFocus() { return this->recycler; }

void DownloadView::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    this->loadItems();
    this->updateStorage();
}

void DownloadView::loadItems() {
    auto items = DownloadManager::instance().getItems();
    bool empty = items.empty();
    // always a data source now (row 0 = storage card, then the sections or an
    // empty placeholder row) — no setEmpty, which would hide the storage card
    this->recycler->setDataSource(new DownloadDataSource(std::move(items)));
    if (empty) {
        // no focusable card left (e.g. removing the last one): the focus was on
        // a now-recycled card -> ghost highlight halo. Deferred so a confirm
        // dialog has finished closing/restoring focus; if the focus is still
        // inside this (now unfocusable) tab, hand it back to the sidebar.
        ASYNC_RETAIN
        brls::sync([ASYNC_TOKEN]() {
            ASYNC_RELEASE
            if (hasFocusWithin(this)) AutoTabFrame::focus2Sidebar(this);
        });
    }
}

void DownloadView::updateStorage() {
    // in-place refresh of the storage card (row 0) — no list rebuild, so the
    // scroll/focus are preserved during download progress ticks
    auto* cell = dynamic_cast<StorageCard*>(this->recycler->getGridItemByIndex(DownloadDataSource::STORAGE_ROW));
    if (cell) cell->update();
}

void DownloadView::dismiss(std::function<void(void)> cb) { AutoTabFrame::focus2Sidebar(this); }
