#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <imgui.h>

namespace mcbe::imgui_tess {

struct ClipRect {
    float left{};
    float top{};
    float right{};
    float bottom{};

    [[nodiscard]] bool valid() const noexcept {
        return right > left && bottom > top &&
               std::isfinite(left) && std::isfinite(top) &&
               std::isfinite(right) && std::isfinite(bottom);
    }

    friend bool operator==(const ClipRect& a, const ClipRect& b) noexcept {
        return a.left == b.left && a.top == b.top &&
               a.right == b.right && a.bottom == b.bottom;
    }
};

struct RenderStats {
    std::uint32_t drawLists{};
    std::uint32_t commands{};
    std::uint32_t callbacks{};
    std::uint32_t batches{};
    std::uint32_t mergedCommands{};
    std::uint64_t submittedVertices{};
};

struct Config {
    // MCBE's triangle rasterizer expects the opposite winding from Dear ImGui's
    // stock DX backends. Keep this enabled unless your material disables culling.
    bool reverseWinding = true;

    // Ignore degenerate/incomplete tail indices rather than reading past buffers.
    bool requireCompleteTriangles = true;

    // Adjacent ImDrawCmd objects with identical clip + texture state are emitted
    // in one Minecraft batch. This is the biggest win over older MCBE backends.
    bool mergeAdjacentCommands = true;
};

// Adapter contract (intentionally duck-typed / header-only):
//   bool ready() const;
//   ImVec2 displaySizePixels() const;
//   float uiScale() const;
//   bool createFontTexture(const unsigned char*, int w, int h, int bytesPerPixel);
//   ImTextureID fontTextureId() const;
//   void beginFrame();
//   void endFrame();
//   void resetRenderState();
//   void beginBatch(std::size_t vertexCount, const ClipRect& clip);
//   void emitVertex(float x, float y, float u, float v, ImU32 color);
//   void flushBatch(ImTextureID textureId);
//
// Calls are statically dispatched and inlineable. There is no virtual call in the
// per-vertex hot path.
template <class Adapter>
class Backend final {
public:
    explicit Backend(Adapter& adapter, Config config = {}) noexcept
        : adapter_(&adapter), config_(config) {}

    [[nodiscard]] bool initialize() {
        if (!adapter_ || !adapter_->ready())
            return false;

        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        int bytesPerPixel = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytesPerPixel);

        if (!pixels || width <= 0 || height <= 0 || bytesPerPixel != 4)
            return false;
        if (!adapter_->createFontTexture(pixels, width, height, bytesPerPixel))
            return false;

        io.Fonts->SetTexID(adapter_->fontTextureId());
        io.BackendRendererName = "mcbe_imgui_tess_26_52";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        initialized_ = true;
        return true;
    }

    void shutdown() noexcept {
        if (!initialized_)
            return;
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = nullptr;
        io.Fonts->SetTexID(nullptr);
        initialized_ = false;
    }

    void newFrame(float deltaSeconds = 1.0f / 60.0f) {
        if (!adapter_)
            return;

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 size = adapter_->displaySizePixels();
        io.DisplaySize = size;
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = std::max(deltaSeconds, 1.0e-6f);
    }

    [[nodiscard]] RenderStats render(ImDrawData* drawData) {
        RenderStats stats{};
        if (!initialized_ || !adapter_ || !adapter_->ready() || !drawData || drawData->CmdListsCount <= 0)
            return stats;

        const float scale = std::max(adapter_->uiScale(), 1.0e-6f);
        adapter_->beginFrame();
        struct EndFrameGuard {
            Adapter* adapter;
            ~EndFrameGuard() { adapter->endFrame(); }
        } guard{adapter_};

        stats.drawLists = static_cast<std::uint32_t>(drawData->CmdListsCount);

        for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
            const ImDrawList* list = drawData->CmdLists[listIndex];
            if (!list)
                continue;

            int cmdIndex = 0;
            while (cmdIndex < list->CmdBuffer.Size) {
                const ImDrawCmd& first = list->CmdBuffer[cmdIndex];
                ++stats.commands;

                if (first.UserCallback) {
                    ++stats.callbacks;
                    if (first.UserCallback == ImDrawCallback_ResetRenderState)
                        adapter_->resetRenderState();
                    else
                        first.UserCallback(list, &first);
                    ++cmdIndex;
                    continue;
                }

                const ClipRect firstClip = convertClip(first.ClipRect, drawData->DisplayPos, scale);
                if (!firstClip.valid() || first.ElemCount < 3) {
                    ++cmdIndex;
                    continue;
                }

                const ImTextureID textureId = first.TextureId ? first.TextureId : adapter_->fontTextureId();
                std::uint64_t totalElements = first.ElemCount;
                int runEnd = cmdIndex + 1;

                if (config_.mergeAdjacentCommands) {
                    for (; runEnd < list->CmdBuffer.Size; ++runEnd) {
                        const ImDrawCmd& next = list->CmdBuffer[runEnd];
                        if (next.UserCallback)
                            break;
                        const ClipRect nextClip = convertClip(next.ClipRect, drawData->DisplayPos, scale);
                        const ImTextureID nextTexture = next.TextureId ? next.TextureId : adapter_->fontTextureId();
                        if (!(nextClip == firstClip) || nextTexture != textureId || next.ElemCount < 3)
                            break;
                        totalElements += next.ElemCount;
                        ++stats.mergedCommands;
                        ++stats.commands;
                    }
                }

                const std::size_t reserveVertices = static_cast<std::size_t>(std::min<std::uint64_t>(
                    totalElements, static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())));
                adapter_->beginBatch(reserveVertices, firstClip);

                for (int emitIndex = cmdIndex; emitIndex < runEnd; ++emitIndex) {
                    emitCommand(*list, list->CmdBuffer[emitIndex], drawData->DisplayPos, scale, stats);
                }

                adapter_->flushBatch(textureId);
                ++stats.batches;
                cmdIndex = runEnd;
            }
        }

        return stats;
    }

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] Config& config() noexcept { return config_; }
    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    static ClipRect convertClip(const ImVec4& clip, const ImVec2& displayPos, float scale) noexcept {
        return {
            (clip.x - displayPos.x) / scale,
            (clip.y - displayPos.y) / scale,
            (clip.z - displayPos.x) / scale,
            (clip.w - displayPos.y) / scale,
        };
    }

    void emitCommand(const ImDrawList& list,
                     const ImDrawCmd& command,
                     const ImVec2& displayPos,
                     float scale,
                     RenderStats& stats) {
        if (command.ElemCount < 3)
            return;

        const ImDrawVert* vertices = list.VtxBuffer.Data + command.VtxOffset;
        const ImDrawIdx* indices = list.IdxBuffer.Data + command.IdxOffset;
        const unsigned int usable = config_.requireCompleteTriangles
            ? command.ElemCount - (command.ElemCount % 3u)
            : command.ElemCount;

        for (unsigned int i = 0; i + 2 < usable; i += 3) {
            const ImDrawIdx ia = indices[i + (config_.reverseWinding ? 2 : 0)];
            const ImDrawIdx ib = indices[i + 1];
            const ImDrawIdx ic = indices[i + (config_.reverseWinding ? 0 : 2)];

            emitVertex(vertices[ia], displayPos, scale);
            emitVertex(vertices[ib], displayPos, scale);
            emitVertex(vertices[ic], displayPos, scale);
            stats.submittedVertices += 3;
        }
    }

    inline void emitVertex(const ImDrawVert& vertex, const ImVec2& displayPos, float scale) {
        adapter_->emitVertex(
            (vertex.pos.x - displayPos.x) / scale,
            (vertex.pos.y - displayPos.y) / scale,
            vertex.uv.x,
            vertex.uv.y,
            vertex.col);
    }

    Adapter* adapter_{};
    Config config_{};
    bool initialized_{};
};

} // namespace mcbe::imgui_tess
