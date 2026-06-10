/*
    pleNx — onglet sidebar « Listes de lecture » (voir playlists_tab.hpp).
*/

#include "tab/playlists_tab.hpp"
#include "tab/playlist_view.hpp"
#include "api/plex.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_card.hpp"
#include "view/auto_tab_frame.hpp"
#include "utils/image.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

/// Cartes carrées des playlists : affiche (thumb, sinon composite 1:1)
/// + titre + « N éléments ».
/// Pas un VideoDataSource : le menu contextuel X/long-press de VideoCardCell
/// (qui caste vers VideoDataSource) reste inerte ici, c'est voulu — aucune
/// action contextuelle ne s'applique à une playlist.
class PlaylistsDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Item>;

    explicit PlaylistsDataSource(const MediaList& r) : list(std::move(r)) {}

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setId(item.ratingKey);
        cell->labelTitle->setText(item.title);
        if (item.leafCount > 0) {
            cell->labelExt->setText(fmt::format("{} {}", item.leafCount,
                item.leafCount > 1 ? "main/playlist/items"_i18n : "main/playlist/item"_i18n));
            cell->labelExt->setVisibility(brls::Visibility::VISIBLE);
        } else {
            cell->labelExt->setVisibility(brls::Visibility::GONE);
        }
        // cellule recyclée : purger l'affiche de la playlist précédente
        // (Image::load ignore un chemin vide sans nettoyer la texture)
        cell->picture->clear();
        // affiche personnalisée (thumb) prioritaire ; repli sur la mosaïque
        // 1:1 générée par le serveur (retour recette UI n°5)
        Image::load(cell->picture, item.thumb.empty() ? item.composite : item.thumb, 325);
        // ni badge « vu » ni barre de progression sur une playlist
        cell->badgeTopRight->setVisibility(brls::Visibility::GONE);
        cell->rectProgress->getParent()->setVisibility(brls::Visibility::GONE);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        ui::presentDetail(recycler, new PlaylistView(item));
    }

    void clearData() override { this->list.clear(); }

    void appendData(const MediaList& data) { this->list.insert(this->list.end(), data.begin(), data.end()); }

private:
    MediaList list;
};

PlaylistsTab::PlaylistsTab() {
    brls::Logger::debug("PlaylistsTab: create");
    this->inflateFromXMLRes("xml/tabs/playlists.xml");

    this->recycler->registerCell("Cell", VideoCardCell::create);
    this->recycler->onNextPage([this]() { this->doRequest(); });

    auto actionRefresh = [this](...) {
        this->startIndex = 0;
        this->recycler->showSkeleton();
        this->doRequest();
        return true;
    };
    this->recycler->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->doRequest();
}

brls::View* PlaylistsTab::getDefaultFocus() { return this->recycler; }

brls::View* PlaylistsTab::create() { return new PlaylistsTab(); }

void PlaylistsTab::doRequest() {
    HTTP::Form query;
    // périmètre vidéo uniquement (la musique est hors scope, PLEX_MIGRATION D2/D4)
    query["playlistType"] = "video";
    plex::addPagination(query, this->startIndex, this->pageSize);

    ASYNC_RETAIN
    // GET /playlists?playlistType=video → Metadata[] type "playlist"
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            this->startIndex = r.StartIndex + this->pageSize;
            if (r.TotalRecordCount == 0) {
                this->recycler->setEmpty();
            } else if (r.StartIndex == 0) {
                this->recycler->setDataSource(new PlaylistsDataSource(r.Items));
            } else if (r.Items.size() > 0) {
                auto dataSrc = dynamic_cast<PlaylistsDataSource*>(this->recycler->getDataSource());
                dataSrc->appendData(r.Items);
                this->recycler->notifyDataChanged();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            if (this->startIndex > 0) {
                brls::Application::notify(ex);
            } else {
                this->recycler->setError(ex);
            }
        },
        plex::apiPlaylists, HTTP::encode_form(query));
}
