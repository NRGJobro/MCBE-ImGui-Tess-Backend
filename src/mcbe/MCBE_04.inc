    std::uint32_t arraySize{1};

    ImageDescription() = default;
    explicit ImageDescription(const mce::Image& image)
        : width(image.width), height(image.height),
          textureFormat(image.imageFormat == mce::ImageFormat::RGBA8Unorm
              ? mce::TextureFormat::R8g8b8a8Unorm : mce::TextureFormat::Unknown),
          colorSpace(image.usage == mce::ImageUsage::sRGB ? ColorSpace::sRGB : ColorSpace::Linear) {}

    int getRequiredBufferSize() const {
        if (textureFormat == mce::TextureFormat::R8g8b8a8Unorm)
            return static_cast<int>(width * height * 4u);
        return 0;
    }
};

struct TextureDescription : ImageDescription {
    std::uint32_t mipCount{1};
    TextureDescription() = default;
    explicit TextureDescription(const ImageDescription& image) : ImageDescription(image), mipCount(1) {}
};

struct ImageBuffer {
    mce::Blob storage;
    ImageDescription imageDescription;
    ImageBuffer() = default;
    explicit ImageBuffer(const mce::Image& image) : storage(image.imageData), imageDescription(image) {}
    bool isValid() const { return storage.size == static_cast<std::size_t>(imageDescription.getRequiredBufferSize()); }
};

class ImageResource {
    void** vtable_{};
public:
    std::vector<ImageBuffer> storage;
    explicit ImageResource(ImageBuffer& image) {
        vtable_ = reinterpret_cast<void**>(mcbe::signatures::imageResourceVtable());
        storage.emplace_back(image);
    }
};
} // namespace cg

namespace mce {
struct TextureDescription : cg::TextureDescription {
    SampleDescription sampleDescription{};
    Color clearColor{};
    float optimizedClearDepth{};
    std::uint8_t optimizedClearStencil{};
    BindFlagsBit bindFlags{BindFlagsBit::ShaderResourceBit};
    bool isStaging{};

    TextureDescription() = default;
    explicit TextureDescription(const cg::ImageDescription& image) : cg::TextureDescription(image) {}
};

struct TextureContainer {
    std::shared_ptr<cg::ImageResource> storage{};
    TextureDescription description{};
    bool valid{};

    explicit TextureContainer(cg::ImageBuffer& image)
        : storage(std::make_shared<cg::ImageResource>(image)),
          description(image.imageDescription), valid(true) {}
};

class BedrockTextureData {
public:
    ClientTexture clientTexture;
};

class BedrockTexture {
public:
    std::shared_ptr<BedrockTextureData> texture;
    std::shared_ptr<BedrockTextureData> mersTexture;
    std::shared_ptr<BedrockTextureData> normalTexture;
};

class TextureGroup {
public:
    BedrockTexture& uploadTexture(
        const ResourceLocation& location,
        TextureContainer& container,
        std::optional<std::string_view> debugName = {}) {
        using Fn = BedrockTexture&(__fastcall*)(TextureGroup*, const ResourceLocation&, TextureContainer&, std::optional<std::string_view>);
        auto address = mcbe::signatures::textureUpload();
        return reinterpret_cast<Fn>(address)(this, location, container, debugName);
    }
};
} // namespace mce

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
