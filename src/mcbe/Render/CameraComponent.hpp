#pragma once

#include "../../MCBE.hpp"

#include <cstdint>

namespace mcbe::world {

struct Quaternion {
    float w{1.f};
    float x{};
    float y{};
    float z{};
};
static_assert(sizeof(Quaternion) == 0x10);

struct CameraComponent {
    HashedString viewName{};   // 0x000
    Quaternion quat{};         // 0x030
    Vec3 origin{};             // 0x040
    Vec4 fov{};                // 0x04C
    Mat4 world{};              // 0x05C
    Mat4 view{};               // 0x09C
    Mat4 projection{};         // 0x0DC
    std::uint8_t padding[4]{}; // 0x11C
};

static_assert(offsetof(CameraComponent, quat) == 0x30);
static_assert(offsetof(CameraComponent, origin) == 0x40);
static_assert(sizeof(CameraComponent) == 0x120);

} // namespace mcbe::world
