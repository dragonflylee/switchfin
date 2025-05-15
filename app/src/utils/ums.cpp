#include "utils/ums.hpp"

#ifdef USE_LIBUSBHSFS
#include <usbhsfs.h>

int Ums::init() {
    Result rc = usbHsFsInitialize(0);
    if (R_FAILED(rc)) return rc;
    usbHsFsSetPopulateCallback(
        [](const UsbHsFsDevice *devices, u32 device_count, void *user_data) {
            auto *self = static_cast<Ums *>(user_data);
            DeviceList ndev;
            ndev.reserve(device_count);

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

                Device dev{
                    .intf_id = d.usb_if_id,
                    .name = std::move(name),
                    .mount_name = d.name,
                };
                size_t found = self->devices.erase(d.usb_if_id);
                ndev.insert(std::make_pair(d.usb_if_id, dev));
                if (!found) brls::Application::notify(fmt::format("{} Found", dev.mount_name));
            }

            for (auto &it : self->devices) {
                brls::Application::notify(fmt::format("{} Removed", it.second.mount_name));
            }
            self->devices = std::move(ndev);
            self->event.fire(self->devices);
        },
        this);

    brls::Application::getExitEvent()->subscribe([this]() {
        usbHsFsSetPopulateCallback(nullptr, nullptr);
        for (auto &dev : this->devices) this->unmount(dev.second);
        usbHsFsExit();
    });
    return 0;
}

bool Ums::unmount(const Device &dev) {
    UsbHsFsDevice d = {.usb_if_id = dev.intf_id};
    this->devices.erase(dev.intf_id);
    return usbHsFsUnmountDevice(&d, true);
}

#else

int Ums::init() { return 0; }

bool Ums::unmount(const Device& dev) { return false; }

#endif