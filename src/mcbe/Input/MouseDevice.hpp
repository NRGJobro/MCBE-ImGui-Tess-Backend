#pragma once

#include "../MCBE.hpp"

#include <cstdint>
#include <vector>

struct MouseAction {
    std::int16_t x{};
    std::int16_t y{};
    std::int16_t dx{};
    std::int16_t dy{};
    std::int8_t action{};
    std::int8_t data{};
    int pointerId{};
    bool forceMotionlessPointer{};
};

class MouseDevice {
public:
    std::int16_t clickX{};
    std::int16_t clickY{};
    std::int16_t x{};
    std::int16_t y{};
    std::int16_t dx{};
    std::int16_t dy{};
    std::int16_t xOld{};
    std::int16_t yOld{};
    bool buttonStates[7]{};
    std::vector<MouseAction> inputs;
    std::int32_t firstMovementType{};

    static MouseDevice* get() {
        return reinterpret_cast<MouseDevice*>(mcbe::signatures::mouseDevice());
    }
};
