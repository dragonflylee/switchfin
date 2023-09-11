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

    inputUrl->init("URL", "https://", [](...){}, "", "", 255);

    btnConnect->registerClickAction([this](...) { return this->onConnect(); });
}

ServerAdd::~ServerAdd() { brls::Logger::debug("ServerAdd Activity: delete"); }

ServerAdd* ServerAdd::create() { return new ServerAdd(); }

bool ServerAdd::onConnect() {
    brls::Application::blockInputs();
    this->btnConnect->setTextColor(brls::Application::getTheme().getColor("font/grey"));
    std::string baseUrl = this->inputUrl->getValue();
    brls::Logger::debug("ServerAdd onConnect: click {}", baseUrl);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, baseUrl]() {
        try {
            auto resp = HTTP::get(baseUrl + jellyfin::apiPublicInfo, HTTP::Timeout{3000});
            jellyfin::PublicSystemInfo info = nlohmann::json::parse(resp);
            AppServer s = {
                .name = info.ServerName,
                .id = info.Id,
                .version = info.Version,
                .os = info.OperatingSystem,
                .urls = {baseUrl},
            };
            brls::sync([ASYNC_TOKEN, s]() {
                ASYNC_RELEASE
                AppConfig::instance().addServer(s);
                brls::Application::unblockInputs();
                this->present(new ServerLogin(s.name, s.urls.front()));
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                this->btnConnect->setTextColor(brls::Application::getTheme().getColor("brls/text"));
                brls::Application::unblockInputs();
                Dialog::show(msg);
            });
        }
    });
    return false;
}
