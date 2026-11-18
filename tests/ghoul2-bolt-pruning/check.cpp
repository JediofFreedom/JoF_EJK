#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v); std::exit(1); } } while (0)
typedef int qboolean;
enum {qfalse,qtrue,G2SURFACEFLAG_GENERATED = 0x200};
struct surfaceInfo_t {
    int surface, offFlags;
    float genBarycentricI, genBarycentricJ;
    int genPolySurfaceIndex, genLod;
};
struct boltInfo_t { int boneNumber = -1, surfaceNumber = -1, surfaceType = 0, boltUsed = 0; };
typedef std::vector<surfaceInfo_t> surfaceInfo_v;
typedef std::vector<boltInfo_t> boltInfo_v;
struct CGhoul2Info { bool mValid = true; surfaceInfo_v mSlist; };
static int G2_DecideTraceLod(CGhoul2Info &, int lod) { return lod; }
namespace dedicated {
#include "dedicated.h"
}
namespace vanilla {
#include "vanilla.h"
}
namespace rend2 {
#include "rend2.h"
}
namespace vulkan {
#include "vulkan.h"
}
typedef int (*AddSurface)(CGhoul2Info *,int,int,float,float,int);
typedef int (*AddBolt)(CGhoul2Info *,boltInfo_v &,surfaceInfo_v &,int);
typedef qboolean (*RemoveSurface)(surfaceInfo_v &,int);
typedef void (*Prune)(boltInfo_v &,surfaceInfo_v &,int *,int *);
static boltInfo_t original(int surface, int bone) {
    boltInfo_t bolt; bolt.surfaceNumber = surface; bolt.boneNumber = bone; bolt.boltUsed = 3; return bolt;
}
static void check(AddSurface addSurface,AddBolt addBolt,RemoveSurface removeSurface,Prune prune) {
    CGhoul2Info model;
    // These are list slots, not model surface numbers. All generated entries
    // have the marker10000 and reference triangle0 of model surface0.
    for (int slot = 0; slot < 8; ++slot) CHECK(addSurface(&model,0,0,0.25f,0.25f,0) == slot);
    boltInfo_v bolts;
    CHECK(addBolt(&model,bolts,model.mSlist,7) == 0);
    CHECK(addBolt(&model,bolts,model.mSlist,7) == 0 && bolts[0].boltUsed == 2);
    std::vector<int> activeSurfaces(1,1), activeBones(2,1);
    prune(bolts,model.mSlist,activeSurfaces.data(),activeBones.data());
    CHECK(bolts.size() == 1 && bolts[0].surfaceNumber == 7 && bolts[0].boltUsed == 2);
    // An independent model-surface override with the same numeric id cannot
    // keep a deleted generated slot alive.
    CHECK(removeSurface(model.mSlist,7));
    model.mSlist[0].surface = 7; model.mSlist[0].offFlags = 0;
    prune(bolts,model.mSlist,activeSurfaces.data(),activeBones.data());
    CHECK(bolts.empty());

    // A hole in the middle must not be mistaken for a live slot. Both a live
    // tail and an ordinary bone bolt survive the forced removal before them.
    model = CGhoul2Info();
    for (int slot = 0; slot < 3; ++slot) CHECK(addSurface(&model,0,0,0.25f,0.25f,0) == slot);
    CHECK(addBolt(&model,bolts,model.mSlist,1) == 0);
    CHECK(addBolt(&model,bolts,model.mSlist,2) == 1);
    bolts.push_back(original(-1,0));
    CHECK(removeSurface(model.mSlist,1) && model.mSlist.size() == 3);
    prune(bolts,model.mSlist,activeSurfaces.data(),activeBones.data());
    CHECK(bolts.size() == 3 && bolts[0].surfaceNumber == -1 && bolts[1].surfaceNumber == 2 && bolts[2].boneNumber == 0);

    // Original model-surface and bone indices retain their separate arrays.
    model = CGhoul2Info(); bolts.clear();
    bolts.push_back(original(0,-1)); bolts.push_back(original(-1,0)); bolts.push_back(original(-1,1));
    activeSurfaces[0] = 0; activeBones[0] = 0;
    prune(bolts,model.mSlist,activeSurfaces.data(),activeBones.data());
    CHECK(bolts.size() == 3 && bolts[0].surfaceNumber == -1 && bolts[1].boneNumber == -1 && bolts[2].boneNumber == 1);
    CHECK(bolts[2].boltUsed == 3);
    activeBones[1] = 0;
    prune(bolts,model.mSlist,activeSurfaces.data(),activeBones.data());
    CHECK(bolts.empty());
}
int main() {
    check(dedicated::G2_AddSurface,dedicated::G2_Add_Bolt_Surf_Num,dedicated::G2_RemoveSurface,dedicated::G2_RemoveRedundantBolts);
    check(vanilla::G2_AddSurface,vanilla::G2_Add_Bolt_Surf_Num,vanilla::G2_RemoveSurface,vanilla::G2_RemoveRedundantBolts);
    check(rend2::G2_AddSurface,rend2::G2_Add_Bolt_Surf_Num,rend2::G2_RemoveSurface,rend2::G2_RemoveRedundantBolts);
    check(vulkan::G2_AddSurface,vulkan::G2_Add_Bolt_Surf_Num,vulkan::G2_RemoveSurface,vulkan::G2_RemoveRedundantBolts);
    std::puts("All renderer generated/original surface and bone bolt pruning checks passed.");
}
