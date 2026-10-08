#pragma once

#include "Tessellator.hpp"
#include "Textures.hpp"

class ScreenContext {
public:
    mce::MeshContext* toMeshContext() {
        return reinterpret_cast<mce::MeshContext*>(reinterpret_cast<std::uintptr_t>(this) + 0x10);
    }
    Tessellator* getTessellator() {
        return *reinterpret_cast<Tessellator**>(reinterpret_cast<std::uintptr_t>(this) + 0xB8);
    }
};

class GuiData {
public:
    Vec2 getMcResolution() const {
        return *reinterpret_cast<const Vec2*>(reinterpret_cast<std::uintptr_t>(this) + 0x40);
    }
    Vec2 getResolution() const {
        return *reinterpret_cast<const Vec2*>(reinterpret_cast<std::uintptr_t>(this) + 0x50);
    }
    float getScale() const {
        return *reinterpret_cast<const float*>(reinterpret_cast<std::uintptr_t>(this) + 0x5C);
    }
    Vec2 getMousePos() const {
        return *reinterpret_cast<const Vec2*>(reinterpret_cast<std::uintptr_t>(this) + 0x7A);
    }
};

class ClientInstance {
public:
    GuiData* getGuiData() const {
        return *reinterpret_cast<GuiData* const*>(reinterpret_cast<std::uintptr_t>(this) + 0x650);
    }
};

class MinecraftUIRenderContext {
public:
    void** vtable{};                  // 0x00
    ClientInstance* clientInstance{}; // 0x08
    ScreenContext* screenContext{};   // 0x10
    std::byte pad_18[0x40]{};         // 0x18 -> 0x58
    std::shared_ptr<mce::TextureGroup> textureGroup; // 0x58

    void setClippingRectangle(const Rect& rect) {
        mcbe::abi::callVFunc<22, void>(this, static_cast<const Rect&>(rect));
    }

    void saveCurrentClippingRectangle() {
        mcbe::abi::callVFunc<24, void>(this);
    }

    void restoreSavedClippingRectangle() {
        mcbe::abi::callVFunc<25, void>(this);
    }

    mce::TexturePtr getTexture(const ResourceLocation& location, bool forceReload) {
        return mcbe::abi::callVFunc<31, mce::TexturePtr>(
            this, static_cast<const ResourceLocation&>(location), forceReload);
    }
};
static_assert(offsetof(MinecraftUIRenderContext, textureGroup) == 0x58);

struct ScreenView {
    std::byte pad_00[0x4]{};
    float deltaTime{};                 // 0x04
    std::byte pad_08[0x8]{};
    Vec2 screenScale{};                // 0x10
};
static_assert(offsetof(ScreenView, screenScale) == 0x10);


