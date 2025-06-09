#pragma once

#include <borealis/core/application.hpp>
#include <borealis/core/singleton.hpp>

class Ums : public brls::Singleton<Ums> {
public:
    struct Device {
        int32_t id;
        std::string name, mount;
    };

    using DeviceList = std::vector<Device>;
    using DeviceEvent = brls::Event<const DeviceList &>;

public:
    int init();
    bool unmount(const Device &dev);
    DeviceEvent *getEvent() { return &this->event; }
    inline const DeviceList &getDevice() const { return this->devices; }

private:
    DeviceList devices;
    DeviceEvent event;
};