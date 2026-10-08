#pragma once

#include "../BedrockSDK.hpp"

#include <cstdint>

namespace mcbe::world {

class LevelRendererPlayer {
public:
    Vec3 cameraPosition() const {
        return *reinterpret_cast<const Vec3*>(
            reinterpret_cast<std::uintptr_t>(this) + 0x660);
    }
};

class LevelRenderer {
public:
    LevelRendererPlayer* playerRenderer() const {
        return *reinterpret_cast<LevelRendererPlayer* const*>(
            reinterpret_cast<std::uintptr_t>(this) + 0x468);
    }

    Vec3 origin() const {
        const auto* player = playerRenderer();
        return player ? player->cameraPosition() : Vec3{};
    }
};

} // namespace mcbe::world
