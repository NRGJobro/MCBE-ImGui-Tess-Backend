#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_win32.h>

#include "MCBE.hpp"
#include "CrashLogger.hpp"
#include "ImGuiTessBackend.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <thread>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace {

using RenderFn = void(__fastcall*)(ScreenView*, MinecraftUIRenderContext*);
using WindowProcFn = LRESULT(__fastcall*)(HWND, UINT, WPARAM, LPARAM);

HMODULE g_module{};
RenderFn g_original{};
WindowProcFn g_originalWindowProc{};
mcbe::ImGuiTessBackend g_renderer{};

std::atomic_bool g_running{true};
std::atomic_bool g_showWindow{true};
std::atomic_bool g_platformReady{false};
std::atomic_bool g_windowHookReady{false};
std::atomic_uint g_activeRenderCalls{0};
std::atomic_uint g_activeWindowCalls{0};
HWND g_platformWindow{};

struct ActiveRenderCall {
    ActiveRenderCall() { g_activeRenderCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveRenderCall() { g_activeRenderCalls.fetch_sub(1, std::memory_order_acq_rel); }
};

struct ActiveWindowCall {
    ActiveWindowCall() { g_activeWindowCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveWindowCall() { g_activeWindowCalls.fetch_sub(1, std::memory_order_acq_rel); }
};

// Bedrock invokes RenderContext for several UI views during one visual frame.
// Learn the repeating view order and render on the last view from the previous
// cycle. This converges after one cycle and avoids submitting ImGui several
// times per screen frame.
struct RenderOwner {
    ScreenView* cycleFirst{};
    ScreenView* previous{};
    ScreenView* owner{};
    unsigned missedOwnerCalls{};

    bool shouldRender(ScreenView* view) {
        if (!view)
            return false;

        if (!cycleFirst) {
            cycleFirst = previous = owner = view;
            missedOwnerCalls = 0;
            return true;
        }

        if (view == cycleFirst && previous && previous != view) {
            owner = previous;
            missedOwnerCalls = 0;
        }

        // Screen changes can replace every ScreenView pointer. Re-learn quickly
        // instead of waiting forever for the old cycle's first pointer.
        if (view == owner) {
            missedOwnerCalls = 0;
        } else if (++missedOwnerCalls > 24) {
            cycleFirst = view;
            owner = view;
            missedOwnerCalls = 0;
        }

        previous = view;
        return view == owner;
    }
};

RenderOwner g_renderOwner{};

HWND findMinecraftWindow() {
    const DWORD pid = GetCurrentProcessId();

    HWND foreground = GetForegroundWindow();
    if (foreground) {
        DWORD windowPid = 0;
        GetWindowThreadProcessId(foreground, &windowPid);
        if (windowPid == pid)
            return foreground;
    }

    struct Search {
        DWORD pid{};
        HWND window{};
    } search{pid, nullptr};

    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto* state = reinterpret_cast<Search*>(param);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(hwnd, &windowPid);
        if (windowPid != state->pid || !IsWindowVisible(hwnd))
            return TRUE;

        RECT rect{};
        if (!GetClientRect(hwnd, &rect) ||
            rect.right - rect.left < 320 ||
            rect.bottom - rect.top < 200)
            return TRUE;

        state->window = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));

    return search.window;
}

bool initializePlatform(HWND hwnd) {
    if (!hwnd || !ImGui::GetCurrentContext())
        return false;

    if (g_platformReady.load(std::memory_order_acquire))
        return g_platformWindow == hwnd;

    if (!ImGui_ImplWin32_Init(hwnd))
        return false;

    g_platformWindow = hwnd;
    g_platformReady.store(true, std::memory_order_release);
    CrashLog::append("Win32 ImGui platform backend initialized: hwnd=%p\r\n", hwnd);
    return true;
}

bool isMouseCaptureMessage(UINT msg) {
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return true;
    default:
        return false;
    }
}

bool isKeyboardCaptureMessage(UINT msg) {
    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_UNICHAR:
    case WM_IME_CHAR:
        return true;
    default:
        return false;
    }
}

LRESULT __fastcall windowProcDetour(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ActiveWindowCall active;

    if (g_running.load(std::memory_order_acquire) && ImGui::GetCurrentContext()) {
        if (!g_platformReady.load(std::memory_order_acquire))
            (void)initializePlatform(hwnd);

        if (g_platformReady.load(std::memory_order_acquire) && hwnd == g_platformWindow) {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

            if (g_showWindow.load(std::memory_order_relaxed)) {
                const ImGuiIO& io = ImGui::GetIO();

                // Feed move/focus messages to both systems. Suppress clicks/wheel
                // only while ImGui owns them so dragging does not click Minecraft.
                if (io.WantCaptureMouse && isMouseCaptureMessage(msg))
                    return 0;
                if (io.WantCaptureKeyboard && isKeyboardCaptureMessage(msg))
                    return 0;
            }
        }
    }

    return g_originalWindowProc
        ? g_originalWindowProc(hwnd, msg, wParam, lParam)
        : DefWindowProcW(hwnd, msg, wParam, lParam);
}

void updateFallbackInput(ImGuiIO& io, HWND hwnd) {
    if (!hwnd)
        return;

    POINT point{};
    if (GetCursorPos(&point) && ScreenToClient(hwnd, &point))
        io.AddMousePosEvent(static_cast<float>(point.x), static_cast<float>(point.y));

    io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
}

void prepareImGuiFrame(ScreenView* view, MinecraftUIRenderContext* ctx) {
    ImGuiIO& io = ImGui::GetIO();

    if (g_platformReady.load(std::memory_order_acquire)) {
        ImGui_ImplWin32_NewFrame();
        io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
        return;
    }

    HWND hwnd = g_platformWindow ? g_platformWindow : findMinecraftWindow();
    if (hwnd && initializePlatform(hwnd)) {
        ImGui_ImplWin32_NewFrame();
        io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
        return;
    }

    Vec2 display = view ? view->screenScale : Vec2{};
    if (ctx && ctx->clientInstance) {
        if (auto* gui = ctx->clientInstance->getGuiData()) {
            const Vec2 physical = gui->getMcResolution();
            if (physical.x > 1.f && physical.y > 1.f)
                display = physical;
        }
    }

    io.DisplaySize = ImVec2(
        std::max(display.x, 1.f),
        std::max(display.y, 1.f));
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
    io.DeltaTime = std::clamp(
        view && view->deltaTime > 0.f ? view->deltaTime : (1.f / 60.f),
        1.f / 1000.f, 0.1f);

    updateFallbackInput(io, hwnd);
}

void drawTestWindow() {
    ImGui::SetNextWindowPos(ImVec2(35.f, 35.f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(430.f, 235.f), ImGuiCond_Once);

    if (ImGui::Begin("MCBE Tessellator ImGui Backend")) {
        ImGui::TextUnformatted("Standalone renderer test");
        ImGui::Separator();
        ImGui::TextUnformatted("ImGui draw lists -> Minecraft Tessellator -> mce::Mesh");
        ImGui::Text("Frame rate: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Mouse: %.0f, %.0f", ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
        ImGui::Spacing();
        ImGui::TextUnformatted("Drag this title bar to verify input.");
        ImGui::TextUnformatted("INSERT: show/hide this window");
        ImGui::TextUnformatted("END: safely uninject");
        ImGui::Spacing();
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(ImGui::GetTime() * 2.0));
        ImGui::ProgressBar(pulse, ImVec2(-1.f, 0.f), "Tessellator render path active");
    }
    ImGui::End();
}

void __fastcall renderDetour(ScreenView* view, MinecraftUIRenderContext* ctx) {
    ActiveRenderCall active;
    CrashLog::RenderScope crashScope;
    CrashLog::setPointers(ctx);
    CrashLog::setStage("renderDetour: enter");

    if (!g_running.load(std::memory_order_acquire) || !view || !ctx) {
        if (g_original)
            g_original(view, ctx);
        return;
    }

    const bool owner = g_renderOwner.shouldRender(view);
    const bool visible = g_showWindow.load(std::memory_order_relaxed);

    // Resource creation needs the live pre-vanilla UI context, but only do it
    // on the single owner pass.
    if (owner && visible && !g_renderer.initialized()) {
        CrashLog::checkpoint("renderDetour: initialize backend");
        (void)g_renderer.initialize(ctx);
    }

    // Let this Bedrock layer finish first. The learned owner is normally the
    // final layer in the visual cycle, so our Tessellator mesh lands on top and
    // is not immediately overdrawn by later native UI passes.
    if (g_original)
        g_original(view, ctx);

    if (!owner || !visible || !g_renderer.initialized())
        return;

    CrashLog::setStage("renderDetour: prepare ImGui frame");
    prepareImGuiFrame(view, ctx);

    ImGui::NewFrame();
    drawTestWindow();
    ImGui::Render();

    CrashLog::setStage("renderDetour: Tessellator submit");
    g_renderer.render(ImGui::GetDrawData(), ctx);
    CrashLog::setStage("renderDetour: complete");
}

void openConsole() {
    if (!AllocConsole()) return;
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    SetConsoleTitleW(L"MCBE ImGui Tessellator Backend");
}

DWORD WINAPI startup(void* module) {
    g_module = static_cast<HMODULE>(module);
    CrashLog::install(g_module);
    openConsole();
    std::puts("[MCBE-ImGui-Tess] loading standalone backend...");
    std::wprintf(L"[MCBE-ImGui-Tess] diagnostics: %ls\n", CrashLog::directory());

    CrashLog::append("RenderContext: 0x%llX\r\n", static_cast<unsigned long long>(mcbe::signatures::renderContext()));
    CrashLog::append("WindowProcCallback: 0x%llX\r\n", static_cast<unsigned long long>(mcbe::signatures::windowProcCallback()));
    CrashLog::append("Mesh::_renderMesh: 0x%llX\r\n", static_cast<unsigned long long>(mcbe::signatures::meshRender()));
    CrashLog::append("RenderMaterialGroup::common: 0x%llX\r\n", static_cast<unsigned long long>(mcbe::signatures::materialCommon()));
    CrashLog::append("TextureGroup::uploadTexture: 0x%llX\r\n", static_cast<unsigned long long>(mcbe::signatures::textureUpload()));
    CrashLog::append("cg::ImageResource::vtable: 0x%llX\r\n", static_cast<unsigned long long>(mcbe::signatures::imageResourceVtable()));

    CrashLog::setStage("startup: IMGUI_CHECKVERSION");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (HWND hwnd = findMinecraftWindow())
        (void)initializePlatform(hwnd);

    if (!mcbe::signatures::requiredReady()) {
        std::puts("[MCBE-ImGui-Tess] ERROR: one or more 26.52 renderer signatures were not found.");
        std::puts("[MCBE-ImGui-Tess] Press END to unload.");
    } else if (MH_Initialize() != MH_OK) {
        std::puts("[MCBE-ImGui-Tess] ERROR: MinHook initialization failed.");
    } else {
        auto* renderTarget = reinterpret_cast<void*>(mcbe::signatures::renderContext());
        auto* windowTarget = reinterpret_cast<void*>(mcbe::signatures::windowProcCallback());

        std::printf("[MCBE-ImGui-Tess] RenderContext target: %p\n", renderTarget);
        std::printf("[MCBE-ImGui-Tess] WindowProc target: %p\n", windowTarget);

        bool renderHookOk =
            MH_CreateHook(
                renderTarget,
                reinterpret_cast<LPVOID>(&renderDetour),
                reinterpret_cast<LPVOID*>(&g_original)) == MH_OK &&
            MH_EnableHook(renderTarget) == MH_OK;

        bool windowHookOk = false;
        if (windowTarget) {
            windowHookOk =
                MH_CreateHook(
                    windowTarget,
                    reinterpret_cast<LPVOID>(&windowProcDetour),
                    reinterpret_cast<LPVOID*>(&g_originalWindowProc)) == MH_OK &&
                MH_EnableHook(windowTarget) == MH_OK;
        }
        g_windowHookReady.store(windowHookOk, std::memory_order_release);

        if (!renderHookOk) {
            std::puts("[MCBE-ImGui-Tess] ERROR: RenderContext hook failed.");
        } else {
            std::puts("[MCBE-ImGui-Tess] Tessellator renderer hooked.");
            std::puts(windowHookOk
                ? "[MCBE-ImGui-Tess] Win32 input hooked through Minecraft MainWindow."
                : "[MCBE-ImGui-Tess] WARNING: window callback hook missing; using polling fallback.");
            std::puts("[MCBE-ImGui-Tess] INSERT toggles it, END uninjects.");
        }
    }

    bool lastInsert = false;
    while ((GetAsyncKeyState(VK_END) & 1) == 0) {
        const bool insert = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (insert && !lastInsert)
            g_showWindow.store(!g_showWindow.load(std::memory_order_relaxed), std::memory_order_relaxed);
        lastInsert = insert;
        Sleep(10);
    }

    g_running.store(false, std::memory_order_release);

    if (auto* renderTarget = reinterpret_cast<void*>(mcbe::signatures::renderContext()))
        MH_DisableHook(renderTarget);
    if (auto* windowTarget = reinterpret_cast<void*>(mcbe::signatures::windowProcCallback()))
        MH_DisableHook(windowTarget);

    while (g_activeRenderCalls.load(std::memory_order_acquire) != 0 ||
           g_activeWindowCalls.load(std::memory_order_acquire) != 0)
        Sleep(1);

    if (auto* renderTarget = reinterpret_cast<void*>(mcbe::signatures::renderContext()))
        MH_RemoveHook(renderTarget);
    if (auto* windowTarget = reinterpret_cast<void*>(mcbe::signatures::windowProcCallback()))
        MH_RemoveHook(windowTarget);

    g_renderer.shutdown();

    if (g_platformReady.exchange(false, std::memory_order_acq_rel))
        ImGui_ImplWin32_Shutdown();
    g_platformWindow = nullptr;

    if (ImGui::GetCurrentContext())
        ImGui::DestroyContext();

    MH_Uninitialize();

    CrashLog::append("Clean unload completed.\r\n");
    CrashLog::uninstall();

    std::puts("[MCBE-ImGui-Tess] unloaded.");
    Sleep(100);
    FreeConsole();
    FreeLibraryAndExitThread(g_module, 0);
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (HANDLE thread = CreateThread(nullptr, 0, startup, module, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}
