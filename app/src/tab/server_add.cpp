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

    inputUrl->init("URL", "https://jf1.3m3m.top");

    btnConnect->registerClickAction(std::bind(&ServerAdd::onConnect, this));
}

ServerAdd::~ServerAdd() { brls::Logger::debug("ServerAdd Activity: delete"); }

ServerAdd* ServerAdd::create() { return new ServerAdd(); }

bool ServerAdd::onConnect() {
    this->btnConnect->setActionsAvailable(false);
    this->btnConnect->setTextColor(brls::Application::getTheme().getColor("font/grey"));
    std::string baseUrl = this->inputUrl->getValue();
    brls::Logger::debug("ServerAdd onConnect: click");

    ASYNC_RETAIN
    HTTP::get_async(
        [ASYNC_TOKEN](const std::string& resp) {
            jellyfin::PublicSystemInfo info = nlohmann::json::parse(resp);
            brls::sync([ASYNC_TOKEN, info]() {
                ASYNC_RELEASE
                AppServer s = {
                    info.ServerName,
                    info.Id,
                    info.Version,
                    info.OperatingSystem,
                    {this->inputUrl->getValue()},
                };
                AppConfig::instance().addServer(s);
                this->present(new ServerLogin(s));
            });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            brls::sync([ex, ASYNC_TOKEN]() {
                ASYNC_RELEASE
                this->btnConnect->setActionsAvailable(true);
                this->btnConnect->setTextColor(brls::Application::getTheme().getColor("brls/text"));
                Dialog::show(ex);
            });
        },
        baseUrl + jellyfin::apiPublicInfo, HTTP::Timeout{1000});
    return false;
}
