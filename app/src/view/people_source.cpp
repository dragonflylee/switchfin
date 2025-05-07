#include "view/people_source.hpp"
#include "view/video_card.hpp"

using namespace brls::literals;  // for _i18n

PeopleDataSource::PeopleDataSource(const MediaList& r) : list(std::move(r)) {}

size_t PeopleDataSource::getItemCount() { return this->list.size(); }

RecyclingGridItem* PeopleDataSource::cellForRow(RecyclingView* recycler, size_t index) {
    VideoCardCell* cell = dynamic_cast<VideoCardCell*>(recycler->dequeueReusableCell("Cell"));
    auto& item = this->list.at(index);

    cell->labelTitle->setText(item.Name);
    cell->labelExt->setText(item.Role);

    if (!item.PrimaryImageTag.empty()) {
        Image::load(cell->picture, jellyfin::apiPrimaryImage, item.Id,
            HTTP::encode_form({{"tag", item.PrimaryImageTag}, {"maxWidth", "350"}}));
    }
    return cell;
}

void PeopleDataSource::onItemSelected(brls::Box* recycler, size_t index) {}

void PeopleDataSource::clearData() { this->list.clear(); }
