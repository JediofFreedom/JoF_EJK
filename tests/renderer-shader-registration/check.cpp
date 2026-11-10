#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); std::exit(1); } } while (0)
#define PAD(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#define PADP(value, alignment) ((void *)PAD((intptr_t)(value), (alignment)))
typedef unsigned char byte;
const int MAX_SHADERS = 16384, QSORT_SHADERNUM_SHIFT = 17, QSORT_REFENTITYNUM_SHIFT = 7, QSORT_FOGNUM_SHIFT = 2;
struct shader_t { float sort; int sortedIndex; void *stages[2]; };
struct drawSurf_t { uint64_t sort; void *surface; };
struct setColorCommand_t { int commandId; float color[4]; };
struct drawBufferCommand_t { int commandId, buffer; };
struct swapBuffersCommand_t { int commandId; };
struct stretchPicCommand_t { int commandId; shader_t *shader; float coords[8]; };
struct rotatePicCommand_t { int commandId; shader_t *shader; float coords[10]; };
struct drawSurfsCommand_t { int commandId; void *refdef; void *viewParms; drawSurf_t *drawSurfs; int numDrawSurfs; };
enum { RC_END_OF_LIST, RC_SET_COLOR, RC_STRETCH_PIC, RC_ROTATE_PIC, RC_ROTATE_PIC2,
       RC_DRAW_SURFS, RC_DRAW_BUFFER, RC_SWAP_BUFFERS, RC_WORLD_EFFECTS, RC_AUTO_MAP };
struct renderCommandList_t { alignas(void *) byte cmds[4096]; int used; };
static struct { renderCommandList_t commands; } storage, *backEndData = &storage;
static struct { int numShaders; shader_t *shaders[MAX_SHADERS], *sortedShaders[MAX_SHADERS]; } tr;
static bool gServerSkinHack;
static uint64_t key(int shaderIndex, int entity = 5, int middle = 3, int low = 1) {
    return (uint64_t(shaderIndex) << QSORT_SHADERNUM_SHIFT) |
        (entity << QSORT_REFENTITYNUM_SHIFT) | (middle << QSORT_FOGNUM_SHIFT) | low;
}
static int shaderIndex(const drawSurf_t &surface) { return int(surface.sort >> QSORT_SHADERNUM_SHIFT) & (MAX_SHADERS - 1); }
#ifdef TEST_REND2
static uint64_t R_CreateSortKey(int entity, int shader, int cubemap, int post) { return key(shader, entity, cubemap, post); }
static void R_DecomposeSort(uint64_t sort, int *entity, shader_t **shader, int *middle, int *low) {
#else
static void R_DecomposeSort(uint64_t sort, int *entity, shader_t **shader, int *middle, int *low) {
#endif
    *shader = tr.sortedShaders[(sort >> QSORT_SHADERNUM_SHIFT) & (MAX_SHADERS - 1)];
    *entity = int(sort >> QSORT_REFENTITYNUM_SHIFT) & 1023;
    *middle = int(sort >> QSORT_FOGNUM_SHIFT) & 31;
    *low = int(sort) & 3;
}
#include RENDERER_HEADER

static shader_t a, b, inserted;
static void reset() {
    std::memset(&storage, 0, sizeof(storage));
    std::memset(&tr, 0, sizeof(tr));
    a = {3, 0, {nullptr, nullptr}};
    b = {5, 1, {nullptr, nullptr}};
    inserted = {1, 2, {nullptr, nullptr}};
    tr.numShaders = 3;
    tr.shaders[0] = tr.sortedShaders[0] = &a;
    tr.shaders[1] = tr.sortedShaders[1] = &b;
    tr.shaders[2] = tr.sortedShaders[2] = &inserted;
    gServerSkinHack = false;
}
static drawSurfsCommand_t *drawCommand(int offset, drawSurf_t *surfaces, int count) {
    auto *command = reinterpret_cast<drawSurfsCommand_t *>(storage.commands.cmds + offset);
    command->commandId = RC_DRAW_SURFS;
    command->drawSurfs = surfaces;
    command->numDrawSurfs = count;
    return command;
}

int main() {
    const int commandSize = PAD(sizeof(drawSurfsCommand_t), sizeof(void *));
    reset();
    drawSurf_t surface = {key(1), nullptr};
    drawCommand(0, &surface, 1);
    storage.commands.used = commandSize;
    // After a flush, command bytes remain. A stale batch can reference the
    // same reused draw-surface storage as the currently queued batch.
    drawCommand(commandSize, &surface, 1);
    SortNewShader();
    CHECK(shaderIndex(surface) == 2);
    CHECK(tr.sortedShaders[shaderIndex(surface)] == &b); // Previously NULL.
    CHECK((surface.sort & ((1u << QSORT_SHADERNUM_SHIFT) - 1)) == (key(1) & ((1u << QSORT_SHADERNUM_SHIFT) - 1)));
    CHECK(inserted.sortedIndex == 0 && a.sortedIndex == 1 && b.sortedIndex == 2);

    reset();
    drawSurf_t expired = {key(1), nullptr};
    drawCommand(0, &expired, 1);
    storage.commands.used = 0;
    SortNewShader();
    CHECK(expired.sort == key(1)); // An empty queue must not replay old bytes.

    reset();
    drawSurf_t active[] = {{key(0), nullptr}, {key(1), nullptr}};
    drawSurf_t old[] = {{key(0), nullptr}, {key(1), nullptr}};
    auto *color = reinterpret_cast<setColorCommand_t *>(storage.commands.cmds);
    color->commandId = RC_SET_COLOR;
    int offset = PAD(sizeof(*color), sizeof(void *));
    drawCommand(offset, active, 2);
    offset += commandSize;
    storage.commands.used = offset;
    drawCommand(offset, old, 2);
    SortNewShader();
    CHECK(shaderIndex(active[0]) == 1 && shaderIndex(active[1]) == 2);
    CHECK(shaderIndex(old[0]) == 0 && shaderIndex(old[1]) == 1);

    reset();
    gServerSkinHack = true;
    drawSurf_t server = {key(1), nullptr};
    drawCommand(0, &server, 1);
    storage.commands.used = commandSize;
    FixRenderCommandList(0);
    CHECK(server.sort == key(1));
    std::puts("Shader insertion updates only live render commands.");
}
