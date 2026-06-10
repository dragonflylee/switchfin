/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <view/auto_tab_frame.hpp>

struct AppRemote;

class RemoteTab : public AttachedView {
public:
    RemoteTab();
    ~RemoteTab() override;

    static brls::View* create();

    void onCreate() override;

    /// Reconstruit les pills (serveurs distants + Téléchargements + Fichiers)
    /// après ajout/édition/suppression d'un serveur.
    void refresh();

private:
    /// Menu « Gérer le serveur » (modifier / supprimer) d'un serveur distant
    void manageRemote(size_t index, const AppRemote& r);

    BRLS_BIND(AutoTabFrame, tabFrame, "remote/tabFrame");
};
