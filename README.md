# MCBE ImGui Tessellator Backend

A small, rendering-only Dear ImGui backend for Minecraft Bedrock Edition that sends ImGui geometry through Minecraft's native `Tessellator`/`mce::Mesh` path instead of DX11/DX12 ImGui backends.

Target reference: **Minecraft Bedrock 26.52**. The included Phase adapter is based on the current `Phase-Client` SDK surface rather than old 1.21.0.3 layouts.

## Why this backend

Older MCBE ImGui/Tessellator backends commonly create and flush one Minecraft mesh for every `ImDrawCmd`. This backend instead:

- merges adjacent commands when texture + clip state match;
- pre-reserves position, color, and UV vectors for the whole batch;
- uses a header-only/template hot loop so per-vertex calls inline into the adapter;
- honors `VtxOffset` / `IdxOffset` and ImGui reset callbacks;
- uploads the font atlas directly through `TextureGroup::uploadTexture` (no resource-pack PPM round trip);
- prefers MCBE 26.52's dedicated common `im_gui` material, falling back to `ui_textured`;
- rebinds the current `ScreenContext`/`Tessellator` each frame to survive world/dimension/UI context changes;
- keeps version-specific game SDK details outside the renderer core.

## Layout

- `include/mcbe_imgui_tess/Backend.hpp` — generic high-performance ImGui draw-data translator.
- `integration/phase_26_52/PhaseAdapter.hpp` — adapter for the current Phase SDK / MCBE 26.52.
- `integration/phase_26_52/ExampleUsage.hpp` — minimal test window integration.
- `tests/` — standalone batching/coordinate test using a tiny fake ImGui ABI.

## Build the standalone core test

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Phase integration

See `integration/phase_26_52/README.md`. The renderer deliberately contains no injection code and no independent offset scanner; it consumes the host client's current render SDK.

### Texture IDs

The Phase adapter treats `ImTextureID` as `mce::ClientTexture*`. The font atlas is registered automatically. For custom images, pass a stable `mce::ClientTexture*` to `ImGui::Image` / `AddImage`.

## Performance notes

The main remaining cost is Minecraft's own mesh submission. For menus that use many different clip rectangles or textures, each state change still requires a mesh flush. Keeping UI atlases consolidated and avoiding unnecessary nested clipping gives the best results.

## Material choice

The 26.52 material table contains a dedicated common material named `im_gui`. The Phase adapter prefers it and falls back to `ui_textured` if it cannot be resolved. This keeps the generic backend independent of material discovery while using Minecraft's purpose-built path when available.
