#include "utils/gesture.hpp"

OsdGestureRecognizer::OsdGestureRecognizer(brls::PanGestureEvent::Callback respond, brls::PanAxis axis)
    : PanGestureRecognizer(respond, axis) {}

brls::GestureState OsdGestureRecognizer::recognitionLoop(
    brls::TouchState touch, brls::MouseState mouse, brls::View* view, brls::Sound* soundToPlay) {
    if (!enabled) return brls::GestureState::FAILED;

    if (touch.phase != brls::TouchPhase::NONE)
        return PanGestureRecognizer::recognitionLoop(touch, mouse, view, soundToPlay);

    brls::GestureState result;
    if (mouse.scroll.x != 0 || mouse.scroll.y != 0) {
        result = brls::GestureState::STAY;
        brls::PanGestureStatus status{
            .state = brls::GestureState::STAY,
            .position = brls::Point(),
            .startPosition = brls::Point(),
            .delta = mouse.offset,
            .deltaOnly = true,
        };
        this->getPanGestureEvent().fire(status, soundToPlay);
    } else {
#ifdef NO_TOUCH_SCROLLING
        result = brls::GestureState::FAILED;
#else
        result = PanGestureRecognizer::recognitionLoop(touch, mouse, view, soundToPlay);
#endif
    }

    return result;
}
