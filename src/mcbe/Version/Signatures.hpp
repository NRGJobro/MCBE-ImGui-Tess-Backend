#pragma once

#include "../Memory/PatternScanner.hpp"

#include <cstdint>

namespace mcbe::signatures {
inline std::uintptr_t renderContext() {
    static const auto value = memory::resolveCall(memory::scan(
        "E8 ? ? ? ? 48 8B 4B ? 48 85 C9 74 ? 48 8B 01 48 8B 40 ? 48 89 FA FF 15 ? ? ? ? 48 8D 4D"));
    return value;
}
inline std::uintptr_t meshRender() {
    static const auto value = memory::resolveCall(memory::scan(
        "E8 ? ? ? ? F3 0F 5C F7 F3 0F 58 F7"));
    return value;
}
inline std::uintptr_t materialCommon() {
    static const auto value = memory::resolveRip(memory::scan(
        "48 8D 15 ? ? ? ? 4C 8D 45 ? E8 ? ? ? ? 48 8D 4D ? E8 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? E9 ? ? ? ? 48 8D 0D"), 3);
    return value;
}
inline std::uintptr_t materialSwitchable() {
    static const auto value = memory::resolveRip(memory::scan(
        "48 8D 15 ? ? ? ? 4D 89 F8 E8 ? ? ? ? 4C 89 F9 E8 ? ? ? ? 4C 89 E9 E8 ? ? ? ? 48 8B 85"), 3);
    return value;
}
inline std::uintptr_t textureUpload() {
    static const auto value = memory::scan(
        "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 4D 89 CF 4D 89 C6 48 89 D6 48 89 CB");
    return value;
}
inline std::uintptr_t imageResourceVtable() {
    static const auto value = memory::resolveRip(memory::scan(
        "48 8D 0D ? ? ? ? 48 89 4B ? 48 89 D9"), 3);
    return value;
}
inline std::uintptr_t windowProcCallback() {
    // Bedrock 26.52 target: MainWindow::_windowProcCallback / WindowProcCallbackHook::keymapSig.
    static const auto value = memory::scan(
        "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 89 D6 4C 8B 3D");
    return value;
}
inline std::uintptr_t mouseRefresh() {
    // Bedrock 26.52 target: GameControllerHandler_GameCore::refresh.
    static const auto value = memory::scan(
        "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 BD ? ? ? ? 44 0F 29 B5 ? ? ? ? 44 0F 29 AD ? ? ? ? 44 0F 29 A5 ? ? ? ? 44 0F 29 9D ? ? ? ? 44 0F 29 95 ? ? ? ? 44 0F 29 8D ? ? ? ? 44 0F 29 85 ? ? ? ? 0F 29 BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 CE 8B 05");
    return value;
}
inline std::uintptr_t mouseDevice() {
    // Bedrock 26.52 target: MouseDevice::instance. The RIP-relative target is the live
    // MouseDevice storage itself, matching Phase's current SDK accessor.
    static const auto value = memory::resolveRip(memory::scan(
        "89 15 ? ? ? ? C7 47"), 2);
    return value;
}
inline std::uintptr_t levelRenderer() {
    // Bedrock 26.52 target: LevelRendererHook::levelRendererHookSig.
    static const auto value = memory::resolveCall(memory::scan(
        "E8 ? ? ? ? 45 31 E4 48 83 BE"));
    return value;
}
inline std::uintptr_t cameraTick() {
    // Bedrock 26.52 target: CameraOriginHook::tickSig / CameraBlendSystem::tick.
    static const auto value = memory::scan(
        "41 57 41 56 41 54 56 57 55 53 48 81 EC ? ? ? ? 44 0F 29 BC 24 ? ? ? ? 44 0F 29 B4 24 ? ? ? ? 44 0F 29 AC 24 ? ? ? ? 44 0F 29 A4 24 ? ? ? ? 44 0F 29 9C 24 ? ? ? ? 44 0F 29 94 24 ? ? ? ? 44 0F 29 8C 24 ? ? ? ? 44 0F 29 44 24 ? 0F 29 7C 24 ? 0F 29 74 24 ? 0F 28 F2");
    return value;
}
inline bool requiredReady() {
    return renderContext() && meshRender() && materialCommon() &&
           textureUpload() && imageResourceVtable();
}
} // namespace mcbe::signatures

