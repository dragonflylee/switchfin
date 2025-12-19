#include "tab/dashboard.hpp"
#include "view/h_recycling.hpp"
#include "view/auto_tab_frame.hpp"
#include "utils/image.hpp"
#include "utils/misc.hpp"
#include "utils/dialog.hpp"
#include "utils/keybind.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

inline const brls::ButtonStyle BUTTONSTYLE_DANDER = {
    .shadowType = brls::ShadowType::GENERIC,
    .hideHighlightBackground = true,

    .highlightPadding = "brls/button/primary_highlight_padding",
    .borderThickness = "",

    .enabledBackgroundColor = "color/danger",
    .enabledLabelColor = "brls/button/default_enabled_text",
    .enabledBorderColor = "",

    .disabledBackgroundColor = "color/danger",
    .disabledLabelColor = "brls/button/default_disabled_text",
    .disabledBorderColor = "",
};

// ----------------- Media Count ---------------------------

using ItemCount = std::pair<std::string, long>;

class ItemCountCell : public RecyclingGridItem {
public:
    ItemCountCell() {
        this->setPadding(15);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        this->setBackgroundColor(brls::Application::getTheme()["color/grey_2"]);

        this->name = new brls::Label();
        this->count = new brls::Label();

        this->addView(this->name);
        this->addView(this->count);
    }

    void setCell(const ItemCount& it) {
        this->name->setText(it.first);
        this->count->setText(std::to_string(it.second));
    }

private:
    brls::Label* name;
    brls::Label* count;
};

class ItemCountDataSource : public RecyclingGridDataSource {
public:
    using ItemList = std::vector<ItemCount>;

    explicit ItemCountDataSource(const jellyfin::ItemCount& r) {
        this->list.push_back({"main/search/genres/movie"_i18n, r.MovieCount});
        this->list.push_back({"main/search/genres/series"_i18n, r.SeriesCount});
        this->list.push_back({"main/search/genres/music"_i18n, r.SongCount});
        this->list.push_back({"main/search/genres/episode"_i18n, r.EpisodeCount});
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        ItemCountCell* cell = dynamic_cast<ItemCountCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setCell(item);
        return cell;
    }

    void clearData() override { this->list.clear(); }

private:
    ItemList list;
};

// ----------------- Device Tab ----------------------------

class DeviceCell : public RecyclingGridItem {
public:
    DeviceCell() { this->inflateFromXMLRes("xml/view/device.xml"); }

    void setCell(const jellyfin::Device& item) {
        this->time->setText(misc::formatTime(item.DateLastActivity));
        this->name->setText(item.Name);
        this->app->setText(fmt::format("{} {}", item.AppName, item.AppVersion));
        this->user->setText(item.LastUserName);
    }

    void setImage(const std::string& userId, const std::string& tag) {
        Image::load(this->avatar, jellyfin::apiUserImage, userId, "tag=" + tag);
    }

    void prepareForReuse() override { this->avatar->setImageFromRes("img/account.png"); }

    void cacheForReuse() override { Image::cancel(this->avatar); }

private:
    BRLS_BIND(brls::Label, time, "device/time");
    BRLS_BIND(brls::Label, name, "device/name");
    BRLS_BIND(brls::Label, app, "device/app");
    BRLS_BIND(brls::Label, user, "device/user");
    BRLS_BIND(brls::Image, avatar, "device/avatar");
};

using UserTag = std::unordered_map<std::string, std::string>;

class DeviceDataSource : public RecyclingGridDataSource {
public:
    using ItemList = std::vector<jellyfin::Device>;

    explicit DeviceDataSource(const ItemList& r, const UserTag& tags) : list(std::move(r)), userTags(tags) {
        brls::Logger::debug("DeviceDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        DeviceCell* cell = dynamic_cast<DeviceCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setCell(item);

        auto it = this->userTags.find(item.LastUserId);
        if (it != this->userTags.end()) cell->setImage(it->first, it->second);
        return cell;
    }

    void clearData() override { this->list.clear(); }

private:
    ItemList list;
    UserTag userTags;
};

class DeviceTab : public RecyclingGrid {
public:
    DeviceTab() {
        this->setGrow(1.f);
        this->registerCell("Cell", []() { return new DeviceCell(); });
        this->spanCount = 1;
        this->estimatedRowSpace = 5;
        this->estimatedRowHeight = 60;
        this->doUsers();

        auto doRefresh = [this](...) {
            this->showSkeleton();
            this->doDevices();
            return true;
        };
        this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, doRefresh);
        this->registerAction(KeyBind::getRefresh(), doRefresh);
    }

    void doDevices() {
        ASYNC_RETAIN
        jellyfin::getJSON<jellyfin::Result<jellyfin::Device>>(
            [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Device>& r) {
                ASYNC_RELEASE
                if (r.TotalRecordCount == 0) {
                    this->setEmpty();
                } else {
                    this->setDataSource(new DeviceDataSource(r.Items, this->userTags));
                }
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            jellyfin::apiDevices);
    }

    void doUsers() {
        ASYNC_RETAIN
        jellyfin::getJSON<std::vector<jellyfin::UserInfo>>(
            [ASYNC_TOKEN](const std::vector<jellyfin::UserInfo>& list) {
                ASYNC_RELEASE
                for (auto& u : list) {
                    if (u.PrimaryImageTag.size() > 0) {
                        userTags.insert_or_assign(u.Id, u.PrimaryImageTag);
                    }
                }
                this->doDevices();
            },
            [ASYNC_TOKEN](const std::string& ex) {
                ASYNC_RELEASE
                this->setError(ex);
            },
            jellyfin::apiUsers);
    }

private:
    UserTag userTags;
};

// ----------------- ActivityLog Tab ----------------------------

class ActivityLogCell : public RecyclingGridItem {
public:
    ActivityLogCell(const std::string& res) { this->inflateFromXMLRes(res); }

    void setCell(const jellyfin::ActivityLog& item) {
        this->time->setText(misc::formatTime(item.Date));
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
        this->registerCell("Cell", []() { return new ActivityLogCell("xml/view/activity_log.xml"); });
        this->spanCount = 1;
        this->estimatedRowSpace = 5;
        this->estimatedRowHeight = 60;
        this->onNextPage([this]() { this->doActivityLog(); });
        this->doActivityLog();

        auto doRefresh = [this](...) {
            this->start = 0;
            this->showSkeleton();
            this->doActivityLog();
            return true;
        };
        this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, doRefresh);
        this->registerAction(KeyBind::getRefresh(), doRefresh);
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
                    this->setEmpty();
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

// ----------------- Session ----------------------------

class SessionCell : public RecyclingGridItem {
public:
    SessionCell() { this->inflateFromXMLRes("xml/view/session.xml"); }

    void prepareForReuse() override { this->avatar->setImageFromRes("img/account.png"); }

    void cacheForReuse() override { Image::cancel(this->avatar); }

    void setCell(const jellyfin::Session& item) {
        this->name->setText(item.DeviceName);
        this->app->setText(fmt::format("{} {}", item.Client, item.ApplicationVersion));
        this->user->setText(item.UserName);
        if (item.UserPrimaryImageTag.size() > 0) {
            Image::load(this->avatar, jellyfin::apiUserImage, item.UserId, "tag=" + item.UserPrimaryImageTag);
        }
        if (item.NowPlayingItem.Id.size() > 0) {
            if (item.NowPlayingItem.Type == jellyfin::mediaTypeEpisode) {
                this->playing->setText(fmt::format("S{}E{} - {}", item.NowPlayingItem.ParentIndexNumber,
                    item.NowPlayingItem.IndexNumber, item.NowPlayingItem.Name));
            } else {
                this->playing->setText(item.NowPlayingItem.Name);
            }
        } else {
            this->playing->setText(misc::formatTime(item.LastActivityDate));
        }
    }

private:
    BRLS_BIND(brls::Label, name, "session/name");
    BRLS_BIND(brls::Label, app, "session/app");
    BRLS_BIND(brls::Label, user, "session/user");
    BRLS_BIND(brls::Label, playing, "session/playing");
    BRLS_BIND(brls::Image, avatar, "session/avatar");
};

class SessionDataSource : public RecyclingGridDataSource {
public:
    using ItemList = std::vector<jellyfin::Session>;

    explicit SessionDataSource(const ItemList& r) : list(std::move(r)) {
        brls::Logger::debug("SessionDataSource: create {}", r.size());
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        SessionCell* cell = dynamic_cast<SessionCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setCell(item);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {}

    void clearData() override { this->list.clear(); }

private:
    ItemList list;
};

// ----------------- Storage ----------------------------

class StorageView : public brls::Box {
public:
    StorageView(const jellyfin::FolderInfo& info) {
        this->path = new brls::Label();
        this->size = new brls::Label();
        this->line = new brls::Rectangle();

        brls::Theme theme = brls::Application::getTheme();

        this->setPadding(20);
        this->setMarginBottom(20);
        this->setAxis(brls::Axis::COLUMN);
        this->setBackgroundColor(theme["color/grey_2"]);

        this->line->setColor(theme["brls/slider/line_filled"]);
        this->line->setMarginTop(15);
        this->line->setMarginBottom(10);
        this->line->setHeight(7.f);

        this->path->setText(info.Path);
        this->size->setFontSize(16);
        this->size->setTextColor(theme["font/grey"]);
        this->size->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        this->size->setText(fmt::format("{} / {}", misc::formatSize(info.UsedSpace), misc::formatSize(info.FreeSpace)));
        this->line->setWidthPercentage(info.UsedSpace * 100.f / (info.UsedSpace + info.FreeSpace));

        this->addView(path);
        this->addView(line);
        this->addView(size);
    }

private:
    brls::Label* path;
    brls::Label* size;
    brls::Rectangle* line;
};

Dashboard::Dashboard() {
    brls::Logger::debug("Tab Dashboard: create");
    this->inflateFromXMLRes("xml/tabs/dashboard.xml");

    auto* item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setLabel("main/dashboard/device"_i18n);
    this->tabFrame->addTab(item, []() { return new DeviceTab(); });

    item = new AutoSidebarItem();
    item->setTabStyle(AutoTabBarStyle::ACCENT);
    item->setLabel("main/dashboard/activity"_i18n);
    this->tabFrame->addTab(item, []() { return new ActivityLogTab(); });

    this->btnRestart->setStyle(&BUTTONSTYLE_DANDER);
    this->btnRestart->registerClickAction([this](...) {
        Dialog::cancelable("main/dashboard/confirm_restart"_i18n, [this] { this->doRestart(); });
        return true;
    });
    this->btnScan->registerClickAction([this](...) {
        this->doRunTask("RefreshLibrary");
        return true;
    });

    this->tabFrame->registerTabAction(this);
    this->activity->registerCell("Cell", []() { return new ActivityLogCell("xml/view/activity_warn.xml"); });
    this->sess->registerCell("Cell", []() { return new SessionCell(); });
    this->itemCount->registerCell("Cell", []() { return new ItemCountCell(); });

    if (brls::Application::ORIGINAL_WINDOW_HEIGHT < 720) {
        this->itemCount->setVisibility(brls::Visibility::GONE);
    } else {
        this->doItemCount();
    }

    auto doRefresh = [this](...) {
        this->sess->showSkeleton();
        this->activity->showSkeleton();
        this->doSession();
        this->doActivityWarn();
        return true;
    };
    this->mainBox->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, doRefresh);
    this->mainBox->registerAction(KeyBind::getRefresh(), doRefresh);

    this->doSystemInfo();
    this->doActivityWarn();
    this->doSession();
    this->doListTask();
    this->doStorage();
}

Dashboard::~Dashboard() { brls::Logger::debug("View Dashboard: delete"); }

void Dashboard::doItemCount() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::ItemCount>(
        [ASYNC_TOKEN](const jellyfin::ItemCount& r) {
            ASYNC_RELEASE
            this->itemCount->setDataSource(new ItemCountDataSource(r));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiItemCount);
}

void Dashboard::doSystemInfo() {
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
        jellyfin::apiSystemInfo);
}

void Dashboard::doActivityWarn() {
    std::string query = HTTP::encode_form({
        {"limit", "4"},
        {"hasUserId", "false"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::ActivityLog>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::ActivityLog>& r) {
            ASYNC_RELEASE
            if (r.TotalRecordCount == 0) {
                this->activity->setEmpty();
            } else {
                this->activity->setDataSource(new ActivityDataSource(r.Items));
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->activity->setError(ex);
        },
        jellyfin::apiActivityLog, query);
}

void Dashboard::doSession() {
    ASYNC_RETAIN
    jellyfin::getJSON<std::vector<jellyfin::Session>>(
        [ASYNC_TOKEN](const std::vector<jellyfin::Session>& r) {
            ASYNC_RELEASE
            this->sess->setDataSource(new SessionDataSource(r));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiSessionList, "activeWithinSeconds=960");
}

void Dashboard::doRestart() {
    this->btnRestart->setState(brls::ButtonState::DISABLED);
    ASYNC_RETAIN
    jellyfin::postJSON(
        {},
        [ASYNC_TOKEN](...) {
            ASYNC_RELEASE
            this->btnRestart->setState(brls::ButtonState::ENABLED);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
            this->btnRestart->setState(brls::ButtonState::ENABLED);
        },
        jellyfin::apiRestart);
}

void Dashboard::doListTask() {
    ASYNC_RETAIN
    jellyfin::getJSON<std::vector<jellyfin::TaskInfo>>(
        [ASYNC_TOKEN](const std::vector<jellyfin::TaskInfo>& r) {
            ASYNC_RELEASE
            for (auto& task : r) this->taskMap.insert_or_assign(task.Key, task.Id);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiScheduledTasks);
}

void Dashboard::doRunTask(const std::string& id) {
    auto it = this->taskMap.find(id);
    if (it == this->taskMap.end()) return;

    this->btnScan->setState(brls::ButtonState::DISABLED);
    ASYNC_RETAIN
    jellyfin::postJSON(
        {},
        [ASYNC_TOKEN](...) {
            ASYNC_RELEASE
            this->btnScan->setState(brls::ButtonState::ENABLED);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
            this->btnScan->setState(brls::ButtonState::ENABLED);
        },
        jellyfin::apiRunTask, it->second);
}

void Dashboard::doStorage() {
    brls::View* cell = new SkeletonCell();
    cell->setGrow(1.0f);
    this->storage->addView(cell);

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::StorageInfo>(
        [ASYNC_TOKEN](const jellyfin::StorageInfo& r) {
            ASYNC_RELEASE
            this->storage->clearViews();
            this->storage->addView(new StorageView(r.WebFolder));
            this->storage->addView(new StorageView(r.LogFolder));
            this->storage->addView(new StorageView(r.CacheFolder));
            this->storage->addView(new StorageView(r.TranscodingTempFolder));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->storage->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiStorage);
}