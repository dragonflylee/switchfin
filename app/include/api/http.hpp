/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <fstream>
#include <sstream>
#include <curl/system.h>
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_native_http_diagnostics.hpp"
#include "utils/ps5_native_socket_mode.hpp"
#include "utils/ps5_native_socket_io.hpp"
#include "utils/ps5_download_file.hpp"
#endif

#include <borealis/core/event.hpp>

class HTTP {
public:
    using Header = std::vector<std::string>;
    using Form = std::unordered_map<std::string, std::string>;
    // For cancellable requests
    using Cancel = std::shared_ptr<std::atomic_bool>;
    using Progress = brls::Event<curl_off_t, curl_off_t>;

    const static std::string USER_AGENT;
    inline static bool PROXY_STATUS = false;
    inline static std::string PROXY;

    struct Range {
        int start = 0;
        int end = 0;
    };

    struct Timeout {
        long timeout = TIMEOUT;
    };

    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;
        std::string path;
        bool http_only;
    };

    using Cookies = std::vector<Cookie>;

    HTTP();
    HTTP(const HTTP& other) = delete;
    ~HTTP();

    static std::string encode_form(const Form& form);
    void _get(const std::string& url, std::ostream* out);
    bool getinfo(char** arg);
    int propfind(const std::string& url, std::ostream* out);
    std::string _post(const std::string& url, const std::string& data);
    void set_user_agent(const std::string& agent);
    void set_basic_auth(const std::string& user, const std::string& passwd);
    void _delete(const std::string& url, std::ostream* out);

    template <typename... Ts>
    static void set_option(HTTP& s, Ts&&... ts) {
        s.set_option_internal<Ts...>(std::forward<Ts>(ts)...);
    }

    // Get methods
    template <typename... Ts>
    static std::string get(const std::string& url, Ts&&... ts) {
        HTTP s;
        std::ostringstream body;
        set_option(s, std::forward<Ts>(ts)...);
        s._get(url, &body);
        return body.str();
    }

    template <typename... Ts>
    static void download(const std::string& url, const std::string& path, Ts&&... ts) {
        HTTP s;
#ifdef PS5_NATIVE_GPU
        ps5::downloads::File<> file(path);
#else
        std::ofstream of(path, std::ios_base::binary);
#endif
        set_option(s, std::forward<Ts>(ts)...);
#ifdef PS5_NATIVE_GPU
        s._get(url, &file.stream());
        if (!file.commit(s.is_cancel.get())) throw ps5::downloads::Error(ps5::downloads::Failure::Cancelled);
#else
        s._get(url, &of);
#endif
    }

    // Post methods
    template <typename... Ts>
    static std::string post(const std::string& url, const std::string& data, Ts&&... ts) {
        HTTP s;
        set_option(s, std::forward<Ts>(ts)...);
        return s._post(url, data);
    }

    template <typename... Ts>
    static std::string post(const std::string& url, const Form& form, Ts&&... ts) {
        HTTP s;
        set_option(s, std::forward<Ts>(ts)...);
        return s._post(url, s.encode_form(form));
    }

    inline static long TIMEOUT = 3000L;
#ifdef PS5_NATIVE_GPU

    /**
     * Drop the per-request state a previous caller left on this handle.
     *
     * Only matters for a shared handle: ThreadPool gives each worker one
     * long-lived HTTP and hands it to every task, so without this a cancel flag
     * or a progress subscriber from one request stays installed for all the
     * later, unrelated ones on the same worker.
     */
    void reset();

    /** Status of the most recent response on this handle, 0 if there was none. */
    long last_status() const;
#endif

private:
    static size_t easy_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);
    static int easy_progress_cb(
        void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    int perform(std::ostream* body);

    void add_header(const std::string& header);
    void set_option(const Header& hs);
    void set_option(const Range& r);
    void set_option(const Timeout& t);
    void set_option(const Cancel& c);
    void set_option(const Cookies& cookies);
    void set_option(Progress::Callback p);

    template <typename CT>
    void set_option_internal(CT&& option) {
        set_option(std::forward<CT>(option));
    }

    template <typename CT, typename... Ts>
    void set_option_internal(CT&& option, Ts&&... ts) {
        set_option_internal<CT>(std::forward<CT>(option));
        set_option_internal<Ts...>(std::forward<Ts>(ts)...);
    }

    void* easy;
#ifdef PS5_NATIVE_GPU
    ps5_native_http::ErrorBuffer native_error_buffer;
    // Read by the socket callback, which runs before connect. The default is
    // the same value every request would otherwise be given.
    long connect_deadline_ms = TIMEOUT;
#endif
    struct curl_slist* chunk;
    Cancel is_cancel;
    Progress event;
};
