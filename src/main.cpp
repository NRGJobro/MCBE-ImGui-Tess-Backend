#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <MinHook.h>
#include <imgui.h>

#include "MCBE.hpp"
#include "ImGuiTessBackend.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

using RenderFn = void(__fastcall*)(ScreenView*, MinecraftUIRenderContext*);

HMODULE g_module{};
RenderFn g_original{};
mcbe::ImGuiTessBackend g_renderer{};
std::atomic_bool g_running{true};
std::atomic_bool g_showWindow{true};
std::atomic_uint g_activeRenderCalls{0};

struct ActiveCall {
    ActiveCall() { g_activeRenderCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveCall() { g_activeRenderCalls.fetch_sub(1, std::memory_order_acq_rel); }
};

void drawTestWindow() {
    ImGui::SetNextWindowPos(ImVec2(35.f, 35.f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(430.f, 235.f), ImGuiCond_Once);

    if (ImGui::Begin("MCBE Tessellator ImGui Backend")) {
        ImGui::TextUnformatted("Standalone renderer test");
        ImGui::Separator();
        ImGui::TextUnformatted("ImGui draw lists -> Minecraft Tessellator -> mce::Mesh");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Spacing();
        ImGui::TextUnformatted("INSERT: show/hide this window");
        ImGui::TextUnformatted("END: safely uninject");
        ImGui::Spacing();
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(ImGui::GetTime() * 2.0));
        ImGui::ProgressBar(pulse, ImVec2(-1.f, 0.f), "Tessellator render path active");
    }
    ImGui::End();
}

void __fastcall renderDetour(ScreenView* view, MinecraftUIRenderContext* ctx) {
    ActiveCall active;

    // Let vanilla finish this UI layer first, then place our native tessellator mesh over it.
    if (g_original) g_original(view, ctx);

    if (!g_running.load(std::memory_order_acquire) || !view || !ctx)
        return;

    if (GetAsyncKeyState(VK_INSERT) & 1)
        g_showWindow.store(!g_showWindow.load(std::memory_order_relaxed), std::memory_order_relaxed);

    if (!g_showWindow.load(std::memory_order_relaxed))
        return;

    if (view->screenScale.x <= 1.f || view->screenScale.y <= 1.f)
        return;

    if (!g_renderer.initialized() && !g_renderer.initialize(ctx))
        return;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(view->screenScale.x, view->screenScale.y);
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
    io.DeltaTime = std::clamp(view->deltaTime > 0.f ? view->deltaTime : (1.f / 60.f), 1.f / 1000.f, 0.1f);

    ImGui::NewFrame();
    drawTestWindow();
    ImGui::Render();
    g_renderer.render(ImGui::GetDrawData(), ctx);
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
    openConsole();
    std::puts("[MCBE-ImGui-Tess] loading standalone backend...");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    if (!mcbe::signatures::requiredReady()) {
        std::puts("[MCBE-ImGui-Tess] ERROR: one or more 26.52 signatures were not found.");
        std::puts("[MCBE-ImGui-Tess] Press END to unload.");
    } else if (MH_Initialize() != MH_OK) {
        std::puts("[MCBE-ImGui-Tess] ERROR: MinHook initialization failed.");
    } else {
        auto* target = reinterpret_cast<void*>(mcbe::signatures::renderContext());
        std::printf("[MCBE-ImGui-Tess] RenderContext target: %p\n", target);

        if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&renderDetour), reinterpret_cast<LPVOID*>(&g_original)) != MH_OK ||
            MH_EnableHook(target) != MH_OK) {
            std::puts("[MCBE-ImGui-Tess] ERROR: RenderContext hook failed.");
        } else {
            std::puts("[MCBE-ImGui-Tess] hooked. The Tessellator ImGui window should now be visible.");
            std::puts("[MCBE-ImGui-Tess] INSERT toggles it, END uninjects.");
        }
    }

    while ((GetAsyncKeyState(VK_END) & 1) == 0)
        Sleep(50);

    g_running.store(false, std::memory_order_release);

    if (auto* target = reinterpret_cast<void*>(mcbe::signatures::renderContext())) {
        MH_DisableHook(target);
        while (g_activeRenderCalls.load(std::memory_order_acquire) != 0)
            Sleep(1);
        MH_RemoveHook(target);
    }

    g_renderer.shutdown();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    MH_Uninitialize();

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
