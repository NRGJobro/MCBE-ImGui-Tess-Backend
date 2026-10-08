#pragma once

#include "Resources.hpp"
#include "../Version/Signatures.hpp"

namespace mce {
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

static_assert(sizeof(cg::ImageBuffer) == 0x30,
    "ImageBuffer ABI mismatch for MCBE 26.52.");
static_assert(sizeof(TextureDescription) == 0x40,
    "TextureDescription ABI mismatch for MCBE 26.52.");
static_assert(sizeof(TextureContainer) == 0x58,
    "TextureContainer ABI mismatch for MCBE 26.52.");

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
    BedrockTexture& uploadTexture(const ResourceLocation& location, cg::ImageBuffer& buffer) {
        TextureContainer container(buffer);
        return uploadTexture(location, container, {});
    }

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

