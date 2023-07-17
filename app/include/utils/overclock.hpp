#pragma once

#ifdef __SWITCH__

#include <switch.h>

class SwitchSys {
public:
    static void setClock(bool overclock);

private:
    enum class CPUClock {
        Stock = 0,         // default clock when application is launched
        Min = 1020000000,  // minimal clock
        Med = 1224000000,  // medium clock
        Max = 1785000000   // maximal clock
    };

    enum class GPUClock {
        Stock = 0,        // default clock when application is launched
        Min = 307200000,  // minimal clock
        Med = 460000000,  // medium clock
        Max = 921000000   // maximal clock
    };

    enum class EMCClock {
        Stock = 0,         // default clock when application is launched
        Min = 1331200000,  // minimal clock
        Med = 1331200000,  // medium clock
        Max = 1600000000   // maximal clock
    };

    enum class Module { Cpu = PcvModule_CpuBus, Gpu = PcvModule_GPU, Emc = PcvModule_EMC };

    static int getClock(const Module &module, bool stockClocks = false);

    static bool setClock(const Module &module, int hz);

    static int stock_cpu_clock;
    static int stock_gpu_clock;
    static int stock_emc_clock;
};

#endif