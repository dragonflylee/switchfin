"""Pixel admission arithmetic and reservations surviving delayed UI delivery."""
import unittest
from source_fixture import compile_run


class PixelTests(unittest.TestCase):
    def test_dimensions_retained_budget_and_cross_thread_release(self):
        compile_run(r'''
#include <utils/ps5_native_artwork_pixels.hpp>
#include <cassert>
#include <climits>
#include <memory>
#include <thread>
#include <vector>
using namespace ps5::artwork;
int main() {
    assert(admittedPixelBytes(1920,1080,false)==8294400);
    assert(admittedPixelBytes(1080,1920,false)==8294400);
    assert(admittedPixelBytes(350,525,true)==735000);
    for (bool sixteen : {false,true}) {
        assert(!admittedPixelBytes(3840,2160,sixteen));
        assert(!admittedPixelBytes(INT_MAX,INT_MAX,sixteen));
        assert(!admittedPixelBytes(0,1,sixteen) && !admittedPixelBytes(1,-1,sixteen));
    }
    assert(!admittedPixelBytes(1920,1080,true));
    EncodedBudget budget(pixelResidentLimit);
    const auto size=admittedPixelBytes(1920,1080,false);
    auto first=std::make_shared<PixelReservation>(budget,size);
    auto second=std::make_shared<PixelReservation>(budget,size);
    auto rejected=std::make_shared<PixelReservation>(budget,size);
    assert(*first && *second && !*rejected && budget.bytes()==size*2);
    auto queued=first;first.reset();assert(budget.bytes()==size*2);
    std::thread retire([queued=std::move(queued)]() mutable { queued.reset(); });retire.join();
    assert(budget.bytes()==size);
    second.reset();rejected.reset();assert(budget.bytes()==0);
    std::vector<std::thread> workers;
    for(int i=0;i<8;++i) workers.emplace_back([&] {
        for(int pass=0;pass<1000;++pass) { PixelReservation lease(budget,size); assert(budget.bytes()<=pixelResidentLimit); }
    });
    for(auto& worker:workers) worker.join();
    assert(budget.bytes()==0);
}
''')


if __name__ == "__main__":
    unittest.main()
