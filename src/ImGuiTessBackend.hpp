#pragma once

#include "MCBE.hpp"
#include "CrashLogger.hpp"
#include <imgui.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
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
        fontWhiteUv_ = io.Fonts->TexUvWhitePixel;

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

        // Split world rendering by primitive type:
        //  - solid ImGui geometry uses a depth-tested alpha-blended entity material
        //  - glyph triangles use a world text material with a text-friendly sampler
        worldFillMaterialName_ = "sign_text";
        worldFillMaterial_ = mce::MaterialPtr::createMaterial(
            HashedString("sign_text"));

        if (!worldFillMaterial_) {
            worldFillMaterialName_ = "entity_alphablend";
            worldFillMaterial_ = mce::MaterialPtr::createMaterial(
                HashedString("entity_alphablend"), true);
        }

        if (!worldFillMaterial_) {
            worldFillMaterialName_ = "entity_alphatest";
            worldFillMaterial_ = mce::MaterialPtr::createMaterial(
                HashedString("entity_alphatest"), true);
        }

        if (!worldFillMaterial_) {
            worldFillMaterialName_ = "ui_textured (last fallback)";
            worldFillMaterial_ = material_;
        }

        worldTextMaterialName_ = "name_text_depth_tested";
        worldTextMaterial_ = mce::MaterialPtr::createMaterial(
            HashedString("name_text_depth_tested"));

        if (!worldTextMaterial_) {
            worldTextMaterialName_ = "sign_text";
            worldTextMaterial_ = mce::MaterialPtr::createMaterial(
                HashedString("sign_text"));
        }

        if (!worldTextMaterial_) {
            worldTextMaterialName_ = worldFillMaterialName_;
            worldTextMaterial_ = worldFillMaterial_;
        }

        CrashLog::append(
            "Backend init: world fill=%s ptr=%p, world text=%s ptr=%p\r\n",
            worldFillMaterialName_,
            worldFillMaterial_,
            worldTextMaterialName_,
            worldTextMaterial_);

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
        worldFillMaterial_ = nullptr;
        worldTextMaterial_ = nullptr;
        worldFillMaterialName_ = "none";
        worldTextMaterialName_ = "none";
        initialized_ = false;
    }

    bool initialized() const { return initialized_; }
    const char* worldFillMaterialName() const { return worldFillMaterialName_; }
    const char* worldTextMaterialName() const { return worldTextMaterialName_; }

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
        if (!initialized_ || !list || !screen ||
            (!worldFillMaterial_ && !worldTextMaterial_) ||
            sourceSize.x <= 1.f || sourceSize.y <= 1.f ||
            panelWidth <= 0.01f || list->VtxBuffer.empty() || list->IdxBuffer.empty())
            return;

        Tessellator* tess = screen->getTessellator();
        if (!tess || tess->tessellating || tess->overridden)
            return;

        const float panelHeight = panelWidth * (sourceSize.y / sourceSize.x);

        Vec3 panelNormal{
            panelRight.y * panelUp.z - panelRight.z * panelUp.y,
            panelRight.z * panelUp.x - panelRight.x * panelUp.z,
            panelRight.x * panelUp.y - panelRight.y * panelUp.x};

        const float normalLengthSq =
            panelNormal.x * panelNormal.x +
            panelNormal.y * panelNormal.y +
            panelNormal.z * panelNormal.z;

        if (normalLengthSq > 0.000001f) {
            const float invLength = 1.0f / std::sqrt(normalLengthSq);
            panelNormal.x *= invLength;
            panelNormal.y *= invLength;
            panelNormal.z *= invLength;
        } else {
            panelNormal = {0.f, 0.f, 0.f};
        }

        const auto toWorldLocal = [&](const ImDrawVert& vertex, float frontBias) -> Vec3 {
            const float nx = ((vertex.pos.x - sourcePos.x) / sourceSize.x) - 0.5f;
            const float ny = 0.5f - ((vertex.pos.y - sourcePos.y) / sourceSize.y);

            const Vec3 world{
                panelCenter.x + panelRight.x * (nx * panelWidth) + panelUp.x * (ny * panelHeight) + panelNormal.x * frontBias,
                panelCenter.y + panelRight.y * (nx * panelWidth) + panelUp.y * (ny * panelHeight) + panelNormal.y * frontBias,
                panelCenter.z + panelRight.z * (nx * panelWidth) + panelUp.z * (ny * panelHeight) + panelNormal.z * frontBias};

            return {
                world.x - renderOrigin.x,
                world.y - renderOrigin.y,
                world.z - renderOrigin.z};
        };

        const auto nearUv = [](const ImVec2& a, const ImVec2& b) {
            constexpr float epsilon = 0.00001f;
            return std::abs(a.x - b.x) <= epsilon &&
                   std::abs(a.y - b.y) <= epsilon;
        };

        const auto isSolidTriangle = [&](const ImDrawVert& a,
                                         const ImDrawVert& b,
                                         const ImDrawVert& d) {
            // ImGui solid primitives sample TexUvWhitePixel. Glyphs/images use
            // varying atlas UVs, so this cleanly separates UI fill from text.
            return nearUv(a.uv, fontWhiteUv_) &&
                   nearUv(b.uv, fontWhiteUv_) &&
                   nearUv(d.uv, fontWhiteUv_);
        };

        struct ClippedVertex {
            ImVec2 pos{};
            ImVec2 uv{};
            ImU32 col{};
        };

        const auto lerpColor = [](ImU32 a, ImU32 b, float t) -> ImU32 {
            const auto channel = [t](unsigned av, unsigned bv) -> unsigned {
                return static_cast<unsigned>(
                    static_cast<float>(av) +
                    (static_cast<float>(bv) - static_cast<float>(av)) * t +
                    0.5f);
            };

            const unsigned ar = (a      ) & 0xFFu;
            const unsigned ag = (a >>  8) & 0xFFu;
            const unsigned ab = (a >> 16) & 0xFFu;
            const unsigned aa = (a >> 24) & 0xFFu;

            const unsigned br = (b      ) & 0xFFu;
            const unsigned bg = (b >>  8) & 0xFFu;
            const unsigned bb = (b >> 16) & 0xFFu;
            const unsigned ba = (b >> 24) & 0xFFu;

            return
                channel(ar, br) |
                (channel(ag, bg) << 8) |
                (channel(ab, bb) << 16) |
                (channel(aa, ba) << 24);
        };

        const auto interpolate = [&](const ClippedVertex& a,
                                     const ClippedVertex& b,
                                     float t) -> ClippedVertex {
            t = std::clamp(t, 0.0f, 1.0f);
            return {
                {
                    a.pos.x + (b.pos.x - a.pos.x) * t,
                    a.pos.y + (b.pos.y - a.pos.y) * t
                },
                {
                    a.uv.x + (b.uv.x - a.uv.x) * t,
                    a.uv.y + (b.uv.y - a.uv.y) * t
                },
                lerpColor(a.col, b.col, t)
            };
        };

        const auto clipTriangle = [&](const ImDrawVert& va,
                                      const ImDrawVert& vb,
                                      const ImDrawVert& vc,
                                      const ImVec4& clip,
                                      std::array<ClippedVertex, 12>& result) -> int {
            std::array<ClippedVertex, 12> a{};
            std::array<ClippedVertex, 12> b{};

            a[0] = {va.pos, va.uv, va.col};
            a[1] = {vb.pos, vb.uv, vb.col};
            a[2] = {vc.pos, vc.uv, vc.col};
            int count = 3;

            const auto clipEdge = [&](int edge, float boundary) {
                if (count <= 0)
                    return;

                int outCount = 0;

                const auto inside = [&](const ClippedVertex& v) {
                    switch (edge) {
                    case 0: return v.pos.x >= boundary; // left
                    case 1: return v.pos.x <= boundary; // right
                    case 2: return v.pos.y >= boundary; // top
                    default:return v.pos.y <= boundary; // bottom
                    }
                };

                const auto intersect = [&](const ClippedVertex& from,
                                           const ClippedVertex& to) {
                    float denominator = 0.0f;
                    float numerator = 0.0f;

                    if (edge <= 1) {
                        denominator = to.pos.x - from.pos.x;
                        numerator = boundary - from.pos.x;
                    } else {
                        denominator = to.pos.y - from.pos.y;
                        numerator = boundary - from.pos.y;
                    }

                    const float t =
                        std::abs(denominator) > 0.000001f
                            ? numerator / denominator
                            : 0.0f;
                    return interpolate(from, to, t);
                };

                ClippedVertex previous = a[count - 1];
                bool previousInside = inside(previous);

                for (int i = 0; i < count; ++i) {
                    const ClippedVertex current = a[i];
                    const bool currentInside = inside(current);

                    if (currentInside != previousInside && outCount < 12)
                        b[outCount++] = intersect(previous, current);

                    if (currentInside && outCount < 12)
                        b[outCount++] = current;

                    previous = current;
                    previousInside = currentInside;
                }

                a = b;
                count = outCount;
            };

            clipEdge(0, clip.x);
            clipEdge(1, clip.z);
            clipEdge(2, clip.y);
            clipEdge(3, clip.w);

            for (int i = 0; i < count; ++i)
                result[i] = a[i];

            return count;
        };

        for (int commandIndex = 0; commandIndex < list->CmdBuffer.Size; ++commandIndex) {
            const ImDrawCmd& cmd = list->CmdBuffer[commandIndex];
            if (cmd.UserCallback || cmd.ElemCount < 3)
                continue;

            if (cmd.ClipRect.x >= cmd.ClipRect.z ||
                cmd.ClipRect.y >= cmd.ClipRect.w)
                continue;

            const ImTextureID texture =
                cmd.TextureId ? cmd.TextureId : toTextureId(&fontTexture_);
            const auto* clientTexture = fromTextureId(texture);
            if (!clientTexture || !clientTexture->resourcePointerBlock)
                clientTexture = &fontTexture_;
            if (!clientTexture || !clientTexture->resourcePointerBlock)
                continue;

            const ImDrawVert* vertices = list->VtxBuffer.Data + cmd.VtxOffset;
            const ImDrawIdx* indices = list->IdxBuffer.Data + cmd.IdxOffset;
            const unsigned usable = cmd.ElemCount - (cmd.ElemCount % 3u);

            const auto submitPass = [&](bool solidPass, mce::MaterialPtr* material) {
                if (!material)
                    return;

                unsigned matchedElements = 0;
                for (unsigned i = 0; i < usable; i += 3) {
                    const ImDrawVert& a = vertices[indices[i + 0]];
                    const ImDrawVert& b = vertices[indices[i + 1]];
                    const ImDrawVert& d = vertices[indices[i + 2]];
                    if (isSolidTriangle(a, b, d) == solidPass)
                        matchedElements += 3;
                }

                if (!matchedElements || tess->tessellating || tess->overridden)
                    return;

                // One clipped triangle can become a polygon of up to seven
                // vertices, or five output triangles. Reserve that worst case
                // without changing the actual submitted vertex count.
                const std::uint64_t worstCase =
                    static_cast<std::uint64_t>(matchedElements) * 5ull;
                const int reserve = static_cast<int>(
                    std::min<std::uint64_t>(
                        worstCase,
                        static_cast<std::uint64_t>(INT_MAX)));

                tess->begin(mce::PrimitiveMode::TriangleList, reserve);
                if (!tess->tessellating)
                    return;

                tess->meshData.enableField(mce::VertexField::Color);
                tess->meshData.enableField(mce::VertexField::UV0);
                tess->isFormatFixed = true;

                auto& positions = tess->meshData.positions;
                auto& colors = tess->meshData.colors;
                auto& uvs = tess->meshData.textureUVs[0];

                // sign_text now handles solid ImGui geometry correctly, so keep
                // every solid primitive on one stable plane and let ImGui's
                // original triangle order do the painting. Text remains slightly
                // forward to avoid fighting the panel background.
                constexpr float kSolidBias = 0.0010f;
                constexpr float kTextBias = 0.0100f;
                const float frontBias = solidPass ? kSolidBias : kTextBias;

                const auto emit = [&](const ClippedVertex& vertex) {
                    ImDrawVert converted{};
                    converted.pos = vertex.pos;
                    converted.uv = vertex.uv;
                    converted.col = vertex.col;

                    positions.push_back(toWorldLocal(converted, frontBias));
                    colors.push_back(vertex.col);
                    uvs.push_back({vertex.uv.x, vertex.uv.y});
                };

                std::array<ClippedVertex, 12> polygon{};

                for (unsigned i = 0; i < usable; i += 3) {
                    const ImDrawVert& a = vertices[indices[i + 0]];
                    const ImDrawVert& b = vertices[indices[i + 1]];
                    const ImDrawVert& d = vertices[indices[i + 2]];

                    if (isSolidTriangle(a, b, d) != solidPass)
                        continue;

                    const int polygonCount =
                        clipTriangle(a, b, d, cmd.ClipRect, polygon);
                    if (polygonCount < 3)
                        continue;

                    // Fan-triangulate the clipped polygon. Mapping ImGui's
                    // Y-down plane into our Y-up world basis reverses winding,
                    // so emit each triangle in reverse order.
                    for (int p = 1; p + 1 < polygonCount; ++p) {
                        emit(polygon[p + 1]);
                        emit(polygon[p]);
                        emit(polygon[0]);
                    }
                }

                tess->count = static_cast<int>(positions.size());

                if (tess->count <= 0) {
                    tess->clear();
                    return;
                }

                mce::Mesh mesh{};
                if (tess->endTransient(mesh)) {
                    CrashLog::setStage(
                        solidPass
                            ? "backend.world: submit clipped fill"
                            : "backend.world: submit clipped text");
                    mesh.renderMesh(
                        screen->toMeshContext(),
                        material,
                        *clientTexture);
                    tess->reclaimTransient(mesh);
                }
            };

            // Both passes now honor ImGui's command clip rectangle in geometry
            // space. This is required for scrollbars and animated/clipped widgets
            // because a 3D ScreenContext cannot use the ordinary 2D ImGui scissor.
            submitPass(true, worldFillMaterial_);
            submitPass(false, worldTextMaterial_);
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
    mce::MaterialPtr* worldFillMaterial_{};
    mce::MaterialPtr* worldTextMaterial_{};
    const char* worldFillMaterialName_{"none"};
    const char* worldTextMaterialName_{"none"};
    ImVec2 fontWhiteUv_{};
    std::shared_ptr<mce::BedrockTextureData> fontTextureData_{};
    mce::ClientTexture fontTexture_{};
};

} // namespace mcbe
