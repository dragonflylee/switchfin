/*
    Copyright 2023 dragonflylee
*/

#include "tab/server_add.hpp"
#include "tab/server_login.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "api/jellyfin.hpp"
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_native_http_diagnostics.hpp"
#include "utils/ps5_native_startup.hpp"
namespace {
void traceServerConnection(ps5_native_http::Outcome outcome, const ps5_native_http::Snapshot& value = {}) noexcept {
    ps5_native_http::report(outcome, value, +[](const char* line) noexcept {
        ps5_native_startup::detail::line("%s\n", line);
    });
}
void traceServerWait(unsigned event, unsigned phase) noexcept {
    // UI-thread only, fixed numeric categories. No endpoint, input or response.
    static unsigned reports = 0;
    if (reports == 16) return;
    ps5_native_startup::detail::line("SERVER-WAIT report=%u event=%u phase=%u\n", ++reports, event, phase);
}
}
#endif

using namespace brls::literals;  // for _i18n

ServerAdd::ServerAdd() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/server_add.xml");
    brls::Logger::debug("ServerAdd: create");

    inputUrl->init("URL", "", [](std::string) {}, "http://<Server IP>:8096", "", 255);

    btnConnect->registerClickAction([this](...) { return this->onConnect(); });
}

ServerAdd::~ServerAdd() { brls::Logger::debug("ServerAdd Activity: delete"); }

brls::View* ServerAdd::getDefaultFocus() { return this->inputUrl; }

bool ServerAdd::onConnect() {
#ifdef PS5_NATIVE_GPU
    try {
        std::string baseUrl = this->inputUrl->getValue();
        if (baseUrl.length() < 10 || baseUrl.substr(0, 4).compare("http")) {
            Dialog::show("main/setting/server/invalid"_i18n);
            return false;
        }
        while (baseUrl.back() == '/') baseUrl.pop_back();
        // Shared only as a bounded numeric observation; never contains a View,
        // endpoint or response. The worker owns it independently of completion.
        auto phase = std::make_shared<std::atomic<unsigned>>(0);
        bool admitted = requests.start([baseUrl, phase](const ps5_native_requests::Cancel& cancel) {
            std::ostringstream body;
            phase->store(1); // Enter HTTP construction.
            {
                HTTP session;
                phase->store(2); // Enter option setup.
                HTTP::set_option(session, HTTP::Timeout{3000}, cancel);
                phase->store(3); // Enter get/perform.
                session._get(baseUrl + jellyfin::apiPublicInfo, &body);
                phase->store(4); // Enter HTTP cleanup.
            }
            phase->store(5); // Enter response parsing, without recording data.
            auto resp = body.str();
            jellyfin::PublicSystemInfo info = nlohmann::json::parse(resp);
            phase->store(6); // Construct owned server result.
            return AppServer{.name = info.ServerName, .id = info.Id, .urls = {baseUrl}};
        }, [this, phase](AppServer* server, std::exception_ptr failure) {
            // Input has already been released and the page checked alive.
            this->btnConnect->setTextColor(brls::Application::getTheme().getColor("brls/text"));
            if (failure) {
                try { std::rethrow_exception(failure); }
                catch (const ps5_native_requests::DeadlineExceeded& ex) {
                    traceServerWait(2, phase->load());
                    Dialog::show(ex.what());
                }
                catch (const std::exception& ex) {
                    const auto* transport = dynamic_cast<const ps5_native_http::Failure*>(&ex);
                    traceServerConnection(transport ? ps5_native_http::Outcome::TransportFailure
                                                    : ps5_native_http::Outcome::OtherFailure,
                        transport ? transport->snapshot() : ps5_native_http::Snapshot{});
                    Dialog::show(ex.what());
                } catch (...) {
                    traceServerConnection(ps5_native_http::Outcome::OtherFailure);
                    Dialog::show("Could not connect to server");
                }
                return;
            }
            if (!server) return;
            traceServerConnection(ps5_native_http::Outcome::Success);
            if (AppConfig::instance().addServer(*server)) {
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
            } else {
                this->present(new ServerLogin(server->name, server->urls.front()));
            }
        }, true, std::chrono::milliseconds(8000));
        if (admitted) {
            traceServerWait(1, 0);
            this->btnConnect->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        } else {
            Dialog::show("Could not start server request");
        }
    } catch (...) {
        Dialog::show("Could not start server request");
#else
    brls::Application::blockInputs();
    std::string baseUrl = this->inputUrl->getValue();
    if (baseUrl.length() < 10 || baseUrl.substr(0, 4).compare("http")) {
        brls::Application::unblockInputs();
        Dialog::show("main/setting/server/invalid"_i18n);
        return false;
#endif
    }
#ifdef PS5_NATIVE_GPU
#else
    while (baseUrl.back() == '/') baseUrl.pop_back();
    this->btnConnect->setTextColor(brls::Application::getTheme().getColor("font/grey"));

    brls::Logger::debug("ServerAdd onConnect: click {}", baseUrl);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, baseUrl]() {
        try {
            auto resp = HTTP::get(baseUrl + jellyfin::apiPublicInfo, HTTP::Timeout{3000});
            jellyfin::PublicSystemInfo info = nlohmann::json::parse(resp);
            AppServer s = {
                .name = info.ServerName,
                .id = info.Id,
                .urls = {baseUrl},
            };
            brls::sync([ASYNC_TOKEN, s]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                if (AppConfig::instance().addServer(s)) {
                    brls::Application::popActivity(brls::TransitionAnimation::NONE);
                } else {
                    this->present(new ServerLogin(s.name, s.urls.front()));
                }
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
#endif
    return false;
}
