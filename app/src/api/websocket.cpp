#include "api/websocket.hpp"
#include "utils/config.hpp"
#include "api/jellyfin.hpp"
#include <view/mpv_core.hpp>
#include <activity/player_view.hpp>
#include <libretro-common/retro_timers.h>
#include <curl/curl.h>

const std::string msgKeepAlive = R"({"MessageType":"KeepAlive"})";

websocket::websocket(const std::string& url) {
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
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

#ifdef BOREALIS_USE_STD_THREAD
    this->th = std::make_shared<std::thread>(wsRecv, this);
#else
    pthread_create(&this->th, nullptr, wsRecv, this);
#endif
#endif
}

websocket::~websocket() {
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
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
#ifdef BOREALIS_USE_STD_THREAD
    this->th->join();
#else
    pthread_join(this->th, nullptr);
#endif
    // 5) 最后清理
    {
        std::lock_guard<std::mutex> lock(this->easyMutex);
        if (this->easy != nullptr) {
            curl_easy_cleanup(this->easy);
            this->easy = nullptr;
        }
    }
#endif
}

void* websocket::wsRecv(void* ptr) {
    websocket* p = reinterpret_cast<websocket*>(ptr);
    // 指数退避，上限 60s，避免长期断网时 t 溢出/睡眠过久
    for (uint64_t t = 500;; t = std::min<uint64_t>(t * 2, 60000)) {
        CURLcode res = curl_easy_perform(p->easy);
        if (res == CURLE_OK) break;
        p->hb.stop();
        brls::Logger::warning("ws perform failed: {}", curl_easy_strerror(res));
        if (p->isStop.load()) break;
        retro_sleep(t);
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
    uint64_t StartPositionTicks;
    std::string PlayCommand;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MsgPlay, ItemIds, StartPositionTicks, PlayCommand);

size_t websocket::onMsg(char* b, size_t size, size_t nitems, void* ptr) {
    try {
        if (nitems <= 2) return nitems;
        std::string resp(b, nitems);
        Message m = nlohmann::json::parse(resp);
        if (m.MessageType == "Playstate") {
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
            });
        } else if (m.MessageType == "Play") {
            MsgPlay cmd = m.Data.get<MsgPlay>();
            if (cmd.PlayCommand == "PlayNow") {
                if (cmd.ItemIds.empty()) {
                    brls::Logger::warning("ws PlayNow without ItemIds: {}", resp);
                } else {
                    websocket::onPlayNow(cmd.ItemIds.front(), cmd.StartPositionTicks);
                }
            } else {
                brls::Logger::info("play command: {}", resp);
            }
        } else if (m.MessageType == "GeneralCommand") {
            MsgArguments args = m.Data.at("Arguments");
            brls::sync([args]() { brls::Application::notify(args.Text); });
        } else if (m.MessageType == "ForceKeepAlive") {
            websocket* p = reinterpret_cast<websocket*>(ptr);
            jellyfin::postJSON(
                {
                    {"PlayableMediaTypes", {"Video"}},
                    {"SupportedCommands", {"DisplayMessage"}},
                    {"SupportsPersistentIdentifier", false},
                    {"SupportsMediaControl", true},
                },
                [](...) {}, nullptr, jellyfin::apiCapabilities);
            p->hb.start(20000);
        } else if (m.MessageType == "Sessions") {
        } else if (m.MessageType != "KeepAlive") {
            brls::Logger::debug("ws recv: {}", resp);
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("parse ws {}", ex.what());
    }
    return nitems;
}

void websocket::onPlayNow(const std::string& itemId, uint64_t seekTicks) {
    jellyfin::getJSON<jellyfin::Episode>(
        [seekTicks](const jellyfin::Episode& item) {
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
}