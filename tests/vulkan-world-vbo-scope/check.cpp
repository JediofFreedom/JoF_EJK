#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v); std::exit(1); } } while (0)
#define USE_VBO
typedef unsigned char byte;
enum {qfalse,qtrue,ERR_DROP,h_low,BSP_VERSION = 1};
enum {LUMP_SHADERS,LUMP_FOGS,LUMP_LIGHTMAPS,LUMP_SURFACES,LUMP_PLANES,LUMP_BRUSHES,LUMP_BRUSHSIDES,
    LUMP_DRAWVERTS,LUMP_DRAWINDEXES,LUMP_LEAFSURFACES,LUMP_NODES,LUMP_LEAFS,LUMP_MODELS,LUMP_VISIBILITY,
    LUMP_ENTITIES,LUMP_LIGHTGRID,LUMP_LIGHTARRAY,LUMP_COUNT};
struct lump_t { int fileofs,filelen; };
struct dheader_t { int ident,version; lump_t lumps[LUMP_COUNT]; };
struct msurface_t { int id,vboItemIndex; };
struct world_t { char name[64],baseName[64]; msurface_t *surfaces; int numsurfaces; ptrdiff_t dataSize; };
static world_t s_worldData;
static struct { bool worldMapLoaded,mapLoading; float sunDirection[3]; world_t *world; char worldDir[64]; } tr;
static int skyboxportal,c_gridVerts,builds,flushes,files,cacheClears,entities;
static byte *fileBase;
static void *cached;
static void *header() {
    auto *data = static_cast<dheader_t *>(std::calloc(1,sizeof(dheader_t))); CHECK(data);
    data->version = BSP_VERSION; ++files; return data;
}
static void release(void *data) { CHECK(data); std::free(data); --files; }
static void Com_Error(int,const char *,...) { throw std::runtime_error("world loader error"); }
static void Q_strncpyz(char *out,const char *in,size_t length) { std::snprintf(out,length,"%s",in); }
static const char *COM_SkipPath(const char *name) { const char *slash = std::strrchr(name,'/'); return slash ? slash+1 : name; }
static void COM_StripExtension(const char *in,char *out,size_t length) {
    if (in != out) Q_strncpyz(out,in,length);
    char *dot = std::strrchr(out,'.'); if (dot) *dot = 0;
}
static void VectorNormalize(float *) {}
static int LittleLong(int value) { return value; }
static void R_FlushTextureJobs() { ++flushes; }
static byte hunk[1];
static void *Hunk_Alloc(size_t,int) { return hunk; }
static void Z_Free(void *data) { release(data); }
static struct {
    static void *CM_GetCachedMapDiskImage() { return cached; }
    static void CM_SetCachedMapDiskImage(void *value) { CHECK(!value); ++cacheClears; cached = value; }
    static void FS_ReadFile(const char *,void **data) { *data = header(); }
    static void FS_FreeFile(void *data) { release(data); }
} ri;
static void R_LoadShaders(const lump_t *,world_t &) {}
static void R_PreLoadFogs(const lump_t *) {}
static void R_LoadLightmaps(const lump_t *,const lump_t *,world_t &) {}
static void R_LoadPlanes(const lump_t *,world_t &) {}
static void R_LoadFogs(const lump_t *,const lump_t *,const lump_t *,world_t &,int) {}
static msurface_t surfaces[3] = {{0,0},{1,0},{2,0}};
static void R_LoadSurfaces(const lump_t *,const lump_t *,const lump_t *,world_t &world,int index) {
    CHECK(index >= 0 && index < 3); world.surfaces = &surfaces[index]; world.numsurfaces = 1;
}
static void R_LoadMarksurfaces(const lump_t *,world_t &) {}
static void R_LoadNodesAndLeafs(const lump_t *,const lump_t *,world_t &) {}
static void R_LoadSubmodels(const lump_t *,world_t &,int) {}
static void R_LoadVisibility(const lump_t *,world_t &) {}
static void R_LoadEntities(const lump_t *,world_t &) { ++entities; }
static void R_LoadLightGrid(const lump_t *,world_t &) {}
static void R_LoadLightGridArray(const lump_t *,world_t &) {}
static void R_BuildWorldVBO(msurface_t *data,int count) {
    ++builds; CHECK(data == s_worldData.surfaces && count == 1);
    // The real builder's grid flush path depends on this phase. Its single
    // global GPU resource must survive all later instanced-BSP registrations.
    CHECK(tr.mapLoading); data->vboItemIndex = 1234;
}
#include "world_loader.h"
int main() {
    RE_LoadWorldMap_Actual("maps/main.bsp",s_worldData,0);
    CHECK(builds == 1 && surfaces[0].vboItemIndex == 1234 && !tr.mapLoading && tr.world == &s_worldData);
    CHECK(flushes == 2 && entities == 1 && files == 0);
    world_t first;
    cached = header();
    RE_LoadWorldMap_Actual("maps/sub-first.bsp",first,1);
    CHECK(builds == 1 && surfaces[0].vboItemIndex == 1234 && first.surfaces == &surfaces[1] && !surfaces[1].vboItemIndex);
    CHECK(!tr.mapLoading && tr.world == &s_worldData && flushes == 2 && entities == 1 && !cached && cacheClears == 1 && files == 0);
    world_t second;
    RE_LoadWorldMap_Actual("maps/sub-second.bsp",second,2);
    CHECK(builds == 1 && second.surfaces == &surfaces[2] && !surfaces[2].vboItemIndex && files == 0);
    bool rejected = false;
    try { RE_LoadWorldMap_Actual("maps/duplicate.bsp",s_worldData,0); } catch (const std::runtime_error &) { rejected = true; }
    CHECK(rejected && builds == 1 && !tr.mapLoading && tr.world == &s_worldData && files == 0);
    std::puts("Actual Vulkan loader retains main VBO across cached and file-backed instanced BSP registration.");
}
