/*
    GMCA — sign-in to Plex.
    PIN flow (only method): PLEX_MIGRATION.md §2.2;
    server/profile discovery: §2.3.
    Entered from ServerTypeChoose (the Plex cell present()s this view), so it is
    a content view of the ServerList AppletFrame: it keeps the footer and B
    returns to the type chooser via the frame's hints/back action.
*/

#include "tab/plex_add.hpp"
#include "activity/main_activity.hpp"
#include "activity/loading_overlay.hpp"
#include "api/plex.hpp"
#include "api/plex/auth.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n

/// "SC4B" -> "S C 4 B": the code breathes at large size
static std::string spaced(const std::string& code) {
    std::string out;
    for (size_t i = 0; i < code.size(); i++) {
        if (i > 0) out += ' ';
        out += code[i];
    }
    return out;
}

PlexAdd::PlexAdd() {
    this->inflateFromXMLRes("xml/tabs/server_add.xml");
    brls::Logger::debug("PlexAdd: create");

    this->btnRetry->registerClickAction([this](...) {
        this->startPin();
        return true;
    });
    this->ticker.setCallback([this]() { this->pollOnce(); });
    this->startPin();
}

PlexAdd::~PlexAdd() {
    brls::Logger::debug("PlexAdd: delete");
    this->ticker.stop();
}

brls::View* PlexAdd::getDefaultFocus() { return this->btnRetry; }

void PlexAdd::startPin() {
    this->ticker.stop();
    this->labelCode->setText("· · · ·");
    this->labelStatus->setText("main/plex/generating"_i18n);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        try {
            plex::PinResult fresh = plex::requestPin();
            brls::sync([ASYNC_TOKEN, fresh]() {
                ASYNC_RELEASE
                this->pin = fresh;
                this->labelCode->setText(spaced(fresh.code));
                this->labelStatus->setText("main/plex/waiting"_i18n);
                // PIN expiry on plex.tv side: ~2 minutes (§2.2)
                this->deadline = brls::getCPUTimeUsec() + 120 * 1000000;
                this->ticker.start(2000);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                this->labelCode->setText("· · · ·");
                this->labelStatus->setText(msg);
            });
        }
    });
}

void PlexAdd::pollOnce() {
    if (brls::getCPUTimeUsec() > this->deadline) {
        this->ticker.stop();
        this->labelStatus->setText("main/plex/expired"_i18n);
        return;
    }
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        try {
            std::string account = plex::pollPin(this->pin.id);
            brls::sync([ASYNC_TOKEN, account]() {
                ASYNC_RELEASE
                if (account.empty()) return;
                this->ticker.stop();
                this->labelStatus->setText("main/plex/connected"_i18n);
                this->onAccount(account);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                // 404/410: the PIN was consumed or expired server-side — that
                // is terminal, stop and let the user request a fresh code.
                // Anything else (timeout, 5xx, a TLS/network hiccup — frequent
                // on Vita) is transient: keep polling. The 2-minute deadline
                // above stays the real give-up, so a single blip no longer
                // aborts the whole account link ("took a couple tries ...
                // error'd out").
                if (msg.find("404") != std::string::npos || msg.find("410") != std::string::npos) {
                    this->ticker.stop();
                    this->labelStatus->setText("main/plex/expired"_i18n);
                } else {
                    brls::Logger::warning("pollPin transient error: {}", msg);
                    this->labelStatus->setText("main/plex/waiting"_i18n);
                }
            });
        }
    });
}

void PlexAdd::onAccount(const std::string& accountToken) {
    // getResources + getHomeUsers hit plex.tv (10 s timeout): show a loading
    // screen rather than a frozen one while we wait (cf. ServerList).
    brls::Application::blockInputs();
    brls::Application::pushActivity(new LoadingOverlay(), brls::TransitionAnimation::NONE);
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, accountToken]() {
        try {
            plex::AccountUser account = plex::getUser(accountToken);
            // ALL the account's servers (owned + shared) become connections.
            std::vector<plex::ServerResource> servers = plex::getResources(accountToken);
            // Home profiles use the account token only — no server needed yet.
            std::vector<plex::HomeUser> homes;
            try {
                homes = plex::getHomeUsers(accountToken);
            } catch (const std::exception& ex) {
                brls::Logger::warning("getHomeUsers: {}", ex.what());
            }
            brls::sync([ASYNC_TOKEN, account, accountToken, servers, homes]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                if (servers.empty()) {
                    Dialog::show("main/plex/no_server"_i18n);
                    return;
                }
                // More than one Plex Home profile: pick one; it applies to every
                // server we register (one link = one account+profile, all its
                // servers). A single/absent profile uses the account holder.
                if (homes.size() > 1) {
                    std::vector<std::string> names;
                    for (auto& h : homes) names.push_back(h.isProtected ? fmt::format("{} 🔒", h.title) : h.title);
                    // The profile pick must run AFTER the dropdown has popped
                    // itself: brls::Dropdown fires cb() while still on top of the
                    // stack, then pops (dropdown.cpp:147-152). doSwitch pushes a
                    // loading screen, so run it from dismissCb (fires in the pop
                    // completion) to keep the stack clean.
                    auto* dropdown = new brls::Dropdown(
                        "main/plex/choose_profile"_i18n, names, [](int) {}, 0,
                        [this, homes, accountToken](int selected) {
                            if (selected < 0) return;
                            this->onProfilePicked(homes.at(selected), accountToken);
                        });
                    brls::Application::pushActivity(new brls::Activity(dropdown));
                    // The dropdown's recycler builds its rows only on its first
                    // layout, which runs AFTER this push; at push time
                    // getDefaultFocus() finds no cell and A does nothing. Re-give
                    // focus a tick later — target the top of the stack, not a
                    // captured pointer, so a same-frame dismiss can't dangle.
                    brls::sync([]() {
                        auto stack = brls::Application::getActivitiesStack();
                        if (!stack.empty()) brls::Application::giveFocus(stack.back()->getDefaultFocus());
                    });
                } else {
                    this->finishAll(account.uuid, account.username, account.thumb, accountToken, servers);
                }
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                Dialog::show(msg);
            });
        }
    });
}

void PlexAdd::onProfilePicked(const plex::HomeUser& home, const std::string& accountToken) {
    if (home.isProtected) {
        brls::Application::getImeManager()->openForText(
            [this, home, accountToken](const std::string& pin) { this->doSwitch(home, accountToken, pin); },
            "main/plex/profile_pin"_i18n, "", 4, "");
    } else {
        this->doSwitch(home, accountToken, "");
    }
}

void PlexAdd::doSwitch(const plex::HomeUser& home, const std::string& accountToken, const std::string& pin) {
    // switchHomeUser + a fresh getResources round-trip: keep the loading screen
    // up so the profile switch never reads as a freeze either. The per-server
    // accessToken depends on the active profile (PLEX_MIGRATION.md §2.3), so we
    // re-fetch ALL resources with the profile token.
    brls::Application::blockInputs();
    brls::Application::pushActivity(new LoadingOverlay(), brls::TransitionAnimation::NONE);
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, home, accountToken, pin]() {
        try {
            std::string profileToken = plex::switchHomeUser(accountToken, home.uuid, pin);
            std::vector<plex::ServerResource> servers = plex::getResources(profileToken);
            brls::sync([ASYNC_TOKEN, home, profileToken, servers]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                this->finishAll(home.uuid, home.title, home.thumb, profileToken, servers);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                // 403 = wrong PIN (code 1041)
                Dialog::show(msg.find("403") != std::string::npos ? "main/plex/wrong_pin"_i18n : msg);
            });
        }
    });
}

void PlexAdd::finishAll(const std::string& uuid, const std::string& name, const std::string& thumb,
    const std::string& plexTvToken, const std::vector<plex::ServerResource>& servers) {
    // Activate the first owned server (a server you own is the natural landing
    // point), else the first available. Only THIS server is probed now:
    // findBestConnection races its candidates in series (2 s each; plex.direct
    // servers advertise 10+), so probing every server would take far too long.
    // The other servers store their ranked candidate urls and resolve a
    // reachable one lazily when the user switches to them (connectWithUser).
    size_t primaryIdx = 0;
    for (size_t i = 0; i < servers.size(); i++) {
        if (servers[i].owned) {
            primaryIdx = i;
            break;
        }
    }
    // Keep the loading screen up during the (possibly multi-second) probe —
    // without it the app looks frozen (thcolin/pleNx#1 on Vita).
    brls::Application::blockInputs();
    brls::Application::pushActivity(new LoadingOverlay(), brls::TransitionAnimation::NONE);
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, uuid, name, thumb, plexTvToken, servers, primaryIdx]() {
        try {
            std::string base = plex::findBestConnection(servers.at(primaryIdx));
            if (base.empty()) throw std::runtime_error("main/plex/unreachable"_i18n);
            brls::sync([ASYNC_TOKEN, uuid, name, thumb, plexTvToken, servers, primaryIdx, base]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                brls::Application::popActivity(brls::TransitionAnimation::NONE);

                auto persist = [&](const plex::ServerResource& r, const std::string& frontUrl, bool activate) {
                    AppServer s;
                    s.name = r.name;
                    s.id = r.clientIdentifier;
                    s.access_token = r.accessToken;
                    s.type = "plex";
                    // frontUrl (the probed, reachable url) first for the active
                    // server; then the ranked candidates for a later re-probe.
                    if (!frontUrl.empty()) s.urls.push_back(frontUrl);
                    for (auto& u : plex::rankConnections(r)) {
                        if (u != frontUrl) s.urls.push_back(u);
                    }

                    AppUser u;
                    // Composite id: one profile can back several servers, so key
                    // the connection by (profile uuid + server). A bare uuid
                    // would dedupe every server of the account onto one tile.
                    u.id = fmt::format("{}@{}", uuid, r.clientIdentifier);
                    u.name = name;
                    u.access_token = plexTvToken;
                    u.server_id = r.clientIdentifier;
                    u.thumb = thumb;

                    if (activate) {
                        AppConfig::instance().addServer(s);
                        AppConfig::instance().addUser(u, frontUrl);
                    } else {
                        AppConfig::instance().upsertServer(s);
                        AppConfig::instance().upsertUser(u);
                    }
                };

                // Re-link cleanup: before multi-server support a connection was
                // keyed by the bare profile uuid, so an already-configured
                // account has one legacy tile for this profile. Its composite
                // replacement (uuid@server) won't dedupe onto it, so drop it
                // here to avoid a duplicate tile. uuids are globally unique, so
                // this only ever matches this very profile's old entry.
                AppConfig::instance().removeUser(uuid);

                // Register the secondary servers first (stored, not activated),
                // then the primary last — addUser sets the active connection.
                for (size_t i = 0; i < servers.size(); i++) {
                    if (i == primaryIdx) continue;
                    persist(servers[i], "", false);
                }
                persist(servers.at(primaryIdx), base, true);

                brls::Application::clear();
                brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                brls::Application::unblockInputs();
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                Dialog::show(msg);
            });
        }
    });
}
