#include "view/icon_button.hpp"
#include "view/svg_image.hpp"

const std::string iconButtonXML = R"xml(
    <brls:Box
        width="auto"
        height="44"
        axis="row"
        focusable="true"
        cornerRadius="22"
        highlightCornerRadius="22"
        alignItems="center"
        justifyContent="center"
        paddingLeft="22"
        paddingRight="24">

        <SVGImage
            id="icon_button/icon"
            width="18"
            height="18"
            marginRight="10" />

        <!-- singleLine: the button's async GONE->VISIBLE toggle runs a
             measure at a degenerate width, and the wrap branch of
             labelMeasureFunc cut "Lire" into "Lir / e"; in singleLine
             that branch is unreachable (label.cpp:150) -->
        <brls:Label
            id="icon_button/label"
            singleLine="true"
            fontSize="16"
            horizontalAlign="center" />

    </brls:Box>
)xml";

IconButton::IconButton() {
    this->inflateFromXMLString(iconButtonXML);

    this->registerStringXMLAttribute("icon", [this](std::string value) { this->setIcon(value); });
    this->registerStringXMLAttribute("text", [this](std::string value) { this->setText(value); });
    this->registerStringXMLAttribute("buttonStyle", [this](std::string value) { this->setButtonStyle(value); });

    // mouse/touch click: replays the A action registered by the caller
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

    // the borealis highlight background would cover the primary style's
    // gold background on focus (unreadable dark text): animated border only
    this->setHideHighlightBackground(true);

    this->applyStyle();
}

void IconButton::setIcon(const std::string& res) {
    // XML attributes pass an @res path already resolved to a relative path
    std::string path = res;
    const std::string prefix = "@res/";
    if (path.rfind(prefix, 0) == 0) path = path.substr(prefix.size());
    this->icon->setImageFromSVGRes(path);
}

void IconButton::setText(const std::string& text) { this->label->setText(text); }

void IconButton::setButtonStyle(const std::string& style) {
    this->styleName = style;
    this->applyStyle();
}

void IconButton::applyStyle() {
    auto theme = brls::Application::getTheme();
    if (this->styleName == "primary") {
        this->setBackgroundColor(theme.getColor("color/app"));
        this->setBorderThickness(0);
        this->label->setTextColor(theme.getColor("brls/button/primary_enabled_text"));
    } else {
        this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        this->setBorderColor(theme.getColor("color/grey_3"));
        this->setBorderThickness(2);
        this->label->setTextColor(theme.getColor("brls/text"));
    }
}

brls::View* IconButton::create() { return new IconButton(); }
