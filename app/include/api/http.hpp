/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <curl/curl.h>

#include <borealis/core/event.hpp>

class HTTP {
public:
    using Header = std::vector<std::string>;
    using Form = std::unordered_map<std::string, std::string>;
    // For cancellable requests
    using Cancel = std::shared_ptr<std::atomic_bool>;
    using Progress = brls::Event<curl_off_t, curl_off_t>;

    inline static bool PROXY_STATUS = false;
    inline static std::string PROXY_HOST = "192.168.1.1";
    inline static int PROXY_PORT = 1080;

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
    std::string get(const std::string& url);
    void download(const std::string& url, const std::string& path);
    std::string post(const std::string& url, const std::string& data);

    template <typename... Ts>
    static void set_option(HTTP& s, Ts&&... ts) {
        s.set_option_internal<Ts...>(std::forward<Ts>(ts)...);
    }

    // Get methods
    template <typename... Ts>
    static std::string get(const std::string& url, Ts&&... ts) {
        HTTP s;
        set_option(s, std::forward<Ts>(ts)...);
        return s.get(url);
    }

    template <typename... Ts>
    static void download(const std::string& url, const std::string& path, Ts&&... ts) {
        HTTP s;
        set_option(s, std::forward<Ts>(ts)...);
        s.download(url, path);
    }

    // Post methods
    template <typename... Ts>
    static std::string post(const std::string& url, const std::string& data, Ts&&... ts) {
        HTTP s;
        set_option(s, std::forward<Ts>(ts)...);
        return s.post(url, data);
    }

    template <typename... Ts>
    static std::string post(const std::string& url, const Form& form, Ts&&... ts) {
        HTTP s;
        set_option(s, std::forward<Ts>(ts)...);
        return s.post(url, s.encode_form(form));
    }

    inline static long TIMEOUT = 3000L;

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
    struct curl_slist* chunk;
    Cancel is_cancel;
    Progress event;
};
