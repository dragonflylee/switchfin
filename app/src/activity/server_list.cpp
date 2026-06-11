/*
    Copyright 2023 dragonflylee
*/

#include "activity/server_list.hpp"
#include "activity/main_activity.hpp"
#include "activity/loading_activity.hpp"
#include "view/recycling_grid.hpp"
#include "view/auto_tab_frame.hpp"
#include "tab/server_add.hpp"
#include "tab/media_collection.hpp"
#include "utils/image.hpp"
#include "utils/dialog.hpp"
#include "api/plex/auth.hpp"

using namespace brls::literals;  // for _i18n

/// Middle-elides an overly long URL: "https://90-105-213-…plex.direct:32400".
/// The borealis DetailCell draws the detail right-aligned WITHOUT scissor
/// (cell_detail.cpp:41-48, label.cpp:492-507): text wider than its cell
/// overflows leftward over the title. So we bound the string upfront for a
/// deterministic render, preserving the more meaningful tail (host:port).
static std::string elideMiddle(const std::string& s, size_t budget) {
    if (s.size() <= budget) return s;
    size_t keep = budget - 1;  // 1 slot for the "…" ellipsis
    size_t head = (keep + 1) / 2;
    size_t tail = keep - head;
    return s.substr(0, head) + "…" + s.substr(s.size() - tail);
}

class ServerCell : public brls::Box {
public:
    ServerCell(const AppServer& s) {
        this->inflateFromXMLRes("xml/view/server_item.xml");

        this->setFocusSound(brls::SOUND_FOCUS_SIDEBAR);
        this->registerAction(
            "hints/ok"_i18n, brls::BUTTON_A,
            [](View* view) {
                brls::Application::onControllerButtonPressed(brls::BUTTON_NAV_RIGHT, false);
                return true;
            },
            false, false, brls::SOUND_CLICK_SIDEBAR);

        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

        this->labelName->setText(s.name.empty() ? "-" : s.name);
        this->labelUrl->setText(elideMiddle(s.urls.front(), 30));
    }

    void setActive(bool active) {
        auto theme = brls::Application::getTheme();
        if (active) {
            this->accent->setVisibility(brls::Visibility::VISIBLE);
            this->labelName->setTextColor(theme["brls/sidebar/active_item"]);
        } else {
            this->accent->setVisibility(brls::Visibility::INVISIBLE);
            this->labelName->setTextColor(theme["brls/text"]);
        }
    }

    bool getActive() { return this->accent->getVisibility() == brls::Visibility::VISIBLE; }

private:
    BRLS_BIND(brls::Rectangle, accent, "brls/sidebar/item_accent");
    BRLS_BIND(brls::Label, labelName, "server/name");
    BRLS_BIND(brls::Label, labelUrl, "server/url");
};

class UserCell : public RecyclingGridItem {
public:
    UserCell() { this->inflateFromXMLRes("xml/view/user_item.xml"); }
    ~UserCell() { Image::cancel(this->picture); }

    void prepareForReuse() override { this->picture->setImageFromRes("img/video-card-bg.png"); }

    void cacheForReuse() override { Image::cancel(this->picture); }

    BRLS_BIND(brls::Label, labelName, "user/name");
    BRLS_BIND(brls::Image, picture, "user/avatar");
};

class ServerUserDataSource : public RecyclingGridDataSource {
public:
    ServerUserDataSource(const std::vector<AppUser>& users, ServerList* server) : list(users), parent(server) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        UserCell* cell = dynamic_cast<UserCell*>(recycler->dequeueReusableCell("Cell"));
        auto& u = this->list.at(index);
        cell->labelName->setText(u.name);

        cell->registerAction("hints/delete"_i18n, brls::BUTTON_X, [this, u](brls::View* view) {
            Dialog::cancelable("main/setting/server/delete"_i18n, [this, u]() {
                AppConfig::instance().removeUser(u.id);
                this->parent->onUser(u.server_id);
            });
            return true;
        });

        // plex.tv avatar (absolute URL)
        if (!u.thumb.empty()) Image::with(cell->picture, u.thumb);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        brls::Application::blockInputs();
        // The URL probing below can take several seconds (2 s timeout PER
        // URL, in series, cf. plex_auth.cpp:probeConnection; plex.direct
        // servers advertise 10+ of them): show a loading screen rather
        // than a frozen one.
        brls::Application::pushActivity(new LoadingActivity(), brls::TransitionAnimation::NONE);
        std::string unreachable = "main/plex/unreachable"_i18n;

        brls::async([this, index, unreachable]() {
            auto& u = this->list.at(index);

            try {
                AppServer target;
                bool found = false;
                for (auto& s : AppConfig::instance().getServers()) {
                    if (s.id == u.server_id) {
                        target = s;
                        found = true;
                    }
                }
                if (!found) throw std::runtime_error(unreachable);

                // Refreshes this profile's server token when plex.tv responds;
                // offline we keep the remembered token (direct LAN).
                try {
                    for (auto& r : plex::getResources(u.access_token)) {
                        if (r.clientIdentifier == target.id && !r.accessToken.empty())
                            target.access_token = r.accessToken;
                    }
                } catch (const std::exception& ex) {
                    brls::Logger::warning("refresh resources: {}", ex.what());
                }

                std::string base;
                for (auto& url : target.urls) {
                    if (plex::probeConnection(url, target.access_token)) {
                        base = url;
                        break;
                    }
                }
                if (base.empty()) throw std::runtime_error(unreachable);

                brls::sync([u, target, base]() {
                    AppServer s = target;
                    s.urls = {base};
                    AppConfig::instance().addServer(s);
                    AppConfig::instance().addUser(u, base);
                    brls::Application::unblockInputs();
                    brls::Application::clear();
                    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                    MediaCollection::clearPref();
                });
            } catch (const std::exception& ex) {
                std::string msg = ex.what();
                brls::sync([msg]() {
                    brls::Application::unblockInputs();
                    // removes the loading screen before showing the error
                    brls::Application::popActivity(brls::TransitionAnimation::NONE);
                    Dialog::show(msg);
                });
            }
        });
    }

    void clearData() override { this->list.clear(); }

private:
    std::vector<AppUser> list;
    ServerList* parent;
};

ServerList::ServerList() { brls::Logger::debug("ServerList: create"); }

ServerList::~ServerList() { brls::Logger::debug("ServerList Activity: delete"); }

void ServerList::onContentAvailable() {
    this->inputUrl->detail->setSingleLine(true);
    // The "detail" cell is capped at 300px (cell_detail.cpp:44): widen it so
    // the elided URL (~36 chars) shows without re-truncation at the right edge.
    this->inputUrl->detail->setMaxWidth(520);
    this->tabFrame->setTabChangedAction([this](size_t index) {
        if (!index) this->willAppear();
    });

    this->btnServerAdd->registerClickAction([](brls::View* view) {
        view->present(new ServerAdd());
        return true;
    });

    if (brls::Application::getActivitiesStack().size() > 1) {
        brls::View* tab = this->getView("tab/remote");
        if (tab) tab->setVisibility(brls::Visibility::GONE);
    }

    this->sidebarServers->registerAction("main/setting/server/connect_new"_i18n, brls::BUTTON_Y, [](brls::View* view) {
        view->present(new ServerAdd());
        return true;
    });

    this->recyclerUsers->registerCell("Cell", []() { return new UserCell(); });

    this->btnSignin->registerClickAction([](brls::View* view) {
        view->present(new ServerAdd());
        return true;
    });
}

void ServerList::willAppear(bool resetState) {
    auto list = AppConfig::instance().getServers();
    ServerCell* item = nullptr;
    std::string url = AppConfig::instance().getUrl();
    this->sidebarServers->clearViews();

    for (auto& s : list) {
        item = new ServerCell(s);
        item->getFocusEvent()->subscribe([this, s](brls::View* view) {
            this->setActive(view);
            this->onServer(s);
        });

        item->registerAction("hints/delete"_i18n, brls::BUTTON_X, [this, s](brls::View* item) {
            Dialog::cancelable("main/setting/server/delete"_i18n, [this, item, s]() {
                if (AppConfig::instance().removeServer(s.id)) {
                    brls::View* view = new ServerAdd();
                    this->tabFrame->setTabAttachedView(view);
                    brls::Application::giveFocus(view);
                } else {
                    this->sidebarServers->removeView(item);
                }
            });
            return true;
        });

        if (s.urls.size() > 0) {
            if (url.empty()) {
                url = s.urls.front();
            }
            if (s.urls.front() == url) {
                item->setActive(true);
                this->onServer(s);
            }
        }

        this->sidebarServers->addView(item);
    }

    if (!item) this->tabFrame->setTabAttachedView(new ServerAdd());
}

void ServerList::onServer(const AppServer& s) {
    this->activeUrl = s.urls.front();
    this->inputUrl->setDetailText(elideMiddle(this->activeUrl, 36));
    this->inputUrl->registerAction("hints/preset"_i18n, brls::BUTTON_X, [this, s](...) {
        return brls::Application::getImeManager()->openForText(
            [this, s](const std::string& text) {
                AppServer server;
                server.id = s.id;
                server.urls.push_back(text);
                AppConfig::instance().addServer(server);
                this->activeUrl = text;
                this->inputUrl->setDetailText(elideMiddle(text, 36));
            },
            "main/setting/url"_i18n, "", 255, this->activeUrl);
    });
    this->inputUrl->registerClickAction([this, s](...) {
        brls::Dropdown* dropdown = new brls::Dropdown("main/setting/url"_i18n, s.urls, [this, s](int selected) {
            AppServer server;
            const std::string& url = s.urls[selected];
            server.id = s.id;
            server.urls.push_back(url);
            AppConfig::instance().addServer(server);
            this->activeUrl = url;
            this->inputUrl->setDetailText(elideMiddle(url, 36));
        });
        brls::Application::pushActivity(new brls::Activity(dropdown));
        return true;
    });

    this->onUser(s.id);
}

void ServerList::onUser(const std::string& id) {
    auto users = AppConfig::instance().getUsers(id);
    if (users.empty()) {
        this->recyclerUsers->setEmpty();
        brls::sync([this]() { brls::Application::giveFocus(this->sidebarServers); });
    } else {
        this->recyclerUsers->setDataSource(new ServerUserDataSource(users, this));
    }
}

std::string ServerList::getUrl() { return this->activeUrl; }

void ServerList::setActive(brls::View* active) {
    for (auto item : this->sidebarServers->getChildren()) {
        ServerCell* cell = dynamic_cast<ServerCell*>(item);
        if (cell) cell->setActive(item == active);
    }
}