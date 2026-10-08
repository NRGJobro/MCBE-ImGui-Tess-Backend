# Contributing

## Scope

Keep changes focused on the standalone ImGui/Tessellator renderer, native input, world-space proof demo, diagnostics, and the minimal Bedrock ABI they require.

## Build

```powershell
cmake --preset vs2022-clangcl
cmake --build --preset release
```

Debug builds are intentionally rejected.

## PR checklist

- State the Bedrock build tested.
- Keep signatures/offsets in the `mcbe/` ABI layer.
- Preserve ABI `static_assert` checks.
- Keep ImGui calls on the render thread.
- Test 2D rendering and native mouse input.
- Test F6/F7 world panels and block occlusion.
- Test progress bars and scrollbars after material changes.
- Verify END unloads cleanly.
- Include diagnostics for crash fixes.

Only contribute code you have the right to redistribute.
