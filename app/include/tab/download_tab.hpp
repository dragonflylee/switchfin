#pragma once

#include <view/auto_tab_frame.hpp>
#include "utils/download.hpp"

class RecyclingGrid;
class SegmentedBar;

/// Onglet Téléchargements (xml/tabs/downloads.xml) : en-tête « Stockage »
/// (barre disque segmentée + légende) au-dessus d'une liste sectionnée
/// « En cours » / « Téléchargés » (flow mode, hauteurs par ligne).
class DownloadView : public AttachedView {
public:
    DownloadView();
    ~DownloadView() override;

    brls::View* getDefaultFocus() override;
    void dismiss(std::function<void(void)> cb = [] {}) override;
    /// Le tab est mis en cache par AutoSidebarItem : rafraîchit liste et
    /// stockage à chaque retour (un ajout en file n'émet pas de StatusEvent)
    void willAppear(bool resetState = false) override;

private:
    void loadItems();
    /// Recalcule la barre et les chiffres de l'en-tête : fs::space sur le
    /// dossier des téléchargements + octets occupés par pleNx
    void updateStorage();

    BRLS_BIND(RecyclingGrid, recycler, "downloads/list");
    BRLS_BIND(brls::Box, storageBarBox, "downloads/storage/bar");
    BRLS_BIND(brls::Label, storageApp, "downloads/storage/app");
    BRLS_BIND(brls::Label, storageFree, "downloads/storage/free");

    SegmentedBar* storageBar = nullptr;

    DownloadManager::StatusEvent::Subscription statusSubId;
    DownloadManager::ProgressEvent::Subscription progressSubId;
};
