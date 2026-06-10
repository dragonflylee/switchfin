//
// Press-and-hold (long press) gesture recognizer, in the spirit of
// brls::TapGestureRecognizer (borealis has tap and pan recognizers only).
//
// Hold the touch / left mouse button on the view for `duration` microseconds
// without moving beyond MOVE_THRESHOLD and the recognizer fires once. Firing
// returns GestureState::START, which makes View::gestureRecognizerRequest
// interrupt every other UNSURE recognizer in the responder chain — the tap
// recognizer of the same view among them — so the following release does NOT
// trigger the primary (BUTTON_A) action.
//
// Canceled (FAILED) if the pointer leaves the view, moves beyond the
// threshold (pan/scroll intent), or is released before the duration.
//

#pragma once

#include <borealis.hpp>

class LongPressGestureRecognizer : public brls::GestureRecognizer {
public:
    // On recognition: focuses `view` first, then fires the gamepad action
    // bound to `button` on it — the exact code path of pressing that button
    // with the view focused (BUTTON_X / KeyBind Setting = context menu on
    // the media cards).
    explicit LongPressGestureRecognizer(brls::View* view, brls::ControllerButton button = brls::BUTTON_X,
        brls::Time duration = LONG_PRESS_DURATION);

    brls::GestureState recognitionLoop(
        brls::TouchState touch, brls::MouseState mouse, brls::View* view, brls::Sound* soundToPlay) override;

    static constexpr brls::Time LONG_PRESS_DURATION = 500000;  // µs
    static constexpr float MOVE_THRESHOLD = 10.0f;             // points

private:
    std::function<void(brls::Sound*)> respond;
    brls::Time duration;
    brls::Point startPosition;
    brls::Time startTime = 0;
};
