/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <curl/curl.h>
#include <borealis/core/thread.hpp>

class HTTP {
public:
    using Header = std::vector<std::string>;
    using Form = std::unordered_map<std::string, std::string>;
    // For cancellable requests
    using Cancel = std::shared_ptr<std::atomic_bool>;

    struct Range {
        int start = 0;
        int end = 0;
    };

    struct Timeout {
        long timeout = -1;
    };

    HTTP();
    HTTP(const HTTP& other) = delete;
    ~HTTP();

    std::string encode_form(const Form& form);
    std::string get(const std::string& url);
    std::string post(const std::string& url, const std::string& data);

    static std::string encode_query(const Form& form) {
        HTTP s;
        return s.encode_form(form);
    }

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

    template <typename Then, typename Error, typename... Ts>
    static void get_async(Then then, Error error, Ts... ts) {
        brls::async(std::bind(
            [](Then then_inner, Error error_inner, Ts... ts_inner) {
                try {
                    then_inner(HTTP::get(std::move(ts_inner)...));
                } catch (const std::exception& ex) {
                    error_inner(ex.what());
                }
            },
            std::move(then), std::move(error), std::move(ts)...));
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

    template <typename Then, typename Error, typename... Ts>
    static void post_async(Then then, Error error, Ts... ts) {
        brls::async(std::bind(
            [](Then then_inner, Error error_inner, Ts... ts_inner) {
                try {
                    then_inner(HTTP::post(std::move(ts_inner)...));
                } catch (const std::exception& ex) {
                    error_inner(ex.what());
                }
            },
            std::move(then), std::move(error), std::move(ts)...));
    }

private:
    static size_t easy_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata);
    static int easy_progress_cb(
        void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    void perform(std::ostream* body);

    void add_header(const std::string& header);
    void set_option(const Header& hs);
    void set_option(const Range& r);
    void set_option(const Timeout& t);
    void set_option(const Cancel& c);

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
};
