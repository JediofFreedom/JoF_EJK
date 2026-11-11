#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "game/surfaceflags.h"
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#value); std::exit(1); } } while (0)
typedef unsigned char byte;
typedef float vec3_t[3];
typedef vec3_t matrix3_t[3];
enum { PLANE_X,PLANE_Y,PLANE_Z,PLANE_NON_AXIAL };
struct cplane_t { vec3_t normal; float dist; byte type,signbits,pad[2]; };
enum surfaceType_t { SF_BAD,SF_SKIP,SF_FACE,SF_GRID,SF_TRIANGLES };
static size_t shaderReads,planeTests,normalTests;
struct TrackedFlags {
    int value = 0;
    TrackedFlags &operator=(int v) { value = v; return *this; }
    operator int() const { ++shaderReads; return value; }
};
struct shader_t { TrackedFlags surfaceFlags,contentFlags; };
// Only fields used by the actual collector are needed in these fixtures.
struct srfSurfaceFace_t { surfaceType_t type; cplane_t plane; };
struct msurface_t { int viewCount = 0; shader_t *shader; surfaceType_t *data; };
struct mnode_t { int contents = 0; cplane_t *plane; mnode_t *children[2]; msurface_t **firstmarksurface; int nummarksurfaces; };
static struct { int viewCount; } tr;
#include "primitives.h"
static int BoxOnPlaneSide(vec3_t mins,vec3_t maxs,cplane_t *plane) { ++planeTests; return MathBoxOnPlaneSide(mins,maxs,plane); }
static float DotProduct(const vec3_t a,const vec3_t b) { ++normalTests; return MathDotProduct(a,b); }
namespace baseline {
#include "baseline-surfaces.h"
}
namespace vulkan {
#include "vulkan.h"
}
namespace vanilla {
#include "vanilla.h"
}
typedef void (*Collect)(mnode_t *,vec3_t,vec3_t,surfaceType_t **,int,int *,vec3_t);
struct World {
    std::vector<cplane_t> planes;
    std::vector<shader_t> shaders;
    std::vector<srfSurfaceFace_t> data;
    std::vector<msurface_t> surfaces;
    std::vector<mnode_t> nodes;
    std::vector<std::vector<msurface_t *>> leafRefs;
};
static cplane_t plane(float x,float y,float z,float distance) {
    cplane_t p = {{x,y,z},distance,0,0,{0,0}};
    p.type = byte(PlaneTypeForNormal(p.normal)); SetPlaneSignbits(&p); return p;
}
static World synthetic() {
    World w;
    w.planes = {plane(1,0,0,0),plane(0,1,0,0),plane(0,1,0,0)};
    w.shaders.resize(10); w.data.resize(10); w.surfaces.resize(10); w.nodes.resize(7); w.leafRefs.resize(4);
    for (int i = 0; i < 10; ++i) {
        w.data[i] = {SF_FACE,plane(0,0,1,0)};
        w.surfaces[i].shader = &w.shaders[i]; w.surfaces[i].data = &w.data[i].type;
    }
    w.data[1].type = SF_GRID; w.data[2].type = SF_TRIANGLES;
    w.shaders[3].surfaceFlags = SURF_NOIMPACT; w.shaders[9].surfaceFlags = SURF_NOMARKS;
    w.data[4].plane = plane(0,0,-1,0); w.data[5].plane = plane(0,0,1,100);
    w.data[6].type = SF_SKIP; w.shaders[7].contentFlags = CONTENTS_FOG;
    w.data[8].plane = plane(0.4f,0,0.9f,0); // Nonaxial face.
    for (int i = 0; i < 3; ++i) {
        w.nodes[i].contents = -1; w.nodes[i].plane = &w.planes[i];
        w.nodes[i].children[0] = &w.nodes[2*i+1]; w.nodes[i].children[1] = &w.nodes[2*i+2];
    }
    const int refs[4][10] = {{0,1,3,4,5,6,7,8,9,0},{0,2,3,4,5,6,7,8,9,-1},{8,1,2,0,3,4,5,6,7,9},{0,1,2,3,4,5,6,7,8,9}};
    for (int leaf = 0; leaf < 4; ++leaf) {
        for (int id : refs[leaf]) if (id >= 0) w.leafRefs[leaf].push_back(&w.surfaces[id]);
        w.nodes[leaf+3].firstmarksurface = w.leafRefs[leaf].data();
        w.nodes[leaf+3].nummarksurfaces = int(w.leafRefs[leaf].size());
    }
    return w;
}
struct Result {
    int count;
    std::array<surfaceType_t *,64> list;
    std::vector<int> views;
    size_t shaderReads,planeTests,normalTests;
};
static surfaceType_t sentinel = SF_BAD;
static Result collect(Collect fn,World &w,vec3_t mins,vec3_t maxs,vec3_t direction,int capacity,int initial,int view) {
    Result result; result.count = initial; result.list.fill(&sentinel);
    tr.viewCount = view; shaderReads = planeTests = normalTests = 0;
    fn(&w.nodes[0],mins,maxs,result.list.data(),capacity,&result.count,direction);
    result.shaderReads = shaderReads; result.planeTests = planeTests; result.normalTests = normalTests;
    for (const auto &surface : w.surfaces) result.views.push_back(surface.viewCount);
    return result;
}
static Result compare(Collect subject,World &w,vec3_t mins,vec3_t maxs,vec3_t direction,int capacity,int initial,int view) {
    std::vector<int> before; for (const auto &surface : w.surfaces) before.push_back(surface.viewCount);
    Result expected = collect(baseline::R_BoxSurfaces_r,w,mins,maxs,direction,capacity,initial,view);
    for (size_t i = 0; i < before.size(); ++i) w.surfaces[i].viewCount = before[i];
    Result actual = collect(subject,w,mins,maxs,direction,capacity,initial,view);
    CHECK(actual.count == expected.count && actual.list == expected.list && actual.views == expected.views);
    CHECK(actual.shaderReads <= expected.shaderReads && actual.planeTests <= expected.planeTests && actual.normalTests <= expected.normalTests);
    return actual;
}
static void fixture(Collect subject) {
    World w = synthetic();
    vec3_t mins = {-2,-2,-2},maxs = {2,2,2},down = {0,0,-1};
    Result expected = collect(baseline::R_BoxSurfaces_r,w,mins,maxs,down,64,0,23);
    CHECK(expected.count == 4 && expected.list[0] == &w.data[0].type && expected.list[1] == &w.data[1].type && expected.list[2] == &w.data[8].type && expected.list[3] == &w.data[2].type);
    for (auto &surface : w.surfaces) surface.viewCount = 0;
    Result actual = compare(subject,w,mins,maxs,down,64,0,23);
    CHECK(actual.shaderReads < expected.shaderReads && actual.planeTests < expected.planeTests && actual.normalTests < expected.normalTests);
    std::printf("Duplicate-leaf work: shader reads %zu->%zu, plane tests %zu->%zu, normal tests %zu->%zu; exact list/order/markers retained.\n",expected.shaderReads,actual.shaderReads,expected.planeTests,actual.planeTests,expected.normalTests,actual.normalTests);
    actual = compare(subject,w,mins,maxs,down,64,0,23); CHECK(actual.count == 0);
    // A new projection gets a new viewCount and must revisit changed flags.
    w.shaders[0].surfaceFlags = SURF_NOIMPACT;
    actual = compare(subject,w,mins,maxs,down,64,0,24); CHECK(actual.count == 3);
    for (int capacity : {0,1,2,3,4,5,64}) for (int initial : {0,capacity}) {
        for (size_t i = 0; i < w.surfaces.size(); ++i) w.surfaces[i].viewCount = i%3 ? 0 : 31;
        compare(subject,w,mins,maxs,down,capacity,initial,31);
    }
    const vec3_t directions[] = {{0,0,1},{1,0,0},{0,0,-0.5f},{0.3f,0.4f,-0.5f}};
    int view = 50;
    for (const auto &d : directions) {
        vec3_t direction = {d[0],d[1],d[2]}; compare(subject,w,mins,maxs,direction,64,0,view++);
    }
    // One-sided node traversal and a nonzero prefilled output prefix.
    mins[0] = 10; maxs[0] = 20; compare(subject,w,mins,maxs,down,64,2,view++);
    mins[0] = -20; maxs[0] = -10; compare(subject,w,mins,maxs,down,64,2,view++);
    // The same query with a fresh call marker must generate a full list again.
    mins[0] = -2; maxs[0] = 2; compare(subject,w,mins,maxs,down,64,0,view++);
}

// Optional installed-map investigation. No game assets are bundled in tests.
#define MAX_QPATH 64
#define MAXLIGHTMAPS 4
#define HEADER_LUMPS 18
#include "bsp_types.h"
template<class T> static std::vector<T> lump(const std::vector<byte> &file,const dheader_t &header,int id) {
    const auto &l = header.lumps[id]; CHECK(l.fileofs >= 0 && l.filelen >= 0 && l.filelen%sizeof(T) == 0);
    CHECK(size_t(l.fileofs) <= file.size() && size_t(l.filelen) <= file.size()-size_t(l.fileofs));
    std::vector<T> result(size_t(l.filelen)/sizeof(T)); if (l.filelen) std::memcpy(result.data(),file.data()+l.fileofs,size_t(l.filelen)); return result;
}
static void installedBsp(const char *path) {
    std::ifstream input(path,std::ios::binary); CHECK(input.good());
    std::vector<byte> file((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    CHECK(file.size() >= sizeof(dheader_t)); dheader_t header; std::memcpy(&header,file.data(),sizeof(header));
    CHECK(header.ident == int(('P'<<24)+('S'<<16)+('B'<<8)+'R') && header.version == 1);
    const auto dp = lump<dplane_t>(file,header,2); const auto dn = lump<dnode_t>(file,header,3);
    const auto dl = lump<dleaf_t>(file,header,4); const auto refs = lump<int>(file,header,5);
    const auto ds = lump<dsurface_t>(file,header,13); const auto shaders = lump<dshader_t>(file,header,1);
    const auto vertices = lump<mapVert_t>(file,header,10); CHECK(!dn.empty());
    World w; w.planes.resize(dp.size()); w.nodes.resize(dn.size()+dl.size());
    w.shaders.resize(shaders.size()); w.data.resize(ds.size()); w.surfaces.resize(ds.size()); w.leafRefs.resize(dl.size());
    for (size_t i = 0; i < dp.size(); ++i) w.planes[i] = plane(dp[i].normal[0],dp[i].normal[1],dp[i].normal[2],dp[i].dist);
    for (size_t i = 0; i < shaders.size(); ++i) { w.shaders[i].surfaceFlags = shaders[i].surfaceFlags; w.shaders[i].contentFlags = shaders[i].contentFlags; }
    for (size_t i = 0; i < ds.size(); ++i) {
        const auto &s = ds[i]; CHECK(s.shaderNum >= 0 && size_t(s.shaderNum) < shaders.size());
        w.data[i].type = s.surfaceType == 1 ? SF_FACE : s.surfaceType == 2 ? SF_GRID : s.surfaceType == 3 ? SF_TRIANGLES : SF_SKIP;
        if (w.data[i].type == SF_FACE) {
            CHECK(s.firstVert >= 0 && size_t(s.firstVert) < vertices.size()); vec3_t normal;
            for (int j = 0; j < 3; ++j) normal[j] = R_ClampDenorm(s.lightmapVecs[2][j]);
            w.data[i].plane = plane(normal[0],normal[1],normal[2],MathDotProduct(vertices[s.firstVert].xyz,normal));
        }
        w.surfaces[i].shader = &w.shaders[s.shaderNum]; w.surfaces[i].data = &w.data[i].type;
    }
    for (size_t i = 0; i < dl.size(); ++i) {
        const auto &leaf = dl[i]; CHECK(leaf.firstLeafSurface >= 0 && leaf.numLeafSurfaces >= 0 && size_t(leaf.firstLeafSurface)+size_t(leaf.numLeafSurfaces) <= refs.size());
        for (int j = 0; j < leaf.numLeafSurfaces; ++j) { const int id = refs[leaf.firstLeafSurface+j]; CHECK(id >= 0 && size_t(id) < ds.size()); w.leafRefs[i].push_back(&w.surfaces[id]); }
        auto &node = w.nodes[dn.size()+i]; node.firstmarksurface = w.leafRefs[i].data(); node.nummarksurfaces = leaf.numLeafSurfaces;
    }
    for (size_t i = 0; i < dn.size(); ++i) {
        CHECK(dn[i].planeNum >= 0 && size_t(dn[i].planeNum) < dp.size());
        auto &node = w.nodes[i]; node.contents = -1; node.plane = &w.planes[dn[i].planeNum];
        for (int j = 0; j < 2; ++j) { const int child = dn[i].children[j]; const size_t id = child >= 0 ? size_t(child) : dn.size()+size_t(-1-child); CHECK(id < w.nodes.size()); node.children[j] = &w.nodes[id]; }
    }
    size_t oldReads = 0,newReads = 0,oldPlanes = 0,newPlanes = 0; int view = 100;
    vec3_t direction = {0,0,-1};
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 6; ++column) {
        const float x = float(632+80*column),y = float(320+72*row);
        vec3_t mins = {x-34,y-34,-20},maxs = {x+34,y+34,20};
        std::vector<int> before; for (const auto &s : w.surfaces) before.push_back(s.viewCount);
        Result original = collect(baseline::R_BoxSurfaces_r,w,mins,maxs,direction,64,0,view);
        for (size_t i = 0; i < before.size(); ++i) w.surfaces[i].viewCount = before[i];
        Result actual = compare(vulkan::R_BoxSurfaces_r,w,mins,maxs,direction,64,0,view++);
        oldReads += original.shaderReads; newReads += actual.shaderReads; oldPlanes += original.planeTests; newPlanes += actual.planeTests;
    }
    CHECK(newReads < oldReads);
    std::printf("Installed BSP crowd-region collector queries: shader reads %zu->%zu, plane tests %zu->%zu. BSP shader flags used; live shader-script overrides are outside this offline count.\n",oldReads,newReads,oldPlanes,newPlanes);
}
int main(int argc,char **argv) {
    CHECK(argc == 1 || (argc == 3 && std::strcmp(argv[1],"--bsp") == 0));
    fixture(vulkan::R_BoxSurfaces_r); fixture(vanilla::R_BoxSurfaces_r);
    if (argc == 3 && std::strcmp(argv[1],"--bsp") == 0) installedBsp(argv[2]);
    std::puts("Actual surface collectors preserve ordering, capacity, markers and per-projection invalidation.");
}
