#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "Dbghelp.lib")

namespace CrashLog {

inline HMODULE g_module{};
inline PVOID g_vectoredHandler{};
inline wchar_t g_directory[MAX_PATH]{};
inline wchar_t g_sessionPath[MAX_PATH]{};
inline wchar_t g_crashPath[MAX_PATH]{};
inline wchar_t g_dumpPath[MAX_PATH]{};
inline std::uintptr_t g_moduleBegin{};
inline std::uintptr_t g_moduleEnd{};

inline thread_local bool tls_inRenderer = false;
inline thread_local const char* tls_stage = "idle";
inline thread_local std::uintptr_t tls_context = 0;
inline thread_local std::uintptr_t tls_tessellator = 0;

inline void setStage(const char* stage) noexcept {
    tls_stage = stage ? stage : "unknown";
}

inline void setPointers(const void* context, const void* tessellator = nullptr) noexcept {
    tls_context = reinterpret_cast<std::uintptr_t>(context);
    tls_tessellator = reinterpret_cast<std::uintptr_t>(tessellator);
}

struct RenderScope {
    RenderScope() noexcept { tls_inRenderer = true; }
    ~RenderScope() { tls_inRenderer = false; tls_stage = "idle"; tls_context = 0; tls_tessellator = 0; }
};

inline void writeRaw(const wchar_t* path, const char* text, DWORD length) noexcept {
    HANDLE file = CreateFileW(
        path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(file, text, length, &written, nullptr);
    CloseHandle(file);
}

inline void append(const char* format, ...) noexcept {
    char buffer[2048]{};
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    if (count <= 0)
        return;
    const DWORD bytes = static_cast<DWORD>(count < static_cast<int>(sizeof(buffer)) ? count : sizeof(buffer) - 1);
    writeRaw(g_sessionPath, buffer, bytes);
}

inline void crashWrite(const char* format, ...) noexcept {
    char buffer[2048]{};
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    if (count <= 0)
        return;
    const DWORD bytes = static_cast<DWORD>(count < static_cast<int>(sizeof(buffer)) ? count : sizeof(buffer) - 1);
    writeRaw(g_crashPath, buffer, bytes);
}

inline void describeAddress(std::uintptr_t address, char* output, std::size_t outputSize) noexcept {
    if (!output || outputSize == 0)
        return;

    HMODULE module = nullptr;
    if (address && GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &module)) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(module, path, MAX_PATH);
        const char* name = path;
        for (const char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/')
                name = p + 1;

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        snprintf(output, outputSize, "%s+0x%llX",
                 name, static_cast<unsigned long long>(address - base));
        return;
    }

    snprintf(output, outputSize, "0x%llX", static_cast<unsigned long long>(address));
}

inline void writeMiniDump(EXCEPTION_POINTERS* exceptionPointers) noexcept {
    HANDLE file = CreateFileW(
        g_dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exceptionPointers;
    info.ClientPointers = FALSE;

    MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file,
        static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
        exceptionPointers ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

inline LONG CALLBACK vectoredHandler(EXCEPTION_POINTERS* exceptionPointers) noexcept {
    if (!exceptionPointers || !exceptionPointers->ExceptionRecord || !exceptionPointers->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = exceptionPointers->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

#if defined(_M_X64)
    const auto rip = static_cast<std::uintptr_t>(exceptionPointers->ContextRecord->Rip);
#else
    const auto rip = reinterpret_cast<std::uintptr_t>(exceptionPointers->ExceptionRecord->ExceptionAddress);
#endif

    const bool insideOurDll = rip >= g_moduleBegin && rip < g_moduleEnd;
    if (!tls_inRenderer && !insideOurDll)
        return EXCEPTION_CONTINUE_SEARCH;

    SYSTEMTIME time{};
    GetLocalTime(&time);

    HANDLE reset = CreateFileW(
        g_crashPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (reset != INVALID_HANDLE_VALUE)
        CloseHandle(reset);

    char where[512]{};
    describeAddress(rip, where, sizeof(where));

    crashWrite("MCBE ImGui Tessellator crash log\r\n");
    crashWrite("Time: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    crashWrite("ExceptionCode: 0x%08lX\r\n", code);
    crashWrite("ExceptionAddress: %s\r\n", where);
    crashWrite("Stage: %s\r\n", tls_stage ? tls_stage : "unknown");
    crashWrite("InRenderer: %s\r\n", tls_inRenderer ? "true" : "false");
    crashWrite("MinecraftUIRenderContext: 0x%llX\r\n",
        static_cast<unsigned long long>(tls_context));
    crashWrite("Tessellator: 0x%llX\r\n",
        static_cast<unsigned long long>(tls_tessellator));

    if (code == EXCEPTION_ACCESS_VIOLATION && exceptionPointers->ExceptionRecord->NumberParameters >= 2) {
        const auto operation = exceptionPointers->ExceptionRecord->ExceptionInformation[0];
        const auto target = exceptionPointers->ExceptionRecord->ExceptionInformation[1];
        const char* op = operation == 0 ? "read" : (operation == 1 ? "write" : (operation == 8 ? "execute" : "unknown"));
        crashWrite("AccessViolation: %s at 0x%llX\r\n", op, static_cast<unsigned long long>(target));
    }

#if defined(_M_X64)
    CONTEXT* c = exceptionPointers->ContextRecord;
    crashWrite("\r\nRegisters:\r\n");
    crashWrite("RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\r\n",
        static_cast<unsigned long long>(c->Rax), static_cast<unsigned long long>(c->Rbx),
        static_cast<unsigned long long>(c->Rcx), static_cast<unsigned long long>(c->Rdx));
    crashWrite("RSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX\r\n",
        static_cast<unsigned long long>(c->Rsi), static_cast<unsigned long long>(c->Rdi),
        static_cast<unsigned long long>(c->Rbp), static_cast<unsigned long long>(c->Rsp));
    crashWrite("R8 =%016llX R9 =%016llX R10=%016llX R11=%016llX\r\n",
        static_cast<unsigned long long>(c->R8), static_cast<unsigned long long>(c->R9),
        static_cast<unsigned long long>(c->R10), static_cast<unsigned long long>(c->R11));
    crashWrite("R12=%016llX R13=%016llX R14=%016llX R15=%016llX\r\n",
        static_cast<unsigned long long>(c->R12), static_cast<unsigned long long>(c->R13),
        static_cast<unsigned long long>(c->R14), static_cast<unsigned long long>(c->R15));
    crashWrite("RIP=%016llX\r\n", static_cast<unsigned long long>(c->Rip));
#endif

    crashWrite("\r\nStack:\r\n");
    void* frames[48]{};
    const USHORT frameCount = CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    for (USHORT i = 0; i < frameCount; ++i) {
        char frameName[512]{};
        describeAddress(reinterpret_cast<std::uintptr_t>(frames[i]), frameName, sizeof(frameName));
        crashWrite("#%02u %s\r\n", static_cast<unsigned>(i), frameName);
    }

    crashWrite("\r\nSession log: %ls\r\n", g_sessionPath);
    crashWrite("Minidump: %ls\r\n", g_dumpPath);

    writeMiniDump(exceptionPointers);
    return EXCEPTION_CONTINUE_SEARCH;
}

inline bool install(HMODULE module) noexcept {
    g_module = module;

    wchar_t temp[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp))
        return false;

    swprintf_s(g_directory, L"%lsMCBE-ImGui-Tess", temp);
    CreateDirectoryW(g_directory, nullptr);
    swprintf_s(g_sessionPath, L"%ls\\session.log", g_directory);
    swprintf_s(g_crashPath, L"%ls\\crash-last.log", g_directory);
    swprintf_s(g_dumpPath, L"%ls\\crash-last.dmp", g_directory);

    DeleteFileW(g_sessionPath);

    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt && nt->Signature == IMAGE_NT_SIGNATURE) {
            g_moduleBegin = reinterpret_cast<std::uintptr_t>(base);
            g_moduleEnd = g_moduleBegin + nt->OptionalHeader.SizeOfImage;
        }
    }

    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    g_vectoredHandler = AddVectoredExceptionHandler(1, &vectoredHandler);
    append("MCBE ImGui Tessellator session started\r\n");
    append("DLL base: 0x%llX size: 0x%llX\r\n",
        static_cast<unsigned long long>(g_moduleBegin),
        static_cast<unsigned long long>(g_moduleEnd - g_moduleBegin));
    return g_vectoredHandler != nullptr;
}

inline void uninstall() noexcept {
    if (g_vectoredHandler) {
        RemoveVectoredExceptionHandler(g_vectoredHandler);
        g_vectoredHandler = nullptr;
    }
    SymCleanup(GetCurrentProcess());
}

inline const wchar_t* directory() noexcept { return g_directory; }
inline const wchar_t* crashPath() noexcept { return g_crashPath; }
inline const wchar_t* dumpPath() noexcept { return g_dumpPath; }

} // namespace CrashLog
