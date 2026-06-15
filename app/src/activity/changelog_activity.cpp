/*
    Copyright 2026 thcolin
*/

#include "activity/changelog_activity.hpp"
#include "utils/misc.hpp"

#include <fstream>
#include <sstream>
#ifdef USE_LIBROMFS
#include <romfs/romfs.hpp>
#endif

using namespace brls::literals;  // for _i18n

void Changelog::onContentAvailable() {
    // CHANGELOG.md is embedded under resources at configure time (CMakeLists):
    // read it via romfs when bundled, else from the resources dir on desktop.
    std::string md;
#ifdef USE_LIBROMFS
    auto& res = romfs::get("CHANGELOG.md");
    md.assign(reinterpret_cast<const char*>(res.data()), res.size());
#else
    std::ifstream f(std::string(BRLS_RESOURCES) + "CHANGELOG.md");
    std::stringstream ss;
    ss << f.rdbuf();
    md = ss.str();
#endif

    if (md.empty())
        this->label->setText("main/changelog/empty"_i18n);
    else
        this->label->setText(misc::markdownToText(md));
}
