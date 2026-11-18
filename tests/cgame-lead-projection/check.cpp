#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); std::exit(1); } } while (0)
typedef float vec_t, vec3_t[3], vec4_t[4];
typedef int qboolean;
enum {qfalse, qtrue, SCREEN_WIDTH=640, SCREEN_HEIGHT=480, MAX_CLIENTS=64,
      RESTRICT_LEAD=1, EF_DEAD=2, BUTTON_ATTACK=1, BUTTON_ALT_ATTACK=2,
      CONTENTS_SOLID=1, CONTENTS_BODY=2, CONTENTS_OPAQUE=4,
      ET_PLAYER=1, CLASS_VEHICLE=2, GT_TEAM=3, PERS_TEAM=0,
      VEH_WEAPON_BASE=0, COLOR_RED=1, COLOR_GREEN=2};
enum {WP_BLASTER,WP_DISRUPTOR,WP_BOWCASTER,WP_BRYAR_PISTOL,WP_BRYAR_OLD,
      WP_REPEATER,WP_DEMP2,WP_FLECHETTE,WP_ROCKET_LAUNCHER,WP_THERMAL,
      WP_TRIP_MINE,WP_CONCUSSION,JAPRO_CINFO_PROJSNIPER=1};
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static void VectorSubtract(const float *a,const float *b,float *out) { for(int i=0;i<3;++i) out[i]=a[i]-b[i]; }
static void VectorMA(const float *a,float scale,const float *b,float *out) { for(int i=0;i<3;++i) out[i]=a[i]+scale*b[i]; }
static void VectorCopy(const float *a,float *out) { std::memcpy(out,a,3*sizeof(float)); }
static void VectorCopy4(const float *a,float *out) { std::memcpy(out,a,4*sizeof(float)); }
static float DotProduct(const float *a,const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static float VectorLength(const float *a) { return std::sqrt(DotProduct(a,a)); }
static float VectorNormalize(float *a) { float n=VectorLength(a); if(n) for(int i=0;i<3;++i) a[i]/=n; return n; }
static bool VectorCompare(const float *a,const float *b) { return a[0]==b[0] && a[1]==b[1] && a[2]==b[2]; }
static int ColorIndex(int color) { return color; }
static vec3_t vec3_origin;
static vec4_t g_color_table[3]={{1,1,1,1},{1,0,0,1},{0,1,0,1}};
struct Weapon { int ID; };
struct VehicleInfo { Weapon weapon[1]; };
struct Vehicle { VehicleInfo *m_pVehicleInfo; };
struct State { int clientNum, eFlags, eType, NPC_class, m_iVehicleNum, teamowner; struct {vec3_t trDelta;} pos; };
struct centity_t { State currentState; vec3_t lerpOrigin; Vehicle *m_pVehicle; };
struct PlayerState { int clientNum, weapon, m_iVehicleNum, eFlags, rocketLockIndex, persistant[1]; vec3_t origin; };
struct Snapshot { PlayerState ps; int ping; };
struct clientInfo_t { int infoValid, team; };
struct trace_t { int entityNum; float fraction; };
static struct { PlayerState predictedPlayerState, predictedVehicleState; Snapshot *snap;
    int clientNum,crosshairClientNum; struct {vec3_t vieworg,viewaxis[3]; float fov_x,fov_y;} refdef; } cg;
static struct { int restricts,gametype,jcinfo; float widthRatioCoef; clientInfo_t clientinfo[MAX_CLIENTS]; struct {int whiteShader;} media; } cgs;
static centity_t cg_entities[MAX_CLIENTS];
struct Cvar { int integer; float value; };
static Cvar cg_leadIndicator,cg_drawVehLeadIndicator;
static struct Movement { struct {int buttons;} cmd; } movement,*pm=&movement;
struct vehWeaponInfo_t { int bIsProjectile,bHasGravity,fHoming; float fSpeed; };
static vehWeaponInfo_t g_vehWeaponInfo[2];
static void CG_Trace(trace_t *trace,const float *,const float *,const float *,const float *,int,int) { trace->entityNum=1; trace->fraction=1; }
struct Draw { float x,y,w,h; int shader; };
static std::vector<Draw> draws;
static bool captureCoordinates=true;
static size_t drawCount;
static void CG_DrawPic(float x,float y,float w,float h,int shader) {
    ++drawCount;
    // Failed-projection controls count submissions without inspecting their
    // undefined coordinates. Valid cases capture and compare every vertex.
    if(captureCoordinates) draws.push_back({x,y,w,h,shader});
}
static struct Trap { static void R_SetColor(const float *) {} static int R_RegisterShader(const char *name) { CHECK(!std::strcmp(name,"gfx/menus/radar/lead")); return 2; } } imports,*trap=&imports;
#include "projection.h"

static Snapshot snapshot;
static VehicleInfo vehicleInfo;
static Vehicle vehicle;
static void reset() {
    cg={}; cgs={}; snapshot={}; vehicleInfo={}; vehicle={};
    std::memset(cg_entities,0,sizeof(cg_entities));
    std::memset(g_vehWeaponInfo,0,sizeof(g_vehWeaponInfo));
    cg.snap=&snapshot; snapshot.ps.weapon=WP_ROCKET_LAUNCHER; snapshot.ps.rocketLockIndex=-1;
    movement.cmd.buttons=BUTTON_ATTACK;
    cg.refdef.fov_x=cg.refdef.fov_y=90;
    for(int i=0;i<3;++i) cg.refdef.viewaxis[i][i]=1;
    cg.crosshairClientNum=1; cgs.widthRatioCoef=1; cgs.media.whiteShader=1;
    cg_leadIndicator={1,1}; cg_drawVehLeadIndicator={1,1};
    auto &target=cg_entities[1]; target.currentState.eType=ET_PLAYER;
    target.currentState.clientNum=1; target.currentState.NPC_class=CLASS_VEHICLE;
    target.currentState.teamowner=1; target.lerpOrigin[0]=10;
    target.currentState.pos.trDelta[1]=450;
    cg.predictedPlayerState.m_iVehicleNum=2;
    vehicleInfo.weapon[0].ID=1; vehicle.m_pVehicleInfo=&vehicleInfo;
    cg_entities[2].m_pVehicle=&vehicle;
    g_vehWeaponInfo[1]={1,0,0,900};
    draws.clear(); drawCount=0; captureCoordinates=true;
}
static bool close(float a,float b) { return std::fabs(a-b)<0.001f; }
static void compare(const Draw &draw,float x,float y,float w,float h,int shader) {
    CHECK(close(draw.x,x) && close(draw.y,y) && close(draw.w,w) && close(draw.h,h) && draw.shader==shader);
}
static void valid(bool veh,bool offViewport,bool mixed=false) {
    reset();
    if(offViewport) cg_entities[1].currentState.pos.trDelta[1]=4500;
    if(mixed) {
        CHECK(!veh);
        cg_entities[3]=cg_entities[1]; cg_entities[3].currentState.clientNum=3;
        cg_entities[1].currentState.pos.trDelta[0]=-1800;
        cg_entities[1].currentState.pos.trDelta[1]=0;
    }
    if(veh) CG_BracketEntity(&cg_entities[1],2); else CG_LeadIndicator();
    const size_t first=veh ? 8 : 0;
    CHECK(drawCount==first+11 && draws.size()==drawCount);
    if(veh) {
        compare(draws[0],280,200,8,1,1); compare(draws[1],280,200,1,8,1);
        compare(draws[2],352,200,8,1,1); compare(draws[3],359,200,1,8,1);
        compare(draws[4],280,279,8,1,1); compare(draws[5],280,272,1,8,1);
        compare(draws[6],352,279,8,1,1); compare(draws[7],359,272,1,8,1);
    }
    const float step=offViewport ? -160 : -16;
    for(int i=0;i<10;++i) compare(draws[first+i],319.5f+step*i,239.5f,1,1,1);
    compare(draws.back(),offViewport ? -1288.f : 152.f,232,veh ? 16.f : 4.f,veh ? 16.f : 4.f,veh ? 2 : 1);
    uint64_t digest=14695981039346656037ULL;
    for(const Draw &draw:draws) {
        const float values[]={draw.x,draw.y,draw.w,draw.h};
        for(float value:values) { uint32_t bits; std::memcpy(&bits,&value,4); digest=(digest^bits)*1099511628211ULL; }
        digest=(digest^uint32_t(draw.shader))*1099511628211ULL;
    }
    std::printf("valid %s %s: %zu draws, coordinate digest %016llx\n",veh?"vehicle":"player",offViewport?"off-viewport":"onscreen",drawCount,static_cast<unsigned long long>(digest));
}
static void invalid(bool veh,const char *kind) {
    reset();
    if(!std::strcmp(kind,"behind")) {
        cg_entities[1].currentState.pos.trDelta[0]=-1800;
        cg_entities[1].currentState.pos.trDelta[1]=0;
    } else if(!std::strcmp(kind,"near")) {
        cg_entities[1].currentState.pos.trDelta[0]=-899.955f;
        cg_entities[1].currentState.pos.trDelta[1]=0;
    } else if(!std::strcmp(kind,"empty")) {
        cg_entities[1]={};
    } else if(!std::strcmp(kind,"origin-behind")) {
        cg_entities[1].lerpOrigin[0]=-10;
    } else CHECK(false);
    captureCoordinates=false;
    if(veh) CG_BracketEntity(&cg_entities[1],2); else CG_LeadIndicator();
    const size_t expected=veh && std::strcmp(kind,"empty") && std::strcmp(kind,"origin-behind") ? 8 : 0;
    std::printf("invalid %s %s: %zu draws, expected %zu\n",veh?"vehicle":"player",kind,drawCount,expected);
    CHECK(drawCount==expected);
}
static void projector() {
    reset();
    for(float depth:{-10.f,0.f,0.0005f,0.000999f}) {
        vec3_t point={depth,0,0}; float x=123,y=456;
        CHECK(!CG_WorldCoordToScreenCoord(point,&x,&y)); CHECK(x==123 && y==456);
    }
    vec3_t point={10,50,0}; float x=0,y=0;
    CHECK(CG_WorldCoordToScreenCoord(point,&x,&y) && close(x,-1280) && close(y,240));
    point[0]=0.0011f; point[1]=0;
    CHECK(CG_WorldCoordToScreenCoord(point,&x,&y) && close(x,320) && close(y,240));
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    const std::string test=argv[1];
    if(test=="projector") projector();
    else if(test=="player-valid") valid(false,false);
    else if(test=="vehicle-valid") valid(true,false);
    else if(test=="player-off-viewport") valid(false,true);
    else if(test=="vehicle-off-viewport") valid(true,true);
    else if(test=="player-mixed") valid(false,false,true);
    else if(test.find("player-")==0) invalid(false,test.c_str()+7);
    else if(test.find("vehicle-")==0) invalid(true,test.c_str()+8);
    else CHECK(false);
}
