#include "tab/dashboard.hpp"
#include "view/h_recycling.hpp"
#include "view/auto_tab_frame.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

// ----------------- Device Tab ----------------------------

class DeviceCell : public RecyclingGridItem {
public:
    DeviceCell() { this->inflateFromXMLRes("xml/view/device.xml"); }

    void setCell(const jellyfin::Device& item) {
        this->time->setText(item.DateLastActivity.substr(0, 19));
        this->name->setText(item.Name);
        this->app->setText(fmt::format("{} {}", item.AppName, item.AppVersion));
        this->user->setText(item.LastUserName);
    }

private:
    BRLS_BIND(brls::Label, time, "device/time");
    BRLS_BIND(brls::Label, name, "device/name");
    BRLS_BIND(brls::Label, app, "device/app");
    BRLS_BIND(brls::Label, user, "device/user");
};

class DeviceDataSource : public RecyclingGridDataSource {
public:
    using ItemList = std::vector<jellyfin::Device>;

    explicit DeviceDataSource(const ItemList& r) : list(std::move(r)) {
        brls::Logger::debug("DeviceDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        DeviceCell* cell = dynamic_cast<DeviceCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setCell(item);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {}

    void clearData() override { this->list.clear(); }

    void appendData(const ItemList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    ItemList list;
};

class DeviceTab : public RecyclingGrid {
public:
    DeviceTab() {
        this->setGrow(1.f);
        this->registerCell("Cell", []() { return new DeviceCell(); });
        this->spanCount = 1;
        this->estimatedRowSpace = 5;
        this->estimatedRowHeight = 60;
        this->onNextPage([this]() { this->doDevices(); });
        this->doDevices();
    }

    void doDevices() {
        std::string query = HTTP::encode_form({
            {"limit", std::to_string(this->pageSize)},
            {"startIndex", std::to_string(this->start)},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Device>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Device>& r) {
                ASYNC_RELEASE
                this->start = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->clearData();
                } else if (r.StartIndex == 0) {
                    this->setDataSource(new DeviceDataSource(r.Items));
                } else {
                    auto dataSrc = dynamic_cast<DeviceDataSource*>(this->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->notifyDataChanged();
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            jellyfin::apiDevices, query);
    }

private:
    size_t start = 0;
    size_t pageSize = 20;
};

// ----------------- ActivityLog Tab ----------------------------

class ActivityLogCell : public RecyclingGridItem {
public:
    ActivityLogCell() { this->inflateFromXMLRes("xml/view/activity_log.xml"); }

    void setCell(const jellyfin::ActivityLog& item) {
        this->time->setText(item.Date.substr(0, 19));
        this->name->setText(item.Name);
        this->overview->setText(item.ShortOverview);
        this->type->setText(item.Type);
    }

private:
    BRLS_BIND(brls::Label, time, "activity/time");
    BRLS_BIND(brls::Label, name, "activity/name");
    BRLS_BIND(brls::Label, overview, "activity/overview");
    BRLS_BIND(brls::Label, type, "activity/type");
};

class ActivityDataSource : public RecyclingGridDataSource {
public:
    using ItemList = std::vector<jellyfin::ActivityLog>;

    explicit ActivityDataSource(const ItemList& r) : list(std::move(r)) {
        brls::Logger::debug("ActivityDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        ActivityLogCell* cell = dynamic_cast<ActivityLogCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setCell(item);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {}

    void clearData() override { this->list.clear(); }

    void appendData(const ItemList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    ItemList list;
};

class ActivityLogTab : public RecyclingGrid {
public:
    ActivityLogTab() {
        this->setGrow(1.f);
        this->registerCell("Cell", []() { return new ActivityLogCell(); });
        this->spanCount = 1;
        this->estimatedRowSpace = 5;
        this->estimatedRowHeight = 60;
        this->onNextPage([this]() { this->doActivityLog(); });
        this->doActivityLog();
    }

    void doActivityLog() {
        std::string query = HTTP::encode_form({
            {"limit", std::to_string(this->pageSize)},
            {"startIndex", std::to_string(this->start)},
        });

        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::ActivityLog>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::ActivityLog>& r) {
                ASYNC_RELEASE
                this->start = r.StartIndex + this->pageSize;
                if (r.TotalRecordCount == 0) {
                    this->clearData();
                } else if (r.StartIndex == 0) {
                    this->setDataSource(new ActivityDataSource(r.Items));
                } else {
                    auto dataSrc = dynamic_cast<ActivityDataSource*>(this->getDataSource());
                    dataSrc->appendData(r.Items);
                    this->notifyDataChanged();
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            jellyfin::apiActivityLog, query);
    }

private:
    size_t start = 0;
    size_t pageSize = 15;
};

// ----------------- Session Tab ----------------------------

Dashboard::Dashboard() {
    brls::Logger::debug("Tab Dashboard: create");
    this->inflateFromXMLRes("xml/tabs/dashboard.xml");

    auto* item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setLabel("main/dashboard/device"_i18n);
    this->tabFrame->addTab(item, [this]() { return new DeviceTab(); });

    item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setLabel("main/dashboard/activity"_i18n);
    this->tabFrame->addTab(item, [this]() { return new ActivityLogTab(); });

    this->tabFrame->registerTabAction(this);
    this->doItemCount();
    this->doInfo();
}

Dashboard::~Dashboard() { brls::Logger::debug("View Dashboard: delete"); }

void Dashboard::doItemCount() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::ItemCount>(
        [ASYNC_TOKEN](const jellyfin::ItemCount& r) {
            ASYNC_RELEASE
            this->labelMovie->setText(std::to_string(r.MovieCount));
            this->labelSeries->setText(std::to_string(r.SeriesCount));
            this->labelMusic->setText(std::to_string(r.AlbumCount));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiItemCount);
}

void Dashboard::doInfo() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::SystemInfo>(
        [ASYNC_TOKEN](const jellyfin::SystemInfo& r) {
            ASYNC_RELEASE
            this->labelServer->setText(r.Version);
            this->labelName->setText(r.ServerName);
            this->labelAddr->setText(r.LocalAddress);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiInfo);
}