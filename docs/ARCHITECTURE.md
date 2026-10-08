# Architecture

## Components

- `DllMain.cpp` owns startup, hooks, ImGui frame creation, input bridging, and clean unload.
- `backend/ImGuiTessellatorBackend.hpp` translates ImGui draw lists into Tessellator meshes and owns uploaded textures/materials.
- `demo/WorldPanelDemo.hpp` stores world-panel transforms and the F6/F7 stack.
- `mcbe/` contains only the engine ABI surface required by this project.
- `diagnostics/CrashDiagnostics.hpp` records render stages, exceptions, and minidumps.

## 2D path

```text
ImGui::NewFrame
 -> demo widgets
 -> ImGui::Render
 -> ImGuiTessellatorBackend::render
 -> UI ScreenContext Tessellator
 -> transient mce::Mesh
 -> mce::Mesh::_renderMesh
```

The font atlas is uploaded with Minecraft's `TextureGroup::uploadTexture`.

## Input

The project does not use Dear ImGui's Win32 platform backend. Minecraft's `MouseDevice` refresh path is hooked and publishes mouse position/buttons/wheel through atomics. ImGui consumes those values on the render thread.

## World-space path

F6 stores a panel center/right/up basis from the rendered camera pose. During LevelRenderer submission:

```text
world = panelCenter
      + panelRight * localX
      + panelUp    * localY
```

The current render origin is subtracted before mesh submission. F7 removes panels in last-in-first-out order.

## Materials

Current Bedrock 26.52 world rendering uses separate paths for solid UI geometry and glyphs:

- solid geometry: `sign_text`
- glyphs: `name_text_depth_tested`

A small uniform white texture is used for selected solid UI elements so texture filtering cannot bleed into neighboring font-atlas pixels.

These material names are version-sensitive implementation details.

## Unload safety

Hooks are disabled before removal. Active detour counters are drained before the ImGui context and hook library are destroyed. ImGui APIs are only called from the render path.
