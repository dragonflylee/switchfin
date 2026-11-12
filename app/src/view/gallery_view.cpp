//
// Created by fang on 2022/8/21.
//

#include "view/gallery_view.hpp"

#include <algorithm>
#include <cmath>
#include <map>

using namespace brls::literals;

class PinchGestureRecognizer : public brls::GestureRecognizer {
public:
    using Callback = std::function<void(float scale, bool active)>;

    explicit PinchGestureRecognizer(Callback callback) : callback(std::move(callback)) {}

    brls::GestureState recognitionLoop(
        brls::TouchState touch, brls::MouseState mouse, brls::View* view, brls::Sound* soundToPlay) override {
        if (touch.phase == brls::TouchPhase::END || touch.phase == brls::TouchPhase::NONE)
            this->points.erase(touch.fingerId);
        else
            this->points[touch.fingerId] = touch.position;

        if (this->points.size() < 2) {
            if (this->lastDistance > 0) this->callback(1.0f, false);
            this->lastDistance = 0;
            return this->state = brls::GestureState::FAILED;
        }

        auto it        = this->points.begin();
        brls::Point a  = it->second;
        brls::Point b  = (++it)->second;
        float distance = std::hypot(b.x - a.x, b.y - a.y);
        if (this->lastDistance > 0 && distance > 0) this->callback(distance / this->lastDistance, true);
        this->lastDistance = distance;
        return this->state = brls::GestureState::STAY;
    }

private:
    Callback callback;
    std::map<int, brls::Point> points;
    float lastDistance = 0;
};

GalleryView::GalleryView() {
    brls::Logger::debug("View GalleryView: create");
    this->setFocusable(true);
    this->registerAction(
        "main/player/prev"_i18n, brls::BUTTON_LB,
        [this](brls::View* view) -> bool {
            prev();
            return true;
        },
        false);
    this->registerAction(
        "main/player/prev"_i18n, brls::BUTTON_LT,
        [this](brls::View* view) -> bool {
            prev();
            return true;
        },
        true);
    this->registerAction(
        "main/player/prev"_i18n, brls::BUTTON_NAV_LEFT,
        [this](brls::View* view) -> bool {
            prev();
            return true;
        },
        true);
    this->registerAction(
        "main/player/next"_i18n, brls::BUTTON_RB,
        [this](brls::View* view) -> bool {
            next();
            return true;
        },
        false);
    this->registerAction(
        "main/player/next"_i18n, brls::BUTTON_RT,
        [this](brls::View* view) -> bool {
            next();
            return true;
        },
        true);
    this->registerAction(
        "main/player/next"_i18n, brls::BUTTON_NAV_RIGHT,
        [this](brls::View* view) -> bool {
            next();
            return true;
        },
        true);

    this->registerAction(
        "main/ebook/zoom_in"_i18n, brls::BUTTON_UP,
        [this](brls::View* view) -> bool {
            this->zoom(1.0f + GalleryItem::ZOOM_STEP);
            return true;
        },
        false, true);
    this->registerAction(
        "main/ebook/zoom_out"_i18n, brls::BUTTON_DOWN,
        [this](brls::View* view) -> bool {
            this->zoom(1.0f / (1.0f + GalleryItem::ZOOM_STEP));
            return true;
        },
        false, true);

    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            if (this->pinching) return;
            if (status.state != brls::GestureState::START && status.state != brls::GestureState::STAY) return;
            if (auto* item = this->currentItem()) item->panBy(status.delta.x, status.delta.y);
        },
        brls::PanAxis::ANY));

    this->addGestureRecognizer(new PinchGestureRecognizer([this](float scale, bool active) {
        this->pinching = active;
        if (active && scale != 1.0f) this->zoom(scale);
    }));

    this->addGestureRecognizer(
        new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            if (status.state != brls::GestureState::END) return;
            float width = getWidth();
            float x = getX();
            if (std::isnan(width) || std::isnan(x)) return;
            if (status.position.x < x + width * 0.25) {
                this->prev();
            } else if (status.position.x > x + width * 0.75) {
                this->next();
            }
        }));
}

GalleryView::~GalleryView() { brls::Logger::debug("View GalleryView: delete"); }

brls::View* GalleryView::create() { return new GalleryView(); }

void GalleryView::setData(GalleryData value) {
    this->clearViews();
    this->data = value;

    if (value.empty()) return;

    for (auto v : value) {
        auto* item = new ImageGalleryItem();
        item->setData(v);
        item->setSize(brls::Size(getWidth(), getHeight()));
        item->setPaddingLeft(getPaddingLeft());
        item->setPaddingRight(getPaddingRight());
        this->addView(item, this->getChildren().size());
        brls::Logger::debug("GalleryView set Data: {}/{}", v.second, v.first);
    }

    auto* first = (GalleryItem*)this->getChildren()[0];
    first->setVisibility(brls::Visibility::VISIBLE);
    first->show([]() {}, true, 500);
    this->index = 0;
}

void GalleryView::addCustomView(GalleryItem* view) {
    view->setSize(brls::Size(getWidth(), getHeight()));
    view->setPaddingLeft(getPaddingLeft());
    view->setPaddingRight(getPaddingRight());
    this->addView(view, this->getChildren().size());
    if (this->getChildren().size() == 1) {
        auto* first = (GalleryItem*)this->getChildren()[0];
        first->setVisibility(brls::Visibility::VISIBLE);
        first->show([]() {}, true, 500);
        this->index = 0;
    }
}

void GalleryView::setIndex(unsigned int index) {
    auto& children = this->getChildren();
    if (index >= children.size()) return;

    for (unsigned int i = 0; i < children.size(); i++) {
        auto* item = (GalleryItem*)children[i];
        if (i != index) item->setVisibility(brls::Visibility::INVISIBLE);
    }
    auto* item = (GalleryItem*)children[index];
    item->setVisibility(brls::Visibility::VISIBLE);
    item->show([]() {}, true, 500);
    this->index = index;
}

void GalleryView::prev() {
    if (this->index <= 0) {
        if (auto* focus = brls::Application::getCurrentFocus())
            focus->shakeHighlight(brls::FocusDirection::LEFT);
        return;
    }
    if (auto* item = this->currentItem()) item->resetZoom();
    ((GalleryItem*)this->getChildren()[this->index])->animate(GalleryAnimation::EXIT_RIGHT);

    this->index--;
    ((GalleryItem*)this->getChildren()[this->index])->animate(GalleryAnimation::ENTER_LEFT);
}

void GalleryView::next() {
    if (this->index + 1 >= this->getChildren().size()) {
        if (auto* focus = brls::Application::getCurrentFocus())
            focus->shakeHighlight(brls::FocusDirection::RIGHT);
        return;
    }
    if (auto* item = this->currentItem()) item->resetZoom();
    ((GalleryItem*)this->getChildren()[this->index])->animate(GalleryAnimation::EXIT_LEFT);

    this->index++;
    ((GalleryItem*)this->getChildren()[this->index])->animate(GalleryAnimation::ENTER_RIGHT);
}

GalleryItem* GalleryView::currentItem() {
    auto& children = this->getChildren();
    if (this->index >= children.size()) return nullptr;
    return (GalleryItem*)children[this->index];
}

void GalleryView::zoom(float factor) {
    if (auto* item = this->currentItem()) item->zoomBy(factor);
}

void GalleryView::setIndicatorPosition(float height) { this->indicatorPosition = height; }

void GalleryView::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    // Enable scissoring
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, width, height);
    Box::draw(vg, x, y, width, height, style, ctx);
    nvgRestore(vg);

    // Draw bottom points
    unsigned int n = this->getChildren().size();
    if (n <= 1) return;

    auto padding = 10;
    auto circleR = 4;

    auto drawW = padding * (n - 1) + circleR * 2 * n;
    auto drawX = (width - drawW) / 2 + getMarginLeft();
    auto drawY = height * indicatorPosition;

    float offsetX = 0;
    for (unsigned int i = 0; i < n; i++) {
        nvgBeginPath(vg);
        if (i == this->index) {
            nvgFillColor(vg, RGBA(160, 160, 160, 160));
        } else {
            nvgFillColor(vg, RGBA(160, 160, 160, 48));
        }
        nvgCircle(vg, drawX + offsetX, drawY, circleR);
        offsetX += circleR * 2 + padding;
        nvgFill(vg);
    }
}

const std::string galleryItemXML = R"xml(
    <brls:Box
        width="100%"
        height="100%"
        axis="column"
        grow="1"
        wireframe="false"
        justifyContent="center"
        alignItems="center">

        <brls:Image
                maxWidth="90%"
                maxHeight="80%"
                id="gallery/image"/>
        <brls:Label
                positionType="absolute"
                positionBottom="4%"
                id="gallery/label"
                fontSize="24"/>
    </brls:Box>
)xml";

/// ImageGalleryItem

ImageGalleryItem::ImageGalleryItem() { this->inflateFromXMLString(galleryItemXML); }

void ImageGalleryItem::setData(GalleryItemData value) {
    this->data = value;
    this->image->setImageFromRes(value.first);
    this->label->setText(value.second);
}

/// GalleryItem

GalleryItem::GalleryItem() {
    this->setVisibility(brls::Visibility::INVISIBLE);
    this->hide([]() {}, false, 0);
    this->detach();
}

void GalleryItem::animate(GalleryAnimation animation) {
    this->setVisibility(brls::Visibility::VISIBLE);
    switch (animation) {
    case GalleryAnimation::ENTER_LEFT:
    case GalleryAnimation::ENTER_RIGHT:
        if (animation == GalleryAnimation::ENTER_LEFT)
            //从左侧进入
            this->contentOffsetX = -getWidth();
        else
            //从右侧进入
            this->contentOffsetX = getWidth();
        brls::View::show([]() {}, true, 500);
        startScrolling(0);
        {
            // 滑入屏幕时按需获取焦点
            View* view = this->getDefaultFocus();
            if (view) {
                brls::Logger::debug("GalleryItem defaultFocus: {}", view->describe());
                brls::Application::giveFocus(view);
            }
        }
        break;
    case GalleryAnimation::EXIT_LEFT:
    case GalleryAnimation::EXIT_RIGHT:
        brls::View::hide([]() {}, true, 100);
        this->contentOffsetX = 0;
        {
            // 滑出屏幕时将焦点归还给 GalleryView
            View* view = this->getDefaultFocus();
            if (view) {
                brls::Application::giveFocus(this->getParent());
            }
        }
        brls::View::hide([]() {}, true, 100);
        this->contentOffsetX = 0;
        if (animation == GalleryAnimation::EXIT_LEFT)
            //向左滑出
            startScrolling(-getWidth());
        else
            //向右滑出
            startScrolling(getWidth());
        break;
    }
}

void GalleryItem::startScrolling(float newScroll) {
    if (newScroll == this->contentOffsetX) return;

    this->contentOffsetX.stop();
    this->contentOffsetX.reset();
    this->contentOffsetX.addStep(newScroll, 500, tweeny::easing::quadraticOut);
    this->contentOffsetX.setTickCallback([this] { this->setTranslationX(this->contentOffsetX); });
    this->contentOffsetX.start();
    this->invalidate();
}

void GalleryItem::zoomBy(float factor) {
    float newZoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, this->zoom * factor));
    if (newZoom == this->zoom) return;
    this->panX *= newZoom / this->zoom;
    this->panY *= newZoom / this->zoom;
    this->zoom = newZoom;
    if (!this->isZoomed()) {
        this->resetZoom();
        return;
    }
    this->clampPan();
    this->invalidate();
}

void GalleryItem::resetZoom() {
    if (!this->isZoomed() && this->panX == 0 && this->panY == 0) return;
    this->zoom = MIN_ZOOM;
    this->panX = this->panY = 0;
    this->invalidate();
}

void GalleryItem::panBy(float dx, float dy) {
    if (!this->isZoomed()) return;
    this->panX += dx;
    this->panY += dy;
    this->clampPan();
    this->invalidate();
}

void GalleryItem::clampPan() {
    float maxX = getWidth() * (this->zoom - 1.0f) * 0.5f;
    float maxY = getHeight() * (this->zoom - 1.0f) * 0.5f;
    this->panX = std::max(-maxX, std::min(maxX, this->panX));
    this->panY = std::max(-maxY, std::min(maxY, this->panY));
}

void GalleryItem::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    if (!this->isZoomed()) {
        Box::draw(vg, x, y, width, height, style, ctx);
        return;
    }
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, width, height);
    nvgTranslate(vg, x + width * 0.5f + this->panX, y + height * 0.5f + this->panY);
    nvgScale(vg, this->zoom, this->zoom);
    nvgTranslate(vg, -width * 0.5f, -height * 0.5f);
    Box::draw(vg, 0, 0, width, height, style, ctx);
    nvgRestore(vg);
}