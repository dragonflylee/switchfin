#pragma once

#include <utils/event.hpp>

class Presenter {
public:
    Presenter();
    virtual ~Presenter();

    virtual void doRequest() = 0;

protected:
    MPVCustomEvent::Subscription customEventSubscribeID;
};