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
#include "dynamic_config.h"
typedef int qboolean;
const qboolean qtrue=1,qfalse=0;
struct Cmd{
 VkCommandBuffer command_buffer;VkPipeline last_pipeline;VkRect2D scissor_rect;Vk_Depth_Range depth_range;
 struct{VkDescriptorSet current[VK_DESC_COUNT];uint32_t offset[VK_DESC_UNIFORM_COUNT];uint32_t start,end;}descriptor_set;
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
static bool nativeScissorValid,nativeViewportValid,usesFog;
struct Descriptor{VkPipelineLayout layout;VkDescriptorSet handle;bool valid;}nativeDescriptor[VK_DESC_COUNT];
struct Image{VkDescriptorSet descriptor_set;}whiteImage;
static struct{Image*whiteImage;}tr;
static VkPipelineLayout layout(uintptr_t id){return (VkPipelineLayout)id;}
static VkDescriptorSet descriptor(uintptr_t id){return (VkDescriptorSet)id;}
static bool compatible(VkPipelineLayout first,VkPipelineLayout second){
 // Main has uniform set0 and a push range; both postFX layouts have sampler set0 and no push range.
 return (first==vk.pipeline_layout)==(second==vk.pipeline_layout);
}
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
static void qvkCmdDrawIndexed(VkCommandBuffer,uint32_t,uint32_t,uint32_t,int32_t,uint32_t){CHECK(!isStatic(bound));CHECK(nativeScissorValid&&nativeViewportValid);if(usesFog)CHECK(nativeDescriptor[VK_DESC_FOG_COLLAPSE].valid&&nativeDescriptor[VK_DESC_FOG_COLLAPSE].layout==vk.pipeline_layout&&nativeDescriptor[VK_DESC_FOG_COLLAPSE].handle==descriptor(50));++indexedDraws;}
static void qvkCmdDraw(VkCommandBuffer,uint32_t,uint32_t,uint32_t,uint32_t){if(!isStatic(bound))CHECK(nativeScissorValid&&nativeViewportValid);}
static void qvkCmdDrawIndexedIndirect(VkCommandBuffer,VkBuffer,VkDeviceSize,uint32_t,uint32_t){CHECK(nativeScissorValid&&nativeViewportValid);}
static void qvkCmdSetDepthBias(VkCommandBuffer,float,float,float){}
VkDrawIndexedIndirectCommand*vk_reserve_draw_indexed_indirect(uint32_t,uint32_t*){return nullptr;}
static void vk_end_render_pass(){}
static void vk_begin_bloom_extract_render_pass(){}
static void vk_begin_bloom_blur_render_pass(uint32_t){}
static void vk_begin_dglow_blur_render_pass(uint32_t){}
static void vk_begin_post_blend_render_pass(VkRenderPass,qboolean){}
static void vk_clear_depthstencil_attachments(qboolean){}
static void vk_update_mvp(const void*){}
static void vk_record_image_layout_transition(VkCommandBuffer,VkImage,VkImageAspectFlags,VkImageLayout,VkImageLayout,uint32_t,uint32_t){}
static void qvkCmdBindDescriptorSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout pipelineLayout,uint32_t first,uint32_t count,const VkDescriptorSet*sets,uint32_t,const uint32_t*){
 CHECK(count>0&&first+count<=VK_DESC_COUNT);
 for(uint32_t n=first;n<first+count;++n){
  CHECK(sets[n-first]!=VK_NULL_HANDLE);
  for(uint32_t m=0;m<n;++m)if(!compatible(nativeDescriptor[m].layout,pipelineLayout))nativeDescriptor[m]={pipelineLayout,VK_NULL_HANDLE,false};
  if(!compatible(nativeDescriptor[n].layout,pipelineLayout))for(uint32_t m=n+1;m<VK_DESC_COUNT;++m)nativeDescriptor[m]={pipelineLayout,VK_NULL_HANDLE,false};
  nativeDescriptor[n]={pipelineLayout,sets[n-first],true};
 }
}
#include "dynamic_state.h"
static void reset(){
 vk={};command={};vk.cmd=&command;command.depth_range=DEPTH_RANGE_COUNT;command.descriptor_set.start=~0U;vk.fboActive=true;backEnd={};backEnd.doneSurfaces=true;shader={};tess={};tess.shader=&shader;tess.numVertexes=3;command.num_indexes=3;
 desired={};desired.extent={640,480};scissorSets=viewportSets=binds=staticBinds=indexedDraws=0;bound=VK_NULL_HANDLE;nativeScissorValid=nativeViewportValid=usesFog=false;
 vk.bloom_extract_pipeline=handle(101);vk.bloom_blend_pipeline=handle(102);vk.dglow_blend_pipeline=handle(103);
 vk.pipeline_layout=layout(1);vk.pipeline_layout_post_process=layout(2);vk.pipeline_layout_blend=layout(3);
 whiteImage.descriptor_set=descriptor(99);tr.whiteImage=&whiteImage;vk.color_descriptor=descriptor(100);
 for(uint32_t i=0;i<VK_DESC_COUNT;++i)nativeDescriptor[i]={vk.pipeline_layout,VK_NULL_HANDLE,false};
 for(int i=0;i<9;++i){vk.bloom_image_descriptor[i]=descriptor(101+i);vk.dglow_image_descriptor[i]=descriptor(120+i);}
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
static void fog_restore(bool glow,bool fog){
 reset();usesFog=fog;
 vk_update_descriptor(VK_DESC_UNIFORM,descriptor(40));
 vk_update_descriptor(VK_DESC_TEXTURE0,descriptor(41));
 if(fog)vk_update_descriptor(VK_DESC_FOG_COLLAPSE,descriptor(50));
 draw();CHECK(command.descriptor_set.start==~0U);
 if(glow)CHECK(vk_begin_dglow_blur());else CHECK(vk_bloom());
 if(fog){
  // Actual fogged stage pushes uniform data again, while reusing the same fog texture.
  vk_reset_descriptor(VK_DESC_UNIFORM);
  vk_update_descriptor(VK_DESC_UNIFORM,descriptor(40));
  vk_update_descriptor(VK_DESC_FOG_COLLAPSE,descriptor(50));
  CHECK(command.descriptor_set.start==VK_DESC_UNIFORM&&command.descriptor_set.end==VK_DESC_UNIFORM);
 }
 draw();CHECK(indexedDraws==2);
 if(!fog)CHECK(command.descriptor_set.current[VK_DESC_FOG_COLLAPSE]==VK_NULL_HANDLE);
}
int main(int argc,char**argv){std::string part=argc>1?argv[1]:"all";
 if(part=="all"||part=="fog_bloom")fog_restore(false,true);
 if(part=="all"||part=="fog_glow")fog_restore(true,true);
 if(part=="all"||part=="no_fog"){fog_restore(false,false);fog_restore(true,false);}
 if(part=="all"||part=="bloom")postprocess(false,false);
 if(part=="all"||part=="glow")postprocess(true,false);
 if(part=="all"||part=="changed"){postprocess(false,true);postprocess(true,true);}
 if(part=="all"||part=="fastpath"){
  reset();for(int i=0;i<100;++i)draw();CHECK(scissorSets==1&&viewportSets==1&&binds==1&&indexedDraws==100);
  vk.renderPassIndex=RENDER_PASS_SCREENMAP;CHECK(!vk_bloom()&&!vk_begin_dglow_blur());CHECK(staticBinds==0);draw();CHECK(scissorSets==1);
 }
 std::printf("%d actual static/dynamic pipeline transition checks passed\n",checks);
}
