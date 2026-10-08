#pragma once

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

// These are engine-facing STL ABI checks. Bedrock 26.52 is built against
// the VS 2022-compatible MSVC STL layout. A newer incompatible STL makes
// ResourceLocation/HashedString larger and crashes engine calls that take them.
static_assert(sizeof(std::string) == 0x20,
    "Incompatible MSVC STL ABI: build with Visual Studio 2022/v143 (Phase toolchain).");
static_assert(sizeof(HashedString) == 0x30,
    "HashedString ABI mismatch.");
static_assert(sizeof(ResourceLocation) == 0x38,
    "ResourceLocation ABI mismatch: expected MCBE 26.52 size 0x38.");

