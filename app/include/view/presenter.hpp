#pragma once

#include <utils/event.hpp>

namespace brls {
class View;
}

/// true when the current focus lives inside `view` — lets a refreshing tab
/// know whether it owned the focus (and should restore it after rebuilding)
/// without stealing it from the sidebar when loading in the background
bool hasFocusWithin(brls::View* view);

class Presenter {
public:
    Presenter();
    virtual ~Presenter();

    virtual void doRequest() = 0;

protected:
    MPVCustomEvent::Subscription customEventSubscribeID;
};