#pragma once

#ifdef USE_LIBUSBHSFS

#include <usbhsfs.h>
#include <borealis/core/application.hpp>
#include <borealis/core/singleton.hpp>

class Ums : public brls::Singleton<Ums> {
public:
    struct Device {
        int32_t intf_id;
        std::string name, mount_name;
    };

    enum DeviceOp {
        OpAdd,
        OpRemove,
    };

public:
    int init() {
        Result rc = usbHsFsInitialize(0);
        if (R_FAILED(rc)) return rc;
        usbHsFsSetPopulateCallback(Ums::usbhsfs_populate_cb, this);

        brls::Application::getExitEvent()->subscribe([this]() {
            usbHsFsSetPopulateCallback(nullptr, nullptr);
            for (auto &dev : this->devices) this->unmount(dev.first);
            usbHsFsExit();
        });
        return 0;
    }

    uint32_t size() const { return usbHsFsGetMountedDeviceCount(); }

    bool unmount(int32_t intf_id) {
        UsbHsFsDevice d = {.usb_if_id = intf_id};
        this->devices.erase(intf_id);
        return usbHsFsUnmountDevice(&d, true);
    }

    brls::Event<const Device &, DeviceOp> *getEvent() { return &this->event; }

private:
    static void usbhsfs_populate_cb(const UsbHsFsDevice *devices, u32 device_count, void *user_data) {
        auto *self = static_cast<Ums *>(user_data);
        std::unordered_map<int32_t, Device> ndev;
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
            if (!found) self->event.fire(dev, OpAdd);
        }

        for (auto &it : self->devices) {
            self->event.fire(it.second, OpRemove);
        }

        self->devices = std::move(ndev);
    }

private:
    std::unordered_map<int32_t, Device> devices;
    brls::Event<const Device &, DeviceOp> event;
};

#endif