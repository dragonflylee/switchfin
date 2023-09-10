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

class ServerCell : public RecyclingGridItem {
public:
    ServerCell() { this->inflateFromXMLRes("xml/view/server_item.xml"); }

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
    ServerUserDataSource(const std::vector<AppUser>& users) : list(users) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        UserCell* cell = dynamic_cast<UserCell*>(recycler->dequeueReusableCell("Cell"));
        auto& u = this->list.at(index);
        cell->labelName->setText(u.name);
        Image::load(cell->picture, jellyfin::apiUserImage, u.id, "");
        return cell;
    }

    void onItemSelected(brls::View* recycler, size_t index) override {
        auto& u = this->list.at(index);
        brls::Application::blockInputs();

        brls::async([u]() {
            auto& conf = AppConfig::instance();
            HTTP::Header header = {
                fmt::format("X-Emby-Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", "
                            "Version=\"{}\", Token=\"{}\"",
                    AppVersion::getPackageName(), AppVersion::getDeviceName(), conf.getDevice(), AppVersion::getVersion(),
                    u.access_token)};
            const long timeout = conf.getItem(AppConfig::REQUEST_TIMEOUT, default_timeout);
            try {
                HTTP::get(conf.getUrl() + jellyfin::apiInfo, header, HTTP::Timeout{timeout});
                brls::sync([u]() {
                    AppConfig::instance().addUser(u);
                    brls::Application::unblockInputs();
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
};

class ServerListDataSource : public RecyclingGridDataSource {
public:
    using Event = brls::Event<AppServer>;

    ServerListDataSource(Event::Callback ev) {
        this->list = AppConfig::instance().getServers();
        this->onSelect.subscribe(ev);
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        ServerCell* cell = dynamic_cast<ServerCell*>(recycler->dequeueReusableCell("Cell"));
        auto& s = this->list.at(index);
        cell->labelName->setText(s.name);
        cell->labelUrl->setText(s.urls.back());
        cell->labelUsers->setText(fmt::format(fmt::runtime("main/setting/server/users"_i18n), s.users.size()));
        return cell;
    }

    void onItemSelected(brls::View* recycler, size_t index) override { this->onSelect.fire(this->list[index]); }

    void clearData() override { this->list.clear(); }

private:
    std::vector<AppServer> list;
    Event onSelect;
};

ServerList::ServerList() { brls::Logger::debug("ServerList: create"); }

ServerList::~ServerList() { brls::Logger::debug("ServerList Activity: delete"); }

void ServerList::onContentAvailable() {
    auto dataSrc = new ServerListDataSource([this](const AppServer& s) { this->onSelect(s); });
    this->recyclerServers->registerCell("Cell", []() { return new ServerCell(); });
    this->recyclerUsers->registerCell("Cell", []() { return new UserCell(); });
    this->recyclerServers->setDataSource(dataSrc);

    if (dataSrc->getItemCount() > 0) {
        dataSrc->onItemSelected(this->recyclerServers, 0);

        this->btnServerAdd->registerClickAction([](brls::View *view) {
            view->present(new ServerAdd());
            return true;
        });
    } else {
        brls::AppletFrame* view = new brls::AppletFrame(new ServerAdd());
        view->setHeaderVisibility(brls::Visibility::GONE);
        this->setContentView(view);
    }
}

void ServerList::onSelect(const AppServer& s) {
    this->serverVersion->setDetailText(s.version);
    this->serverOS->setDetailText(s.os);
    this->selectorUrl->init("main/setting/url"_i18n, s.urls, 0, [](size_t selected) {

    });

    this->btnSignin->registerClickAction([s](brls::View *view) {
        view->present(new ServerLogin(s));
        return true;
    });

    if (s.users.empty()) {
        this->recyclerUsers->setVisibility(brls::Visibility::GONE);
    } else {
        this->recyclerUsers->setVisibility(brls::Visibility::VISIBLE);
        this->recyclerUsers->setDataSource(new ServerUserDataSource(s.users));
    }
}