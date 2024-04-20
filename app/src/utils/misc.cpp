#include "utils/misc.hpp"
#include <borealis/core/logger.hpp>
#include <random>
#include <sstream>
#include <iomanip>

#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#if defined(_WIN32) && !defined(_WINRT_)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

LONG WINAPI createMiniDump(_EXCEPTION_POINTERS* pep) {
    MEMORY_BASIC_INFORMATION memInfo;
    PEXCEPTION_RECORD xcpt = pep->ExceptionRecord;
    HANDLE hProcess = ::GetCurrentProcess();
    HANDLE hThread = ::GetCurrentThread();
    brls::Logger::error("Exception code {:#x}", xcpt->ExceptionCode);

    if (::VirtualQuery(xcpt->ExceptionAddress, &memInfo, sizeof(memInfo))) {
        char modulePath[MAX_PATH] = "Unknown Module";
        HMODULE hModule = (HMODULE)memInfo.AllocationBase;
        if (!memInfo.AllocationBase) hModule = GetModuleHandleA(nullptr);
        ::GetModuleFileNameA(hModule, modulePath, sizeof(modulePath));
        brls::Logger::error("Fault address {} {}", fmt::ptr(xcpt->ExceptionAddress), modulePath);
    }

    // Get handles to kernel32 and dbghelp
    HMODULE dbghelp = ::LoadLibraryA("dbghelp.dll");
    if (!dbghelp) return EXCEPTION_CONTINUE_SEARCH;

    auto fnMiniDumpWriteDump = (WINBOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
        CONST PMINIDUMP_EXCEPTION_INFORMATION, CONST PMINIDUMP_USER_STREAM_INFORMATION,
        CONST PMINIDUMP_CALLBACK_INFORMATION))::GetProcAddress(dbghelp, "MiniDumpWriteDump");
    auto fnSymInitialize = (WINBOOL(WINAPI*)(HANDLE, PCSTR, WINBOOL))::GetProcAddress(dbghelp, "SymInitialize");
    auto fnSymCleanup = (WINBOOL(WINAPI*)(HANDLE))::GetProcAddress(dbghelp, "SymCleanup");
    auto fnStackWalk64 = (WINBOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
        PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64,
        PTRANSLATE_ADDRESS_ROUTINE64))::GetProcAddress(dbghelp, "StackWalk64");
    auto fnSymGetSymFromAddr64 = (WINBOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PIMAGEHLP_SYMBOL64))::GetProcAddress(
        dbghelp, "SymGetSymFromAddr64");
    auto fnSymGetModuleInfo64 =
        (WINBOOL(WINAPI*)(HANDLE, DWORD64, PIMAGEHLP_MODULE64))::GetProcAddress(dbghelp, "SymGetModuleInfo64");
    auto fnSymFTA64 = (PFUNCTION_TABLE_ACCESS_ROUTINE64)::GetProcAddress(dbghelp, "SymFunctionTableAccess64");
    auto fnSymGMB64 = (PGET_MODULE_BASE_ROUTINE64)::GetProcAddress(dbghelp, "SymGetModuleBase64");

    STACKFRAME64 s;
    DWORD imageType;
    CONTEXT ctx = *pep->ContextRecord;

    ZeroMemory(&s, sizeof(s));
    s.AddrPC.Mode = AddrModeFlat;
    s.AddrFrame.Mode = AddrModeFlat;
    s.AddrStack.Mode = AddrModeFlat;
#ifdef _M_IX86
    imageType = IMAGE_FILE_MACHINE_I386;
    s.AddrPC.Offset = ctx.Eip;
    s.AddrFrame.Offset = ctx.Ebp;
    s.AddrStack.Offset = ctx.Esp;
#elif _M_X64
    imageType = IMAGE_FILE_MACHINE_AMD64;
    s.AddrPC.Offset = ctx.Rip;
    s.AddrFrame.Offset = ctx.Rsp;
    s.AddrStack.Offset = ctx.Rsp;
#elif _M_ARM64
    imageType = IMAGE_FILE_MACHINE_ARM64;
    s.AddrPC.Offset = ctx.Pc;
    s.AddrFrame.Offset = ctx.Fp;
    s.AddrStack.Offset = ctx.Sp;
#else
#error "Platform not supported!"
#endif

    std::vector<char> buf(sizeof(IMAGEHLP_SYMBOL64) + MAX_SYM_NAME);
    PIMAGEHLP_SYMBOL64 symbol = reinterpret_cast<PIMAGEHLP_SYMBOL64>(buf.data());
    symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
    symbol->MaxNameLength = MAX_SYM_NAME;
    int n = 0;

    if (!fnSymInitialize(hProcess, nullptr, TRUE)) {
        brls::Logger::warning("SymInitialize failed {}", ::GetLastError());
    }
    while (fnStackWalk64(imageType, hProcess, hThread, &s, &ctx, nullptr, fnSymFTA64, fnSymGMB64, nullptr)) {
        IMAGEHLP_MODULE64 mi = {sizeof(IMAGEHLP_MODULE64)};
        DWORD64 funcOffset = 0;

        if (!fnSymGetModuleInfo64(hProcess, s.AddrPC.Offset, &mi)) {
            strcpy(mi.ImageName, "???");
        }
        if (fnSymGetSymFromAddr64(hProcess, s.AddrPC.Offset, &funcOffset, symbol)) {
            brls::Logger::error("[{}]: {}!{} + {:#x}", n, mi.ImageName, symbol->Name, funcOffset);
        } else {
            brls::Logger::error("[{}]: {} + {:#x}", n, mi.ImageName, s.AddrPC.Offset);
        }
        n++;
    }
    fnSymCleanup(hProcess);

    SYSTEMTIME lt;
    CHAR tempPath[MAX_PATH];
    DWORD cbTemp = ::GetTempPathA(sizeof(tempPath), tempPath);
    ::GetLocalTime(&lt);

    snprintf(tempPath + cbTemp, sizeof(tempPath) - cbTemp, "switchfin-%04d%02d%02d-%02d%02d%02d.dmp", lt.wYear,
        lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
    HANDLE hDump = ::CreateFileA(tempPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    MINIDUMP_EXCEPTION_INFORMATION info;
    info.ThreadId = ::GetCurrentThreadId();
    info.ExceptionPointers = pep;
    info.ClientPointers = TRUE;
    fnMiniDumpWriteDump(hProcess, ::GetCurrentProcessId(), hDump, MiniDumpNormal, &info, nullptr, nullptr);
    ::CloseHandle(hDump);

    return EXCEPTION_CONTINUE_SEARCH;
}

void misc::initCrashDump() { SetUnhandledExceptionFilter(createMiniDump); }

#else

void misc::initCrashDump() {}

#endif

std::string misc::sec2Time(int64_t t) {
    if (t < 3600) {
        return fmt::format("{:%M:%S}", std::chrono::seconds(t));
    }
    return fmt::format("{:%H:%M:%S}", std::chrono::seconds(t));
}

std::string misc::randHex(const int len) {
    std::stringstream ss;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return ss.str();
}

std::string misc::hexEncode(const unsigned char* data, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return ss.str();
}

bool misc::sendIPC(const std::string& sock, const std::string& payload) {
#ifdef _WIN32
    HANDLE hPipe = CreateFile(sock.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) return false;

    if (!WriteFile(hPipe, payload.c_str(), payload.size(), NULL, NULL)) {
        brls::Logger::warning("sendIPC `{}` failed: {}", payload, GetLastError());
    }

    CloseHandle(hPipe);
    return true;
#elif defined(__APPLE__) || defined(__linux__)
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) return false;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock.c_str(), sizeof(addr.sun_path) - 1);

    if ((connect(fd, (struct sockaddr*)&addr, sizeof(addr))) == -1) {
        close(fd);
        return false;
    }

    if (write(fd, payload.c_str(), payload.size()) == -1) {
        brls::Logger::warning("sendIPC `{}` failed: {}", payload, errno);
    }

    close(fd);
    return true;
#else
    return false;
#endif
}