
#include "view/ebook_view.hpp"

#ifdef USE_MUPDF

#include "utils/keybind.hpp"
#include <mupdf/fitz.h>

namespace {
// MuPDF only allows fz_clone_context() when the context has real lock
// callbacks. Without them fz_clone_context() returns NULL and EBookView::open()
// bails out before any page is rendered (blank screen). Provide a mutex array
// so the background (cloned) context can safely share the document/store with
// the main-thread rendering context.
#ifndef FZ_LOCK_MAX
#define FZ_LOCK_MAX 4
#endif
std::mutex gFzLocks[FZ_LOCK_MAX];

void fzLockCB(void* user, int lock) { gFzLocks[lock].lock(); }
void fzUnlockCB(void* user, int lock) { gFzLocks[lock].unlock(); }

fz_locks_context gFzLocksCtx = {nullptr, fzLockCB, fzUnlockCB};
}  // namespace

class fz_error : public std::exception {
public:
    explicit fz_error(fz_context* ctx) : msg(ctx ? ctx->error.message : "") {}
    const char* what() const noexcept override { return msg.c_str(); }

private:
    std::string msg;
};

using namespace brls::literals;

namespace {
constexpr float ZOOM_LEVELS[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
constexpr size_t ZOOM_LEVELS_COUNT = sizeof(ZOOM_LEVELS) / sizeof(ZOOM_LEVELS[0]);
}  // namespace

EBookView::EBookView() {
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setHideHighlightBackground(true);
    this->left = new brls::Image();
    this->left->setMarginRight(10);
    this->right = new brls::Image();
    this->addView(this->left);
    this->addView(this->right);
    this->loading = new brls::ProgressSpinner(brls::ProgressSpinnerSize::LARGE);
    this->loading->setWidth(100);
    this->loading->setHeight(100);
    this->loading->setVisibility(brls::Visibility::GONE);
    this->addView(this->loading);
    this->setJustifyContent(brls::JustifyContent::CENTER);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPaddingLeft(brls::getStyle().getMetric("main/content_padding_sides"));
    this->setPaddingRight(brls::getStyle().getMetric("main/content_padding_sides"));
    this->setPaddingTop(10.f);
    this->setPaddingBottom(10.f);
    this->setClipsToBounds(true);

    this->ctx = fz_new_context(nullptr, &gFzLocksCtx, FZ_STORE_DEFAULT);
    fz_register_document_handlers(ctx);

    this->registerAction("hints/back"_i18n, brls::BUTTON_B,
        [this](brls::View* view) { return brls::Application::popActivity(brls::TransitionAnimation::NONE); });

    this->registerAction("main/ebook/zoom_in"_i18n, brls::BUTTON_UP, [this](brls::View* view) {
        this->zoomIn();
        return true;
    });

    this->registerAction("main/ebook/zoom_out"_i18n, brls::BUTTON_DOWN, [this](brls::View* view) {
        this->zoomOut();
        return true;
    });

    this->registerAction("main/ebook/single_page"_i18n, brls::BUTTON_LSB, [this](brls::View* view) {
        this->togglePageMode();
        return true;
    });
    this->registerAction(KeyBind::getSetting(), [this](brls::View* view) {
        this->togglePageMode();
        return true;
    });

    this->registerAction("main/ebook/outline"_i18n, brls::BUTTON_RSB, [this](brls::View* view) {
        this->showOutline();
        return true;
    });

    this->registerAction(KeyBind::getRefresh(), [this](brls::View* view) {
        this->showOutline();
        return true;
    });

    this->registerAction("main/player/prev"_i18n, brls::BUTTON_LEFT, [this](brls::View* view) {
        if (this->page <= 0) return false;
        this->page--;
        this->renderCurrent();
        return true;
    });

    this->registerAction("main/player/next"_i18n, brls::BUTTON_RIGHT, [this](brls::View* view) {
        if (this->page >= this->count - 1) return false;
        this->page++;
        this->renderCurrent();
        return true;
    });

    this->addGestureRecognizer(
        new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            if (status.state != brls::GestureState::END) return;
            auto frame = this->getFrame();
            if (status.position.x < frame.getMidX()) {
                if (this->page <= 0) return;
                this->page--;
                this->renderCurrent();
            } else {
                if (this->page >= this->count - 1) return;
                this->page++;
                this->renderCurrent();
            }
        }));

    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            if (status.state != brls::GestureState::START && status.state != brls::GestureState::STAY) return;
            this->panX += status.delta.x;
            this->panY += status.delta.y;
            this->clampPan();
            this->updateLayout();
        },
        brls::PanAxis::ANY));
}

EBookView::~EBookView() {
    if (this->doc) fz_drop_document(this->ctx, this->doc);
    if (this->ctx) fz_drop_context(this->ctx);
}

void EBookView::open(const std::string& url, float percent, const HTTP::Header& headers) {
    this->page = std::floor(percent);

    this->loading->setVisibility(brls::Visibility::VISIBLE);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url, headers]() {
        try {
            fz_stream* stream = nullptr;
            if (url.find("://") == std::string::npos) {
                stream = fz_open_file(ctx, url.c_str());
            } else {
                std::string content = HTTP::get(url, HTTP::Timeout{}, headers);
                fz_buffer* buf = fz_new_buffer_from_copied_data(ctx, (const uint8_t*)content.data(), content.size());
                stream = fz_open_buffer(ctx, buf);
                fz_drop_buffer(ctx, buf);
            }
            fz_try(ctx) this->doc = fz_open_document_with_stream(ctx, url.c_str(), stream);
            fz_always(ctx) fz_drop_stream(ctx, stream);
            fz_catch(ctx) throw fz_error(ctx);

            fz_try(ctx) this->count = fz_count_pages(ctx, this->doc);
            fz_catch(ctx) throw fz_error(ctx);

            this->loadOutline();

            fz_try(ctx) {
                fz_page* p = fz_load_page(ctx, this->doc, 0);
                fz_rect bounds = fz_bound_page(ctx, p);
                this->pageWidth = bounds.x1 - bounds.x0;
                this->pageHeight = bounds.y1 - bounds.y0;
                fz_drop_page(ctx, p);
            }
            fz_catch(ctx) {}

            brls::sync([ASYNC_TOKEN]() {
                ASYNC_RELEASE
                this->loading->setVisibility(brls::Visibility::GONE);
                this->renderCurrent();
            });
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                this->loading->setVisibility(brls::Visibility::GONE);
                this->dismiss([msg]() { brls::Application::notify(msg); });
            });
        }
    });
}

void EBookView::render(brls::Image* view, int n) {
    if (n < 0 || n >= this->count) {
        view->clear();
        return;
    }
    fz_pixmap* pix = nullptr;
    float scale = 1.5f * this->zoom;
    // Clamp the render scale so the texture never exceeds the GPU limits
    if (this->pageWidth > 0 && this->pageHeight > 0) {
        float maxDim = std::max(this->pageWidth, this->pageHeight);
        if (maxDim > 0) scale = std::min(scale, 4000.0f / maxDim);
    }
    fz_matrix ctm = fz_scale(scale, scale);
    fz_try(ctx) pix = fz_new_pixmap_from_page_number(ctx, doc, n, ctm, fz_device_rgb(ctx), 1);
    fz_catch(ctx) return;
    auto vg = brls::Application::getNVGContext();
    int tex = nvgCreateImageRGBA(vg, pix->w, pix->h, 0, pix->samples);
    fz_drop_pixmap(ctx, pix);
    view->innerSetImage(tex);
    view->setBackgroundColor(nvgRGB(245, 246, 247));
}

void EBookView::renderCurrent() {
    if (this->count <= 0) return;
    if (this->singlePage) {
        this->render(this->left, this->page);
    } else {
        this->render(this->left, this->page);
        this->render(this->right, this->page + 1);
    }
    this->updateLayout();
}

void EBookView::loadOutline() {
    this->outline.clear();
    this->outlineLoaded = false;
    if (!this->doc) return;

    fz_outline* root = nullptr;
    fz_try(ctx) root = fz_load_outline(ctx, this->doc);
    fz_catch(ctx) return;
    if (!root) return;

    // Walk the (possibly nested) outline tree and flatten it into a flat list.
    std::function<void(fz_outline*, int)> walk = [&](fz_outline* node, int depth) {
        for (fz_outline* cur = node; cur; cur = cur->next) {
            Bookmark bm;
            if (cur->title)
                bm.title = std::string(cur->title);
            else
                bm.title = cur->uri ? std::string(cur->uri) : "";
            // Prefer the resolved page number, fall back to the page location.
            int p = -1;
            if (cur->page.chapter >= 0 && cur->page.page >= 0)
                p = fz_page_number_from_location(ctx, this->doc, cur->page);
            if (p < 0 && cur->uri) {
                float xp, yp;
                fz_location loc = fz_resolve_link(ctx, this->doc, cur->uri, &xp, &yp);
                if (loc.chapter >= 0 && loc.page >= 0) p = fz_page_number_from_location(ctx, this->doc, loc);
            }
            if (p < 0 || p >= this->count) p = 0;
            bm.page = p;
            // Prefix indentation based on outline depth for readability.
            if (depth > 0) bm.title = std::string(depth * 2, ' ') + bm.title;
            this->outline.push_back(bm);
            if (cur->down) walk(cur->down, depth + 1);
        }
    };
    walk(root, 0);

    fz_drop_outline(ctx, root);
    this->outlineLoaded = true;
}

void EBookView::showOutline() {
    if (!this->outlineLoaded) {
        // Outline may not have been loaded yet (e.g. still opening); try now.
        this->loadOutline();
    }
    if (this->outline.empty()) {
        brls::Application::notify("main/ebook/no_outline"_i18n);
        return;
    }

    std::vector<std::string> titles;
    titles.reserve(this->outline.size());
    int selected = 0;
    for (size_t i = 0; i < this->outline.size(); i++) {
        titles.push_back(this->outline[i].title);
        if (this->outline[i].page <= this->page) selected = (int)i;
    }

    brls::Dropdown* dropdown = new brls::Dropdown(
        "main/ebook/outline"_i18n, titles,
        [this](int index) {
            if (index < 0 || index >= (int)this->outline.size()) return;
            this->page = this->outline[index].page;
            this->panX = 0;
            this->panY = 0;
            this->renderCurrent();
        },
        selected);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void EBookView::zoomIn() {
    size_t idx = 0;
    for (size_t i = 0; i < ZOOM_LEVELS_COUNT; i++) {
        if (ZOOM_LEVELS[i] <= this->zoom + 1e-4f) idx = i;
    }
    if (idx + 1 >= ZOOM_LEVELS_COUNT) return;
    this->zoom = ZOOM_LEVELS[idx + 1];
    this->applyZoom();
}

void EBookView::zoomOut() {
    size_t idx = ZOOM_LEVELS_COUNT - 1;
    for (size_t i = ZOOM_LEVELS_COUNT; i-- > 0;) {
        if (ZOOM_LEVELS[i] >= this->zoom - 1e-4f) idx = i;
    }
    if (idx == 0) return;
    this->zoom = ZOOM_LEVELS[idx - 1];
    this->applyZoom();
}

void EBookView::applyZoom() {
    this->panX = 0;
    this->panY = 0;
    this->renderCurrent();
    brls::Application::notify(fmt::format("{} {}%", "main/ebook/zoom"_i18n, (int)(this->zoom * 100)));
}

void EBookView::togglePageMode() {
    this->singlePage = !this->singlePage;
    this->panX = 0;
    this->panY = 0;
    this->updateActionHint(
        brls::BUTTON_LSB, this->singlePage ? "main/ebook/double_page"_i18n : "main/ebook/single_page"_i18n);
    brls::Application::getGlobalHintsUpdateEvent()->fire();
    this->renderCurrent();
}

void EBookView::onLayout() {
    Box::onLayout();
    this->updateLayout();
}

void EBookView::computeLayout(float& dispW, float& dispH, float& groupW, float& groupH) {
    float availW = this->getWidth() - this->getPaddingLeft() - this->getPaddingRight();
    float availH = this->getHeight() - this->getPaddingTop() - this->getPaddingBottom();
    float aspect = this->pageWidth / this->pageHeight;
    const float gap = this->left->getMarginRight();

    if (this->singlePage) {
        dispW = availW;
        dispH = availW / aspect;
        if (dispH > availH) {
            dispH = availH;
            dispW = availH * aspect;
        }
    } else {
        float slotW = (availW - gap) / 2.0f;
        dispW = slotW;
        dispH = slotW / aspect;
        if (dispH > availH) {
            dispH = availH;
            dispW = availH * aspect;
        }
    }

    dispW *= this->zoom;
    dispH *= this->zoom;
    groupW = this->singlePage ? dispW : dispW * 2.0f + gap;
    groupH = dispH;
}

void EBookView::clampPan() {
    if (this->pageHeight <= 0) {
        this->panX = 0;
        this->panY = 0;
        return;
    }
    float availW = this->getWidth() - this->getPaddingLeft() - this->getPaddingRight();
    float availH = this->getHeight() - this->getPaddingTop() - this->getPaddingBottom();
    if (availW <= 0 || availH <= 0) {
        this->panX = 0;
        this->panY = 0;
        return;
    }
    float dispW, dispH, groupW, groupH;
    this->computeLayout(dispW, dispH, groupW, groupH);

    float maxPanX = std::max(0.0f, (groupW - availW) / 2.0f);
    float maxPanY = std::max(0.0f, (groupH - availH) / 2.0f);
    this->panX = std::clamp(this->panX, -maxPanX, maxPanX);
    this->panY = std::clamp(this->panY, -maxPanY, maxPanY);
}

void EBookView::updateLayout() {
    // Guard against re-entrancy: setWidth/setVisibility trigger a synchronous
    // re-layout (invalidate -> YGNodeCalculateLayout) that calls back into
    // onLayout -> updateLayout. Running a nested layout from inside onLayout
    // corrupts yoga and crashes, so skip the nested call.
    if (this->layouting) return;
    if (this->count <= 0 || this->pageWidth <= 0 || this->pageHeight <= 0) return;

    float availW = this->getWidth() - this->getPaddingLeft() - this->getPaddingRight();
    float availH = this->getHeight() - this->getPaddingTop() - this->getPaddingBottom();
    if (availW <= 0 || availH <= 0) return;

    this->layouting = true;

    float dispW, dispH, groupW, groupH;
    this->computeLayout(dispW, dispH, groupW, groupH);
    this->clampPan();

    if (this->singlePage)
        this->right->setVisibility(brls::Visibility::GONE);
    else
        this->right->setVisibility(brls::Visibility::VISIBLE);

    // Only apply when the values actually changed, to avoid re-layout loops
    if (std::abs(this->lastW - dispW) > 1e-3f || std::abs(this->lastH - dispH) > 1e-3f) {
        this->lastW = dispW;
        this->lastH = dispH;
        this->left->setWidth(dispW);
        this->left->setHeight(dispH);
        this->right->setWidth(dispW);
        this->right->setHeight(dispH);
    }
    if (std::abs(this->lastPanX - this->panX) > 1e-3f || std::abs(this->lastPanY - this->panY) > 1e-3f) {
        this->lastPanX = this->panX;
        this->lastPanY = this->panY;
        this->left->setTranslationX(this->panX);
        this->left->setTranslationY(this->panY);
        this->right->setTranslationX(this->panX);
        this->right->setTranslationY(this->panY);
    }

    this->layouting = false;
}

#endif