
#include "view/ebook_view.hpp"

#ifdef USE_MUPDF

#include "utils/keybind.hpp"
#include <mupdf/fitz.h>

namespace {
// MuPDF 仅在 context 具有真实 lock 回调时才允许 fz_clone_context()。
std::mutex gFzLocks[FZ_LOCK_MAX];

void fzLockCB(void* user, int lock) {
    if (lock >= 0 && lock < FZ_LOCK_MAX) gFzLocks[lock].lock();
}
void fzUnlockCB(void* user, int lock) {
    if (lock >= 0 && lock < FZ_LOCK_MAX) gFzLocks[lock].unlock();
}

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
}  // namespace

EBookView::EBookView() {
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setHideHighlightBackground(true);
    this->left = new brls::Image();
    this->left->setFreeTexture(true);  // 逐页持有并释放纹理，避免翻页泄漏
    this->left->setMarginRight(10);
    this->right = new brls::Image();
    this->right->setFreeTexture(true);
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
    if (!this->ctx) brls::Logger::error("EBookView: failed to create MuPDF context");
    else fz_register_document_handlers(this->ctx);

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
        this->goToPage(this->page - this->pageStep());
        return true;
    });

    this->registerAction("main/player/next"_i18n, brls::BUTTON_RIGHT, [this](brls::View* view) {
        if (this->page + this->pageStep() >= this->count) return false;
        this->goToPage(this->page + this->pageStep());
        return true;
    });

    this->addGestureRecognizer(
        new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* soundToPlay) {
            if (status.state != brls::GestureState::END) return;
            auto frame = this->getFrame();
            const int step = this->pageStep();
            if (status.position.x < frame.getMidX()) {
                if (this->page <= 0) return;
                this->goToPage(this->page - step);
            } else {
                if (this->page + step >= this->count) return;
                this->goToPage(this->page + step);
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

EBookView::LoadResult EBookView::loadInBackground(const std::string& url, const HTTP::Header& headers) {
    LoadResult result;
    // 后台仅做网络 IO，不触碰共享的 fz_context
    try {
        if (url.find("://") == std::string::npos) {
            result.local = true;
        } else {
            result.data = HTTP::get(url, HTTP::Timeout{}, headers);
        }
        result.ok = true;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        result.ok = false;
    }
    return result;
}

void EBookView::open(const std::string& url, float percent, const HTTP::Header& headers) {
    this->loading->setVisibility(brls::Visibility::VISIBLE);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url, headers, percent]() {
        LoadResult result = EBookView::loadInBackground(url, headers);
        brls::sync([ASYNC_TOKEN, url, result = std::move(result), percent]() mutable {
            ASYNC_RELEASE
            this->loading->setVisibility(brls::Visibility::GONE);
            this->applyLoadResult(url, std::move(result), percent);
        });
    });
}

void EBookView::applyLoadResult(
    const std::string& url, LoadResult&& result, float percent) {
    // 关闭上一次打开的文档（若 open 被重复调用）
    if (this->doc) {
        fz_drop_document(this->ctx, this->doc);
        this->doc = nullptr;
    }
    this->count = 0;
    this->pageWidth = 0;
    this->pageHeight = 0;
    this->outline.clear();
    this->outlineLoaded = false;

    if (!result.ok) {
        std::string msg = result.error.empty() ? "main/player/error"_i18n : result.error;
        brls::Logger::warning("EBookView open failed: {}", msg);
        this->dismiss([msg]() { brls::Application::notify(msg); });
        return;
    }
    if (!this->ctx) {
        brls::Logger::warning("EBookView context missing");
        this->dismiss([]() { brls::Application::notify("main/player/error"_i18n); });
        return;
    }

    fz_stream* stream = nullptr;
    fz_buffer* buf = nullptr;
    try {
        if (result.local) {
            stream = fz_open_file(this->ctx, url.c_str());
        } else {
            buf = fz_new_buffer_from_copied_data(
                this->ctx, (const uint8_t*)result.data.data(), result.data.size());
            stream = fz_open_buffer(this->ctx, buf);
        }
        fz_try(this->ctx) this->doc = fz_open_document_with_stream(this->ctx, url.c_str(), stream);
        fz_catch(this->ctx) throw fz_error(this->ctx);

        fz_try(this->ctx) this->count = fz_count_pages(this->ctx, this->doc);
        fz_catch(this->ctx) throw fz_error(this->ctx);

        fz_try(this->ctx) {
            fz_page* p = fz_load_page(this->ctx, this->doc, 0);
            fz_rect bounds = fz_bound_page(this->ctx, p);
            this->pageWidth = bounds.x1 - bounds.x0;
            this->pageHeight = bounds.y1 - bounds.y0;
            fz_drop_page(this->ctx, p);
        }
        fz_catch(this->ctx) {}

        this->loadOutline();
    } catch (const std::exception& ex) {
        std::string msg = ex.what();
        if (buf) fz_drop_buffer(this->ctx, buf);
        if (stream) fz_drop_stream(this->ctx, stream);
        if (this->doc) {
            fz_drop_document(this->ctx, this->doc);
            this->doc = nullptr;
        }
        this->count = 0;
        brls::Logger::warning("EBookView load exception: {}", msg);
        this->dismiss([msg]() { brls::Application::notify(msg); });
        return;
    }
    if (buf) fz_drop_buffer(this->ctx, buf);
    if (stream) fz_drop_stream(this->ctx, stream);

    // percent 为页码，夹取到有效范围
    this->page = (int)std::floor(percent);
    if (this->count > 0) this->page = std::clamp(this->page, 0, this->count - 1);
    else this->page = 0;
    this->panX = 0;
    this->panY = 0;

    this->renderCurrent();
}

void EBookView::render(brls::Image* view, int n) {
    if (!view) return;
    if (n < 0 || n >= this->count || !this->ctx || !this->doc) {
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
    fz_try(this->ctx) pix = fz_new_pixmap_from_page_number(this->ctx, this->doc, n, ctm, fz_device_rgb(this->ctx), 1);
    fz_catch(this->ctx) return;
    auto* vg = brls::Application::getNVGContext();
    int tex = nvgCreateImageRGBA(vg, pix->w, pix->h, 0, pix->samples);
    fz_drop_pixmap(this->ctx, pix);
    if (tex <= 0) return;
    // innerSetImage 会在 freeTexture=true 时释放旧纹理
    view->innerSetImage(tex);
    view->setBackgroundColor(nvgRGB(245, 246, 247));
}

void EBookView::renderCurrent() {
    if (this->count <= 0) return;
    if (!this->singlePage) this->page &= ~1;  // 双页模式对齐到左页
    this->page = std::clamp(this->page, 0, this->count - 1);
    this->render(this->left, this->page);
    if (this->singlePage)
        this->right->clear();
    else
        this->render(this->right, this->page + 1);
    this->updateLayout();
}

int EBookView::pageStep() const { return this->singlePage ? 1 : 2; }

void EBookView::goToPage(int target) {
    const int last = std::max(0, this->count - 1);
    int p = std::clamp(target, 0, last);
    if (!this->singlePage) p &= ~1;  // 双页对齐
    this->page = p;
    this->panX = 0;
    this->panY = 0;
    this->renderCurrent();
}

void EBookView::loadOutline() {
    this->outline.clear();
    this->outlineLoaded = false;
    if (!this->doc || !this->ctx) return;

    fz_outline* root = nullptr;
    fz_try(this->ctx) root = fz_load_outline(this->ctx, this->doc);
    fz_catch(this->ctx) return;
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
                p = fz_page_number_from_location(this->ctx, this->doc, cur->page);
            if (p < 0 && cur->uri) {
                float xp, yp;
                fz_location loc = fz_resolve_link(this->ctx, this->doc, cur->uri, &xp, &yp);
                if (loc.chapter >= 0 && loc.page >= 0)
                    p = fz_page_number_from_location(this->ctx, this->doc, loc);
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

    fz_drop_outline(this->ctx, root);
    this->outlineLoaded = true;
}

void EBookView::showOutline() {
    if (!this->outlineLoaded) {
        // Outline may not have been loaded yet (e.g. still opening); try now.
        this->loadOutline();
    }
    if (this->outline.empty()) {
        brls::Logger::info("EBookView outline empty: {}", "main/ebook/no_outline"_i18n);
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
            this->goToPage(this->outline[index].page);
        },
        selected);
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void EBookView::zoomIn() {
    auto it = std::upper_bound(
        std::begin(ZOOM_LEVELS), std::end(ZOOM_LEVELS), this->zoom + 1e-4f);
    if (it == std::end(ZOOM_LEVELS)) return;
    this->zoom = *it;
    this->applyZoom();
}

void EBookView::zoomOut() {
    auto it = std::lower_bound(
        std::begin(ZOOM_LEVELS), std::end(ZOOM_LEVELS), this->zoom - 1e-4f);
    if (it == std::begin(ZOOM_LEVELS)) return;
    this->zoom = *(it - 1);
    this->applyZoom();
}

void EBookView::applyZoom() {
    this->panX = 0;
    this->panY = 0;
    this->renderCurrent();
    brls::Logger::info("EBookView zoom: {}%", (int)(this->zoom * 100));
    brls::Application::notify(fmt::format("{} {}%", "main/ebook/zoom"_i18n, (int)(this->zoom * 100)));
}

void EBookView::togglePageMode() {
    this->singlePage = !this->singlePage;
    this->panX = 0;
    this->panY = 0;
    if (!this->singlePage) this->page &= ~1;  // 切到双页时对齐跨页起始
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
    const float gap = this->left ? this->left->getMarginRight() : 0.f;
    // 防止 pageHeight 为 0 时除零
    float aspect = (this->pageWidth > 0 && this->pageHeight > 0) ? this->pageWidth / this->pageHeight : 1.0f;

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