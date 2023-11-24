#pragma once

#include <view/mpv_core.hpp>

class Presenter {
public:
    Presenter();
    virtual ~Presenter();

    virtual void doRequest() = 0;

protected:
    MPVCustomEvent::Subscription customEventSubscribeID;
};