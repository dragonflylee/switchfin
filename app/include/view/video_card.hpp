#pragma once

#include <view/recycling_grid.hpp>
#include <view/long_press_gesture.hpp>
#include <utils/image.hpp>

class SVGImage;

class BaseCardCell : public RecyclingGridItem {
public:
    BaseCardCell() {
        // appui long (souris/touch) = ouvrir le menu contextuel : rejoue
        // l'action BUTTON_X de la cellule (la même que F4 / KeyBind Setting),
        // après avoir focus la cellule. Point unique : couvre home, grilles,
        // recherche, épisodes de série et fiche personne.
        this->addGestureRecognizer(new LongPressGestureRecognizer(this));
    }

    ~BaseCardCell() { Image::cancel(this->picture); }

    void prepareForReuse() override {
        this->picture->setImageFromRes("img/video-card-bg.png");
        // cellule recyclée : jamais de ticker hérité d'un bind précédent
        this->setLabelsTicker(false);
    }

    void cacheForReuse() override { Image::cancel(this->picture); }

    /// ---- ticker des titres/sous-titres piloté par le focus de la carte ----
    /// Le focus vit sur pic_box (getDefaultFocus), pas sur la racine de la
    /// cellule : on écoute donc la remontée onChildFocus* plutôt que
    /// onFocusGained/Lost. setAnimated(false) restaure l'ellipse (onLayout).
    void onChildFocusGained(brls::View* directChild, brls::View* focusedView) override {
        this->setLabelsTicker(true);
        RecyclingGridItem::onChildFocusGained(directChild, focusedView);
    }

    void onChildFocusLost(brls::View* directChild, brls::View* focusedView) override {
        this->setLabelsTicker(false);
        RecyclingGridItem::onChildFocusLost(directChild, focusedView);
    }

    /// Box::onParentFocus* diffuse l'événement à TOUTE la descendance : un
    /// ancêtre focusé (onglet, grille) allumait le ticker de toutes les
    /// cartes via Label::autoAnimate — d'où des labels qui défilaient en
    /// permanence. Avalé ici : le défilement ne dépend plus que du focus de
    /// la carte elle-même (hooks onChildFocus* ci-dessus).
    void onParentFocusGained(brls::View* focusedView) override { (void)focusedView; }

    void onParentFocusLost(brls::View* focusedView) override { (void)focusedView; }

    /// le halo de focus n'entoure que l'affiche, pas le bloc de titres.
    /// Contrat borealis : getDefaultFocus peut renvoyer this/nullptr mais ne
    /// doit JAMAIS throw — résolution par id (nullptr si absent du layout de
    /// la sous-classe) au lieu d'un binding BRLS_BIND qui throw
    /// (ViewNotFoundException → SIGABRT, cf. crashs 084700/090535).
    brls::View* getDefaultFocus() override {
        brls::View* pic = this->getView("video/card/pic_box");
        if (pic && pic->isFocusable()) return pic;
        return RecyclingGridItem::getDefaultFocus();
    }

    BRLS_BIND(brls::Image, picture, "video/card/picture");
    BRLS_BIND(brls::Label, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelExt, "video/card/label/ext");

private:
    /// (dés)active le défilement des labels singleLine de la carte.
    /// Résolution par id avec repli silencieux (même contrat anti-throw que
    /// getDefaultFocus) : les sous-classes n'ont pas toutes les mêmes labels
    /// (episode_card.xml n'a pas video/card/label/*, et inversement).
    void setLabelsTicker(bool on) {
        static const char* ids[] = {"video/card/label/title", "video/card/label/ext", "episode/card/name"};
        for (auto* id : ids) {
            if (auto* label = dynamic_cast<brls::Label*>(this->getView(id))) label->setAnimated(on);
        }
    }
};

class MediaCardCell : public BaseCardCell {
public:
    MediaCardCell() { this->inflateFromXMLRes("xml/view/video_card.xml"); }

    static MediaCardCell* create() { return new MediaCardCell(); }
};

class VideoCardCell : public BaseCardCell {
public:
    VideoCardCell();

    static VideoCardCell* create() { return new VideoCardCell(); }

    BRLS_BIND(SVGImage, badgeTopRight, "video/card/badge/top");
    BRLS_BIND(brls::Rectangle, rectProgress, "video/card/progress");
};