#include "utils/ums.hpp"
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_storage_home.hpp"
#endif

#ifdef PS5_NATIVE_GPU
#ifdef USE_LIBUSBHSFS
#include <usbhsfs.h>

int Ums::init() {
    Result rc = usbHsFsInitialize(0);
    if (R_FAILED(rc)) return rc;
    usbHsFsSetPopulateCallback(
        [](const UsbHsFsDevice *devices, u32 device_count, void *user_data) {
            auto *self = static_cast<Ums *>(user_data);
            DeviceList ndev;
            ndev.reserve(device_count + 1);
            ndev.push_back({.id = -1, .name = "SD Card", .mount = "sdmc:"});

            for (u32 i = 0; i < device_count; ++i) {
                auto &d = devices[i];

                std::string name;
                if (auto sv = std::string_view(d.product_name); !sv.empty())
                    name = sv;
                else if (sv = std::string_view(d.manufacturer); !sv.empty())
                    name = sv;
                else if (sv = std::string_view(d.serial_number); !sv.empty())
                    name = sv;
                else
                    name = "Unnamed device";
                ndev.push_back({.id = d.usb_if_id, .name = std::move(name), .mount = d.name});
            }
            self->devices = std::move(ndev);
            self->event.fire(self->devices);
        },
        this);

    if (!usbHsFsGetMountedDeviceCount()) {
        this->devices.push_back({.id = -1, .name = "SD Card", .mount = "sdmc:"});
    }

    brls::Application::getExitEvent()->subscribe([this]() {
        usbHsFsSetPopulateCallback(nullptr, nullptr);
        for (auto &dev : this->devices)
            if (dev.id >= 0) this->unmount(dev);
        this->devices.clear();
        usbHsFsExit();
    });
    return 0;
}

bool Ums::unmount(const Device &dev) {
    UsbHsFsDevice d = {.usb_if_id = dev.id};
    return usbHsFsUnmountDevice(&d, true);
}

#else

#if defined(__PSV__)

int Ums::init() {
    this->devices.push_back(Device{.id = -1, .name = "Memory Stock", .mount = "ux0:/data"});
    return 0;
}

#elif defined(__PS4__)

int Ums::init() {
    this->devices.push_back(Device{.id = -1, .name = "HardDisk", .mount = "/data"});
    return 0;
}

#else
#include <sys/stat.h>
#include "utils/config.hpp"

int Ums::init() {
    const auto offer = [this](int id, std::string name, const std::string& mount) {
        // Only offer paths that exist: the packaged title runs in a sandbox with
        // no /data, and listing a missing directory throws at the browser.
        struct stat st {};
        if (stat(mount.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            this->devices.push_back(Device{.id = id, .name = std::move(name), .mount = mount});
    };
    // Removable roots: offer only mount points that actually have a filesystem
    // mounted (device id differs from the parent), so empty /mnt/usbN slots that
    // exist once promoted are not listed.
    const auto offerMount = [this, &offer](int id, std::string name, const std::string& mount) {
        struct stat st {}, parent {};
        const std::string par = mount.substr(0, mount.rfind('/')) + "/";
        if (stat(mount.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
            stat(par.c_str(), &parent) == 0 && st.st_dev != parent.st_dev)
            offer(id, std::move(name), mount);
    };
    // A completed download lives three levels below /download0, in a directory
    // named by the server's item id, inside a directory this title creates
    // 0700. Offering it as a root of its own is the difference between "the
    // download is not there" and "the download is four steps away".
    offer(-1, "Downloads", ps5::storage::downloadHome(AppConfig::instance().configDir() + "/downloads"));
    // The writable partition this build keeps its own data on. Once promoted the
    // real internal drive (/data) is reachable, so offer that; the sandbox image
    // otherwise.
    // The active download location (/data when elevated) is the "Downloads"
    // node above. Once promoted, also expose the guaranteed sandbox downloads
    // (so both roots are reachable) and the real internal drive.
    if (ps5::storage::sandboxRoot().empty()) {
        offer(-1, "Console Storage", "/download0");
    } else {
        const std::string sandboxDownloads = AppConfig::instance().configDir() + "/downloads";
        if (ps5::storage::downloadHome(sandboxDownloads) != sandboxDownloads)
            offer(-1, "Sandbox Downloads", sandboxDownloads);
        offer(-1, "Internal Storage", "/data");
    }
    for (int i = 0; i < 8; i++) offerMount(i, fmt::format("USB{}", i), fmt::format("/mnt/usb{}", i));
    for (int i = 0; i < 8; i++) offerMount(100 + i, fmt::format("External{}", i), fmt::format("/mnt/ext{}", i));
    return 0;
}

#endif

bool Ums::unmount(const Device& dev) { return false; }

#endif
#else
#ifdef USE_LIBUSBHSFS
#include <usbhsfs.h>

int Ums::init() {
    Result rc = usbHsFsInitialize(0);
    if (R_FAILED(rc)) return rc;
    usbHsFsSetPopulateCallback(
        [](const UsbHsFsDevice *devices, u32 device_count, void *user_data) {
            auto *self = static_cast<Ums *>(user_data);
            DeviceList ndev;
            ndev.reserve(device_count + 1);
            ndev.push_back({.id = -1, .name = "SD Card", .mount = "sdmc:"});

            for (u32 i = 0; i < device_count; ++i) {
                auto &d = devices[i];

                std::string name;
                if (auto sv = std::string_view(d.product_name); !sv.empty())
                    name = sv;
                else if (sv = std::string_view(d.manufacturer); !sv.empty())
                    name = sv;
                else if (sv = std::string_view(d.serial_number); !sv.empty())
                    name = sv;
                else
                    name = "Unnamed device";
                ndev.push_back({.id = d.usb_if_id, .name = std::move(name), .mount = d.name});
            }
            self->devices = std::move(ndev);
            self->event.fire(self->devices);
        },
        this);

    if (!usbHsFsGetMountedDeviceCount()) {
        this->devices.push_back({.id = -1, .name = "SD Card", .mount = "sdmc:"});
    }

    brls::Application::getExitEvent()->subscribe([this]() {
        usbHsFsSetPopulateCallback(nullptr, nullptr);
        for (auto &dev : this->devices)
            if (dev.id >= 0) this->unmount(dev);
        this->devices.clear();
        usbHsFsExit();
    });
    return 0;
}

bool Ums::unmount(const Device &dev) {
    UsbHsFsDevice d = {.usb_if_id = dev.id};
    return usbHsFsUnmountDevice(&d, true);
}

#else

#if defined(__PSV__)

int Ums::init() {
    this->devices.push_back(Device{.id = -1, .name = "Memory Stock", .mount = "ux0:/data"});
    return 0;
}

#elif defined(__PS4__)

int Ums::init() {
    this->devices.push_back(Device{.id = -1, .name = "HardDisk", .mount = "/data"});
    return 0;
}

#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

int Ums::init() {
    WCHAR wpath[MAX_PATH];
    std::vector<char> lpath(MAX_PATH);
    SHGetSpecialFolderPathW(0, wpath, CSIDL_MYVIDEO, false);
    WideCharToMultiByte(CP_UTF8, 0, wpath, std::wcslen(wpath), lpath.data(), lpath.size(), nullptr, nullptr);
    this->devices.push_back({.id = -1, .name = lpath.data(), .mount = lpath.data()});
    return 0;
}

#else
int Ums::init() { return 0; }
#endif

bool Ums::unmount(const Device& dev) { return false; }

#endif
#endif
