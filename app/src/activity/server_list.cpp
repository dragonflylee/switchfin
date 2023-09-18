/*
    Copyright 2023 dragonflylee
*/

#include "activity/server_list.hpp"
#include "activity/main_activity.hpp"
#include "view/recycling_grid.hpp"
#include "tab/server_add.hpp"
#include "tab/server_login.hpp"
#include "utils/image.hpp"
#include "utils/dialog.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

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

        this->labelName->setText(s.name);
        this->labelUrl->setText(s.urls.back());
        this->labelUsers->setText(fmt::format(fmt::runtime("main/setting/server/users"_i18n), s.users.size()));
    }

    void setActive(bool active) {
        auto theme = brls::Application::getTheme();
        if (active) {
            this->accent->setVisibility(brls::Visibility::VISIBLE);
            this->labelName->setTextColor(theme["brls/sidebar/active_item"]);
            this->labelUsers->setTextColor(theme["brls/sidebar/active_item"]);
        } else {
            this->accent->setVisibility(brls::Visibility::INVISIBLE);
            this->labelName->setTextColor(theme["brls/text"]);
            this->labelUsers->setTextColor(theme["brls/text"]);
        }
    }

private:
    BRLS_BIND(brls::Rectangle, accent, "brls/sidebar/item_accent");
    BRLS_BIND(brls::Label, labelName, "server/name");
    BRLS_BIND(brls::Label, labelUrl, "server/url");
    BRLS_BIND(brls::Label, labelUsers, "server/users");
};

class UserCell : public RecyclingGridItem {
public:
    UserCell() { this->inflateFromXMLRes("xml/view/user_item.xml"); }

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
        // Image::with(cell->picture, this->parent->getUrl() + fmt::format(fmt::runtime(jellyfin::apiUserImage), u.id, ""));
        return cell;
    }

    void onItemSelected(brls::View* recycler, size_t index) override {
        brls::Application::blockInputs();

        brls::async([this, index]() {
            auto& u = this->list.at(index);
            HTTP::Header header = {fmt::format("X-Emby-Token: {}", u.access_token)};
            try {
                HTTP::get(this->parent->getUrl() + jellyfin::apiInfo, header, HTTP::Timeout{default_timeout});
                brls::sync([this, u]() {
                    AppConfig::instance().addUser(u, this->parent->getUrl());
                    brls::Application::unblockInputs();
                    brls::Application::clear();
                    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                });
            } catch (const std::exception& ex) {
                std::string msg = ex.what();
                brls::sync([msg]() {
                    brls::Application::unblockInputs();
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
    auto list = AppConfig::instance().getServers();
    if (list.empty()) {
        brls::AppletFrame* view = new brls::AppletFrame(new ServerAdd());
        view->setHeaderVisibility(brls::Visibility::GONE);
        this->setContentView(view);
        return;
    }

    this->recyclerUsers->registerCell("Cell", []() { return new UserCell(); });
    this->btnServerAdd->registerClickAction([](brls::View* view) {
        view->present(new ServerAdd());
        return true;
    });

    for (auto& s : list) {
        ServerCell* item = new ServerCell(s);
        item->getFocusEvent()->subscribe([this, s](brls::View* view) {
            this->setActive(view);
            this->onSelect(s);
        });

        if (s.urls.front() == AppConfig::instance().getUrl()) {
            item->setActive(true);
            this->onSelect(s);
        }

        this->items.push_back(item);
        this->sidebarServers->addView(item);
    }
}

void ServerList::onSelect(const AppServer& s) {
    this->serverVersion->setDetailText(s.version);
    this->serverOS->setDetailText(s.os.empty() ? "-" : s.os);
    this->selectorUrl->init("main/setting/url"_i18n, s.urls, 0, [](size_t selected) {

    });

    this->btnSignin->registerClickAction([this, s](brls::View* view) {
        view->present(new ServerLogin(s.name, this->getUrl()));
        return true;
    });

    if (s.users.empty()) {
        this->recyclerUsers->setEmpty();
    } else {
        this->recyclerUsers->setDataSource(new ServerUserDataSource(s.users, this));
    }
}

std::string ServerList::getUrl() { return this->selectorUrl->detail->getFullText(); }

void ServerList::setActive(brls::View* active) {
    for (ServerCell* item : this->items) {
        if (item == active)
            item->setActive(true);
        else
            item->setActive(false);
    }
}