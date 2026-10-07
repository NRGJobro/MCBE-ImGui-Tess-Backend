#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

using ImU32 = std::uint32_t;
using ImDrawIdx = std::uint16_t;
using ImTextureID = void*;

struct ImVec2 { float x{}, y{}; ImVec2()=default; ImVec2(float X,float Y):x(X),y(Y){} };
struct ImVec4 { float x{}, y{}, z{}, w{}; ImVec4()=default; ImVec4(float X,float Y,float Z,float W):x(X),y(Y),z(Z),w(W){} };
struct ImDrawVert { ImVec2 pos; ImVec2 uv; ImU32 col{}; };
struct ImDrawList;
struct ImDrawCmd;
using ImDrawCallback = void(*)(const ImDrawList*, const ImDrawCmd*);
inline void ResetCallback(const ImDrawList*, const ImDrawCmd*) {}
#define ImDrawCallback_ResetRenderState ResetCallback

template<class T> struct ImVector {
    int Size{};
    T* Data{};
    std::vector<T> storage;
    void push_back(const T& v){ storage.push_back(v); sync(); }
    void sync(){ Size=(int)storage.size(); Data=storage.data(); }
    T& operator[](int i){ return storage[(std::size_t)i]; }
    const T& operator[](int i) const { return storage[(std::size_t)i]; }
};
struct ImDrawCmd { ImVec4 ClipRect; ImTextureID TextureId{}; unsigned int VtxOffset{}; unsigned int IdxOffset{}; unsigned int ElemCount{}; ImDrawCallback UserCallback{}; };
struct ImDrawList { ImVector<ImDrawCmd> CmdBuffer; ImVector<ImDrawIdx> IdxBuffer; ImVector<ImDrawVert> VtxBuffer; };
struct ImDrawData { int CmdListsCount{}; ImDrawList** CmdLists{}; ImVec2 DisplayPos{}; ImVec2 DisplaySize{}; ImVec2 FramebufferScale{1,1}; };
struct ImFontAtlas { ImTextureID TexID{}; unsigned char pixel[4]{255,255,255,255}; void GetTexDataAsRGBA32(unsigned char** p,int* w,int* h,int* b){*p=pixel;*w=1;*h=1;*b=4;} void SetTexID(ImTextureID id){TexID=id;} };
enum { ImGuiBackendFlags_RendererHasVtxOffset = 1<<3 };
struct ImGuiIO { ImFontAtlas* Fonts{}; const char* BackendRendererName{}; int BackendFlags{}; ImVec2 DisplaySize{}; ImVec2 DisplayFramebufferScale{}; float DeltaTime{}; };
namespace ImGui { inline ImFontAtlas atlas; inline ImGuiIO io{&atlas}; inline ImGuiIO& GetIO(){return io;} }
