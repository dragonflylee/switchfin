#pragma once

#include <cstdlib>
#include <curl/curl.h>

// Shared HTTPS/WSS trust policy. Overrides select a PEM trust store, never a
// verification bypass. libcurl does not implement the curl tool's CA env logic.
inline CURLcode configureCurlTls(CURL* easy) {
    CURLcode result = curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
    if (result != CURLE_OK) return result;
    result = curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
    if (result != CURLE_OK) return result;
    const char* ca = std::getenv("SSL_CERT_FILE");
    if (!ca || !*ca) ca = std::getenv("CURL_CA_BUNDLE");
    if (!ca || !*ca) ca = "/app0/ca-bundle.crt";
    if (ca && *ca) return curl_easy_setopt(easy, CURLOPT_CAINFO, ca);
    return CURLE_OK;
}
