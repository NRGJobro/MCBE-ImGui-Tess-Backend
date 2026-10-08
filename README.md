# MCBE ImGui Tessellator Backend

Standalone experimental Dear ImGui renderer for **Minecraft Bedrock 26.52** that submits ImGui draw data through Minecraft's own **Tessellator / mce::Mesh** path.

> **Version locked:** signatures, offsets, ABI layouts, and render materials in this repository target Bedrock 26.52.

## Highlights

- No Dear ImGui DX11/DX12 renderer backend.
- Native Minecraft mouse input.
- 2D in-game ImGui rendering through the UI Tessellator context.
- F6 world-space proof demo using the 3D LevelRenderer context.
- Multiple world panels; F7 removes the newest first.
- World panels participate in depth testing and can be occluded by blocks.
- Crash-stage logs and Windows minidumps.
- Visual Studio 2022 + ClangCL CI build.

## Controls

| Key | Action |
| --- | --- |
| `INSERT` | Show/hide the normal 2D window |
| `F6` | Place another world-space panel |
| `F7` | Remove the newest world-space panel |
| `END` | Disable hooks and unload |

## Build

Requirements: Windows x64, Visual Studio 2022, Desktop development with C++, Clang tools for Windows, CMake 3.24+.

```powershell
cmake --preset vs2022-clangcl
cmake --build --preset release
```

Output:

```text
build\bin\Release\MCBE-ImGui-Tess.dll
```

**Do not inject Debug builds.** Engine-facing MSVC STL layouts differ with iterator debugging enabled; the project intentionally rejects incompatible Debug builds.

## How it renders

2D:

```text
ImGui draw lists
 -> MinecraftUIRenderContext
 -> Tessellator
 -> mce::Mesh
 -> Minecraft material
```

World-space:

```text
ImGui vertex
 -> panel-local coordinates
 -> world center/right/up basis
 -> LevelRenderer ScreenContext
 -> Tessellator
 -> mce::Mesh
```

The F6 panel is fixed world geometry, not a desktop overlay projected onto the screen.

## Source layout

```text
src/
├── DllMain.cpp
├── backend/ImGuiTessellatorBackend.hpp
├── demo/WorldPanelDemo.hpp
├── diagnostics/CrashDiagnostics.hpp
└── mcbe/
    ├── BedrockSDK.hpp
    ├── Core/Types.hpp
    ├── Memory/PatternScanner.hpp
    ├── Version/Signatures.hpp
    ├── Input/MouseDevice.hpp
    └── Render/
        ├── CameraComponent.hpp
        ├── LevelRenderer.hpp
        ├── Mesh.hpp
        ├── Resources.hpp
        ├── Tessellator.hpp
        ├── Textures.hpp
        └── UIContext.hpp
```

More detail:

- [Architecture](docs/ARCHITECTURE.md)
- [Updating for a new Bedrock build](docs/UPDATING-MCBE.md)
- [Crash reports](docs/CRASH-REPORTS.md)
- [Contributing](CONTRIBUTING.md)

## Diagnostics

Logs are written to:

```text
%TEMP%\MCBE-ImGui-Tess\
```

Start with `session.log`, `last-stage.log`, and `crash-last.log`. Review minidumps before uploading them publicly because they may contain process memory or local paths.

## Scope

This repository contains the renderer, input bridge, proof demo, diagnostics, and only the minimal Bedrock ABI needed by those components. It intentionally does not contain gameplay modules, authentication/licensing code, or another client's framework.

## License

A license has **not been selected yet**. Add an OSI-approved license before presenting the repository as open source.

## Disclaimer

Independent community project; not affiliated with or endorsed by Mojang Studios or Microsoft. Minecraft is a trademark of Microsoft.
