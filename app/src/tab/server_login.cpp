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
    QuickConnect(const std::string& url, const jellyfin::QuickConnect& r) : result(std::move(r)), url(url) {
        brls::Logger::debug("View QuickConnect: create");
        this->inflateFromXMLRes("xml/view/quick_connect.xml");
        this->isCancel = std::make_shared<std::atomic_bool>(false);
        this->labelCode->setText(this->result.Code);
        this->ticker.setCallback([this]() { this->Query(); });
    }

    void Open() {
        auto dialog = new brls::Dialog(this);
        dialog->addButton("hints/cancel"_i18n, [this]() {
            this->isCancel->store(true);
            this->ticker.stop();
        });
        dialog->open();
        this->ticker.start(2000);
    }

    void Query() {
        ASYNC_RETAIN
        brls::async([ASYNC_TOKEN]() {
            try {
                HTTP::Header header = {AppConfig::instance().getAuth()};
                std::string query =
                    this->url + fmt::format(fmt::runtime(jellyfin::apiQuickConnect), this->result.Secret);
                this->result = nlohmann::json::parse(HTTP::get(query, header, this->isCancel));
                if (!this->result.Authenticated) return;

                nlohmann::json body = {{"secret", this->result.Secret}};
                header.push_back("Content-Type: application/json");
                std::string url = this->url + jellyfin::apiAuthWithQuickConnect;
                std::string resp = HTTP::post(url, body.dump(), header, this->isCancel);
                jellyfin::AuthResult auth = nlohmann::json::parse(resp);
                brls::sync([ASYNC_TOKEN, auth]() {
                    ASYNC_RELEASE
                    this->ticker.stop();

                    AppUser u = {
                        .id = auth.User.Id,
                        .name = auth.User.Name,
                        .access_token = auth.AccessToken,
                        .server_id = auth.ServerId,
                        .is_admin = auth.User.Policy.IsAdministrator,
                        .config = std::move(auth.User.Configuration),
                    };
                    this->dismiss([u, this]() {
                        AppConfig::instance().addUser(u, this->url);
                        brls::Application::clear();
                        brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                    });
                });
            } catch (const std::exception& ex) {
                std::string msg = ex.what();
                brls::sync([ASYNC_TOKEN, msg]() {
                    ASYNC_RELEASE
                    this->ticker.stop();
                    if (!this->isCancel->load()) this->labelCode->setText(msg);
                });
            }
        });
    }

    ~QuickConnect() override {
        brls::Logger::debug("View QuickConnect: delete");
        this->ticker.stop();
    }

private:
    BRLS_BIND(brls::Label, labelCode, "quick/label/code");

    HTTP::Cancel isCancel;
    brls::RepeatingTimer ticker;
    jellyfin::QuickConnect result;
    std::string url;
};

ServerLogin::ServerLogin(const std::string& name, const std::string& url, const std::string& user) : url(url) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/server_login.xml");
    brls::Logger::debug("ServerLogin: create {}", url);

    this->hdrSigin->setTitle(brls::getStr("main/setting/server/sigin_to", name));
    this->inputUser->init("main/setting/username"_i18n, user);
    this->inputPass->init("main/setting/password"_i18n, "", [](std::string text) {}, "", "", 256);

    this->btnSignin->registerClickAction([this](...) { return this->onSignin(); });
    this->btnQuickConnect->setVisibility(brls::Visibility::GONE);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        try {
            std::string resp = HTTP::get(this->url + jellyfin::apiQuickEnabled, HTTP::Timeout{});
            if (resp.compare("true") == 0)
                brls::sync([ASYNC_TOKEN]() {
                    ASYNC_RELEASE
                    this->btnQuickConnect->setVisibility(brls::Visibility::VISIBLE);
                    this->btnQuickConnect->registerClickAction([this](...) {
                        this->doQuickLogin();
                        return true;
                    });
                });
        } catch (const std::exception& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("query quickconnect: {}", ex.what());
        }
    });

    this->Disclaimer();
}

ServerLogin::~ServerLogin() { brls::Logger::debug("ServerLogin Activity: delete"); }

void ServerLogin::Disclaimer() {
    ASYNC_RETAIN
    this->labelDisclaimer->setVisibility(brls::Visibility::INVISIBLE);
    brls::async([ASYNC_TOKEN]() {
        try {
            auto resp = HTTP::get(this->url + jellyfin::apiBranding, HTTP::Timeout{});
            jellyfin::BrandingConfig r = nlohmann::json::parse(resp);
            if (!r.LoginDisclaimer.empty()) {
                brls::sync([ASYNC_TOKEN, r]() {
                    ASYNC_RELEASE
                    this->labelDisclaimer->setText(r.LoginDisclaimer);
                    this->labelDisclaimer->setVisibility(brls::Visibility::VISIBLE);
                });
            }
        } catch (const std::exception& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("get login disclaimer: {}", ex.what());
        }
    });
}

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
        HTTP::Header header = {"Content-Type: application/json", AppConfig::instance().getAuth()};
        brls::Logger::info("login header {}", header[1]);

        try {
            auto resp = HTTP::post(this->url + jellyfin::apiAuthByName, data.dump(), header);
            jellyfin::AuthResult r = nlohmann::json::parse(resp);
            AppUser u = {
                .id = r.User.Id,
                .name = r.User.Name,
                .access_token = r.AccessToken,
                .server_id = r.ServerId,
                .is_admin = r.User.Policy.IsAdministrator,
                .config = std::move(r.User.Configuration),
            };
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

void ServerLogin::doQuickLogin() {
    brls::Application::blockInputs();
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        try {
            HTTP::Header header = {AppConfig::instance().getAuth()};
            std::string resp = HTTP::get(this->url + jellyfin::apiQuickInitiate, header);
            jellyfin::QuickConnect r = nlohmann::json::parse(resp);
            brls::sync([ASYNC_TOKEN, r]() {
                ASYNC_RELEASE
                QuickConnect* view = new QuickConnect(this->url, r);
                brls::Application::unblockInputs();
                view->Open();
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                Dialog::show(msg);
            });
        }
    });
}