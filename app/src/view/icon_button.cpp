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

        <!-- singleLine : la bascule GONE→VISIBLE asynchrone du bouton fait
             passer une mesure à largeur dégénérée, et la branche wrap de
             labelMeasureFunc coupait « Lire » en « Lir / e » ; en singleLine
             cette branche est inatteignable (label.cpp:150) -->
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

    // clic souris/tactile : rejoue l'action A enregistrée par l'appelant
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

    // le fond du highlight borealis recouvrirait le fond or du style
    // primary au focus (texte sombre illisible) : bordure animée seule
    this->setHideHighlightBackground(true);

    this->applyStyle();
}

void IconButton::setIcon(const std::string& res) {
    // les attributs XML passent un chemin @res déjà résolu en chemin relatif
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
