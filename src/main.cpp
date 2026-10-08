#include <Windows.h>
#include <MinHook.h>
#include <imgui.h>

#include "MCBE.hpp"
#include "CrashLogger.hpp"
#include "ImGuiTessBackend.hpp"
#include "WorldPanelDemo.hpp"
#include "mcbe/Input/MouseDevice.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace {

using RenderFn = void(__fastcall*)(ScreenView*, MinecraftUIRenderContext*);
using MouseRefreshFn = void(__fastcall*)(void*);
using LevelRendererFn = std::int64_t(__fastcall*)(
    mcbe::world::LevelRenderer*, ScreenContext*, std::int64_t);
using CameraTickFn = void(__fastcall*)(
    mcbe::world::CameraComponent*, void*, float);

HMODULE g_module{};
RenderFn g_originalRender{};
MouseRefreshFn g_originalMouseRefresh{};
LevelRendererFn g_originalLevelRenderer{};
CameraTickFn g_originalCameraTick{};
mcbe::ImGuiTessBackend g_renderer{};
mcbe::WorldPanelDemo g_worldPanel{};

std::atomic_bool g_running{true};
std::atomic_bool g_showWindow{true};
std::atomic_bool g_guiOwnsMouse{false};

std::atomic_uint g_activeRenderCalls{0};
std::atomic_uint g_activeMouseCalls{0};
std::atomic_uint g_activeLevelRenderCalls{0};
std::atomic_uint g_activeCameraCalls{0};

struct NativeMouseState {
    std::atomic_int x{0};
    std::atomic_int y{0};
    std::atomic_uint buttons{0};
    std::atomic_int wheel{0};
    std::atomic_bool seen{false};
};

NativeMouseState g_mouse{};

struct ActiveRenderCall {
    ActiveRenderCall() { g_activeRenderCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveRenderCall() { g_activeRenderCalls.fetch_sub(1, std::memory_order_acq_rel); }
};

struct ActiveMouseCall {
    ActiveMouseCall() { g_activeMouseCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveMouseCall() { g_activeMouseCalls.fetch_sub(1, std::memory_order_acq_rel); }
};

struct ActiveLevelRenderCall {
    ActiveLevelRenderCall() { g_activeLevelRenderCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveLevelRenderCall() { g_activeLevelRenderCalls.fetch_sub(1, std::memory_order_acq_rel); }
};

struct ActiveCameraCall {
    ActiveCameraCall() { g_activeCameraCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~ActiveCameraCall() { g_activeCameraCalls.fetch_sub(1, std::memory_order_acq_rel); }
};

// Bedrock invokes RenderContext for multiple UI views per visual frame.
// Learn the repeating view order and submit the ImGui mesh only on one owner
// pass so native UI layers do not fight each other.
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


void __fastcall cameraTickDetour(
    mcbe::world::CameraComponent* camera,
    void* a2,
    float a3) {

    ActiveCameraCall active;

    if (g_originalCameraTick)
        g_originalCameraTick(camera, a2, a3);

    if (g_running.load(std::memory_order_acquire))
        g_worldPanel.updateCamera(camera);
}

std::int64_t __fastcall levelRendererDetour(
    mcbe::world::LevelRenderer* renderer,
    ScreenContext* screen,
    std::int64_t a3) {

    ActiveLevelRenderCall active;

    const std::int64_t result =
        g_originalLevelRenderer
            ? g_originalLevelRenderer(renderer, screen, a3)
            : 0;

    if (!g_running.load(std::memory_order_acquire) ||
        !renderer || !screen || !g_renderer.initialized())
        return result;

    const auto panel = g_worldPanel.consumeTransform();
    if (!panel.valid)
        return result;

    ImGuiWindow* source = mcbe::WorldPanelDemo::sourceWindow();
    if (!source)
        return result;

    CrashLog::setStage("worldPanel: Tessellator submit");
    g_renderer.renderWorldWindow(
        source->DrawList,
        source->Pos,
        source->Size,
        screen,
        renderer->origin(),
        panel.center,
        panel.right,
        panel.up,
        panel.width);

    return result;
}

void updateNativeMouseFromAction(const MouseAction& action, unsigned& buttons, int& wheel) {
    g_mouse.x.store(static_cast<int>(action.x), std::memory_order_relaxed);
    g_mouse.y.store(static_cast<int>(action.y), std::memory_order_relaxed);

    if (action.action >= 1 && action.action <= 3) {
        const unsigned bit = 1u << static_cast<unsigned>(action.action - 1);
        if (action.data > 0)
            buttons |= bit;
        else
            buttons &= ~bit;
    } else if (action.action == 4 && action.data != 0) {
        wheel += action.data > 0 ? 1 : -1;
    }
}

void __fastcall mouseRefreshDetour(void* self) {
    ActiveMouseCall active;

    if (g_originalMouseRefresh)
        g_originalMouseRefresh(self);

    if (!g_running.load(std::memory_order_acquire))
        return;

    MouseDevice* mouse = MouseDevice::get();
    if (!mouse)
        return;

    g_mouse.x.store(static_cast<int>(mouse->x), std::memory_order_relaxed);
    g_mouse.y.store(static_cast<int>(mouse->y), std::memory_order_relaxed);

    unsigned buttons = g_mouse.buttons.load(std::memory_order_relaxed);
    int wheel = 0;

    for (const MouseAction& action : mouse->inputs)
        updateNativeMouseFromAction(action, buttons, wheel);

    g_mouse.buttons.store(buttons, std::memory_order_release);
    if (wheel != 0)
        g_mouse.wheel.fetch_add(wheel, std::memory_order_acq_rel);
    g_mouse.seen.store(true, std::memory_order_release);

    // If ImGui owns the pointer, prevent native clicks/wheel from activating
    // Minecraft controls beneath the ImGui window. Motion is left intact so
    // Bedrock's pointer state continues to update normally.
    if (g_showWindow.load(std::memory_order_relaxed) &&
        g_guiOwnsMouse.load(std::memory_order_acquire)) {
        auto it = mouse->inputs.begin();
        while (it != mouse->inputs.end()) {
            if (it->action >= 1 && it->action <= 4)
                it = mouse->inputs.erase(it);
            else
                ++it;
        }
    }
}

void feedNativeMouse(ImGuiIO& io, MinecraftUIRenderContext* ctx) {
    static unsigned previousButtons = 0;

    if (g_mouse.seen.load(std::memory_order_acquire)) {
        const float x = static_cast<float>(g_mouse.x.load(std::memory_order_relaxed));
        const float y = static_cast<float>(g_mouse.y.load(std::memory_order_relaxed));
        io.AddMousePosEvent(x, y);

        const unsigned buttons = g_mouse.buttons.load(std::memory_order_acquire);
        for (int index = 0; index < 3; ++index) {
            const unsigned bit = 1u << static_cast<unsigned>(index);
            const bool now = (buttons & bit) != 0;
            const bool before = (previousButtons & bit) != 0;
            if (now != before)
                io.AddMouseButtonEvent(index, now);
        }
        previousButtons = buttons;

        const int wheel = g_mouse.wheel.exchange(0, std::memory_order_acq_rel);
        if (wheel != 0)
            io.AddMouseWheelEvent(0.0f, static_cast<float>(wheel));
        return;
    }

    // Fallback to Minecraft's own GuiData cursor position if the native mouse
    // refresh has not fired yet. This is still game memory, not desktop input.
    if (ctx && ctx->clientInstance) {
        if (auto* gui = ctx->clientInstance->getGuiData()) {
            const Vec2 mouse = gui->getMousePos();
            io.AddMousePosEvent(mouse.x, mouse.y);
        }
    }
}

void prepareImGuiFrame(ScreenView* view, MinecraftUIRenderContext* ctx) {
    ImGuiIO& io = ImGui::GetIO();

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

    feedNativeMouse(io, ctx);
}

void drawTestWindow() {
    ImGui::SetNextWindowPos(ImVec2(35.f, 35.f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(430.f, 235.f), ImGuiCond_Once);

    if (ImGui::Begin("MCBE Tessellator ImGui Backend")) {
        const ImGuiIO& io = ImGui::GetIO();

        ImGui::TextUnformatted("Standalone renderer test");
        ImGui::Separator();
        ImGui::TextUnformatted("ImGui draw lists -> Minecraft Tessellator -> mce::Mesh");
        ImGui::Text("Frame rate: %.1f", io.Framerate);
        ImGui::Text("Mouse: %.0f, %.0f", io.MousePos.x, io.MousePos.y);
        ImGui::Text("LMB: %s   CaptureMouse: %s",
            io.MouseDown[0] ? "DOWN" : "up",
            io.WantCaptureMouse ? "yes" : "no");
        ImGui::Spacing();
        ImGui::TextUnformatted("Input source: Minecraft MouseDevice");
        ImGui::TextUnformatted("Drag/resize this window to verify native input.");
        ImGui::TextUnformatted("F6: place/remove 3D world panel");
        ImGui::TextUnformatted("INSERT: show/hide 2D copy   END: uninject");
        ImGui::Spacing();

        const float pulse =
            0.5f + 0.5f * static_cast<float>(std::sin(ImGui::GetTime() * 2.0));
        ImGui::ProgressBar(
            pulse, ImVec2(-1.f, 0.f), "Tessellator render path active");
    }
    ImGui::End();
}

void __fastcall renderDetour(ScreenView* view, MinecraftUIRenderContext* ctx) {
    ActiveRenderCall active;
    CrashLog::RenderScope crashScope;
    CrashLog::setPointers(ctx);
    CrashLog::setStage("renderDetour: enter");

    if (!g_running.load(std::memory_order_acquire) || !view || !ctx) {
        if (g_originalRender)
            g_originalRender(view, ctx);
        return;
    }

    const bool owner = g_renderOwner.shouldRender(view);
    const bool visible = g_showWindow.load(std::memory_order_relaxed);
    const bool needImGuiFrame = visible || g_worldPanel.active();

    if (owner && needImGuiFrame && !g_renderer.initialized()) {
        CrashLog::checkpoint("renderDetour: initialize backend");
        (void)g_renderer.initialize(ctx);
    }

    if (g_originalRender)
        g_originalRender(view, ctx);

    if (!owner || !needImGuiFrame || !g_renderer.initialized()) {
        g_guiOwnsMouse.store(false, std::memory_order_release);
        return;
    }

    CrashLog::setStage("renderDetour: prepare ImGui frame");
    prepareImGuiFrame(view, ctx);

    ImGui::NewFrame();
    drawTestWindow();
    ImGui::Render();

    // Publish capture state to the native mouse thread without touching ImGui
    // from that thread.
    g_guiOwnsMouse.store(
        visible && ImGui::GetIO().WantCaptureMouse,
        std::memory_order_release);

    if (visible) {
        CrashLog::setStage("renderDetour: Tessellator submit");
        g_renderer.render(ImGui::GetDrawData(), ctx);
    }
    CrashLog::setStage("renderDetour: complete");
}

DWORD WINAPI startup(void* module) {
    g_module = static_cast<HMODULE>(module);
    CrashLog::install(g_module);

    CrashLog::append("MCBE ImGui Tessellator standalone backend started.\r\n");
    CrashLog::append("RenderContext: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::renderContext()));
    CrashLog::append("MouseRefresh: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::mouseRefresh()));
    CrashLog::append("MouseDevice: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::mouseDevice()));
    CrashLog::append("LevelRenderer: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::levelRenderer()));
    CrashLog::append("CameraBlendSystem::tick: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::cameraTick()));
    CrashLog::append("Mesh::_renderMesh: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::meshRender()));
    CrashLog::append("RenderMaterialGroup::common: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::materialCommon()));
    CrashLog::append("TextureGroup::uploadTexture: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::textureUpload()));
    CrashLog::append("cg::ImageResource::vtable: 0x%llX\r\n",
        static_cast<unsigned long long>(mcbe::signatures::imageResourceVtable()));

    CrashLog::setStage("startup: IMGUI_CHECKVERSION");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    if (!mcbe::signatures::requiredReady()) {
        CrashLog::append("ERROR: one or more required 26.52 renderer signatures are missing.\r\n");
    } else if (MH_Initialize() != MH_OK) {
        CrashLog::append("ERROR: MinHook initialization failed.\r\n");
    } else {
        auto* renderTarget =
            reinterpret_cast<void*>(mcbe::signatures::renderContext());
        auto* mouseTarget =
            reinterpret_cast<void*>(mcbe::signatures::mouseRefresh());
        auto* levelTarget =
            reinterpret_cast<void*>(mcbe::signatures::levelRenderer());
        auto* cameraTarget =
            reinterpret_cast<void*>(mcbe::signatures::cameraTick());

        const bool renderHookOk =
            renderTarget &&
            MH_CreateHook(
                renderTarget,
                reinterpret_cast<LPVOID>(&renderDetour),
                reinterpret_cast<LPVOID*>(&g_originalRender)) == MH_OK &&
            MH_EnableHook(renderTarget) == MH_OK;

        bool mouseHookOk = false;
        if (mouseTarget && mcbe::signatures::mouseDevice()) {
            mouseHookOk =
                MH_CreateHook(
                    mouseTarget,
                    reinterpret_cast<LPVOID>(&mouseRefreshDetour),
                    reinterpret_cast<LPVOID*>(&g_originalMouseRefresh)) == MH_OK &&
                MH_EnableHook(mouseTarget) == MH_OK;
        }

        bool levelHookOk = false;
        if (levelTarget) {
            levelHookOk =
                MH_CreateHook(
                    levelTarget,
                    reinterpret_cast<LPVOID>(&levelRendererDetour),
                    reinterpret_cast<LPVOID*>(&g_originalLevelRenderer)) == MH_OK &&
                MH_EnableHook(levelTarget) == MH_OK;
        }

        bool cameraHookOk = false;
        if (cameraTarget) {
            cameraHookOk =
                MH_CreateHook(
                    cameraTarget,
                    reinterpret_cast<LPVOID>(&cameraTickDetour),
                    reinterpret_cast<LPVOID*>(&g_originalCameraTick)) == MH_OK &&
                MH_EnableHook(cameraTarget) == MH_OK;
        }

        CrashLog::append(
            "Hooks: render=%s nativeMouse=%s levelRenderer=%s camera=%s\r\n",
            renderHookOk ? "OK" : "FAILED",
            mouseHookOk ? "OK" : "FAILED",
            levelHookOk ? "OK" : "FAILED",
            cameraHookOk ? "OK" : "FAILED");
    }

    bool lastInsert = false;
    while ((GetAsyncKeyState(VK_END) & 1) == 0) {
        if (GetAsyncKeyState(VK_F6) & 1) {
            g_worldPanel.toggleRequested();
            CrashLog::append("F6: toggled world panel request.\r\n");
        }

        const bool insert = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (insert && !lastInsert) {
            const bool next =
                !g_showWindow.load(std::memory_order_relaxed);
            g_showWindow.store(next, std::memory_order_relaxed);
            if (!next)
                g_guiOwnsMouse.store(false, std::memory_order_release);
        }
        lastInsert = insert;
        Sleep(10);
    }

    g_running.store(false, std::memory_order_release);
    g_guiOwnsMouse.store(false, std::memory_order_release);

    if (auto* renderTarget =
            reinterpret_cast<void*>(mcbe::signatures::renderContext()))
        MH_DisableHook(renderTarget);
    if (auto* mouseTarget =
            reinterpret_cast<void*>(mcbe::signatures::mouseRefresh()))
        MH_DisableHook(mouseTarget);
    if (auto* levelTarget =
            reinterpret_cast<void*>(mcbe::signatures::levelRenderer()))
        MH_DisableHook(levelTarget);
    if (auto* cameraTarget =
            reinterpret_cast<void*>(mcbe::signatures::cameraTick()))
        MH_DisableHook(cameraTarget);

    while (g_activeRenderCalls.load(std::memory_order_acquire) != 0 ||
           g_activeMouseCalls.load(std::memory_order_acquire) != 0 ||
           g_activeLevelRenderCalls.load(std::memory_order_acquire) != 0 ||
           g_activeCameraCalls.load(std::memory_order_acquire) != 0)
        Sleep(1);

    if (auto* renderTarget =
            reinterpret_cast<void*>(mcbe::signatures::renderContext()))
        MH_RemoveHook(renderTarget);
    if (auto* mouseTarget =
            reinterpret_cast<void*>(mcbe::signatures::mouseRefresh()))
        MH_RemoveHook(mouseTarget);
    if (auto* levelTarget =
            reinterpret_cast<void*>(mcbe::signatures::levelRenderer()))
        MH_RemoveHook(levelTarget);
    if (auto* cameraTarget =
            reinterpret_cast<void*>(mcbe::signatures::cameraTick()))
        MH_RemoveHook(cameraTarget);

    g_renderer.shutdown();

    if (ImGui::GetCurrentContext())
        ImGui::DestroyContext();

    MH_Uninitialize();

    CrashLog::append("Clean unload completed.\r\n");
    CrashLog::uninstall();

    Sleep(50);
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
