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
};

class ScreenContext {
public:
    mce::MeshContext* toMeshContext() {
        return reinterpret_cast<mce::MeshContext*>(reinterpret_cast<std::uintptr_t>(this) + 0x10);
    }
    Tessellator* getTessellator() {
        return *reinterpret_cast<Tessellator**>(reinterpret_cast<std::uintptr_t>(this) + 0xB8);
    }
};

class GuiData {
public:
    Vec2 getMcResolution() const {
        return *reinterpret_cast<const Vec2*>(reinterpret_cast<std::uintptr_t>(this) + 0x40);
    }
    Vec2 getResolution() const {
        return *reinterpret_cast<const Vec2*>(reinterpret_cast<std::uintptr_t>(this) + 0x50);
    }
    float getScale() const {
        return *reinterpret_cast<const float*>(reinterpret_cast<std::uintptr_t>(this) + 0x5C);
    }
};

class ClientInstance {
public:
    GuiData* getGuiData() const {
        return *reinterpret_cast<GuiData* const*>(reinterpret_cast<std::uintptr_t>(this) + 0x650);
    }
};

class MinecraftUIRenderContext {
public:
    void** vtable{};                  // 0x00
    ClientInstance* clientInstance{}; // 0x08
    ScreenContext* screenContext{};   // 0x10
    std::byte pad_18[0x40]{};         // 0x18 -> 0x58
    std::shared_ptr<mce::TextureGroup> textureGroup; // 0x58

    void setClippingRectangle(const Rect& rect) {
        using Fn = void(__fastcall*)(MinecraftUIRenderContext*, const Rect&);
        reinterpret_cast<Fn>(vtable[22])(this, rect);
    }
    void saveCurrentClippingRectangle() {
        using Fn = void(__fastcall*)(MinecraftUIRenderContext*);
        reinterpret_cast<Fn>(vtable[24])(this);
    }
    void restoreSavedClippingRectangle() {
        using Fn = void(__fastcall*)(MinecraftUIRenderContext*);
        reinterpret_cast<Fn>(vtable[25])(this);
    }
};
static_assert(offsetof(MinecraftUIRenderContext, textureGroup) == 0x58);

struct ScreenView {
    std::byte pad_00[0x4]{};
    float deltaTime{};                 // 0x04
    std::byte pad_08[0x8]{};
    Vec2 screenScale{};                // 0x10
};
static_assert(offsetof(ScreenView, screenScale) == 0x10);

