/*
    Copyright 2023 dragonflylee
*/

#include "tab/server_login.hpp"
#include "activity/main_activity.hpp"
#include "api/jellyfin.hpp"
#include "api/analytics.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n

ServerLogin::ServerLogin(const AppServer& s) : url(s.urls.front()) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/server_login.xml");
    brls::Logger::debug("ServerLogin: create");

    this->hdrSigin->setTitle(fmt::format(fmt::runtime("main/setting/server/sigin_to"_i18n), s.name));
    this->inputUser->init("main/setting/username"_i18n, "");
    this->inputPass->init("main/setting/password"_i18n, "");

    this->btnSignin->registerClickAction([this](...) { return this->onSignin(); });
}

ServerLogin::~ServerLogin() { brls::Logger::debug("ServerLogin Activity: delete"); }

bool ServerLogin::onSignin() {
    btnSignin->setTextColor(brls::Application::getTheme().getColor("font/grey"));
    nlohmann::json data = {{"Username", inputUser->getValue()}, {"Pw", inputPass->getValue()}};

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, data]() {
        std::string device = AppConfig::instance().getDevice();
        HTTP::Header header = {
            "Content-Type: application/json",
            fmt::format(
                "X-Emby-Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\"",
                AppVersion::pkg_name, AppVersion::getDeviceName(), device, AppVersion::getVersion()),
        };

        try {
            auto resp = HTTP::post(this->url + jellyfin::apiAuthByName, data.dump(), header);
            jellyfin::AuthResult r = nlohmann::json::parse(std::get<1>(resp));
            AppUser u = {.id = r.User.Id, .name = r.User.Name, .access_token = r.AccessToken, .server_id = r.ServerId};
            brls::sync([ASYNC_TOKEN, u]() {
                ASYNC_RELEASE
                AppConfig::instance().addUser(u);
                brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                GA("login", {{"method", {this->url}}});
            });
        } catch (const std::exception& ex) {
            brls::sync([ASYNC_TOKEN, &ex]() {
                ASYNC_RELEASE
                this->btnSignin->setTextColor(brls::Application::getTheme().getColor("brls/text"));
                Dialog::show(ex.what());
            });
        }
    });
    return true;
}