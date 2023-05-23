#include <borealis/core/application.hpp>
#include "api/jellyfin.hpp"

std::string jellyfin::defaultAuthHeader() {
    static std::string header;
    std::once_flag flag;
    std::call_once(flag, []() {
        std::string deviceId = brls::Application::getPlatform()->getIpAddress();
        header = fmt::format(
            "X-Emby-Authorization: MediaBrowser Client=\"{}\", Device=\"{}\", DeviceId=\"{}\", Version=\"{}\"",
            AppVersion::getPlatform(), AppVersion::getDeviceName(), deviceId, AppVersion::getVersion());
    });
    return header;
}