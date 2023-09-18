/*
    Copyright 2023 dragonflylee
*/

#include "tab/server_login.hpp"
#include "activity/main_activity.hpp"
#include "api/jellyfin.hpp"
#include "api/analytics.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n

class QuickConnect : public brls::Box {
public:
    QuickConnect(const std::string& url) : url(url) {
        brls::Logger::debug("View QuickConnect: create");
        this->inflateFromXMLRes("xml/view/quick_connect.xml");
        this->isCancel = std::make_shared<std::atomic_bool>(false);
        this->header.push_back(AppConfig::instance().getDevice());
    }

    void Run() {
        try {
            std::string resp = HTTP::get(this->url + jellyfin::apiQuickInitiate, this->header, this->isCancel);
            this->result = nlohmann::json::parse(resp);
            brls::sync([this]() {
                this->labelCode->setText(this->result.Code);
                auto dialog = new brls::Dialog(this);
                dialog->addButton("hints/cancel"_i18n, [this]() {
                    this->isCancel->store(true);
                    this->ticker.stop();
                });
                dialog->open();
                brls::Application::unblockInputs();

                this->ticker.setCallback([this, dialog]() { brls::sync([this, dialog]() { this->Query(dialog); }); });
                this->ticker.start(2000);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([msg]() {
                brls::Application::unblockInputs();
                Dialog::show(msg);
            });
        }
    }

    void Query(brls::Dialog* dialog) {
        try {
            std::string query = this->url + fmt::format(fmt::runtime(jellyfin::apiQuickConnect), this->result.Secret);
            this->result = nlohmann::json::parse(HTTP::get(query, header, this->isCancel));
            if (!this->result.Authenticated) return;

            nlohmann::json body = {{"secret", this->result.Secret}};
            this->header.push_back("Content-Type: application/json");
            std::string url = this->url + jellyfin::apiAuthWithQuickConnect;
            std::string resp = HTTP::post(url, body.dump(), this->header, this->isCancel);
            jellyfin::AuthResult auth = nlohmann::json::parse(resp);
            AppUser u = {
                .id = auth.User.Id,
                .name = auth.User.Name,
                .access_token = auth.AccessToken,
                .server_id = auth.ServerId,
            };

            brls::sync([dialog, u, this]() {
                this->ticker.stop();
                dialog->dismiss([u, this]() {
                    AppConfig::instance().addUser(u, this->url);
                    brls::Application::clear();
                    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                });
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([this, msg]() {
                this->ticker.stop();
                if (!this->isCancel->load()) this->labelCode->setText(msg);
            });
        }
    }

    ~QuickConnect() override { brls::Logger::debug("View QuickConnect: delete"); }

private:
    BRLS_BIND(brls::Label, labelCode, "quick/label/code");

    HTTP::Cancel isCancel;
    brls::RepeatingTimer ticker;
    HTTP::Header header;
    jellyfin::QuickConnect result;
    std::string url;
};

ServerLogin::ServerLogin(const std::string& name, const std::string& url, const std::string& user) : url(url) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/server_login.xml");
    brls::Logger::debug("ServerLogin: create");

    this->hdrSigin->setTitle(fmt::format(fmt::runtime("main/setting/server/sigin_to"_i18n), name));
    this->inputUser->init("main/setting/username"_i18n, user);
    this->inputPass->init(
        "main/setting/password"_i18n, "", [](std::string text) {}, "", "", 256);

    this->btnSignin->registerClickAction([this](...) { return this->onSignin(); });
    this->btnQuickConnect->setVisibility(brls::Visibility::GONE);
    this->btnQuickConnect->registerClickAction([this](...) {
        auto view = new QuickConnect(this->url);
        brls::Application::blockInputs();
        brls::async([view]() { view->Run(); });
        return true;
    });

    brls::async([this]() {
        const long timeout = AppConfig::instance().getItem(AppConfig::REQUEST_TIMEOUT, default_timeout);
        try {
            std::string resp = HTTP::get(this->url + jellyfin::apiQuickEnabled, HTTP::Timeout{timeout});
            if (resp.compare("true") == 0)
                brls::sync([this]() { this->btnQuickConnect->setVisibility(brls::Visibility::VISIBLE); });
        } catch (const std::exception& ex) {
            brls::Logger::warning("query quickconnect: {}", ex.what());
        }
    });
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
                AppConfig::instance().addUser(u, this->url);
                this->btnSignin->setState(brls::ButtonState::ENABLED);
                brls::Application::unblockInputs();
                brls::Application::clear();
                brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                GA("login", {{"method", {this->url}}});
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                this->btnSignin->setState(brls::ButtonState::ENABLED);
                brls::Application::unblockInputs();
                Dialog::show(msg);
            });
        }
    });
    return true;
}
