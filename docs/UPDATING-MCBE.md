# Updating for a new Bedrock build

A successful compile does not prove that the ABI is valid for a new Minecraft version.

## Revalidate signatures

Review every entry in `src/mcbe/Version/Signatures.hpp`, including:

- UI render target
- `mce::Mesh::_renderMesh`
- common/switchable render material groups
- `TextureGroup::uploadTexture`
- `cg::ImageResource` vtable
- mouse refresh and `MouseDevice`
- LevelRenderer
- camera tick

## Revalidate layouts

Current Bedrock 26.52 values:

| Field | Offset |
| --- | ---: |
| `ScreenContext::MeshContext` | `0x10` |
| `ScreenContext::Tessellator*` | `0xB8` |
| `MinecraftUIRenderContext::ScreenContext*` | `0x10` |
| `MinecraftUIRenderContext::TextureGroup` | `0x58` |
| `GuiData::mcResolution` | `0x40` |
| `GuiData::resolution` | `0x50` |
| `GuiData::scale` | `0x5C` |
| `GuiData::mousePos` | `0x7A` |
| `ClientInstance::GuiData*` | `0x650` |
| `LevelRenderer::playerRenderer*` | `0x468` |
| `LevelRendererPlayer::cameraPosition` | `0x660` |
| `CameraComponent::quat` | `0x30` |
| `CameraComponent::origin` | `0x40` |
| `sizeof(CameraComponent)` | `0x120` |
| `ScreenView::deltaTime` | `0x04` |
| `ScreenView::screenScale` | `0x10` |

Re-check all SDK `static_assert` sizes as well.

## Revalidate materials

Test `ui_textured`, `sign_text`, and `name_text_depth_tested` again. Material behavior can change independently of C++ layouts.

## Test checklist

1. Build Release with VS 2022 + ClangCL.
2. Confirm required addresses in `session.log`.
3. Verify the 2D window.
4. Verify mouse move/click/wheel/drag/resize.
5. Place an F6 panel and walk around it.
6. Put blocks between the camera and panel to verify depth.
7. Test multiple F6 panels and F7 removal.
8. Test moving progress bars and scrollbars.
9. Verify END unloads cleanly.

If a static assertion fails, fix the layout; do not delete the assertion.
