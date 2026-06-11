#include "view/people_source.hpp"
#include "view/video_card.hpp"
#include "view/auto_tab_frame.hpp"
#include "tab/media_person.hpp"
#include "utils/image.hpp"

PeopleDataSource::PeopleDataSource(const MediaList& r) : list(std::move(r)) {}

size_t PeopleDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* PeopleDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    MediaCardCell* cell = dynamic_cast<MediaCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);

    cell->labelTitle->setText(item.tag);
    cell->labelExt->setText(item.role);

    // recycled cell: purge the previous occupant's portrait
    cell->picture->clear();
    if (!item.thumb.empty()) {
        // depending on the metadata agent, a Role thumb is an absolute URL
        // (provider.plex.tv) or a server-relative path
        if (item.thumb.rfind("http", 0) == 0) {
            Image::with(cell->picture, item.thumb);
        } else {
            Image::load(cell->picture, item.thumb, 300);
        }
    }
    return cell;
}

void PeopleDataSource::onItemSelected(brls::Box* recycler, size_t index) {
    auto& role = this->list.at(index);
    if (role.id.empty()) {
        brls::Application::notify(role.tag);
        return;
    }
    ui::presentDetail(recycler, new MediaPerson(role));
}

void PeopleDataSource::clearData() { this->list.clear(); }
