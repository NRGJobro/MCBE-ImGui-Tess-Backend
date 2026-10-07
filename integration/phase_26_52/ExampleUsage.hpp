#pragma once

#include "PhaseAdapter.hpp"

namespace McImGuiTessExample {

inline mcbe::imgui_tess::phase26_52::PhaseAdapter gAdapter;
inline mcbe::imgui_tess::Backend<mcbe::imgui_tess::phase26_52::PhaseAdapter> gBackend{gAdapter};
inline bool gInitialized = false;

// Call this from a point where MinecraftUIRenderContext is valid for the current
// UI frame (the same place Phase currently captures its 2D ScreenContext).
inline void onRender(MinecraftUIRenderContext* ctx, float deltaSeconds) {
    gAdapter.bind(ctx);
    if (!gInitialized)
        gInitialized = gBackend.initialize();
    if (!gInitialized)
        return;

    gBackend.newFrame(deltaSeconds);
    ImGui::NewFrame();

    // Test window. Replace with the client's normal ImGui draw calls.
    ImGui::Begin("MCBE Tessellator ImGui");
    ImGui::TextUnformatted("Rendered through Minecraft's native Tessellator path.");
    ImGui::Text("FPS %.1f", ImGui::GetIO().Framerate);
    ImGui::End();

    ImGui::Render();
    (void)gBackend.render(ImGui::GetDrawData());
}

inline void shutdown() {
    if (gInitialized)
        gBackend.shutdown();
    gInitialized = false;
}

} // namespace McImGuiTessExample
