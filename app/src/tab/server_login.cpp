/*
    Copyright 2023 dragonflylee
*/

#include "tab/server_login.hpp"
#include "activity/main_activity.hpp"
#include "api/jellyfin.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n

ServerLogin::ServerLogin(const AppServer& s) {
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
    this->btnSignin->setActionsAvailable(false);
    this->btnSignin->setTextColor(brls::Application::getTheme().getColor("font/grey"));

    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"Username", this->inputUser->getValue()},
            {"Pw", this->inputPass->getValue()},
        },
        [ASYNC_TOKEN](const jellyfin::AuthResult& r) {
            ASYNC_RELEASE
            AppUser u = {r.User.Id, r.User.Name, r.AccessToken, r.ServerId};
            AppConfig::instance().addUser(u);
            brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->btnSignin->setActionsAvailable(true);
            this->btnSignin->setTextColor(brls::Application::getTheme().getColor("brls/text"));
            Dialog::show(ex);
        },
        jellyfin::apiAuthByName);
    return false;
}