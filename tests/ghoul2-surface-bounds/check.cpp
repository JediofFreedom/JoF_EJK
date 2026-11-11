#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v); std::exit(1); } } while (0)
typedef int qboolean;
enum {qfalse,qtrue,G2SURFACEFLAG_GENERATED = 0x200};
struct CGhoul2Info { bool mValid = true; };
struct surfaceInfo_t { int surface, offFlags; };
struct boltInfo_t { int boneNumber = -1, surfaceNumber = -1, surfaceType = 0, boltUsed = 0; };
typedef std::vector<surfaceInfo_t> surfaceInfo_v;
typedef std::vector<boltInfo_t> boltInfo_v;
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

typedef qboolean (*Remove)(surfaceInfo_v &,int);
typedef int (*AddBolt)(CGhoul2Info *,boltInfo_v &,surfaceInfo_v &,int);
static void check(Remove remove, AddBolt add) {
    surfaceInfo_v surfaces = {{10000,G2SURFACEFLAG_GENERATED},{10000,G2SURFACEFLAG_GENERATED},{10000,G2SURFACEFLAG_GENERATED}};
    const auto unchanged = surfaces;
    // Public APIs forward these indices to the helpers. Invalid requests must
    // neither index the vector nor mutate its live contents.
    for (int index : {-1,-2,3,4,INT_MAX,INT_MIN}) {
        CHECK(remove(surfaces,index) == qfalse);
        CHECK(surfaces.size() == unchanged.size());
        for (size_t i = 0; i < surfaces.size(); ++i) CHECK(surfaces[i].surface == unchanged[i].surface);
    }
    CGhoul2Info model; boltInfo_v bolts;
    for (int index : {-1,-2,3,4,INT_MAX,INT_MIN}) CHECK(add(&model,bolts,surfaces,index) == -1 && bolts.empty());
    CHECK(add(&model,bolts,surfaces,1) == 0);
    CHECK(bolts.size() == 1 && bolts[0].surfaceNumber == 1 && bolts[0].surfaceType == G2SURFACEFLAG_GENERATED && bolts[0].boltUsed == 1);
    CHECK(add(&model,bolts,surfaces,1) == 0 && bolts[0].boltUsed == 2 && bolts.size() == 1);
    CHECK(remove(surfaces,1) == qtrue && surfaces.size() == 3 && surfaces[1].surface == -1);
    CHECK(add(&model,bolts,surfaces,1) == -1 && bolts[0].boltUsed == 2);
    CHECK(remove(surfaces,2) == qtrue && surfaces.size() == 1); // Trim consecutive free tail entries.
    CHECK(remove(surfaces,0) == qtrue && surfaces.empty());
    CHECK(remove(surfaces,0) == qfalse);
    CHECK(add(&model,bolts,surfaces,0) == -1);
    surfaces.push_back({10000,G2SURFACEFLAG_GENERATED});
    bolts[0] = boltInfo_t();
    CHECK(add(&model,bolts,surfaces,0) == 0 && bolts.size() == 1 && bolts[0].boltUsed == 1);
}
int main() {
    check(dedicated::G2_RemoveSurface,dedicated::G2_Add_Bolt_Surf_Num);
    check(vanilla::G2_RemoveSurface,vanilla::G2_Add_Bolt_Surf_Num);
    check(rend2::G2_RemoveSurface,rend2::G2_Add_Bolt_Surf_Num);
    check(vulkan::G2_RemoveSurface,vulkan::G2_Add_Bolt_Surf_Num);
    std::puts("All renderer Ghoul2 surface removal and surface-bolt bounds checks passed.");
}
