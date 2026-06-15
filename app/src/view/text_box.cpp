#include <borealis/core/application.hpp>
#include <yoga/YGNode.h>
#include "view/text_box.hpp"

static YGSize textBoxMeasureFunc(
    YGNodeRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
    auto* textBox = (TextBox*)YGNodeGetContext(node);
    auto fullText = textBox->getFullText();

    YGSize size = {.width = width, .height = height};
    if (heightMode == YGMeasureMode::YGMeasureModeExactly) return size;
    // never output NaN from a measure: in free measure yoga passes
    // height=NaN, and returning it as-is degenerates the whole parent
    // layout (empty overview -> full-screen season header)
    if (fullText.empty()) {
        size.height = 0;
        return size;
    }
    if (std::isnan(width)) {
        size.height = textBox->getFontSize();
        return size;
    }

    size.height = textBox->cutText(width);
    textBox->setParsedDone(true);
    return size;
}

TextBox::TextBox() {
    this->setAutoAnimate(false);

    this->registerFloatXMLAttribute("maxRows", [this](float value) { this->maxRows = (size_t)value; });

    YGNodeSetMeasureFunc(this->ygNode, textBoxMeasureFunc);
}

TextBox::~TextBox() = default;

brls::View* TextBox::create() { return new TextBox(); }

void TextBox::onLayout() {
    float width = this->getWidth();
    if (std::isnan(width) || width == 0) return;
    if (this->fullText.empty()) return;
    if (!this->parsedDone) this->cutText(width);
}

void TextBox::draw(
    NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    if (width == 0) return;

    // `cuttedText` is produced by cutText() as a side effect of the yoga
    // measure passes, which run with intermediate widths; after a
    // GONE->VISIBLE relayout (e.g. returning from a person page to the movie
    // details) the last measure could leave it cut for the wrong width — the
    // synopsis then collapsed to a single truncated line. Recut for the
    // ACTUAL render width so the drawn text always matches the laid-out box
    // (memoized: only re-breaks when the width changes).
    if (width != this->cuttedWidth) this->cutText(width);

    enum NVGalign horizAlign = this->getNVGHorizontalAlign();
    enum NVGalign vertAlign = this->getNVGVerticalAlign();

    nvgFontSize(vg, this->fontSize);
    nvgTextAlign(vg, horizAlign | vertAlign);
    nvgFontFaceId(vg, this->font);
    nvgFontQuality(vg, this->fontQuality);
    nvgTextLineHeight(vg, this->lineHeight);
    nvgFillColor(vg, a(this->textColor));

    nvgTextAlign(vg, horizAlign | NVG_ALIGN_TOP);
    nvgTextBox(vg, x, y, width, this->cuttedText.c_str(), nullptr);
}

void TextBox::setText(const std::string& text) {
    this->fullText = text;
    this->setParsedDone(false);
    this->cuttedWidth = -1;  // force a recut even if the width is unchanged
    this->invalidate();
}

float TextBox::cutText(float width) {
    NVGcontext* vg = brls::Application::getNVGContext();
    float lineh = 0;
    // Setup nvg state for the measurements
    nvgFontSize(vg, this->fontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontFaceId(vg, this->font);
    nvgTextLineHeight(vg, this->lineHeight);
    nvgTextMetrics(vg, nullptr, nullptr, &lineh);

    float requiredHeight = this->fontSize;

    std::vector<NVGtextRow> rows(this->maxRows);
    const char* stringStart = this->fullText.c_str();
    int nrows = nvgTextBreakLines(vg, stringStart, nullptr, width, rows.data(), rows.size());
    if (nrows > 0) {
        this->cuttedText = this->fullText.substr(0, rows[nrows - 1].end - rows[0].start);
        requiredHeight += nrows * this->lineHeight * lineh;
    }
    this->cuttedWidth = width;  // memoize: draw() recuts only when width changes
    return requiredHeight;
}