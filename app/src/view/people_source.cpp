#include "view/people_source.hpp"
#include "view/video_card.hpp"
#include "utils/image.hpp"

PeopleDataSource::PeopleDataSource(const MediaList& r) : list(std::move(r)) {}

size_t PeopleDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* PeopleDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    MediaCardCell* cell = dynamic_cast<MediaCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);

    cell->labelTitle->setText(item.tag);
    cell->labelExt->setText(item.role);

    if (!item.thumb.empty()) {
        // selon l'agent metadata, le thumb d'un Role est une URL absolue
        // (provider.plex.tv) ou un chemin relatif au serveur
        if (item.thumb.rfind("http", 0) == 0) {
            Image::with(cell->picture, item.thumb);
        } else {
            Image::load(cell->picture, item.thumb, 300);
        }
    }
    return cell;
}

void PeopleDataSource::onItemSelected(brls::Box* recycler, size_t index) {
    // v1 sans fiche personne : pas d'équivalent /library/search par personne
    // (PLEX_MIGRATION.md §2.5 « à dégrader »)
    brls::Application::notify(this->list.at(index).tag);
}

void PeopleDataSource::clearData() { this->list.clear(); }
