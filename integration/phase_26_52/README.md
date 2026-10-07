# Phase 26.52 adapter

This folder targets the current `NRGJobro/Phase-Client` rendering SDK surface for Bedrock 26.52.

## Integration

Add these include roots to the client project:

- this repo's `include/`
- `integration/phase_26_52/`
- `Phase-Client/src/`
- Phase's existing ImGui include directory

Then include `ExampleUsage.hpp` from the render hook where `MinecraftUIRenderContext*` is valid.

The backend deliberately does **not** own or scan Minecraft offsets. It uses Phase's current `OffsetManager`/signature-backed SDK. That makes renderer code independent of game updates: update the host SDK once and the backend continues to compile.

For `ImGui::Image`, use an `mce::ClientTexture*` as `ImTextureID`. The font atlas is uploaded automatically through `mce::TextureGroup::uploadTexture`.
