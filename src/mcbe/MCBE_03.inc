using TextureBinding = std::variant<std::monostate, TexturePtr, ClientTexture>;
using OffscreenVariant = std::variant<std::monostate, UIActorOffscreenCaptureDescription,
    UIThumbnailMeshOffscreenCaptureDescription, UIMeshOffscreenCaptureDescription,
    UIStructureVolumeOffscreenCaptureDescription>;

class Mesh : public IndexBufferContainer {
public:
    std::variant<std::monostate, std::uint64_t, Vec3> cacheKey;
    bool temporary{};
    PrimitiveMode primitiveMode{PrimitiveMode::None};
    std::weak_ptr<BufferResourceService> bufferResourceService;
    MeshData meshData;
    ClientResourcePointer<void*> vertexLayout;
    ClientResourcePointer<void*> vertexBuffer;
    std::optional<std::uint32_t> vertexCount;
    VertexFormat layoutFormat;
    VertexFormat bufferFormat;
    VertexFormat unknownFormat;
    std::vector<std::uint8_t> rawData;

    void renderMesh(MeshContext* context, MaterialPtr* material) const {
        if (!context || !material)
            return;

        StaticVector<TextureBinding, 8> textures;
        renderMeshFull(
            context,
            material,
            textures,
            0,
            0,
            UIMeshOffscreenCaptureDescription{},
            nullptr,
            {});
    }

    void renderMesh(MeshContext* context, MaterialPtr* material, const ClientTexture& texture) const {
        if (!context || !material)
            return;

        StaticVector<TextureBinding, 8> textures;
        textures.push_back(texture);
        renderMeshFull(
            context,
            material,
            textures,
            0,
            0,
            UIMeshOffscreenCaptureDescription{},
            nullptr,
            {});
    }

private:
    void renderMeshFull(
        MeshContext* context,
        MaterialPtr* material,
        StaticVector<TextureBinding, 8> textures,
        std::uint32_t startOffset,
        std::uint32_t count,
        const OffscreenVariant& offscreen,
        void* overrideIndexBuffer,
        const std::optional<dragon_stub::RenderMetadata>& metadata) const {

        const auto address = mcbe::signatures::meshRender();
        if (!address || !context || !material)
            return;

        using Method = decltype(&Mesh::renderMeshFull);
        static_assert(sizeof(Method) == sizeof(std::uintptr_t),
            "Unexpected member-function-pointer ABI for mce::Mesh.");

        const auto method = std::bit_cast<Method>(address);
        (this->*method)(
            context,
            material,
            textures,
            startOffset,
            count,
            offscreen,
            overrideIndexBuffer,
            metadata);
    }
};

enum class ImageFormat : std::uint32_t {
    UnknownFormat, R8Unorm, RG8Unorm, RGB8Unorm, RGBA8Unorm, RGBA16Float
};
enum class ImageUsage : std::uint8_t { UnknownUsage = 0, sRGB = 1, Data = 2 };

enum class TextureFormat : std::uint32_t {
    Unknown = 0,
    R8g8b8a8Unorm = 28
};

class Blob {
public:
    using pointer = std::uint8_t*;
    using delete_function = void(*)(pointer);
    struct Deleter {
        delete_function fn;
        Deleter() : fn(defaultDeleter) {}
        explicit Deleter(delete_function value) : fn(value) {}
        void operator()(pointer p) const { fn(p); }
        static void defaultDeleter(pointer p) { delete[] p; }
    };
    using pointer_type = std::unique_ptr<std::uint8_t[], Deleter>;

    pointer_type blob{};
    std::size_t size{};

    Blob() = default;
    Blob(pointer data, std::size_t length) : blob(data), size(length) {}
    Blob(const Blob& other) { *this = other; }
    Blob& operator=(const Blob& other) {
        if (this == &other) return *this;
        size = other.size;
        if (!size) { blob.reset(); return *this; }
        auto* copy = new std::uint8_t[size];
        std::memcpy(copy, other.blob.get(), size);
        blob.reset(copy);
        return *this;
    }
};

struct Image {
    ImageFormat imageFormat{ImageFormat::UnknownFormat};
    std::uint32_t width{}, height{}, depth{};
    ImageUsage usage{ImageUsage::UnknownUsage};
    Blob imageData{};
};

struct Color {
    float r{1.f}, g{1.f}, b{1.f}, a{1.f};
};

struct SampleDescription { int count{1}; int quality{}; };
enum class BindFlagsBit : std::uint32_t { ShaderResourceBit = 0x8 };

} // namespace mce

namespace cg {
enum class ColorSpace : std::int8_t { Unknown = 0, sRGB = 1, Linear = 2 };
enum class ImageType : std::uint8_t { Texture2D, CubemapDeprecated, Texture3D, TextureCube };

struct ImageDescription {
    std::uint32_t width{}, height{};
    mce::TextureFormat textureFormat{mce::TextureFormat::Unknown};
    ColorSpace colorSpace{ColorSpace::Unknown};
    ImageType imageType{ImageType::Texture2D};
