#pragma once

#include "Mesh.hpp"

class Tessellator;

struct TessellatorQuadInfo {
    std::uint8_t facing{};
    bool twoFace{};
    Vec3 centroid{};
};

class Tessellator {
public:
    bool isFormatFixed{};
    mce::MeshData meshData;
    bool hasNormals{};
    std::uint64_t nextReserve{};
    std::optional<Vec4> nextNormal;
    std::optional<Vec2> nextUV[3];
    std::optional<std::uint32_t> nextColor;
    std::optional<std::uint16_t> nextBoneId;
    std::optional<std::uint16_t> nextPBRTextureIdx;
    std::optional<std::uint32_t> nextMers;
    bool indexPhase{};
    Vec3 postTransformationOffset{};
    Vec3 postTransformationScale{};
    std::uint8_t quadFacing{};
    bool quadTwoSided{};
    std::vector<TessellatorQuadInfo> quadInfoList;

    Vec3 faceCenterAccumulator{};
    int currQuadIndex{};
    bool applyTransform{};
    Mat4 transformMatrix{};
    bool noColor{};
    bool overridden{};
    bool forceTessellateIntercept{};
    std::function<void(Tessellator*, mce::MaterialPtr*, const mce::TexturePtr&)> interceptTessellator;
    int count{};
    int maxFaces{};
    bool tessellating{};
    bool buildFaceData{};
    std::unique_ptr<mce::Mesh> preGeneratedMesh;
    std::weak_ptr<mce::BufferResourceService> bufferResourceService;

    void clear() {
        count = 0;
        overridden = false;
        tessellating = false;
        indexPhase = false;
        meshData.clear();
        isFormatFixed = false;
        hasNormals = false;
        nextColor.reset();
        nextUV[0].reset();
    }

    void begin(mce::PrimitiveMode mode, int maxVertices = 0) {
        if (tessellating || overridden) return;
        clear();
        meshData.mode = mode;
        noColor = false;
        tessellating = true;
        currQuadIndex = 0;
        quadInfoList.clear();
        meshData.enableField(mce::VertexField::Position);
        if (maxVertices > 0) meshData.reserveVertices(maxVertices);
    }

    void vertex(float x, float y, float z) {
        if (count == maxFaces) return;
        ++count;
        isFormatFixed = true;
        meshData.positions.push_back({x, y, z});
        if (nextColor) meshData.colors.push_back(*nextColor);
        if (nextUV[0]) meshData.textureUVs[0].push_back(*nextUV[0]);
    }

    void vertexUV(float x, float y, float z, float u, float v) {
        nextUV[0] = Vec2{u, v};
        if (!isFormatFixed) meshData.enableField(mce::VertexField::UV0);
        vertex(x, y, z);
    }

    void color(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
        nextColor = (static_cast<std::uint32_t>(a) << 24) |
                    (static_cast<std::uint32_t>(b) << 16) |
                    (static_cast<std::uint32_t>(g) << 8) |
                    static_cast<std::uint32_t>(r);
        if (!isFormatFixed) meshData.enableField(mce::VertexField::Color);
    }

    void end(mce::Mesh& mesh) {
        if (bufferResourceService.lock()) {
            mesh.temporary = true;
            mesh.primitiveMode = meshData.mode;
            mesh.bufferResourceService = bufferResourceService;
            mesh.meshData = meshData;
        }
        clear();
    }

    // ImGui emits fresh transient geometry every frame. Avoid deep-copying all
    // CPU vertex arrays into Mesh: move them for submission, then reclaim the
    // vector storage after _renderMesh so capacity is reused next frame.
    bool endTransient(mce::Mesh& mesh) {
        if (!bufferResourceService.lock()) {
            clear();
            return false;
        }

        mesh.temporary = true;
        mesh.primitiveMode = meshData.mode;
        mesh.bufferResourceService = bufferResourceService;
        mesh.meshData = std::move(meshData);
        clear();
        return true;
    }

    void reclaimTransient(mce::Mesh& mesh) {
        meshData = std::move(mesh.meshData);
        meshData.clear();
    }
};

