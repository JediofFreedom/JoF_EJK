#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #v); std::exit(1); } } while (0)
#include "tokens.h"
typedef float vector_t[3];
enum {WL_ERROR, WL_WARNING, WL_DEBUG};
static void Com_sprintf(char *out, size_t size, const char *format, ...) {
    va_list args; va_start(args,format); std::vsnprintf(out,size,format,args); va_end(args);
}
struct CBlockMember {
    int id;
    union Data { float number; int integer; char text[128]; Data() { std::memset(this,0,sizeof(*this)); } } data;
    int GetID() { return id; }
    static CBlockMember number(float value, int kind = TK_FLOAT) { CBlockMember out; out.id = kind; out.data.number = value; return out; }
    static CBlockMember token(int kind) { return number(float(kind),kind); }
    static CBlockMember integer(int value) { CBlockMember out; out.id = TK_INT; out.data.integer = value; return out; }
    static CBlockMember text(const char *text) { CBlockMember out; out.id = TK_STRING; std::snprintf(out.data.text,sizeof(out.data.text),"%s",text); return out; }
};
struct CBlock {
    std::vector<CBlockMember> members;
    CBlockMember *GetMember(int index) { CHECK(index >= 0 && size_t(index) < members.size()); return &members[size_t(index)]; }
    void *GetMemberData(int index) { return &GetMember(index)->data; }
};
struct Interface {
    unsigned tags = 0, vectors = 0, moves = 0;
    bool hasAngles = false;
    float duration = 0;
    vector_t position = {}, angles = {};
    char text[32] = "resolved-tag";
    void I_DPrintf(int, const char *, ...) {}
    bool I_GetString(int, int, char *name, char **value) {
        if (!std::strcmp(name,"missing")) return false;
        *value = text; return true;
    }
    bool I_GetFloat(int, int, char *name, float *value) {
        if (!std::strcmp(name,"missing")) return false;
        *value = 37.5f; return true;
    }
    bool I_GetVector(int, int, char *name, vector_t &value) {
        ++vectors;
        if (!std::strcmp(name,"missing")) return false;
        value[0] = 4; value[1] = 5; value[2] = 6; return true;
    }
    bool I_GetTag(int, char *name, int, vector_t &value) {
        ++tags;
        if (!std::strcmp(name,"missing")) return false;
        value[0] = 1; value[1] = 2; value[2] = 3; return true;
    }
    float I_Random(float low, float high) { return (low + high) / 2; }
    void I_Lerp2Pos(int, int, vector_t positionIn, float *anglesIn, float time) {
        ++moves; duration = time; hasAngles = anglesIn != nullptr;
        std::memcpy(position,positionIn,sizeof(position));
        if (anglesIn) std::memcpy(angles,anglesIn,sizeof(angles));
    }
};
struct Owner {
    Interface api;
    Interface *GetInterface() { return &api; }
};
struct CTask {
    CBlock *block;
    CBlock *GetBlock() { return block; }
    int GetTimeStamp() { return 100; }
    int GetGUID() { return 5; }
};
class CTaskManager {
public:
    Owner *m_owner;
    int m_ownerID = 7;
    explicit CTaskManager(Owner *owner) : m_owner(owner) {}
    bool Check(int,CBlock*,int);
    int GetFloat(int,CBlock*,int&,float&);
    int GetVector(int,CBlock*,int&,vector_t&);
    int Get(int,CBlock*,int&,char**);
    int Move(CTask*);
};
#include "helpers.h"
using Member = CBlockMember;
static void append(CBlock &out, const CBlock &tail) { out.members.insert(out.members.end(),tail.members.begin(),tail.members.end()); }
static CBlock get(int type, const char *name) { return {{Member::token(ID_GET),Member::number(float(type)),Member::text(name)}}; }
static CBlock vector(float x, float y, float z) {
    return {{Member::token(TK_VECTOR),Member::number(x),Member::number(y),Member::number(z)}};
}
static CBlock tag(const CBlock &name, const CBlock &lookup) {
    CBlock result = {{Member::token(ID_TAG)}}; append(result,name); append(result,lookup); return result;
}

int main() {
    static_assert(TASK_OK == 0 && TASK_FAILED == 1, "Task status and boolean success are different conventions");
    Owner owner; CTaskManager manager(&owner);
    const CBlock goodName = {{Member::text("tag")}}, lookup = {{Member::number(0)}};
    for (CBlock block : {tag(get(TK_STRING,"missing"),lookup), tag(goodName,get(TK_FLOAT,"missing")),
                         tag(goodName,{{Member::token(ID_TAG)}}), tag({{Member::text("missing")}},lookup)}) {
        int cursor = 0; vector_t out = {-101,-102,-103};
        CHECK(manager.GetVector(7,&block,cursor,out) == false);
        CHECK(out[0] == -101 && out[1] == -102 && out[2] == -103);
        cursor = 0; char sentinel[] = "unchanged"; char *text = sentinel;
        CHECK(manager.Get(7,&block,cursor,&text) == false);
        CHECK(text == sentinel);
    }
    // Wrong-type vector get() must fail before the interface can fill a value.
    CBlock wrongType = get(TK_STRING,"existing");
    int cursor = 0; vector_t out = {-101,-102,-103}; const auto vectors = owner.api.vectors;
    CHECK(manager.GetVector(7,&wrongType,cursor,out) == false && owner.api.vectors == vectors);

    CBlock goodTag = tag(get(TK_STRING,"existing"),get(TK_FLOAT,"existing"));
    cursor = 0; CHECK(manager.GetVector(7,&goodTag,cursor,out));
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && size_t(cursor) == goodTag.members.size());
    cursor = 0; char *text = nullptr; CHECK(manager.Get(7,&goodTag,cursor,&text));
    CHECK(std::string(text) == "1.000000 2.000000 3.000000");
    CBlock literal = vector(8,9,10); cursor = 0; CHECK(manager.GetVector(7,&literal,cursor,out));
    CHECK(out[0] == 8 && out[1] == 9 && out[2] == 10);
    cursor = 0; CHECK(manager.Get(7,&literal,cursor,&text));
    CHECK(std::string(text) == "8.000000 9.000000 10.000000");
    CBlock random = {{Member::token(ID_RANDOM),Member::number(2),Member::number(6)}};
    cursor = 0; CHECK(manager.GetVector(7,&random,cursor,out)); CHECK(out[0] == 4 && out[1] == 4 && out[2] == 4);
    CBlock numeric = {{Member::integer(12)}}; float value = 0; cursor = 0;
    CHECK(manager.GetFloat(7,&numeric,cursor,value) && value == 12);
    CBlock brokenVector = {{Member::token(TK_VECTOR),Member::number(1),Member::token(ID_TAG)}};
    cursor = 0; CHECK(!manager.GetVector(7,&brokenVector,cursor,out));
    cursor = 0; CHECK(!manager.Get(7,&brokenVector,cursor,&text));
    for (CBlock block : {get(TK_FLOAT,"existing"),get(TK_STRING,"missing")}) {
        cursor = 0;
        if (block.members[1].data.number == TK_FLOAT) { CHECK(manager.Get(7,&block,cursor,&text)); CHECK(std::string(text) == "37.500000"); }
        else CHECK(!manager.Get(7,&block,cursor,&text));
    }
    for (bool angles : {false,true}) {
        CBlock movement = goodTag;
        if (angles) append(movement,vector(90,180,270));
        movement.members.push_back(Member::number(1000)); CTask task = {&movement};
        const auto before = owner.api.moves;
        CHECK(manager.Move(&task) == TASK_OK);
        CHECK(owner.api.moves == before + 1 && owner.api.position[0] == 1 && owner.api.duration == 1000 && owner.api.hasAngles == angles);
        if (angles) CHECK(owner.api.angles[0] == 90 && owner.api.angles[1] == 180 && owner.api.angles[2] == 270);
    }
    CBlock invalidMove = tag(get(TK_STRING,"missing"),lookup);
    invalidMove.members.push_back(Member::number(1000)); CTask task = {&invalidMove};
    const auto moves = owner.api.moves;
    CHECK(manager.Move(&task) == TASK_FAILED && owner.api.moves == moves);
    std::puts("Icarus boolean parameter failures and task-command status checks passed.");
}
