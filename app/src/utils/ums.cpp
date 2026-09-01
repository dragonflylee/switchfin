#include "utils/ums.hpp"

#ifdef USE_LIBUSBHSFS
#include <usbhsfs.h>

int Ums::init() {
    Result rc = usbHsFsInitialize(0);
    if (R_FAILED(rc)) return rc;
    usbHsFsSetPopulateCallback(
        [](const UsbHsFsDevice* devices, u32 device_count, void* user_data) {
            auto* self = static_cast<Ums*>(user_data);
            DeviceList ndev;
            ndev.reserve(device_count + 1);
            ndev.push_back({.id = -1, .name = "SD Card", .mount = "sdmc:"});

            for (u32 i = 0; i < device_count; ++i) {
                auto& d = devices[i];

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
        for (auto& dev : this->devices)
            if (dev.id >= 0) this->unmount(dev);
        this->devices.clear();
        usbHsFsExit();
    });
    return 0;
}

bool Ums::unmount(const Device& dev) {
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
#include <initguid.h>
#include <shlobj.h>
#include <dbt.h>

DEFINE_GUID(GUID_DEVINTERFACE_VOLUME, 0x53f5630dL, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

/* windowproc fo detect any USB device addition/removal */
static LRESULT CALLBACK Ums_UsbDetectProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DEVICECHANGE) {
        switch (wParam) {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE:
            PDEV_BROADCAST_HDR hdr = reinterpret_cast<PDEV_BROADCAST_HDR>(lParam);
            brls::Logger::info("ums device: {:x} {}", wParam, hdr->dbch_devicetype);
            break;
        }
        return 0;
    }
    return CallWindowProc(DefWindowProc, hwnd, msg, wParam, lParam);
}

static void* Ums_ThreadLoop(void* ptr) {
    WNDCLASSEX wincl = {sizeof(WNDCLASSEX)};
    wincl.hInstance = GetModuleHandle(NULL);
    wincl.lpszClassName = TEXT("Ums_UsbDetect");
    wincl.lpfnWndProc = Ums_UsbDetectProc;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!RegisterClassEx(&wincl)) {
        brls::Logger::warning("ums register class failed: {}", GetLastError());
        return nullptr;
    }
    HWND hwnd = CreateWindowEx(0, wincl.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    if (!hwnd) {
        brls::Logger::warning("ums create message window failed: {}", GetLastError());
        return nullptr;
    }

    DEV_BROADCAST_DEVICEINTERFACE dbh = {sizeof(DEV_BROADCAST_DEVICEINTERFACE)};
    dbh.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    dbh.dbcc_classguid = GUID_DEVINTERFACE_VOLUME;
    HDEVNOTIFY notify = RegisterDeviceNotification(hwnd, &dbh, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!notify) {
        brls::Logger::warning("ums create notify device failed: {}", GetLastError());
        return nullptr;
    }

    brls::Logger::verbose("ums thread: start {} notify {}", fmt::ptr(hwnd), fmt::ptr(notify));

    MSG msg;
    while (!GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterDeviceNotification(notify);
    DestroyWindow(hwnd);
    UnregisterClass(wincl.lpszClassName, wincl.hInstance);
    CoUninitialize();

    brls::Logger::verbose("ums thread: exit {}", fmt::ptr(hwnd));
    return nullptr;
}

int Ums::init() {
    WCHAR wpath[MAX_PATH];
    std::vector<char> lpath(MAX_PATH);
    SHGetSpecialFolderPathW(0, wpath, CSIDL_MYVIDEO, false);
    WideCharToMultiByte(CP_UTF8, 0, wpath, std::wcslen(wpath), lpath.data(), lpath.size(), nullptr, nullptr);
    this->devices.push_back({.id = -1, .name = lpath.data(), .mount = lpath.data()});

#ifdef BOREALIS_USE_STD_THREAD
    auto th = std::make_shared<std::thread>(Ums_ThreadLoop, this);
#else
    pthread_t th = 0;
    pthread_create(&th, nullptr, Ums_ThreadLoop, this);
#endif
    return 0;
}

#else
int Ums::init() { return 0; }
#endif

bool Ums::unmount(const Device& dev) { return false; }

#endif