/*
    pleNx — page complète d'un hub Plex (rangées « related » des fiches :
    suggestions, collections, « More with… »). Ouverte par la carte « + » en
    fin de rangée quand le serveur annonce more=1.
    En-tête scrollé (titre + « N éléments ») et grille dans l'ordre serveur ;
    pagination X-Plex-Container-* sur la key du hub (qui peut déjà porter des
    paramètres de requête : /library/sections/2/all?actor=…).
*/

#pragma once

#include <view/auto_tab_frame.hpp>
#include <api/plex/types.hpp>

class RecyclingGrid;

class HubView : public AttachedView {
public:
    /// @param title titre du hub (localisé par le serveur)
    /// @param key   chemin relatif du hub (Hub.key / hubKey)
    HubView(const std::string& title, const std::string& key);

    brls::View* getDefaultFocus() override;

private:
    BRLS_BIND(RecyclingGrid, recycler, "hub/items");

    /// labels de l'en-tête scrollé (xml/view/grid_header.xml, possédé par la
    /// grille via setHeaderView)
    brls::Label* labelTitle = nullptr;
    brls::Label* labelMeta = nullptr;

    void doRequest();

    std::string hubKey;
    size_t pageSize;
    size_t startIndex = 0;
};
