/*
    Copyright 2023 dragonflylee
*/

#include "tab/server_add.hpp"
#include "tab/server_login.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "api/jellyfin.hpp"

ServerAdd::ServerAdd() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/server_add.xml");
    brls::Logger::debug("ServerAdd: create");

    inputUrl->init("URL", "https://");

    btnConnect->registerClickAction([this](...) { return this->onConnect(); });
}

ServerAdd::~ServerAdd() { brls::Logger::debug("ServerAdd Activity: delete"); }

ServerAdd* ServerAdd::create() { return new ServerAdd(); }

bool ServerAdd::onConnect() {
    this->btnConnect->setActionsAvailable(false);
    this->btnConnect->setTextColor(brls::Application::getTheme().getColor("font/grey"));
    std::string baseUrl = this->inputUrl->getValue();
    brls::Logger::debug("ServerAdd onConnect: click {}", baseUrl);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, baseUrl]() {
        try {
            auto resp = HTTP::get(baseUrl + jellyfin::apiPublicInfo, HTTP::Timeout{});
            jellyfin::PublicSystemInfo info = nlohmann::json::parse(std::get<1>(resp));
            AppServer s = {
                info.ServerName,
                info.Id,
                info.Version,
                info.OperatingSystem,
                {baseUrl},
            };
            brls::sync([ASYNC_TOKEN, s]() {
                ASYNC_RELEASE
                AppConfig::instance().addServer(s);
                this->present(new ServerLogin(s));
            });
        } catch (const std::exception& ex) {
            brls::sync([ASYNC_TOKEN, &ex]() {
                ASYNC_RELEASE
                this->btnConnect->setActionsAvailable(true);
                this->btnConnect->setTextColor(brls::Application::getTheme().getColor("brls/text"));
                Dialog::show(ex.what());
            });
        }
    });
    return false;
}
