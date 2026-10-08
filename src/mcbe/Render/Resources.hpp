#pragma once

#include "../Core/Types.hpp"

namespace mce {

template <typename T>
struct ResourceBlockTemplate;

template <typename T>
struct ResourcePointer {
    void* vtable{};
    std::shared_ptr<ResourceBlockTemplate<T>> resourcePointerBlock{};
};

template <typename T>
struct ClientResourcePointer : ResourcePointer<T> {};

struct ClientTexture : ClientResourcePointer<void*> {};
struct BufferResourceService {};
struct MeshContext {};

class BedrockTextureData;

class TexturePtr {

public:
    std::shared_ptr<BedrockTextureData> clientTexture;
    std::shared_ptr<ResourceLocation> resourceLocation;
};


} // namespace mce
