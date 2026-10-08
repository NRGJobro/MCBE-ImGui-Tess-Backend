#pragma once

#include "MCBE.hpp"
#include "CrashLogger.hpp"
#include <imgui.h>

#include <algorithm>
#include <climits>
#include <cstdint>

namespace mcbe {

class ImGuiTessBackend {
public:
    bool initialize(MinecraftUIRenderContext* ctx) {
        CrashLog::setStage("backend.initialize: validate context");
        CrashLog::setPointers(ctx);
        if (initialized_) return true;
        if (!ctx || !ctx->textureGroup || !ctx->screenContext) return false;
        if (!signatures::requiredReady()) return false;

        CrashLog::setStage("backend.initialize: build font atlas");
        ImGuiStyle& style = ImGui::GetStyle();
        style.AntiAliasedLines = false;
        style.AntiAliasedFill = false;
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

        CrashLog::setStage("backend.initialize: build ImageBuffer");
        cg::ImageBuffer buffer(image);
        if (!buffer.isValid()) return false;
        ResourceLocation location("imgui_tess/font_atlas_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        CrashLog::append(
            "Backend init: ctx=%p client=%p screen=%p textureGroup=%p sizeof(ResourceLocation)=0x%zX sizeof(ImageBuffer)=0x%zX sizeof(TextureContainer)=0x%zX\r\n",
            ctx,
            ctx->clientInstance,
            ctx->screenContext,
            ctx->textureGroup.get(),
            sizeof(ResourceLocation),
            sizeof(cg::ImageBuffer),
            sizeof(mce::TextureContainer));
        CrashLog::append(
            "Backend init: font=%dx%d bytes=%zu imageResourceVtable=0x%llX\r\n",
            width, height, byteCount,
            static_cast<unsigned long long>(signatures::imageResourceVtable()));

        // TextureGroup::uploadTexture returns a BedrockTexture&. Keep the returned
        // shared BedrockTextureData alive and copy its ClientTexture directly. This
        // avoids MinecraftUIRenderContext::getTexture's non-trivial return ABI.
        CrashLog::checkpoint("backend.initialize: TextureGroup::uploadTexture(ImageBuffer)");
        auto& uploaded = ctx->textureGroup->uploadTexture(location, buffer);

        CrashLog::checkpoint("backend.initialize: validate uploaded BedrockTexture");
        if (!uploaded.texture ||
            !uploaded.texture->clientTexture.resourcePointerBlock) {
            CrashLog::append(
                "Backend init: upload returned invalid texture data: bedrock=%p texture=%p\r\n",
                &uploaded,
                uploaded.texture.get());
            return false;
        }

        fontTextureData_ = uploaded.texture;
        fontTexture_ = fontTextureData_->clientTexture;
        CrashLog::append(
            "Backend init: upload OK bedrock=%p data=%p resource=%p\r\n",
            &uploaded,
            fontTextureData_.get(),
            fontTexture_.resourcePointerBlock.get());
        io.Fonts->SetTexID(toTextureId(&fontTexture_));
        io.BackendRendererName = "mcbe_tessellator_26_52";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

        CrashLog::checkpoint("backend.initialize: create ui_textured material");
        material_ = mce::MaterialPtr::createMaterial(HashedString("ui_textured"));
        if (!material_) material_ = mce::MaterialPtr::createMaterial(HashedString("im_gui"));

        // Keep the world panel's texture/color semantics identical to the
        // already-correct 2D Tess backend. sign_text/entity_alphatest alpha-test
        // the ImGui font atlas too aggressively and make glyphs look speckled.
        worldMaterial_ = material_;
        worldBackMaterial_ = mce::MaterialPtr::createMaterial(HashedString("ui_fill_color"));
        if (!worldBackMaterial_)
            worldBackMaterial_ = material_;

        initialized_ = material_ != nullptr;
        CrashLog::setStage(initialized_ ? "backend.initialize: complete" : "backend.initialize: material failed");
        return initialized_;
    }

    void shutdown() {
        if (!initialized_) return;
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = nullptr;
        io.Fonts->SetTexID(static_cast<ImTextureID>(0));
        material_ = nullptr;
        worldMaterial_ = nullptr;
        worldBackMaterial_ = nullptr;
        initialized_ = false;
    }

    bool initialized() const { return initialized_; }

    void render(ImDrawData* drawData, MinecraftUIRenderContext* ctx) {
        CrashLog::setStage("backend.render: validate draw data");
        CrashLog::setPointers(ctx);
        if (!initialized_ || !drawData || !ctx || !ctx->screenContext || drawData->CmdListsCount <= 0) return;
        CrashLog::setStage("backend.render: get Tessellator");
        const float scale = guiScale(ctx);
        auto* tess = ctx->screenContext->getTessellator();
        CrashLog::setPointers(ctx, tess);
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

                const ClipRect clip = makeClip(first.ClipRect, drawData->DisplayPos, scale);
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
                    const ClipRect nextClip = makeClip(next.ClipRect, drawData->DisplayPos, scale);
                    ImTextureID nextTexture = next.TextureId ? next.TextureId : toTextureId(&fontTexture_);
                    if (!(nextClip == clip) || nextTexture != texture) break;
                    totalElements += next.ElemCount;
                }

                const int reserve = static_cast<int>(std::min<std::uint64_t>(totalElements, INT_MAX));
                if (tess->tessellating || tess->overridden) {
                    commandIndex = runEnd;
                    continue;
                }

                CrashLog::checkpoint("backend.render: Tessellator::begin");
                tess->begin(mce::PrimitiveMode::TriangleList, reserve);
                if (!tess->tessellating) {
                    commandIndex = runEnd;
                    continue;
                }
                tess->meshData.colors.reserve(tess->meshData.colors.size() + static_cast<std::size_t>(reserve));
                tess->meshData.textureUVs[0].reserve(tess->meshData.textureUVs[0].size() + static_cast<std::size_t>(reserve));

                CrashLog::setStage("backend.render: set clipping rectangle");
                ctx->saveCurrentClippingRectangle();
                ctx->setClippingRectangle(Rect{clip.left, clip.right, clip.top, clip.bottom});

                CrashLog::setStage("backend.render: emit ImGui vertices");
                for (int emit = commandIndex; emit < runEnd; ++emit)
                    emitCommand(*tess, *list, list->CmdBuffer[emit], drawData->DisplayPos, scale);

                const auto* clientTexture = fromTextureId(texture);
                if (!clientTexture || !clientTexture->resourcePointerBlock)
                    clientTexture = &fontTexture_;

                if (clientTexture && clientTexture->resourcePointerBlock) {
                    CrashLog::setStage("backend.render: Tessellator::endTransient");
                    mce::Mesh mesh{};
                    if (tess->endTransient(mesh)) {
                        CrashLog::checkpoint("backend.render: mce::Mesh::_renderMesh");
                        mesh.renderMesh(ctx->screenContext->toMeshContext(), material_, *clientTexture);
                        tess->reclaimTransient(mesh);
                    }
                } else {
                    tess->clear();
                }

                CrashLog::setStage("backend.render: restore clipping rectangle");
                ctx->restoreSavedClippingRectangle();
                commandIndex = runEnd;
            }
        }
    }


    void renderWorldWindow(
        const ImDrawList* list,
        const ImVec2& sourcePos,
        const ImVec2& sourceSize,
        ScreenContext* screen,
        const Vec3& renderOrigin,
        const Vec3& panelCenter,
        const Vec3& panelRight,
        const Vec3& panelUp,
        float panelWidth) {

        CrashLog::setStage("backend.world: validate");
        if (!initialized_ || !list || !screen || !worldMaterial_ ||
            sourceSize.x <= 1.f || sourceSize.y <= 1.f ||
            panelWidth <= 0.01f || list->VtxBuffer.empty() || list->IdxBuffer.empty())
            return;

        Tessellator* tess = screen->getTessellator();
        if (!tess || tess->tessellating || tess->overridden)
            return;

        const float panelHeight = panelWidth * (sourceSize.y / sourceSize.x);
        const auto toWorldLocal = [&](const ImDrawVert& vertex) -> Vec3 {
            const float nx = ((vertex.pos.x - sourcePos.x) / sourceSize.x) - 0.5f;
            const float ny = 0.5f - ((vertex.pos.y - sourcePos.y) / sourceSize.y);

            const Vec3 world{
                panelCenter.x + panelRight.x * (nx * panelWidth) + panelUp.x * (ny * panelHeight),
                panelCenter.y + panelRight.y * (nx * panelWidth) + panelUp.y * (ny * panelHeight),
                panelCenter.z + panelRight.z * (nx * panelWidth) + panelUp.z * (ny * panelHeight)};

            return {
                world.x - renderOrigin.x,
                world.y - renderOrigin.y,
                world.z - renderOrigin.z};
        };

        int commandIndex = 0;
        while (commandIndex < list->CmdBuffer.Size) {
            const ImDrawCmd& first = list->CmdBuffer[commandIndex];
            if (first.UserCallback || first.ElemCount < 3) {
                ++commandIndex;
                continue;
            }

            ImTextureID texture =
                first.TextureId ? first.TextureId : toTextureId(&fontTexture_);

            std::uint64_t totalElements = first.ElemCount;
            int runEnd = commandIndex + 1;
            for (; runEnd < list->CmdBuffer.Size; ++runEnd) {
                const ImDrawCmd& next = list->CmdBuffer[runEnd];
                if (next.UserCallback || next.ElemCount < 3)
                    break;
                const ImTextureID nextTexture =
                    next.TextureId ? next.TextureId : toTextureId(&fontTexture_);
                if (nextTexture != texture)
                    break;
                totalElements += next.ElemCount;
            }

            const int reserve = static_cast<int>(
                std::min<std::uint64_t>(totalElements, static_cast<std::uint64_t>(INT_MAX)));

            tess->begin(mce::PrimitiveMode::TriangleList, reserve);
            if (!tess->tessellating) {
                commandIndex = runEnd;
                continue;
            }

            tess->meshData.enableField(mce::VertexField::Color);
            tess->meshData.enableField(mce::VertexField::UV0);
            tess->isFormatFixed = true;

            auto& positions = tess->meshData.positions;
            auto& colors = tess->meshData.colors;
            auto& uvs = tess->meshData.textureUVs[0];

            positions.reserve(positions.size() + static_cast<std::size_t>(reserve));
            colors.reserve(colors.size() + static_cast<std::size_t>(reserve));
            uvs.reserve(uvs.size() + static_cast<std::size_t>(reserve));

            const auto emitVertex = [&](const ImDrawVert& vertex) {
                positions.push_back(toWorldLocal(vertex));
                colors.push_back(vertex.col);
                uvs.push_back({vertex.uv.x, vertex.uv.y});
            };

            for (int emit = commandIndex; emit < runEnd; ++emit) {
                const ImDrawCmd& cmd = list->CmdBuffer[emit];
                const ImDrawVert* vertices = list->VtxBuffer.Data + cmd.VtxOffset;
                const ImDrawIdx* indices = list->IdxBuffer.Data + cmd.IdxOffset;
                const unsigned usable = cmd.ElemCount - (cmd.ElemCount % 3u);

                for (unsigned i = 0; i < usable; i += 3) {
                    const ImDrawVert& a = vertices[indices[i + 0]];
                    const ImDrawVert& b = vertices[indices[i + 1]];
                    const ImDrawVert& d = vertices[indices[i + 2]];

                    // Mapping ImGui's Y-down coordinates to panel Y-up flips
                    // winding, so reverse the triangle once to keep the front
                    // normal facing the camera that placed the panel.
                    emitVertex(d);
                    emitVertex(b);
                    emitVertex(a);
                }
            }

            tess->count = static_cast<int>(positions.size());

            const auto* clientTexture = fromTextureId(texture);
            if (!clientTexture || !clientTexture->resourcePointerBlock)
                clientTexture = &fontTexture_;

            if (clientTexture && clientTexture->resourcePointerBlock) {
                mce::Mesh mesh{};
                if (tess->endTransient(mesh)) {
                    CrashLog::setStage("backend.world: mce::Mesh::_renderMesh");
                    mesh.renderMesh(
                        screen->toMeshContext(),
                        worldMaterial_,
                        *clientTexture);
                    tess->reclaimTransient(mesh);
                }
            } else {
                tess->clear();
            }

            commandIndex = runEnd;
        }
    }


    void renderWorldBack(
        ScreenContext* screen,
        const Vec3& renderOrigin,
        const Vec3& panelCenter,
        const Vec3& panelRight,
        const Vec3& panelUp,
        float panelWidth,
        float aspectRatio) {

        if (!initialized_ || !screen || !worldBackMaterial_ ||
            panelWidth <= 0.01f || aspectRatio <= 0.01f)
            return;

        Tessellator* tess = screen->getTessellator();
        if (!tess || tess->tessellating || tess->overridden)
            return;

        const float halfW = panelWidth * 0.5f;
        const float halfH = panelWidth * aspectRatio * 0.5f;

        const auto point = [&](float x, float y) -> Vec3 {
            const Vec3 world{
                panelCenter.x + panelRight.x * x + panelUp.x * y,
                panelCenter.y + panelRight.y * x + panelUp.y * y,
                panelCenter.z + panelRight.z * x + panelUp.z * y};
            return {
                world.x - renderOrigin.x,
                world.y - renderOrigin.y,
                world.z - renderOrigin.z};
        };

        const Vec3 tl = point(-halfW, +halfH);
        const Vec3 tr = point(+halfW, +halfH);
        const Vec3 br = point(+halfW, -halfH);
        const Vec3 bl = point(-halfW, -halfH);

        tess->begin(mce::PrimitiveMode::TriangleList, 6);
        if (!tess->tessellating)
            return;

        tess->meshData.enableField(mce::VertexField::Color);
        tess->isFormatFixed = true;

        auto& positions = tess->meshData.positions;
        auto& colors = tess->meshData.colors;
        positions.reserve(6);
        colors.reserve(6);

        constexpr std::uint32_t backColor = 0xFF161616u;
        const auto emit = [&](const Vec3& p) {
            positions.push_back(p);
            colors.push_back(backColor);
        };

        // Reverse of the front face so the rear side has a clean backing.
        emit(tl); emit(tr); emit(br);
        emit(tl); emit(br); emit(bl);
        tess->count = 6;

        mce::Mesh mesh{};
        if (tess->endTransient(mesh)) {
            mesh.renderMesh(
                screen->toMeshContext(),
                worldBackMaterial_,
                fontTexture_);
            tess->reclaimTransient(mesh);
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

    static float guiScale(MinecraftUIRenderContext* ctx) {
        if (!ctx || !ctx->clientInstance)
            return 1.0f;
        const auto* gui = ctx->clientInstance->getGuiData();
        if (!gui)
            return 1.0f;
        const float scale = gui->getScale();
        return (scale > 0.01f && scale < 16.0f) ? scale : 1.0f;
    }

    static ClipRect makeClip(const ImVec4& value, const ImVec2& displayPos, float scale) {
        return {
            (value.x - displayPos.x) / scale,
            (value.z - displayPos.x) / scale,
            (value.y - displayPos.y) / scale,
            (value.w - displayPos.y) / scale
        };
    }

    static void emitCommand(Tessellator& tess, const ImDrawList& list, const ImDrawCmd& cmd, const ImVec2& displayPos, float scale) {
        const ImDrawVert* vertices = list.VtxBuffer.Data + cmd.VtxOffset;
        const ImDrawIdx* indices = list.IdxBuffer.Data + cmd.IdxOffset;
        unsigned usable = cmd.ElemCount - (cmd.ElemCount % 3u);

        if (tess.maxFaces > 0) {
            const int remaining = tess.maxFaces - tess.count;
            if (remaining <= 0)
                return;
            usable = std::min<unsigned>(usable, static_cast<unsigned>(remaining));
            usable -= usable % 3u;
        }

        if (!usable)
            return;

        // ImGui's ImU32 is already packed as AABBGGRR on little-endian Windows,
        // exactly matching Tessellator::color's packed integer representation.
        // Write the three streams directly instead of updating optional
        // nextColor/nextUV state for every vertex.
        tess.meshData.enableField(mce::VertexField::Color);
        tess.meshData.enableField(mce::VertexField::UV0);
        tess.isFormatFixed = true;

        auto& positions = tess.meshData.positions;
        auto& colors = tess.meshData.colors;
        auto& uvs = tess.meshData.textureUVs[0];

        const auto emit = [&](const ImDrawVert& vertex) {
            positions.push_back({
                (vertex.pos.x - displayPos.x) / scale,
                (vertex.pos.y - displayPos.y) / scale,
                0.0f});
            colors.push_back(vertex.col);
            uvs.push_back({vertex.uv.x, vertex.uv.y});
        };

        for (unsigned i = 0; i < usable; i += 3) {
            // Bedrock's UI material needs the reverse of ImGui's default DX winding.
            emit(vertices[indices[i + 2]]);
            emit(vertices[indices[i + 1]]);
            emit(vertices[indices[i + 0]]);
        }

        tess.count += static_cast<int>(usable);
    }

    bool initialized_{};
    mce::MaterialPtr* material_{};
    mce::MaterialPtr* worldMaterial_{};
    mce::MaterialPtr* worldBackMaterial_{};
    std::shared_ptr<mce::BedrockTextureData> fontTextureData_{};
    mce::ClientTexture fontTexture_{};
};

} // namespace mcbe
