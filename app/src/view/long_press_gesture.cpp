#include "view/long_press_gesture.hpp"

#include <cmath>

LongPressGestureRecognizer::LongPressGestureRecognizer(
    brls::View* view, brls::ControllerButton button, brls::Time duration)
    : duration(duration) {
    // Same pattern as brls::TapGestureRecognizer(View*), which replays the
    // BUTTON_A action on tap: here we replay the `button` (X by default)
    // action on long press. The recognizer is owned by `view` and deleted
    // with it, so the captured pointer cannot outlive the view.
    this->respond = [view, button](brls::Sound* soundToPlay) {
        // Focus the pressed view first: the triggered action (context menu)
        // pushes an activity whose focus restore must come back here.
        brls::Application::giveFocus(view);

        for (auto& action : view->getActions()) {
            if (action->getType() != brls::ACTION_GAMEPAD || action->getButton() != button) continue;
            if (!action->isAvailable()) continue;
            if (action->getActionListener()(view)) *soundToPlay = action->getSound();
        }
    };
}

brls::GestureState LongPressGestureRecognizer::recognitionLoop(
    brls::TouchState touch, brls::MouseState mouse, brls::View* view, brls::Sound* soundToPlay) {
    // Unify touch and mouse, as brls::TapGestureRecognizer does
    brls::TouchPhase phase = touch.phase;
    brls::Point position = touch.position;

    if (phase == brls::TouchPhase::NONE) {
        position = mouse.position;
        phase = mouse.leftButton;
    }

    if (!enabled || phase == brls::TouchPhase::NONE) return brls::GestureState::FAILED;

    switch (phase) {
        case brls::TouchPhase::START:
            this->state = brls::GestureState::UNSURE;
            this->startPosition = position;
            this->startTime = brls::getCPUTimeUsec();
            break;
        case brls::TouchPhase::STAY:
            // START must only be reported for one frame (gesture.hpp contract)
            if (this->state == brls::GestureState::START) {
                this->state = brls::GestureState::STAY;
                break;
            }

            if (this->state != brls::GestureState::UNSURE) break;

            // Out of the view's bounds, or moved enough to be a pan/scroll:
            // this is not a long press
            if (position.x < view->getX() || position.x > view->getX() + view->getWidth()
                || position.y < view->getY() || position.y > view->getY() + view->getHeight()
                || std::fabs(position.x - this->startPosition.x) > MOVE_THRESHOLD
                || std::fabs(position.y - this->startPosition.y) > MOVE_THRESHOLD) {
                this->state = brls::GestureState::FAILED;
            } else if (brls::getCPUTimeUsec() - this->startTime >= this->duration) {
                this->state = brls::GestureState::START;
                this->respond(soundToPlay);
            }
            break;
        case brls::TouchPhase::END:
            this->state = (this->state == brls::GestureState::START || this->state == brls::GestureState::STAY)
                              ? brls::GestureState::END
                              : brls::GestureState::FAILED;
            break;
        case brls::TouchPhase::NONE:
            this->state = brls::GestureState::FAILED;
            break;
    }

    return this->state;
}
