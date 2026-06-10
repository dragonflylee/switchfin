/*
    pleNx — fiche personne : portrait + filmographie (films / séries).
    Données : GET /library/people/{id}/media (plex_client.dart:2509-2511).
*/

#pragma once

#include <borealis.hpp>
#include <api/plex/types.hpp>

class RecylingVideo;

class MediaPerson : public brls::Box {
public:
    MediaPerson(const plex::Role& role);
    ~MediaPerson() override;

private:
    void doMedia();

    BRLS_BIND(brls::Image, imagePhoto, "people/image/photo");
    BRLS_BIND(brls::Label, labelName, "people/label/name");
    BRLS_BIND(brls::Label, labelRole, "people/label/role");
    BRLS_BIND(brls::Box, pillMovies, "people/pill/movies");
    BRLS_BIND(brls::Label, labelMovies, "people/label/movies");
    BRLS_BIND(brls::Box, pillShows, "people/pill/shows");
    BRLS_BIND(brls::Label, labelShows, "people/label/shows");
    BRLS_BIND(RecylingVideo, rowMovies, "people/movie");
    BRLS_BIND(RecylingVideo, rowSeries, "people/series");

    std::string personId;
};
