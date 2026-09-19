#pragma once

#include "api/curl_tls.hpp"
#include "utils/ps5_native_socket_mode.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct WebSocketRetry {
    void reset() { delay = 500; }
    unsigned next() {
        unsigned current = delay;
        delay = std::min(delay * 2, 30000u);
        return current;
    }
private:
    unsigned delay = 500;
};

// The receive thread owns every curl operation, including heartbeat and close.
// Stop only wakes waits; it never touches a handle being used by another thread.
class WebSocketTransport {
public:
    using Clock = std::chrono::steady_clock;
    using Message = std::function<void(const std::string&)>;
    using Error = std::function<void(CURLcode, long)>;

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopped = true;
        }
        wake.notify_all();
    }

    bool isStopped() const { return stopped.load(); }

    // Called only by the owner while dispatching ForceKeepAlive.
    void enableHeartbeat() {
        heartbeat = true;
        nextHeartbeat = Clock::now() + std::chrono::seconds(20);
    }

    void run(const std::string& url, const Message& message, const Error& error,
             const std::string& authorization = "") {
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
        WebSocketRetry retry;
        while (!isStopped()) {
            heartbeat = false;
            bool connected = false;
            long responseCode = 0;
            CURLcode result = connectAndReceive(url, message, connected, responseCode, authorization);
            if (isStopped()) break;
            error(result, responseCode);
            if (connected) retry.reset();
            if (wait(retry.next())) break;
        }
#else
        (void)url; (void)message; (void)error; (void)authorization;
#endif
    }

private:
    bool wait(unsigned milliseconds) {
        std::unique_lock<std::mutex> lock(mutex);
        return wake.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return isStopped(); });
    }

#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
    CURLcode sendText(CURL* easy, const std::string& text) {
        size_t offset = 0;
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        while (!isStopped() && Clock::now() < deadline) {
            size_t sent = 0;
            CURLcode result = curl_ws_send(easy, text.data() + offset, text.size() - offset,
                                          &sent, 0, CURLWS_TEXT);
            if (result != CURLE_OK && result != CURLE_AGAIN) return result;
            if (sent > text.size() - offset) return CURLE_SEND_ERROR;
            offset += sent;
            if (offset == text.size()) return CURLE_OK;
            if (wait(20)) break;
        }
        return isStopped() ? CURLE_ABORTED_BY_CALLBACK : CURLE_OPERATION_TIMEDOUT;
    }

    CURLcode connectAndReceive(const std::string& url, const Message& message, bool& connected,
                              long& responseCode, const std::string& authorization) {
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
        std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi(curl_multi_init(), curl_multi_cleanup);
        if (!easy || !multi) return CURLE_OUT_OF_MEMORY;
        CURLcode configured = configureCurlTls(easy.get());
        if (configured != CURLE_OK) return configured;
        // Match the native HTTP connection contract. Curl owns the descriptor
        // and closes rejected sockets; no callback data outlives this attempt.
        configured = curl_easy_setopt(easy.get(), CURLOPT_SOCKOPTFUNCTION,
            +[](void*, curl_socket_t socket, curlsocktype purpose) noexcept -> int {
                if (purpose != CURLSOCKTYPE_IPCXN) return CURL_SOCKOPT_OK;
                return ps5_native_socket_mode::establish(socket).accepted
                    ? CURL_SOCKOPT_OK : CURL_SOCKOPT_ERROR;
            });
        if (configured != CURLE_OK) return configured;
        if (!authorization.empty()) {
            if (authorization.find_first_of("\r\n") != std::string::npos) return CURLE_BAD_FUNCTION_ARGUMENT;
            headers.reset(curl_slist_append(nullptr, authorization.c_str()));
            if (!headers) return CURLE_OUT_OF_MEMORY;
            configured = curl_easy_setopt(easy.get(), CURLOPT_HTTPHEADER, headers.get());
            if (configured != CURLE_OK) return configured;
        }
        curl_easy_setopt(easy.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy.get(), CURLOPT_CONNECT_ONLY, 2L);
        curl_easy_setopt(easy.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy.get(), CURLOPT_CONNECTTIMEOUT_MS, 5000L);
        curl_easy_setopt(easy.get(), CURLOPT_TIMEOUT_MS, 10000L);
        curl_easy_setopt(easy.get(), CURLOPT_ACCEPT_ENCODING, "");
        // Redirect responses must remain visible to the caller; do not replay
        // an authenticated handshake at a different endpoint automatically.
        curl_easy_setopt(easy.get(), CURLOPT_FOLLOWLOCATION, 0L);
        if (curl_multi_add_handle(multi.get(), easy.get()) != CURLM_OK) return CURLE_FAILED_INIT;
        // Keep the easy handle attached through connect-only recv/send. Its
        // connection remains owned by this multi handle until cleanup.
        struct Attached {
            CURLM* multi;
            CURL* easy;
            ~Attached() { curl_multi_remove_handle(multi, easy); }
        } attached{multi.get(), easy.get()};

        CURLcode result = CURLE_COULDNT_CONNECT;
        int running = 0;
        while (!isStopped()) {
            if (curl_multi_perform(multi.get(), &running) != CURLM_OK) return CURLE_RECV_ERROR;
            int pending = 0;
            if (auto* done = curl_multi_info_read(multi.get(), &pending)) {
                if (done->msg == CURLMSG_DONE) {
                    result = done->data.result;
                    break;
                }
            }
            if (!running) break;
            int descriptors = 0;
            if (curl_multi_poll(multi.get(), nullptr, 0, 50, &descriptors) != CURLM_OK)
                return CURLE_RECV_ERROR;
        }
        if (curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &responseCode) != CURLE_OK)
            responseCode = 0;
        if (isStopped()) return CURLE_ABORTED_BY_CALLBACK;
        if (result != CURLE_OK) return result;
        connected = true;

        std::string payload;
        bool binary = false;
        while (!isStopped()) {
            if (heartbeat && Clock::now() >= nextHeartbeat) {
                static const std::string keepAlive = R"({"MessageType":"KeepAlive"})";
                result = sendText(easy.get(), keepAlive);
                if (result != CURLE_OK) return result;
                nextHeartbeat = Clock::now() + std::chrono::seconds(20);
            }
            char buffer[4096];
            size_t received = 0;
            const curl_ws_frame* frame = nullptr;
            result = curl_ws_recv(easy.get(), buffer, sizeof(buffer), &received, &frame);
            if (result == CURLE_AGAIN) {
                if (wait(20)) break;
                continue;
            }
            if (result != CURLE_OK) return result;
            if (!frame) return CURLE_RECV_ERROR;
            if (frame->flags & CURLWS_CLOSE) {
                size_t sent = 0;
                curl_ws_send(easy.get(), buffer, received, &sent, 0, CURLWS_CLOSE);
                return CURLE_OK;
            }
            if (frame->flags & (CURLWS_PING | CURLWS_PONG)) continue;
            if (frame->flags & CURLWS_BINARY) binary = true;
            if (received > 1024 * 1024 - payload.size()) return CURLE_RECV_ERROR;
            payload.append(buffer, received);
            if (frame->bytesleft == 0 && !(frame->flags & CURLWS_CONT)) {
                if (!binary && !payload.empty() && !isStopped()) message(payload);
                payload.clear();
                binary = false;
            }
        }
        // Best effort only: close cannot wait indefinitely for the peer.
        size_t sent = 0;
        curl_ws_send(easy.get(), "", 0, &sent, 0, CURLWS_CLOSE);
        return CURLE_ABORTED_BY_CALLBACK;
    }
#endif

    std::atomic_bool stopped{false};
    std::mutex mutex;
    std::condition_variable wake;
    bool heartbeat = false;
    Clock::time_point nextHeartbeat;
};
