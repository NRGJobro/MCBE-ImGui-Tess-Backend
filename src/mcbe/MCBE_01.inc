#pragma once

#include "PatternScanner.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(_DEBUG)
#error "MCBE engine ABI mismatch: do not inject a Debug STL build. Use Release or RelWithDebInfo."
#endif

#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
#error "MCBE engine ABI mismatch: _ITERATOR_DEBUG_LEVEL must be 0."
#endif

struct Vec2 {
    float x{}, y{};
};
struct Vec3 {
    float x{}, y{}, z{};
};
struct Vec4 {
    float x{}, y{}, z{}, w{};
};
struct Mat4 {
    float m[16]{};
};

struct Rect {
    // Bedrock's UI rectangle ABI is left, right, top, bottom.
    float left{}, right{}, top{}, bottom{};
};

namespace mcbe::abi {

template <std::size_t Index, typename Return, typename Instance, typename... Args>
Return callVFunc(Instance* instance, Args&&... args) {
    if (!instance) {
        if constexpr (std::is_void_v<Return>)
            return;
        else
            return Return{};
    }

    auto** vtable = *reinterpret_cast<void***>(instance);
    if (!vtable || !vtable[Index]) {
        if constexpr (std::is_void_v<Return>)
            return;
        else
            return Return{};
    }

    using Method = Return(Instance::*)(Args...);
    static_assert(sizeof(Method) == sizeof(void*),
        "This helper expects the MSVC/ClangCL single-inheritance member-function ABI.");

    const auto method = std::bit_cast<Method>(vtable[Index]);
    if constexpr (std::is_void_v<Return>) {
        (instance->*method)(std::forward<Args>(args)...);
        return;
    } else {
        return (instance->*method)(std::forward<Args>(args)...);
    }
}

} // namespace mcbe::abi

template <typename T, std::size_t Size>
class StaticVector {
    T data_[Size]{};
    std::size_t size_{};
public:
    void push_back(const T& value) {
        if (size_ < Size) data_[size_++] = value;
    }
    std::size_t size() const { return size_; }
};

class HashedString {
public:
    std::uint64_t hash{};
    std::string str{};
    HashedString* lastMatch{};

    static constexpr std::uint64_t computeHash(std::string_view value) {
        std::uint64_t result = 0xCBF29CE484222325ULL;
        for (char c : value) result = static_cast<unsigned char>(c) ^ (0x100000001B3ULL * result);
        return result;
    }

    HashedString() = default;
    explicit HashedString(const char* value) : hash(computeHash(value)), str(value), lastMatch(nullptr) {}
};

enum class ResourceFileSystem : int {
    UserPackage, AppPackage, Raw, RawPersistent, SettingsDir, ExternalDir,
    ServerPackage, DataDir, UserDir, ScreenshotsDir, StoreCache, Invalid
};

class ResourceLocation {
public:
    ResourceFileSystem fileSystem{ResourceFileSystem::UserPackage};
    std::string path{};
    std::uint64_t pathHash{};
    std::uint64_t fullHash{};

    ResourceLocation() { compute(); }
    explicit ResourceLocation(std::string value) : path(std::move(value)) { compute(); }

    void compute() {
        pathHash = HashedString::computeHash(path);
        fullHash = pathHash ^ static_cast<std::uint64_t>(fileSystem);
    }
};

// These are engine-facing STL ABI checks. Minecraft/Phase 26.52 is built against
// the VS 2022-compatible MSVC STL layout. A newer incompatible STL makes
// ResourceLocation/HashedString larger and crashes engine calls that take them.
static_assert(sizeof(std::string) == 0x20,
    "Incompatible MSVC STL ABI: build with Visual Studio 2022/v143 (Phase toolchain).");
static_assert(sizeof(HashedString) == 0x30,
    "HashedString ABI mismatch.");
static_assert(sizeof(ResourceLocation) == 0x38,
    "ResourceLocation ABI mismatch: expected MCBE 26.52 size 0x38.");

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
inline bool requiredReady() {
    return renderContext() && meshRender() && materialCommon() && textureUpload() && imageResourceVtable();
}
} // namespace mcbe::signatures

namespace mce {

template <typename T>
struct ResourceBlockTemplate;

template <typename T>
struct ResourcePointer {
    void* vtable{};
    std::shared_ptr<ResourceBlockTemplate<T>> resourcePointerBlock{};
};

template <typename T>
struct ClientResourcePointer : ResourcePointer<T> {};

struct ClientTexture : ClientResourcePointer<void*> {};
struct BufferResourceService {};
struct MeshContext {};

class BedrockTextureData;

class TexturePtr {
