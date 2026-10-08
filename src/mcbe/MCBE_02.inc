public:
    std::shared_ptr<BedrockTextureData> clientTexture;
    std::shared_ptr<ResourceLocation> resourceLocation;
};

enum class PrimitiveMode : std::uint8_t {
    None, QuadList, TriangleList, TriangleStrip, LineList, LineStrip
};

enum class VertexField : std::uint16_t {
    Position, Color, Normal, Tangent, UV0, UV1, UV2, PBRTextureIdx,
    BoneId0, PreviousPosition, HummingbirdPosition, HummingbirdColor,
    HummingbirdAdditional, Mers, GeoType, Count
};

class MeshData {
public:
    PrimitiveMode mode{};
    bool meshNotFree{};
    std::vector<Vec3> positions;
    std::vector<Vec4> normals;
    std::vector<std::uint32_t> tangents;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> colors;
    std::vector<std::uint16_t> boneId0s;
    std::vector<Vec2> textureUVs[3];
    std::vector<std::uint16_t> pbrTextureIndices;
    std::vector<std::uint32_t> mersList;
    std::vector<std::uint8_t> geoType;
    std::size_t unk1{}, unk2{}, unk3{}, unk4{}, unk5{};
    bool fieldEnabled[15]{};

    void clear() {
        mode = PrimitiveMode::None;
        positions.clear();
        colors.clear();
        textureUVs[0].clear();
        unk1 = 0xFF7FFFFF7F7FFFFFULL;
        unk2 = 0x7F7FFFFF7F7FFFFFULL;
        unk3 = 0x7F7FFFFF7F7FFFFFULL;
        unk4 = 0xFF7FFFFFFF7FFFFFULL;
        unk5 = 0xFF7FFFFFFF7FFFFFULL;
        std::memset(fieldEnabled, 0, sizeof(fieldEnabled));
    }

    void enableField(VertexField field) {
        const auto index = static_cast<std::uint16_t>(field) & 0xFFFFu;
        if (index < std::size(fieldEnabled)) fieldEnabled[index] = true;
    }

    void reserveVertices(int count) {
        if (count <= 0) return;
        positions.reserve(static_cast<std::size_t>(count));
        colors.reserve(static_cast<std::size_t>(count));
        textureUVs[0].reserve(static_cast<std::size_t>(count));
    }
};

struct VertexFormat {
    static constexpr std::size_t FieldCount = 15;
    std::uint16_t fieldMask{};
    std::uint16_t fieldOffset[FieldCount];
    std::uint16_t vertexSize{};
    bool allowHalfFloats{true};

    VertexFormat() { std::memset(fieldOffset, 0xFF, sizeof(fieldOffset)); }
};

class IndexBufferContainer {
public:
    ClientResourcePointer<void*> indexBuffer;
    std::uint32_t indexCount{};
    std::uint32_t indexSize{};
    int iteration{};
    int pad{};
};

class RenderMaterialGroup {};

class MaterialPtr {
    char pad_[0x138]{};
public:
    static MaterialPtr* createMaterial(const HashedString& name) {
        auto* group = reinterpret_cast<RenderMaterialGroup*>(mcbe::signatures::materialCommon());
        if (!group)
            return nullptr;

        auto result = mcbe::abi::callVFunc<1, std::shared_ptr<MaterialPtr>>(
            group, static_cast<const HashedString&>(name));
        return result.get();
    }
};

struct OffscreenCaptureData {
    std::uint32_t width{};
    std::uint32_t height{};
    ClientTexture texture{};
};
struct UIActorOffscreenCaptureDescription { OffscreenCaptureData data; std::uint16_t width{}, height{}; std::size_t pad{}; };
struct UIThumbnailMeshOffscreenCaptureDescription { OffscreenCaptureData data; };
struct UIMeshOffscreenCaptureDescription { OffscreenCaptureData data; std::uint16_t width{}, height{}; std::size_t pad{}; };
struct UIStructureVolumeOffscreenCaptureDescription { OffscreenCaptureData data; };

namespace dragon_stub {
struct RenderMetadata {
    std::int64_t id{};
    std::variant<std::monostate, UIActorOffscreenCaptureDescription,
        UIThumbnailMeshOffscreenCaptureDescription, UIMeshOffscreenCaptureDescription,
        UIStructureVolumeOffscreenCaptureDescription> offscreen{};
};
}

