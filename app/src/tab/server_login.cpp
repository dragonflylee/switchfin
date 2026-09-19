/*
    Copyright 2023 dragonflylee
*/

#include "tab/server_login.hpp"
#include "activity/main_activity.hpp"
#include "api/jellyfin.hpp"
#include "api/analytics.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n
#ifdef PS5_NATIVE_GPU

namespace {
AppUser nativeLoginUser(jellyfin::AuthResult result) {
    return AppUser{.id = result.User.Id, .name = result.User.Name, .access_token = result.AccessToken,
        .server_id = result.ServerId, .is_admin = result.User.Policy.IsAdministrator,
        .config = std::move(result.User.Configuration)};
}
std::string nativeLoginError(std::exception_ptr failure) {
    try { std::rethrow_exception(failure); }
    catch (const std::exception& ex) { return ex.what(); }
    catch (...) { return "Server request failed"; }
}
struct NativeQuickReply {
    jellyfin::QuickConnect state;
    std::optional<AppUser> user;
};
}
#endif

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
#ifdef PS5_NATIVE_GPU
            this->requests.close();
#endif
        });
        dialog->open();
        this->ticker.start(2000);
    }

    void Query() {
#ifdef PS5_NATIVE_GPU
        if (queryPending || isCancel->load()) return;
        try {
            const auto baseUrl = this->url;
            const auto secret = this->result.Secret;
            HTTP::Header header = {AppConfig::instance().getAuth()};
            queryPending = requests.start([baseUrl, secret, header](const ps5_native_requests::Cancel& cancel) mutable {
                auto query = baseUrl + fmt::format(fmt::runtime(jellyfin::apiQuickConnect), secret);
                NativeQuickReply reply{nlohmann::json::parse(HTTP::get(query, header, cancel)), std::nullopt};
                if (reply.state.Authenticated) {
                    nlohmann::json body = {{"secret", reply.state.Secret}};
                    header.push_back("Content-Type: application/json");
                    auto resp = HTTP::post(baseUrl + jellyfin::apiAuthWithQuickConnect, body.dump(), header, cancel);
                    reply.user = nativeLoginUser(nlohmann::json::parse(resp));
                }
                return reply;
            }, [this, baseUrl](NativeQuickReply* reply, std::exception_ptr failure) {
                queryPending = false;
                if (failure) {
                    ticker.stop();
                    labelCode->setText(nativeLoginError(failure));
                } else if (reply) {
                    result = std::move(reply->state);
                    if (!reply->user) return;
                    ticker.stop();
                    // Dismiss may destroy this view before its callback runs.
                    this->dismiss([user = std::move(*reply->user), baseUrl]() {
                        AppConfig::instance().addUser(user, baseUrl);
#else
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
#endif
                        brls::Application::clear();
                        brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                    });
#ifdef PS5_NATIVE_GPU
                }
            });
            if (!queryPending) {
                ticker.stop();
                labelCode->setText("Could not start server request");
#else
                });
            } catch (const std::exception& ex) {
                std::string msg = ex.what();
                brls::sync([ASYNC_TOKEN, msg]() {
                    ASYNC_RELEASE
                    this->ticker.stop();
                    if (!this->isCancel->load()) this->labelCode->setText(msg);
                });
#endif
            }
#ifdef PS5_NATIVE_GPU
        } catch (...) {
            ticker.stop();
            labelCode->setText("Could not start server request");
        }
#else
        });
#endif
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
#ifdef PS5_NATIVE_GPU
    ps5_native_requests::Scope requests;
    bool queryPending = false;
#endif
};

ServerLogin::ServerLogin(const std::string& name, const std::string& url, const std::string& user) : url(url) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/server_login.xml");
#ifdef PS5_NATIVE_GPU
    brls::Logger::debug("ServerLogin: create");
#else
    brls::Logger::debug("ServerLogin: create {}", url);
#endif

    this->hdrSigin->setTitle(brls::getStr("main/setting/server/sigin_to", name));
    this->inputUser->init("main/setting/username"_i18n, user);
    this->inputPass->init("main/setting/password"_i18n, "", [](std::string text) {}, "", "", 256);

    this->btnSignin->registerClickAction([this](...) { return this->onSignin(); });
    this->btnQuickConnect->setVisibility(brls::Visibility::GONE);

#ifdef PS5_NATIVE_GPU
    const auto baseUrl = this->url;
    requests.start([baseUrl](const ps5_native_requests::Cancel& cancel) {
        return HTTP::get(baseUrl + jellyfin::apiQuickEnabled, HTTP::Timeout{}, cancel) == "true";
    }, [this](bool* enabled, std::exception_ptr failure) {
        if (failure || !enabled || !*enabled) return;
        this->btnQuickConnect->setVisibility(brls::Visibility::VISIBLE);
        this->btnQuickConnect->registerClickAction([this](...) { this->doQuickLogin(); return true; });
#else
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
#endif
    });

    this->Disclaimer();
}

ServerLogin::~ServerLogin() { brls::Logger::debug("ServerLogin Activity: delete"); }

void ServerLogin::Disclaimer() {
#ifdef PS5_NATIVE_GPU
#else
    ASYNC_RETAIN
#endif
    this->labelDisclaimer->setVisibility(brls::Visibility::INVISIBLE);
#ifdef PS5_NATIVE_GPU
    const auto baseUrl = this->url;
    requests.start([baseUrl](const ps5_native_requests::Cancel& cancel) {
        auto resp = HTTP::get(baseUrl + jellyfin::apiBranding, HTTP::Timeout{}, cancel);
        jellyfin::BrandingConfig branding = nlohmann::json::parse(resp);
        return branding.LoginDisclaimer;
    }, [this](std::string* text, std::exception_ptr failure) {
        if (!failure && text && !text->empty()) {
            this->labelDisclaimer->setText(*text);
            this->labelDisclaimer->setVisibility(brls::Visibility::VISIBLE);
#else
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
#endif
        }
    });
}

bool ServerLogin::onSignin() {
#ifdef PS5_NATIVE_GPU
    try {
        const auto baseUrl = this->url;
        std::string username = inputUser->getValue();
        if (username.empty()) { Dialog::show("Username is empty"); return false; }
        nlohmann::json data = {{"Username", username}, {"Pw", inputPass->getValue()}};
        HTTP::Header header = {"Content-Type: application/json", AppConfig::instance().getAuth()};
        bool admitted = requests.start([baseUrl, data, header](const ps5_native_requests::Cancel& cancel) {
            auto resp = HTTP::post(baseUrl + jellyfin::apiAuthByName, data.dump(), header, cancel

            );
            return nativeLoginUser(nlohmann::json::parse(resp));
        }, [this, baseUrl](AppUser* user, std::exception_ptr failure) {
            this->btnSignin->setState(brls::ButtonState::ENABLED);
            if (failure) { Dialog::show(nativeLoginError(failure)); return; }
            if (!user) return;
            AppConfig::instance().addUser(*user, baseUrl);
            GA("login", {{"method", {baseUrl}}});
            brls::Application::clear();
            brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
        }, true);
        if (admitted) this->btnSignin->setState(brls::ButtonState::DISABLED);
        else Dialog::show("Could not start server request");
        return admitted;
    } catch (...) {
        Dialog::show("Could not start server request");
#else
    std::string username = inputUser->getValue();
    std::string password = inputPass->getValue();
    if (username.empty()) {
        Dialog::show("Username is empty");
#endif
        return false;
    }
#ifdef PS5_NATIVE_GPU
#else

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
#endif
}

void ServerLogin::doQuickLogin() {
#ifdef PS5_NATIVE_GPU
    try {
        const auto baseUrl = this->url;
        HTTP::Header header = {AppConfig::instance().getAuth()};
        bool admitted = requests.start([baseUrl, header](const ps5_native_requests::Cancel& cancel) {
            return nlohmann::json::parse(HTTP::get(baseUrl + jellyfin::apiQuickInitiate, header, cancel))
                .get<jellyfin::QuickConnect>();
        }, [baseUrl](jellyfin::QuickConnect* result, std::exception_ptr failure) {
            if (failure) { Dialog::show(nativeLoginError(failure)); return; }
            if (result) (new QuickConnect(baseUrl, *result))->Open();
        }, true);
        if (!admitted) Dialog::show("Could not start server request");
    } catch (...) { Dialog::show("Could not start server request"); }
}
#else
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
#endif
