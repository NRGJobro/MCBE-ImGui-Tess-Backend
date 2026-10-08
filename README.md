# MCBE ImGui Tessellator Backend — Standalone Client

This is a **standalone injectable Windows x64 DLL** for Minecraft Bedrock **26.52**. It does **not** include, link to, or require Phase, Flow, Stray, or any other client project.

Phase/Flow were used only as references for the current MCBE 26.52 ABI/signatures. The required layouts and signatures are copied into this repository under `src/`.

## What it does

After injection it hooks Bedrock's current `ScreenView` / `MinecraftUIRenderContext` render path and draws a small Dear ImGui test window using:

`ImGui draw lists -> Minecraft Tessellator -> mce::Mesh -> im_gui material`

There is no DX11/DX12 ImGui renderer in this project.

The renderer batches adjacent ImGui commands that share a texture and clipping rectangle, pre-reserves Tessellator vertex/color/UV storage, honors `VtxOffset`/`IdxOffset`, and uses Minecraft's known-good `ui_textured` material first, with `im_gui` only as a fallback.

## Controls

- **INSERT** — show/hide the test ImGui window
- **END** — disable the hook and unload the DLL

## Build

### Toolchain ABI

Do **not** build this engine-facing DLL with the VS 2026 STL. MCBE 26.52/Phase uses the VS 2022-compatible STL ABI, and engine-facing types such as `ResourceLocation` contain `std::string`. The project contains compile-time size checks for `std::string`, `ResourceLocation`, `ImageBuffer`, `TextureDescription`, and `TextureContainer` so an incompatible toolchain fails at build time instead of crashing during `TextureGroup::uploadTexture`.

The CI intentionally matches Phase's current shipping build: **Visual Studio 17 2022 + ClangCL**.

**Do not inject a Debug build.** MSVC Debug STL changes the binary layout of engine-facing types such as `std::string` and `std::vector`. The project now rejects `_DEBUG` / `_ITERATOR_DEBUG_LEVEL != 0` builds at compile time. Use **Release** for injection.


Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with **Desktop development with C++** and the **Clang tools for Windows** component
- CMake 3.24+
- Internet access on the first configure so CMake can download pinned Dear ImGui + MinHook

From Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build --config Release
```

Output:

```text
build\bin\Release\MCBE-ImGui-Tess.dll
```

Inject the DLL into `Minecraft.Windows.exe` using your normal LoadLibrary-compatible injector. A console opens with signature/hook status. When successful, the Tessellator-backed ImGui window appears in-game.

## Standalone architecture

- `src/PatternScanner.hpp` — PE executable-section signature scanner.
- `src/MCBE.hpp` — only the current Bedrock structures required for this renderer (Tessellator, Mesh, texture upload, UI render context).
- `src/ImGuiTessBackend.hpp` — efficient ImGui draw-list -> Tessellator translator.
- `src/main.cpp` — DLL entry point, RenderContext hook, test window and unload handling.

## 26.52 data used

The current signatures were copied from the current Phase 26.52 source for:

- RenderContext hook target
- `mce::Mesh::_renderMesh`
- common `mce::RenderMaterialGroup`
- `mce::TextureGroup::uploadTexture`
- `cg::ImageResource` vtable

The current render-layout values used are:

- `ScreenContext::meshContext = 0x10`
- `ScreenContext::tessellator = 0xB8`
- `MinecraftUIRenderContext::ScreenContext = 0x10`
- `MinecraftUIRenderContext::textureGroup = 0x58`
- `ScreenView::ScreenScale = 0x10`

When Bedrock updates, these are the only version-sensitive pieces expected to need review.

## Notes

This project intentionally contains only a rendering test. It has no gameplay modules, networking modifications, key/auth system, or dependency on Phase's module framework.


## Crash diagnostics

The DLL installs a read-only vectored exception logger around its own render/backend work. It does **not** swallow access violations; Minecraft's normal exception handling still runs after the log is written.

Logs are stored in:

```text
%TEMP%\MCBE-ImGui-Tess\
```

Files:

- `session.log` — resolved 26.52 signature addresses and normal startup/unload information.
- `crash-last.log` — the most recent exception, backend stage, MCBE context/Tessellator pointers, registers, and stack addresses.
- `crash-last.dmp` — a small Windows minidump for crashes that need deeper inspection.
- `last-stage.log` — disk-flushed checkpoint of the last dangerous render operation. This is written even when Minecraft terminates before the exception logger can produce `crash-last.log`.

For a crash report, send `crash-last.log` and `session.log` first. The `Stage:` line is specifically updated around font upload, material creation, Tessellator begin/end, clipping, vertex emission, and `mce::Mesh::_renderMesh`.

