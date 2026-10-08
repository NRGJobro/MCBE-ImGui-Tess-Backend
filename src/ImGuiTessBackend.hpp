#pragma once

#include "MCBE.hpp"
#include <imgui.h>

#include <algorithm>
#include <climits>
#include <cstdint>

namespace mcbe {

class ImGuiTessBackend {
public:
    bool initialize(MinecraftUIRenderContext* ctx) {
        if (initialized_) return true;
        if (!ctx || !ctx->textureGroup || !ctx->screenContext) return false;
        if (!signatures::requiredReady()) return false;

        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0, height = 0, bytesPerPixel = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytesPerPixel);
        if (!pixels || width <= 0 || height <= 0 || bytesPerPixel != 4) return false;

        mce::Image image{};
        image.imageFormat = mce::ImageFormat::RGBA8Unorm;
        image.width = static_cast<std::uint32_t>(width);
        image.height = static_cast<std::uint32_t>(height);
        image.depth = 1;
        image.usage = mce::ImageUsage::sRGB;

        const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        auto* copy = new std::uint8_t[byteCount];
        std::memcpy(copy, pixels, byteCount);
        image.imageData = mce::Blob(copy, byteCount);

        cg::ImageBuffer buffer(image);
        if (!buffer.isValid()) return false;
        mce::TextureContainer container(buffer);

        ResourceLocation location("imgui_tess/font_atlas_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        auto& uploaded = ctx->textureGroup->uploadTexture(
            location, container, std::optional<std::string_view>{"MCBE ImGui Tess font"});
        if (!uploaded.texture || !uploaded.texture->clientTexture.resourcePointerBlock) return false;

        fontTexture_ = uploaded.texture->clientTexture;
        io.Fonts->SetTexID(toTextureId(&fontTexture_));
        io.BackendRendererName = "mcbe_tessellator_26_52";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

        material_ = mce::MaterialPtr::createMaterial(HashedString("im_gui"));
        if (!material_) material_ = mce::MaterialPtr::createMaterial(HashedString("ui_textured"));

        initialized_ = material_ != nullptr;
        return initialized_;
    }

    void shutdown() {
        if (!initialized_) return;
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = nullptr;
        io.Fonts->SetTexID(static_cast<ImTextureID>(0));
        material_ = nullptr;
        initialized_ = false;
    }

    bool initialized() const { return initialized_; }

    void render(ImDrawData* drawData, MinecraftUIRenderContext* ctx) {
        if (!initialized_ || !drawData || !ctx || !ctx->screenContext || drawData->CmdListsCount <= 0) return;
        auto* tess = ctx->screenContext->getTessellator();
        if (!tess || !material_) return;

        for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
            const ImDrawList* list = drawData->CmdLists[listIndex];
            if (!list) continue;

            int commandIndex = 0;
            while (commandIndex < list->CmdBuffer.Size) {
                const ImDrawCmd& first = list->CmdBuffer[commandIndex];
                if (first.UserCallback) {
                    if (first.UserCallback != ImDrawCallback_ResetRenderState)
                        first.UserCallback(list, &first);
                    ++commandIndex;
                    continue;
                }

                const ClipRect clip = makeClip(first.ClipRect, drawData->DisplayPos);
                if (!clip.valid() || first.ElemCount < 3) {
                    ++commandIndex;
                    continue;
                }

                ImTextureID texture = first.TextureId ? first.TextureId : toTextureId(&fontTexture_);
                std::uint64_t totalElements = first.ElemCount;
                int runEnd = commandIndex + 1;

                for (; runEnd < list->CmdBuffer.Size; ++runEnd) {
                    const ImDrawCmd& next = list->CmdBuffer[runEnd];
                    if (next.UserCallback || next.ElemCount < 3) break;
                    const ClipRect nextClip = makeClip(next.ClipRect, drawData->DisplayPos);
                    ImTextureID nextTexture = next.TextureId ? next.TextureId : toTextureId(&fontTexture_);
                    if (!(nextClip == clip) || nextTexture != texture) break;
                    totalElements += next.ElemCount;
                }

                const int reserve = static_cast<int>(std::min<std::uint64_t>(totalElements, INT_MAX));
                tess->begin(mce::PrimitiveMode::TriangleList, reserve);
                tess->meshData.colors.reserve(tess->meshData.colors.size() + static_cast<std::size_t>(reserve));
                tess->meshData.textureUVs[0].reserve(tess->meshData.textureUVs[0].size() + static_cast<std::size_t>(reserve));

                ctx->saveCurrentClippingRectangle();
                ctx->setClippingRectangle(Rect{clip.left, clip.right, clip.top, clip.bottom});

                for (int emit = commandIndex; emit < runEnd; ++emit)
                    emitCommand(*tess, *list, list->CmdBuffer[emit], drawData->DisplayPos);

                const auto* clientTexture = fromTextureId(texture);
                if (!clientTexture || !clientTexture->resourcePointerBlock)
                    clientTexture = &fontTexture_;

                if (clientTexture && clientTexture->resourcePointerBlock) {
                    mce::Mesh mesh{};
                    tess->end(mesh);
                    mesh.renderMesh(ctx->screenContext->toMeshContext(), material_, *clientTexture);
                } else {
                    tess->clear();
                }

                ctx->restoreSavedClippingRectangle();
                commandIndex = runEnd;
            }
        }
    }

private:
    static ImTextureID toTextureId(const mce::ClientTexture* texture) {
        return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
    }

    static const mce::ClientTexture* fromTextureId(ImTextureID texture) {
        return reinterpret_cast<const mce::ClientTexture*>(static_cast<std::uintptr_t>(texture));
    }

    struct ClipRect {
        float left{}, right{}, top{}, bottom{};
        bool valid() const { return right > left && bottom > top; }
        friend bool operator==(const ClipRect& a, const ClipRect& b) {
            return a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
        }
    };

    static ClipRect makeClip(const ImVec4& value, const ImVec2& displayPos) {
        return {
            value.x - displayPos.x,
            value.z - displayPos.x,
            value.y - displayPos.y,
            value.w - displayPos.y
        };
    }

    static void emitCommand(Tessellator& tess, const ImDrawList& list, const ImDrawCmd& cmd, const ImVec2& displayPos) {
        const ImDrawVert* vertices = list.VtxBuffer.Data + cmd.VtxOffset;
        const ImDrawIdx* indices = list.IdxBuffer.Data + cmd.IdxOffset;
        const unsigned usable = cmd.ElemCount - (cmd.ElemCount % 3u);

        for (unsigned i = 0; i < usable; i += 3) {
            // Bedrock's UI material needs the reverse of ImGui's default DX winding.
            emitVertex(tess, vertices[indices[i + 2]], displayPos);
            emitVertex(tess, vertices[indices[i + 1]], displayPos);
            emitVertex(tess, vertices[indices[i + 0]], displayPos);
        }
    }

    static void emitVertex(Tessellator& tess, const ImDrawVert& vertex, const ImVec2& displayPos) {
        const std::uint32_t c = vertex.col;
        tess.color(
            static_cast<std::uint8_t>(c & 0xFFu),
            static_cast<std::uint8_t>((c >> 8) & 0xFFu),
            static_cast<std::uint8_t>((c >> 16) & 0xFFu),
            static_cast<std::uint8_t>((c >> 24) & 0xFFu));
        tess.vertexUV(vertex.pos.x - displayPos.x, vertex.pos.y - displayPos.y, 0.0f, vertex.uv.x, vertex.uv.y);
    }

    bool initialized_{};
    mce::MaterialPtr* material_{};
    mce::ClientTexture fontTexture_{};
};

} // namespace mcbe
