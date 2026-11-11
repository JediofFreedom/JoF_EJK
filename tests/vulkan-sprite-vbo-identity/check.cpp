#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v); std::exit(1); } } while (0)
#include "sprite_bits.h"
typedef unsigned char byte;
enum {ERR_DROP,h_low,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
struct VBO_t { int index; uint64_t buffer,memory; int size; };
struct IBO_t {};
static struct {
    int numVBOs;
#include "vbo_array.h"
} tr;
static unsigned allocated,released,uploaded;
static struct {
    static void Error(int,const char *,...) { throw std::runtime_error("VBO capacity"); }
    static void *Hunk_Alloc(size_t size,int) { ++allocated; auto data = std::calloc(1,size); CHECK(data); return data; }
} ri;
static void vk_release_model_vbo(unsigned index) {
    CHECK(index < sizeof(tr.vbos)/sizeof(tr.vbos[0]));
    if (tr.vbos[index]) { ++released; std::free(tr.vbos[index]); tr.vbos[index] = nullptr; }
}
static void vk_create_static_model_buffer(const char *,const byte *,int size,int usage,uint64_t *buffer,uint64_t *memory) {
    CHECK(size == 4 && usage == VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    *buffer = ++uploaded; *memory = *buffer + 10000;
}
#include "create_vbo.h"
int main() {
    const byte bytes[4] = {1,2,3,4};
    VBO_t *first = R_CreateVBO("quad",bytes,4);
    VBO_t *sprite = R_CreateVBO("grass instances",bytes,4);
    VBO_t *other = R_CreateVBO("model",bytes,4);
    CHECK(first->index == 1 && sprite->index == 2 && other->index == 3);
    CHECK(first->size == 4 && sprite->size == 4 && other->size == 4);
    const uint32_t packed = SS_PACK_SURF_BITS(1023,sprite->index-1,4);
    CHECK(tr.vbos[SS_UNPACK_VBO(packed)] == sprite);
    CHECK(SS_UNPACK_ENT(packed) == 1023 && SS_UNPACK_FOG(packed) == 4);
    CHECK(sizeof(tr.vbos)/sizeof(tr.vbos[0]) == MAX_VBOS);
    CHECK(sizeof(tr.ibos)/sizeof(tr.ibos[0]) == MAX_VBOS);
    CHECK(SS_ENT_BITS + SS_VBO_BITS + SS_FOG_BITS <= 32);
    // Verify the actual CPU draw-group format over its entire supported VBO
    // range, including1024/2048/4096 boundaries that previously aliased.
    for (unsigned slot = 0; slot < MAX_VBOS; ++slot) {
        for (unsigned entity : {0u,1023u,unsigned(SS_ENT_MASK)}) for (unsigned fog : {0u,4u,unsigned(SS_FOG_MASK)}) {
            const uint32_t bits = SS_PACK_SURF_BITS(entity,slot,fog);
            CHECK(SS_UNPACK_VBO(bits) == slot && SS_UNPACK_ENT(bits) == entity && SS_UNPACK_FOG(bits) == fog);
        }
        if (slot >= 3) CHECK(R_CreateVBO("capacity",bytes,4)->index == int(slot+1));
    }
    CHECK(tr.numVBOs == MAX_VBOS);
    bool rejected = false;
    try { R_CreateVBO("overflow",bytes,4); } catch (const std::runtime_error &) { rejected = true; }
    CHECK(rejected && allocated == MAX_VBOS && uploaded == MAX_VBOS);
    // Renderer restarts reset the cursor. A replacement must retain the same
    // one-based identity and release the previous resource in that slot.
    tr.numVBOs = 0;
    CHECK(R_CreateVBO("restart quad",bytes,4)->index == 1 && released == 1);
    sprite = R_CreateVBO("restart grass",bytes,4);
    CHECK(sprite->index == 2 && released == 2);
    CHECK(tr.vbos[SS_UNPACK_VBO(SS_PACK_SURF_BITS(1023,sprite->index-1,4))] == sprite);
    for (unsigned i = 0; i < MAX_VBOS; ++i) vk_release_model_vbo(i);
    CHECK(allocated == released);
    std::puts("Actual static VBO identities, restart ownership and complete sprite grouping range passed.");
}
