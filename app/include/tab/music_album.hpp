#pragma once

#include <borealis.hpp>

class MusicAlbum : public brls::Box {
public:
    MusicAlbum(const std::string& itemId);
    ~MusicAlbum() override;
};