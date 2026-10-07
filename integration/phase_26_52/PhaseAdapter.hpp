#pragma once

// This adapter intentionally uses the current Phase-Client SDK surface instead
// of hard-coding offsets in the renderer. The host client remains responsible
// for keeping its 26.52 signatures/offsets current.

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <imgui.h>
#include <mcbe_imgui_tess/Backend.hpp>

// Add Phase-Client/src to your include path.
#include "SDK/MC.h"
#include "SDK/Core/GuiData.h"
#include "SDK/Core/MCE/Image.h"
#include "SDK/Core/CG/ImageBuffer.h"
#include "SDK/Render/MinecraftUIRenderContext.h"
#include "Phase/Utils/Render/Tessellator/Tess.h"

namespace mcbe::imgui_tess::phase26_52 {

class PhaseAdapter final {
public:
    void bind(MinecraftUIRenderContext* context) {
        context_ = context;
        Tess::setTessellator2D(context);
    }

    [[nodiscard]] bool ready() const {
        return context_ && context_->textureGroup &&
               Tess::getScreenContext2D() && Tess::getTessellator2D() &&
               renderMaterial();
    }

    [[nodiscard]] ImVec2 displaySizePixels() const {
        const auto* gui = mc::getGuiData();
        if (!gui)
            return ImVec2(0.0f, 0.0f);

        Vec2 px = gui->getmcResolution();
        if (px.x <= 1.0f || px.y <= 1.0f) {
            const Vec2 logical = gui->getResolution();
            const float scale = std::max(gui->getScale(), 1.0f);
            px = {logical.x * scale, logical.y * scale};
        }
        return ImVec2(px.x, px.y);
    }

    [[nodiscard]] float uiScale() const {
        const auto* gui = mc::getGuiData();
        return gui ? std::max(gui->getScale(), 1.0e-6f) : 1.0f;
    }

    bool createFontTexture(const unsigned char* pixels, int width, int height, int bytesPerPixel) {
        if (!ready() || !pixels || width <= 0 || height <= 0 || bytesPerPixel != 4)
            return false;

        mce::Image image{};
        image.imageFormat = mce::ImageFormat::RGBA8Unorm;
        image.width = static_cast<std::uint32_t>(width);
        image.height = static_cast<std::uint32_t>(height);
        image.depth = 1;
        image.usage = mce::ImageUsage::sRGB;

        const std::size_t bytes = static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height) * 4u;
        auto* copy = new std::uint8_t[bytes];
        std::memcpy(copy, pixels, bytes);
        image.imageData.blob.reset(copy);
        image.imageData.size = bytes;

        cg::ImageBuffer buffer(image);
        if (!buffer.isValid())
            return false;

        // Unload a stale atlas before replacing it (important after DLL hot reload).
        context_->textureGroup->unloadTexture(fontLocation_);
        BedrockTexture& uploaded = context_->textureGroup->uploadTexture(fontLocation_, buffer);
        if (!uploaded.texture || !uploaded.texture->clientTexture.resourcePointerBlock)
            return false;

        fontTexture_ = uploaded.texture->clientTexture;
        return static_cast<bool>(fontTexture_.resourcePointerBlock);
    }

    [[nodiscard]] ImTextureID fontTextureId() const {
        return const_cast<mce::ClientTexture*>(&fontTexture_);
    }

    void beginFrame() {
        // Rebind every frame because ScreenContext/Tessellator can change when
        // joining/leaving worlds, changing dimensions, or rebuilding the UI.
        if (context_)
            Tess::setTessellator2D(context_);
    }

    void endFrame() {}

    void resetRenderState() {
        if (context_)
            Tess::setTessellator2D(context_);
    }

    void beginBatch(std::size_t vertexCount, const ClipRect& clip) {
        auto* tess = Tess::getTessellator2D();
        if (!tess || !context_)
            return;

        tess->begin(mce::PrimitiveMode::TriangleList,
                    static_cast<int>(std::min<std::size_t>(vertexCount, static_cast<std::size_t>(INT_MAX))));

        // Tessellator::begin only pre-reserves positions. Reserve the other two
        // hot arrays too so large menus don't repeatedly grow vectors.
        tess->meshData.colors.reserve(tess->meshData.colors.size() + vertexCount);
        tess->meshData.textureUVs[0].reserve(tess->meshData.textureUVs[0].size() + vertexCount);

        context_->saveCurrentClippingRectangle();
        // Phase Rect uses x=left, y=right, z=top, w=bottom.
        context_->setClippingRectangle(Rect(clip.left, clip.right, clip.top, clip.bottom));
        clipSaved_ = true;
    }

    inline void emitVertex(float x, float y, float u, float v, ImU32 color) {
        auto* tess = Tess::getTessellator2D();
        if (!tess)
            return;
        // ImGui packs RGBA8 exactly the way the current Phase Tessellator::color
        // overload expects when passed as bytes, so avoid float conversion.
        const auto r = static_cast<std::uint8_t>( color        & 0xFFu);
        const auto g = static_cast<std::uint8_t>((color >> 8)  & 0xFFu);
        const auto b = static_cast<std::uint8_t>((color >> 16) & 0xFFu);
        const auto a = static_cast<std::uint8_t>((color >> 24) & 0xFFu);
        tess->color(r, g, b, a);
        tess->vertexUV(x, y, 0.0f, u, v);
    }

    void flushBatch(ImTextureID textureId) {
        auto* tess = Tess::getTessellator2D();
        auto* screen = Tess::getScreenContext2D();
        auto* material = renderMaterial();

        const mce::ClientTexture* texture = textureId
            ? static_cast<const mce::ClientTexture*>(textureId)
            : &fontTexture_;
        if (!texture || !texture->resourcePointerBlock)
            texture = &fontTexture_;

        if (tess && screen && material && texture->resourcePointerBlock)
            MeshHelpers::renderMeshImmediately(screen, tess, material, *texture);
        else if (tess)
            tess->clear();

        if (clipSaved_ && context_) {
            context_->restoreSavedClippingRectangle();
            clipSaved_ = false;
        }
    }

private:
    static mce::MaterialPtr* renderMaterial() {
        // MCBE 26.52 exposes a dedicated common `im_gui` material. Prefer it
        // because it is intended for this exact UI geometry path; fall back to
        // Phase's known-good `ui_textured` material if it cannot be resolved.
        static mce::MaterialPtr* nativeImGui =
            mce::MaterialPtr::createMaterial(HashedString("im_gui"));
        return nativeImGui ? nativeImGui : Tess::getUiTexturedMaterial();
    }

    MinecraftUIRenderContext* context_{};
    ResourceLocation fontLocation_{"phase/imgui_tess/font_atlas_26_52"};
    mce::ClientTexture fontTexture_{};
    bool clipSaved_{};
};

} // namespace mcbe::imgui_tess::phase26_52
