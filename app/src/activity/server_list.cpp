/*
    Copyright 2023 dragonflylee
*/

#include "activity/server_list.hpp"
#include "tab/server_add.hpp"
#include "tab/server_login.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

class ServerUserDataSource : public RecyclingGridDataSource {
public:
    void clearData() override {}

    ServerUserDataSource(const std::vector<AppUser>& users) : list(std::move(users)) {}

    size_t getItemCount() override { return this->list.size(); }

private:
    std::vector<AppUser> list;
};

class ServerListDataSource : public RecyclingGridDataSource {
public:
    using Event = std::function<void(const AppServer&)>;

    ServerListDataSource(const std::vector<AppServer>& s) : list(std::move(s)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        ServerCell* cell = dynamic_cast<ServerCell*>(recycler->dequeueReusableCell("Cell"));
        auto& s = this->list[index];
        cell->labelName->setText(s.name);
        cell->labelUrl->setText(s.urls.back());
        cell->labelUsers->setText(fmt::format("main/setting/server/users"_i18n, s.users.size()));
        return cell;
    }

    void setEvent(const Event& ev) { this->onSelect = ev; }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override { this->onSelect(this->list[index]); }

    void clearData() override { this->list.clear(); }

private:
    std::vector<AppServer> list;
    Event onSelect;
};

ServerCell::ServerCell() { this->inflateFromXMLRes("xml/cell/server.xml"); }

ServerCell* ServerCell::create() { return new ServerCell(); }

ServerList::ServerList() { brls::Logger::debug("ServerList: create"); }

ServerList::~ServerList() { brls::Logger::debug("ServerList Activity: delete"); }

void ServerList::onContentAvailable() {
    auto svrs = AppConfig::instance().getServers();
    auto dataSrc = new ServerListDataSource(svrs);
    this->recyclerServers->registerCell("Cell", &ServerCell::create);
    this->recyclerServers->setDataSource(dataSrc);
    dataSrc->setEvent([this](const AppServer& s) { this->onSelect(s); });
    if (svrs.size() > 0) {
        this->onSelect(svrs[this->recyclerServers->getDefaultFocusedIndex()]);
    } else {
        brls::AppletFrame *view = new brls::AppletFrame(new ServerAdd());
        view->setHeaderVisibility(brls::Visibility::GONE);
        this->setContentView(view);
    }
}

void ServerList::onSelect(const AppServer& s) {
    this->serverVersion->setDetailText(s.version);
    this->serverOS->setDetailText(s.os);
    this->selectorUrl->init("main/setting/url"_i18n, s.urls, 0, [](size_t selected) {

    });

    this->btnSignin->registerClickAction([this, s](...) {
        this->appletFrame->pushContentView(new ServerLogin(s));
        return true;
    });

    if (s.users.empty()) {
        this->recyclerUsers->setVisibility(brls::Visibility::GONE);
    } else {
        this->recyclerUsers->setVisibility(brls::Visibility::VISIBLE);
        this->recyclerUsers->setDataSource(new ServerUserDataSource(s.users));
    }
}