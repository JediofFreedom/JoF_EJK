#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#value); std::exit(1); } } while (0)
typedef float vec3_t[3];
#define MAX_VERTS_ON_POLY 64
#define SIDE_FRONT 0
#define SIDE_BACK 1
#define SIDE_ON 2
#include "primitives.h"
static size_t clearedBytes,clearCalls;
template<class T> static T NoteClear(size_t bytes) { clearedBytes += bytes; ++clearCalls; return T(0); }
namespace baseline {
#include "baseline-chop.h"
}
namespace baselineWork {
#include "baseline-work.h"
}
namespace vulkan {
#include "vulkan.h"
}
namespace vulkanWork {
#include "vulkan-work.h"
}
namespace vanilla {
#include "vanilla.h"
}
namespace vanillaWork {
#include "vanilla-work.h"
}
typedef void (*Chop)(int,vec3_t *,int *,vec3_t *,vec3_t,float,float);
static uint32_t rng = 0x9287384u;
static uint32_t random32() { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng; }
static float randomFloat(float lo,float hi) { return lo+(hi-lo)*(float(random32() & 65535)/65535.0f); }
struct Polygon { uint32_t before; vec3_t points[MAX_VERTS_ON_POLY]; uint32_t after; };
static Polygon blank() { Polygon p; std::memset(&p,0xa5,sizeof(p)); return p; }
static void compare(Chop subject,int count,vec3_t *points,vec3_t normal,float distance,float epsilon) {
    Polygon expected = blank(),actual = expected;
    int expectedCount = -123,actualCount = -123;
    baseline::R_ChopPolyBehindPlane(count,points,&expectedCount,expected.points,normal,distance,epsilon);
    subject(count,points,&actualCount,actual.points,normal,distance,epsilon);
    CHECK(expectedCount == actualCount && actualCount >= 0 && actualCount <= MAX_VERTS_ON_POLY);
    // Include untouched output storage and canaries, not merely active vertices.
    CHECK(std::memcmp(&expected,&actual,sizeof(actual)) == 0);
}
static void polygon(vec3_t *points,int count) {
    const float yaw = randomFloat(-3.14f,3.14f),pitch = randomFloat(-3.14f,3.14f);
    const float radius = randomFloat(0.01f,1500.0f);
    const vec3_t center = {randomFloat(-10000,10000),randomFloat(-10000,10000),randomFloat(-10000,10000)};
    const vec3_t u = {std::cos(yaw),std::sin(yaw),0};
    const vec3_t v = {-std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)};
    for (int i = 0; i < count; ++i) {
        const float angle = 6.283185307f*i/count;
        for (int j = 0; j < 3; ++j) points[i][j] = center[j]+radius*(u[j]*std::cos(angle)+v[j]*std::sin(angle));
    }
}
static void differential(Chop subject) {
    vec3_t points[MAX_VERTS_ON_POLY] = {},normal = {1,0,0};
    // Counts on both sides of the capacity guard must not touch output.
    for (int count : {INT_MIN,-1,0,1,2,3,61,62,63,64,INT_MAX}) {
        if (count > 0 && count <= MAX_VERTS_ON_POLY) polygon(points,count);
        compare(subject,count,points,normal,0,0.5f);
    }
    const float positions[] = {-1,-0.5f,-0.49999997f,0,0.49999997f,0.5f,1};
    for (float x : positions) {
        const vec3_t edge[4] = {{x,-1,-1},{x,1,-1},{x,1,1},{x,-1,1}};
        std::memcpy(points,edge,sizeof(edge));
        for (float epsilon : {0.0f,0.5f,0.000001f}) compare(subject,4,points,normal,0,epsilon);
    }
    for (int iteration = 0; iteration < 12000; ++iteration) {
        const int count = 3+int(random32()%59);
        polygon(points,count);
        for (int axis = 0; axis < 3; ++axis) normal[axis] = randomFloat(-1,1);
        const float distance = DotProduct(points[random32()%count],normal)+randomFloat(-50,50);
        compare(subject,count,points,normal,distance,iteration%3 ? 0.5f : 0.0f);
    }
    // Repeated plane clipping exercises every newly generated intersection.
    for (int iteration = 0; iteration < 1000; ++iteration) {
        Polygon expected = blank(),actual = blank();
        int expectedCount = 3+int(random32()%25),actualCount = expectedCount;
        // Generate exactly the active number of vertices in both copies.
        polygon(expected.points,expectedCount); actual = expected;
        for (int plane = 0; plane < 6 && expectedCount; ++plane) {
            Polygon nextExpected = blank(),nextActual = blank();
            for (int axis = 0; axis < 3; ++axis) normal[axis] = randomFloat(-1,1);
            const float distance = DotProduct(expected.points[random32()%expectedCount],normal);
            baseline::R_ChopPolyBehindPlane(expectedCount,expected.points,&expectedCount,nextExpected.points,normal,distance,0.5f);
            subject(actualCount,actual.points,&actualCount,nextActual.points,normal,distance,0.5f);
            CHECK(expectedCount == actualCount && std::memcmp(&nextExpected,&nextActual,sizeof(nextActual)) == 0);
            expected = nextExpected; actual = nextActual;
        }
    }
}
static void work() {
    vec3_t points[MAX_VERTS_ON_POLY] = {{1,0,0},{1,1,0},{1,0,1}},out[MAX_VERTS_ON_POLY],normal = {1,0,0};
    int count;
    clearedBytes = clearCalls = 0;
    baselineWork::R_ChopPolyBehindPlane(3,points,&count,out,normal,0,0.5f);
    CHECK(count == 3 && clearCalls == 2 && clearedBytes == (MAX_VERTS_ON_POLY+4)*(sizeof(float)+sizeof(int)));
    const size_t baselineBytes = clearedBytes;
    clearedBytes = clearCalls = 0;
    vulkanWork::R_ChopPolyBehindPlane(3,points,&count,out,normal,0,0.5f);
    vanillaWork::R_ChopPolyBehindPlane(3,points,&count,out,normal,0,0.5f);
    CHECK(count == 3 && clearCalls == 0 && clearedBytes == 0);
    std::printf("Both actual clippers preserve outputs; eliminated two array initializers (%zu bytes) per clip call.\n",baselineBytes);
}
int main() {
    differential(vulkan::R_ChopPolyBehindPlane);
    differential(vanilla::R_ChopPolyBehindPlane);
    work();
    std::puts("Random, boundary and sequential production polygon clipping is byte-identical to the frozen baseline.");
}
