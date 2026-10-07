#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>
#include <mcbe_imgui_tess/Backend.hpp>

struct MockAdapter {
    struct V { float x,y,u,v; ImU32 c; };
    bool isReady=true;
    float scale=2.f;
    int beginFrames=0,endFrames=0,batches=0,resets=0;
    std::vector<std::vector<V>> emitted;
    std::vector<mcbe::imgui_tess::ClipRect> clips;
    int fontToken=1;
    bool ready() const { return isReady; }
    ImVec2 displaySizePixels() const { return {1920,1080}; }
    float uiScale() const { return scale; }
    bool createFontTexture(const unsigned char*,int,int,int){ return true; }
    ImTextureID fontTextureId() const { return (void*)&fontToken; }
    void beginFrame(){++beginFrames;}
    void endFrame(){++endFrames;}
    void resetRenderState(){++resets;}
    void beginBatch(std::size_t reserve,const mcbe::imgui_tess::ClipRect& c){ ++batches; clips.push_back(c); emitted.emplace_back(); emitted.back().reserve(reserve); }
    void emitVertex(float x,float y,float u,float v,ImU32 c){ emitted.back().push_back({x,y,u,v,c}); }
    void flushBatch(ImTextureID){}
};

int main(){
    MockAdapter a;
    mcbe::imgui_tess::Backend<MockAdapter> backend(a);
    assert(backend.initialize());
    backend.newFrame(0.01f);
    assert(ImGui::GetIO().DisplaySize.x==1920);

    ImDrawList list;
    list.VtxBuffer.storage = {
        {{20,40},{0,0},0xFFFFFFFF}, {{40,40},{1,0},0xFFFFFFFF}, {{40,60},{1,1},0xFFFFFFFF},
        {{20,60},{0,1},0xFFFFFFFF}
    }; list.VtxBuffer.sync();
    list.IdxBuffer.storage = {0,1,2,0,2,3, 0,1,2}; list.IdxBuffer.sync();
    int tex=9;
    ImDrawCmd a0; a0.ClipRect={10,20,210,220}; a0.TextureId=&tex; a0.ElemCount=6; a0.IdxOffset=0; a0.VtxOffset=0;
    ImDrawCmd a1=a0; a1.ElemCount=3; a1.IdxOffset=6;
    list.CmdBuffer.storage={a0,a1}; list.CmdBuffer.sync();
    ImDrawList* lists[]={&list};
    ImDrawData data; data.CmdListsCount=1; data.CmdLists=lists; data.DisplayPos={0,0};
    auto stats=backend.render(&data);
    assert(stats.batches==1);
    assert(stats.mergedCommands==1);
    assert(stats.submittedVertices==9);
    assert(a.beginFrames==1 && a.endFrames==1);
    assert(a.emitted.size()==1 && a.emitted[0].size()==9);
    assert(std::fabs(a.emitted[0][0].x-20.0f)<0.01f); // reversed first tri uses vertex 2: x=40 / scale 2
    assert(std::fabs(a.emitted[0][0].y-30.0f)<0.01f);
    assert(a.clips[0].left==5.f && a.clips[0].right==105.f);
    backend.shutdown();
}
