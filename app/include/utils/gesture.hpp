#pragma once

#include <borealis/core/touch/pan_gesture.hpp>

class OsdGestureRecognizer : public brls::PanGestureRecognizer
{
  public:
    OsdGestureRecognizer(brls::PanGestureEvent::Callback respond, brls::PanAxis axis);
    brls::GestureState recognitionLoop(brls::TouchState touch, brls::MouseState mouse, brls::View* view, brls::Sound* soundToPlay) override;
};
