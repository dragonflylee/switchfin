#pragma once

#include <curl/curl.h>
#include <mutex>
#include <stdexcept>

// curl's DNS cache may be shared between easy handles only with application
// locking. This mutex protects cache operations during transfers, not just
// creation of the easy handles.
class CurlDnsShare {
public:
    CurlDnsShare() : share(curl_share_init()) {
        if (!share) throw std::runtime_error("curl DNS share allocation failed");
        CURLSHcode result = curl_share_setopt(share, CURLSHOPT_USERDATA, this);
        if (result == CURLSHE_OK) result = curl_share_setopt(share, CURLSHOPT_LOCKFUNC, lock);
        if (result == CURLSHE_OK) result = curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, unlock);
        if (result == CURLSHE_OK) result = curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        if (result != CURLSHE_OK) {
            curl_share_cleanup(share);
            share = nullptr;
            throw std::runtime_error(curl_share_strerror(result));
        }
    }

    ~CurlDnsShare() { if (share) curl_share_cleanup(share); }
    CurlDnsShare(const CurlDnsShare&) = delete;
    CurlDnsShare& operator=(const CurlDnsShare&) = delete;
    CURLSH* get() const { return share; }

private:
    static void lock(CURL*, curl_lock_data, curl_lock_access, void* opaque) {
        static_cast<CurlDnsShare*>(opaque)->mutex.lock();
    }
    static void unlock(CURL*, curl_lock_data, void* opaque) {
        static_cast<CurlDnsShare*>(opaque)->mutex.unlock();
    }

    CURLSH* share;
    std::mutex mutex;
};
