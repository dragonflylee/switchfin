/*
    pleNx — lecteur vidéo Plex.
    Pipeline : PLEX_MIGRATION.md §2.7 (direct play, transcodeur universel, timeline, scrobble).
*/

#pragma once

#include <borealis.hpp>
#include <utils/event.hpp>
#include <api/plex/types.hpp>

class VideoView;

class PlayerView : public brls::Box {
public:
    PlayerView(const plex::Item& item, const int64_t seekMs = 0);
    ~PlayerView();

    /// Charge la liste d'épisodes de la série (navigation précédent/suivant)
    void setSeries(const std::string& showRatingKey);
    void setTitie(const std::string& title);

#ifdef ANDROID
    void willDisappear(bool resetState) override {
        if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(235, 235, 235, 255));
        else
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(45, 45, 45, 255));
    }

    void willAppear(bool resetState) override {
        brls::Application::getTheme().addColor("brls/clear", nvgRGBA(0, 0, 0, 0));
    }
#endif

private:
    void setChapters(const std::vector<plex::Chapter>& chaps, int64_t durationMs);
    /// Récupère les métadonnées fraîches puis choisit direct play ou transcode
    void playMedia(const int64_t seekMs);
    void playDirect(const int64_t seekMs);
    void playTranscode(const int64_t seekMs);
    bool playIndex(int index);
    /// Rapport POST /:/timeline (time/duration en ms)
    void reportTimeline(const std::string& state, int64_t timeMs);
    void reportStop();
    /// Marque vu via /:/scrobble au-delà du seuil (90 %)
    void maybeScrobble(int64_t timeMs);
    bool toggleQuality();

    // Lecture
    std::string itemId;  // ratingKey
    /// playMethod : "directplay" | "transcode" (affichage VideoProfile)
    std::string playMethod;
    /// X-Plex-Session-Identifier : stable pour toute la session de lecture
    std::string sessionId;
    /// session du transcodeur : régénérée à chaque (re)démarrage
    std::string transcodeSession;
    plex::Item item;     // métadonnées fraîches (media/chapters/markers)
    plex::Media stream;  // version sélectionnée
    bool scrobbled = false;
    std::vector<plex::Item> episodes;

    MPVEvent::Subscription eventSubscribeID;
    brls::VoidEvent::Subscription exitSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
    MPVCustomEvent::Subscription customEventSubscribeID;
    VideoView* view = nullptr;
};
