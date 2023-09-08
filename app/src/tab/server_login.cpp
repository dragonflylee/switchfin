/*
    Copyright 2023 dragonflylee
*/

#include "tab/server_login.hpp"
#include "activity/main_activity.hpp"
#include "api/jellyfin.hpp"
#include "api/analytics.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n

ServerLogin::ServerLogin(const AppServer& s, const std::string& user) : url(s.urls.front()) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/server_login.xml");
    brls::Logger::debug("ServerLogin: create");

    this->hdrSigin->setTitle(fmt::format(fmt::runtime("main/setting/server/sigin_to"_i18n), s.name));
    this->inputUser->init("main/setting/username"_i18n, user);
    this->inputPass->init(
        "main/setting/password"_i18n, "", [](std::string text) {}, "", "", 256);

    this->btnSignin->registerClickAction([this](...) { return this->onSignin(); });
}

ServerLogin::~ServerLogin() { brls::Logger::debug("ServerLogin Activity: delete"); }

bool ServerLogin::onSignin() {
    std::string username = inputUser->getValue();
    std::string password = inputPass->getValue();
    if (username.empty()) {
        Dialog::show("Username is empty");
        return false;
    }

    brls::Application::blockInputs();
    this->btnSignin->setState(brls::ButtonState::DISABLED);
    nlohmann::json data = {{"Username", username}, {"Pw", password}};

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, data]() {
        HTTP::Header header = {"Content-Type: application/json", AppConfig::instance().getDevice()};
        brls::Logger::info("login header {}", header[1]);

        try {
            auto resp = HTTP::post(this->url + jellyfin::apiAuthByName, data.dump(), header);
            jellyfin::AuthResult r = nlohmann::json::parse(resp);
            AppUser u = {.id = r.User.Id, .name = r.User.Name, .access_token = r.AccessToken, .server_id = r.ServerId};
            brls::sync([ASYNC_TOKEN, u]() {
                ASYNC_RELEASE
                AppConfig::instance().addUser(u);
                this->btnSignin->setState(brls::ButtonState::ENABLED);
                brls::Application::unblockInputs();
                brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                GA("login", {{"method", {this->url}}});
            });
        } catch (const std::exception& ex) {
            brls::sync([ASYNC_TOKEN, &ex]() {
                ASYNC_RELEASE
                this->btnSignin->setState(brls::ButtonState::ENABLED);
                brls::Application::unblockInputs();
                Dialog::show(ex.what());
            });
        }
    });
    return true;
}
