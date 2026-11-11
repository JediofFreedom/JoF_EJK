#include "vulkan/vulkan_core.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::exit(1);}++checks;}while(0)
static int checks;
#define ARRAY_LEN(v) (sizeof(v)/sizeof(*(v)))
#define GLS_DEPTHMASK_TRUE 1
#define VK_NUM_BLUR_PASSES 4
#define VK_DESC_COUNT 7
#define VK_DESC_UNIFORM 1
#define VK_DESC_UNIFORM_COUNT 6
#define VK_DESC_UNIFORM_MAIN_BINDING 0
#define VK_DESC_UNIFORM_CAMERA_BINDING 1
#define VK_DESC_UNIFORM_ENTITY_BINDING 2
#define VK_DESC_UNIFORM_BONES_BINDING 3
#define VK_DESC_UNIFORM_FOGS_BINDING 4
#define VK_DESC_UNIFORM_GLOBAL_BINDING 5
#define RENDER_PASS_MAIN 0
#define RENDER_PASS_SCREENMAP 1
#define DEPTH_RANGE_NORMAL 0
#define DEPTH_RANGE_ZERO 1
#define DEPTH_RANGE_COUNT 7
typedef int qboolean;
const qboolean qtrue=1,qfalse=0;
typedef int Vk_Depth_Range;
struct Cmd{
 VkCommandBuffer command_buffer;VkPipeline last_pipeline;VkRect2D scissor_rect;Vk_Depth_Range depth_range;
 struct{VkDescriptorSet current[VK_DESC_COUNT];uint32_t offset[VK_DESC_UNIFORM_COUNT];}descriptor_set;
 uint32_t num_indexes;VkBuffer indirect_buffer;
}command;
static struct State{
 Cmd*cmd;uint32_t geometry_buffer_size_new;int renderPassIndex;bool fboActive;
 struct{struct{unsigned state_bits;}def;}pipelines[2];
 VkPipeline bloom_extract_pipeline,bloom_blur_pipeline[8],bloom_blend_pipeline;
 VkPipeline dglow_blur_pipeline[8],dglow_blend_pipeline;
 VkDescriptorSet color_descriptor,bloom_image_descriptor[9],dglow_image_descriptor[9];VkImage dglow_image[9];
 VkPipelineLayout pipeline_layout_post_process,pipeline_layout_blend,pipeline_layout;
 struct{struct{VkRenderPass handle;}bloomBlend,dummy;struct{struct{VkRenderPass handle;}blend;}bloom,dglow;}render_pass;
}vk;
static struct{int dirty_depth_attachment;}vk_world;
static struct{bool doneBloom,doneSurfaces;}backEnd;
struct Shader{bool polygonOffset;}shader;
static struct{Shader*shader;int numVertexes,multiDrawPrimitives;size_t multiDrawFirstIndex[4];uint32_t multiDrawNumIndexes[4];}tess;
static struct{float value;}offsetSetting;
static decltype(offsetSetting)*r_offsetUnits=&offsetSetting,*r_offsetFactor=&offsetSetting;
static VkRect2D desired;
static int scissorSets,viewportSets,binds,staticBinds,indexedDraws;
static VkPipeline bound;
static bool nativeScissorValid,nativeViewportValid;
static VkPipeline handle(uintptr_t id){return (VkPipeline)id;}
static bool isStatic(VkPipeline pipeline){return uintptr_t(pipeline)>=100;}
static VkPipeline vk_gen_pipeline(uint32_t index){CHECK(index<2);return handle(index+1);}
static void qvkCmdBindPipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline pipeline){
 ++binds;bound=pipeline;
 // Vulkan invalidates a dynamic state value when a static-state pipeline is bound.
 if(isStatic(pipeline)){++staticBinds;nativeScissorValid=false;nativeViewportValid=false;}
}
static void get_scissor_rect(VkRect2D*out){*out=desired;}
static void get_viewport(VkViewport*out,Vk_Depth_Range){*out={};out->width=640;out->height=480;}
static void qvkCmdSetScissor(VkCommandBuffer,uint32_t,uint32_t,const VkRect2D*rect){CHECK(std::memcmp(rect,&desired,sizeof(desired))==0);++scissorSets;nativeScissorValid=true;}
static void qvkCmdSetViewport(VkCommandBuffer,uint32_t,uint32_t,const VkViewport*){++viewportSets;nativeViewportValid=true;}
static void qvkCmdDrawIndexed(VkCommandBuffer,uint32_t,uint32_t,uint32_t,int32_t,uint32_t){CHECK(!isStatic(bound));CHECK(nativeScissorValid&&nativeViewportValid);++indexedDraws;}
static void qvkCmdDraw(VkCommandBuffer,uint32_t,uint32_t,uint32_t,uint32_t){if(!isStatic(bound))CHECK(nativeScissorValid&&nativeViewportValid);}
static void qvkCmdDrawIndexedIndirect(VkCommandBuffer,VkBuffer,VkDeviceSize,uint32_t,uint32_t){CHECK(nativeScissorValid&&nativeViewportValid);}
static void qvkCmdSetDepthBias(VkCommandBuffer,float,float,float){}
static void vk_bind_descriptor_sets(){}
VkDrawIndexedIndirectCommand*vk_reserve_draw_indexed_indirect(uint32_t,uint32_t*){return nullptr;}
static void vk_end_render_pass(){}
static void vk_begin_bloom_extract_render_pass(){}
static void vk_begin_bloom_blur_render_pass(uint32_t){}
static void vk_begin_dglow_blur_render_pass(uint32_t){}
static void vk_begin_post_blend_render_pass(VkRenderPass,qboolean){}
static void vk_clear_depthstencil_attachments(qboolean){}
static void vk_update_mvp(const void*){}
static void vk_record_image_layout_transition(VkCommandBuffer,VkImage,VkImageAspectFlags,VkImageLayout,VkImageLayout,uint32_t,uint32_t){}
static void qvkCmdBindDescriptorSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t,const VkDescriptorSet*,uint32_t,const uint32_t*){}
#include "dynamic_state.h"
static void reset(){
 vk={};command={};vk.cmd=&command;command.depth_range=DEPTH_RANGE_COUNT;vk.fboActive=true;backEnd={};backEnd.doneSurfaces=true;shader={};tess={};tess.shader=&shader;tess.numVertexes=3;command.num_indexes=3;
 desired={};desired.extent={640,480};scissorSets=viewportSets=binds=staticBinds=indexedDraws=0;bound=VK_NULL_HANDLE;nativeScissorValid=nativeViewportValid=false;
 vk.bloom_extract_pipeline=handle(101);vk.bloom_blend_pipeline=handle(102);vk.dglow_blend_pipeline=handle(103);
 for(int i=0;i<8;++i){vk.bloom_blur_pipeline[i]=handle(110+i);vk.dglow_blur_pipeline[i]=handle(120+i);}
}
static void draw(){vk_bind_pipeline(0);vk_draw_geometry(DEPTH_RANGE_NORMAL,qtrue);}
static void postprocess(bool glow,bool changed){
 reset();draw();CHECK(scissorSets==1&&indexedDraws==1);
 if(glow)CHECK(vk_begin_dglow_blur());else CHECK(vk_bloom());
 CHECK(staticBinds>0&&!nativeScissorValid&&bound==handle(1));
 if(changed)desired.extent.width=320;
 draw();CHECK(scissorSets==2&&indexedDraws==2);
 draw();CHECK(scissorSets==2&&indexedDraws==3);
 // A subsequent dynamic-only pipeline change keeps the valid dynamic cache.
 vk_bind_pipeline(1);vk_draw_geometry(DEPTH_RANGE_NORMAL,qtrue);CHECK(scissorSets==2&&indexedDraws==4);
}
int main(int argc,char**argv){std::string part=argc>1?argv[1]:"all";
 if(part=="all"||part=="bloom")postprocess(false,false);
 if(part=="all"||part=="glow")postprocess(true,false);
 if(part=="all"||part=="changed"){postprocess(false,true);postprocess(true,true);}
 if(part=="all"||part=="fastpath"){
  reset();for(int i=0;i<100;++i)draw();CHECK(scissorSets==1&&viewportSets==1&&binds==1&&indexedDraws==100);
  vk.renderPassIndex=RENDER_PASS_SCREENMAP;CHECK(!vk_bloom()&&!vk_begin_dglow_blur());CHECK(staticBinds==0);draw();CHECK(scissorSets==1);
 }
 std::printf("%d actual static/dynamic pipeline transition checks passed\n",checks);
}
