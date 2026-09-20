"""Parse unknown codec levels without changing playback admission or numeric bounds."""
import unittest
from source_fixture import compile_run


class MediaMetadata(unittest.TestCase):
    def test_unknown_level_and_metadata_bounds(self):
        compile_run(r'''
#include <utils/ps5_playback_admission.hpp>
#include <cassert>
#include <limits>
using nlohmann::json;
int main() {
    // VP9 streams can carry FFmpeg's unknown codec level through Jellyfin.
    const auto video = json::parse(R"({"Codec":"vp9","Type":"Video","Index":0,
        "Width":1920,"Height":1080,"BitDepth":8,"AverageFrameRate":30,
        "RealFrameRate":30,"Profile":"Profile 0","Level":-99,
        "VideoRangeType":"SDR","ColorTransfer":"bt709",
        "ColorPrimaries":"bt709","ColorSpace":"bt709"})");
    jellyfin::Source source;
    for (const auto& level : {json(-99), json(-99.0), json(nullptr), json(0)}) {
        auto input = video; input["Level"] = level;
        const auto stream = input.get<jellyfin::Stream>();
        assert(!stream.Level && stream.Width == 1920 && stream.Height == 1080);
        const json roundTrip = stream;
        assert(!roundTrip.contains("Level"));
        source.MediaStreams = {stream};
        assert(ps5::playback::admitOriginal(source, ps5::playback::Backend::Hdr10) ==
               ps5::playback::Admission::Convert);
    }
    auto missing = video; missing.erase("Level");
    assert(!missing.get<jellyfin::Stream>().Level);
    for (const auto& level : {json(31), json(41), json(120), json(153), json(5.1)}) {
        auto input = video; input["Level"] = level;
        const auto stream = input.get<jellyfin::Stream>();
        assert(stream.Level && *stream.Level == level.get<double>());
        assert(json(stream).at("Level") == level);
    }
    for (const char* key : {"Width", "Height", "BitDepth", "Channels", "RefFrames",
                            "AverageFrameRate", "RealFrameRate", "Level"}) {
        for (const auto& invalid : {json(-1), json(-98), json(true), json("-99"),
                json(uint64_t{1} << 40), json(std::numeric_limits<double>::infinity()),
                json(std::numeric_limits<double>::quiet_NaN())}) {
            auto input = video; input[key] = invalid;
            bool rejected = false;
            try { (void)input.get<jellyfin::Stream>(); }
            catch (const std::exception&) { rejected = true; }
            assert(rejected);
        }
        if (std::string(key) != "Level") {
            auto input = video; input[key] = -99;
            bool rejected = false;
            try { (void)input.get<jellyfin::Stream>(); }
            catch (const std::exception&) { rejected = true; }
            assert(rejected);
        }
    }
    for (const char* key : {"Width", "Height", "BitDepth", "Channels", "RefFrames"}) {
        auto input = video; input[key] = 1.5;
        bool rejected = false;
        try { (void)input.get<jellyfin::Stream>(); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
    }
    for (const auto& invalid : {json(-99), json(1.5), json(true), json("0")}) {
        auto input = video; input["Index"] = invalid;
        bool rejected = false;
        try { (void)input.get<jellyfin::Stream>(); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
    }
}
''')


if __name__ == '__main__':
    unittest.main()
