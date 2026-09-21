#include "api/websocket.hpp"
#include "utils/config.hpp"
#include "api/jellyfin.hpp"
#include <view/mpv_core.hpp>
#include <activity/player_view.hpp>
#ifndef PS5_NATIVE_GPU
#include <libretro-common/retro_timers.h>
#endif
#include <curl/curl.h>
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_remote_playstate.hpp"
#endif

#ifdef PS5_NATIVE_GPU
websocket::websocket(const std::string& url, const std::string& authorization) : url(url), authorization(authorization) {
#else
const std::string msgKeepAlive = R"({"MessageType":"KeepAlive"})";

websocket::websocket(const std::string& url) {
#endif
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
#ifndef PS5_NATIVE_GPU
    this->easy = curl_easy_init();
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());

    // enable all supported built-in compressions
    curl_easy_setopt(this->easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(this->easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(this->easy, CURLOPT_WRITEFUNCTION, onMsg);
    curl_easy_setopt(this->easy, CURLOPT_WRITEDATA, this);

    hb.setCallback([this]() {
        brls::async([this]() {
            // 析构已开始则不再使用 easy，避免与 cleanup 竞态
            if (this->isStop.load()) return;
            std::lock_guard<std::mutex> lock(this->easyMutex);
            if (this->isStop.load() || this->easy == nullptr) return;
            size_t slen = msgKeepAlive.size();
            curl_ws_send(this->easy, msgKeepAlive.data(), slen, &slen, 0, CURLWS_TEXT);
        });
    });

#endif
#ifdef BOREALIS_USE_STD_THREAD
    this->th = std::make_shared<std::thread>(wsRecv, this);
#ifdef PS5_NATIVE_GPU
    this->threadStarted = true;
#endif
#else
#ifdef PS5_NATIVE_GPU
    this->threadStarted = pthread_create(&this->th, nullptr, wsRecv, this) == 0;
    if (!this->threadStarted) brls::Logger::warning("ws receive thread creation failed");
#else
    pthread_create(&this->th, nullptr, wsRecv, this);
#endif
#endif
#endif
}

websocket::~websocket() {
#ifdef PS5_NATIVE_GPU
    this->active->store(false);
    this->transport.stop();
#endif
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
#ifdef PS5_NATIVE_GPU
    if (!this->threadStarted) return;
#else
    // 1) 先置停止标志，让心跳回调与接收循环尽快退出，不再发起新的 easy 操作
    this->isStop.store(true);
    // 2) 停止心跳定时器
    this->hb.stop();
    // 3) 发送关闭帧唤醒接收线程的 curl_easy_perform（send 在锁内，避免与心跳并发）
    {
        std::lock_guard<std::mutex> lock(this->easyMutex);
        if (this->easy != nullptr) {
            size_t sent;
            curl_ws_send(this->easy, "", 0, &sent, 0, CURLWS_CLOSE);
        }
    }
    // 4) 等待接收线程结束，此后不再有人使用 easy
#endif
#ifdef BOREALIS_USE_STD_THREAD
    this->th->join();
#else
    pthread_join(this->th, nullptr);
#endif
#ifndef PS5_NATIVE_GPU
    // 5) 最后清理
    {
        std::lock_guard<std::mutex> lock(this->easyMutex);
        if (this->easy != nullptr) {
            curl_easy_cleanup(this->easy);
            this->easy = nullptr;
        }
    }
#endif
#endif
}

void* websocket::wsRecv(void* ptr) {
    websocket* p = reinterpret_cast<websocket*>(ptr);
#ifdef PS5_NATIVE_GPU
    try {
        p->transport.run(p->url,
            [p](const std::string& text) {
                onMsg(const_cast<char*>(text.data()), 1, text.size(), p);
            },
            [](CURLcode result, long status) {
                // Keep credentials, request URLs and response bodies out of
                // diagnostics. The status distinguishes auth/routing failures
                // from transport errors and from a completed 101 upgrade.
                brls::Logger::warning("ws reconnect: {} (curl={}, HTTP={})",
                    curl_easy_strerror(result), static_cast<int>(result), status);
            }, p->authorization);
    } catch (const std::exception&) {
        brls::Logger::warning("ws receive failed");
#else
    // 指数退避，上限 60s，避免长期断网时 t 溢出/睡眠过久
    for (uint64_t t = 500;; t = std::min<uint64_t>(t * 2, 60000)) {
        CURLcode res = curl_easy_perform(p->easy);
        if (res == CURLE_OK) break;
        p->hb.stop();
        brls::Logger::warning("ws perform failed: {}", curl_easy_strerror(res));
        if (p->isStop.load()) break;
        retro_sleep(t);
#endif
    }
    brls::Logger::info("ws recv exit");
    return nullptr;
}

struct Message {
    std::string MessageType;
    std::string MessageId;
    nlohmann::json Data;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Message, MessageType, MessageId, Data);

struct MsgArguments {
    std::string Header;
    std::string Text;
    std::string TimeoutMs;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MsgArguments, Header, Text, TimeoutMs);

struct MsgPlay {
    std::vector<std::string> ItemIds;
#ifdef PS5_NATIVE_GPU
    uint64_t StartPositionTicks = 0;
#else
    uint64_t StartPositionTicks;
#endif
    std::string PlayCommand;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MsgPlay, ItemIds, StartPositionTicks, PlayCommand);

size_t websocket::onMsg(char* b, size_t size, size_t nitems, void* ptr) {
#ifdef PS5_NATIVE_GPU
    websocket* p = reinterpret_cast<websocket*>(ptr);
    auto active = p->active;
    const size_t count = size * nitems;
    if (!active->load()) return count;
#endif
    try {
#ifdef PS5_NATIVE_GPU
        if (count <= 2) return count;
        std::string resp(b, count);
#else
        if (nitems <= 2) return nitems;
        std::string resp(b, nitems);
#endif
        Message m = nlohmann::json::parse(resp);
        if (m.MessageType == "Playstate") {
#ifdef PS5_NATIVE_GPU
            ps5_remote_playstate::Request request;
            if (!ps5_remote_playstate::decode(m.Data, request)) {
                brls::Logger::warning("invalid ws playstate");
                return count;
            }
            brls::sync([request = std::move(request), active]() {
                if (!active->load()) return;
                ps5_remote_playstate::dispatch(request, MPVCore::instance(), SYNC_STOP);
#else
            std::string cmd = m.Data.at("Command");
            brls::sync([cmd, m]() {
                auto& mpv = MPVCore::instance();
                if (cmd == "PlayPause") {
                    mpv.togglePlay();
                } else if (cmd == "Stop") {
                    mpv.getCustomEvent()->fire(SYNC_STOP, nullptr);
                } else if (cmd == "Seek") {
                    int64_t seek = m.Data.at("SeekPositionTicks").get<int64_t>();
                    mpv.seek(seek / jellyfin::PLAYTICKS, "absolute");
                } else {
                    mpv.getCustomEvent()->fire(cmd, nullptr);
                }
#endif
            });
        } else if (m.MessageType == "Play") {
            MsgPlay cmd = m.Data.get<MsgPlay>();
#ifdef PS5_NATIVE_GPU
            if (cmd.PlayCommand == "PlayNow" && !cmd.ItemIds.empty()) {
                websocket::onPlayNow(cmd.ItemIds.front(), cmd.StartPositionTicks, active);
#else
            if (cmd.PlayCommand == "PlayNow") {
                if (cmd.ItemIds.empty()) {
                    brls::Logger::warning("ws PlayNow without ItemIds: {}", resp);
                } else {
                    websocket::onPlayNow(cmd.ItemIds.front(), cmd.StartPositionTicks);
                }
#endif
            } else {
#ifdef PS5_NATIVE_GPU
                brls::Logger::info("unsupported ws play command");
#else
                brls::Logger::info("play command: {}", resp);
#endif
            }
        } else if (m.MessageType == "GeneralCommand") {
            MsgArguments args = m.Data.at("Arguments");
#ifdef PS5_NATIVE_GPU
            brls::sync([args, active]() { if (active->load()) brls::Application::notify(args.Text); });
#else
            brls::sync([args]() { brls::Application::notify(args.Text); });
#endif
        } else if (m.MessageType == "ForceKeepAlive") {
#ifndef PS5_NATIVE_GPU
            websocket* p = reinterpret_cast<websocket*>(ptr);
#endif
            jellyfin::postJSON(
                {
                    {"PlayableMediaTypes", {"Video"}},
                    {"SupportedCommands", {"DisplayMessage"}},
                    {"SupportsPersistentIdentifier", false},
                    {"SupportsMediaControl", true},
                },
                [](...) {}, nullptr, jellyfin::apiCapabilities);
#ifdef PS5_NATIVE_GPU
            p->transport.enableHeartbeat();
#else
            p->hb.start(20000);
#endif
        } else if (m.MessageType == "Sessions") {
        } else if (m.MessageType != "KeepAlive") {
#ifdef PS5_NATIVE_GPU
            brls::Logger::debug("unhandled ws message");
#else
            brls::Logger::debug("ws recv: {}", resp);
#endif
        }
#ifdef PS5_NATIVE_GPU
    } catch (const std::exception&) {
        // Parser errors can contain response text. Keep server-provided data
        // out of transport diagnostics, just like request credentials.
        brls::Logger::warning("invalid ws message");
#else
    } catch (const std::exception& ex) {
        brls::Logger::warning("parse ws {}", ex.what());
#endif
    }
#ifdef PS5_NATIVE_GPU
    return count;
#else
    return nitems;
#endif
}

#ifdef PS5_NATIVE_GPU
void websocket::onPlayNow(const std::string& itemId, uint64_t seekTicks,
                          const std::shared_ptr<std::atomic_bool>& active) {
#else
void websocket::onPlayNow(const std::string& itemId, uint64_t seekTicks) {
#endif
    jellyfin::getJSON<jellyfin::Episode>(
#ifdef PS5_NATIVE_GPU
        [seekTicks, active](const jellyfin::Episode& item) {
            if (!active->load()) return;
#else
        [seekTicks](const jellyfin::Episode& item) {
#endif
            if (item.Type == "Audio") return;
            MPVCore::instance().getCustomEvent()->fire(SYNC_STOP, nullptr);
            PlayerView* view = new PlayerView(item, seekTicks);
            if (item.Type == jellyfin::mediaTypeEpisode) {
                view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
                if (item.SeriesId.is_string()) view->setSeries(item.SeriesId.get<std::string>());
            } else if (item.ProductionYear) {
                view->setTitie(fmt::format("{} ({})", item.Name, item.ProductionYear));
            } else {
                view->setTitie(item.Name);
            }
        },
        nullptr, jellyfin::apiUserItem, AppConfig::instance().getUserId(), itemId);
#ifdef PS5_NATIVE_GPU
}

#else
}
#endif
