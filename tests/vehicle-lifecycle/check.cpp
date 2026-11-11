#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <cassert>
#ifdef VEHICLE_SEAT_CONSUMERS
#include "vehicle_capacity.h"
#else
#define VEH_MAX_PASSENGERS 4
#endif
#define _GAME
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
typedef int qboolean; typedef unsigned char byte; typedef float vec3_t[3];
enum {qfalse,qtrue,MAX_CLIENTS=64,MAX_GENTITIES=128,CON_CONNECTED=2,MV_SWOOP=9,
 TEAM_SPECTATOR=3,ENTITYNUM_NONE=127,VEH_EJECT_LEFT=0,VEH_EJECT_RIGHT,VEH_EJECT_REAR,
 VEH_EJECT_FRONT,VEH_EJECT_TOP,VEH_EJECT_BOTTOM,CONTENTS_BODY=1,WP_NONE=0,
 PITCH=0,YAW=1,ROLL=2,VH_FIGHTER=1,VH_WALKER,VH_SPEEDER,VH_ANIMAL,
 VEH_MOUNT_THROW_LEFT=-4,VEH_MOUNT_THROW_RIGHT=-5,VEH_BUCKING=1,
 SVF_NOCLIENT=2,EF_NODRAW=4,CHAN_AUTO=0,FL_INACTIVE=8,CS_SHADERSTATE=1,
 PM_DEAD=1,CLASS_VEHICLE=1,BSET_USE=0,CLASS_GONK=2,SCF_NO_RESPONSE=1,
 ORIGIN,NEGATIVE_Y,SETANIM_BOTH,BOTH_STAND2,SETANIM_FLAG_OVERRIDE=1,SETANIM_FLAG_HOLD=2,
 CHAN_VOICE=1,MOD_SUICIDE=1,FL_UNDYING=16};
struct usercmd_t { int buttons,forwardmove,rightmove,upmove; };
struct playerState_t { int m_iVehicleNum,generic1,loopSound,weapon,eFlags,pm_type,groundEntityNum,speed;
 vec3_t origin,viewangles; int legsTimer,torsoTimer; };
struct Client { playerState_t ps; struct {int raceMode,movementStyle,sessionTeam;} sess;
 struct {int connected; usercmd_t cmd;} pers; int solidHack,NPC_class; };
struct NPCInfo { int scriptFlags; };
struct Vehicle_t;
struct Entity { Client *client; NPCInfo *NPC; Vehicle_t *m_pVehicle;
 struct {int number,m_iVehicleNum,owner,loopSound,eFlags;vec3_t origin;} s;
 struct {int ownerNum,contents,svFlags; vec3_t currentOrigin,currentAngles;} r;
 int health,inuse,spawnflags,fly_sound_debounce_time,flags;
 char *targetname,*targetShaderName,*targetShaderNewName,*dialogue,*behaviorSet[1];
 Entity *enemy; void (*use)(Entity *,Entity *,Entity *);
 playerState_t *playerState; void *ghoul2; vec3_t modelScale; int waypoint; };
typedef Entity gentity_t; typedef Entity bgEntity_t; typedef Entity sharedEntity_t;
struct vehicleInfo_t { int type,maxPassengers,numHands,hideRider,soundLoop,soundOn,killRiderOnDeath;
 qboolean (*ValidateBoard)(Vehicle_t *,bgEntity_t *);
 qboolean (*Board)(Vehicle_t *,bgEntity_t *);
 qboolean (*Eject)(Vehicle_t *,bgEntity_t *,qboolean);
 qboolean (*EjectAll)(Vehicle_t *);
 void (*SetPilot)(Vehicle_t *,bgEntity_t *);
 void (*Ghost)(Vehicle_t *,bgEntity_t *),(*UnGhost)(Vehicle_t *,bgEntity_t *);
 qboolean (*UpdateRider)(Vehicle_t *,bgEntity_t *,usercmd_t *); float centerOfGravity[3]; };
struct Vehicle_t { bgEntity_t *m_pParentEntity,*m_pPilot,*m_pOldPilot,*m_pDroidUnit,*m_ppPassengers[VEH_MAX_PASSENGERS];
 vehicleInfo_t *m_pVehicleInfo; int m_iNumPassengers,m_iBoarding,m_iDieTime,m_EjectDir;
 qboolean m_bWasBoarding; unsigned m_ulFlags; usercmd_t m_ucmd; vec3_t m_vOrientation; int m_iDropTime,m_iDroidUnitTag; };
static Entity g_entities[MAX_GENTITIES],g_logicalents[1];
static Client clients[MAX_GENTITIES];
static NPCInfo npc;
static struct {int time,num_entities,num_logicalents;} level;
static struct {Entity *NPC;} NPCS;
#define FOFS(field) int(offsetof(Entity,field))
static int Q_stricmp(const char *a,const char *b) { return std::strcmp(a,b); }
static void VectorCopy(const float *a,float *b) { std::memcpy(b,a,3*sizeof(float)); }
static void VectorSubtract(const float *a,const float *b,float *out) { for(int i=0;i<3;++i) out[i]=a[i]-b[i]; }
static void VectorSet(float *out,float x,float y,float z) { out[0]=x;out[1]=y;out[2]=z; }
static float VectorNormalize(float *a) { float n=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); if(n) for(int i=0;i<3;++i) a[i]/=n; return n; }
static float DotProduct(const float *a,const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void AngleVectors(const float *,float *f,float *r,float *) { if(f) VectorSet(f,1,0,0); if(r) VectorSet(r,0,-1,0); }
static void G_SetOrigin(Entity *ent,const float *origin) { VectorCopy(origin,ent->r.currentOrigin); }
static void SetClientViewAngle(Entity *,const float *) {}
static void BG_SetLegsAnimTimer(playerState_t *ps,int value) { ps->legsTimer=value; }
static void BG_SetTorsoAnimTimer(playerState_t *ps,int value) { ps->torsoTimer=value; }
static void G_Sound(Entity *,int,int) {}
static int G_SoundIndex(const char *) { return 1; }
#ifdef VEHICLE_EJECT_ALL
void G_EjectDroidUnit(Vehicle_t *,qboolean);
#else
static void G_EjectDroidUnit(Vehicle_t *,qboolean) { CHECK(false); }
#endif
static bool allowExit=true;
static int traceCalls,ejectCalls,boardCalls,saves,restores,responses;
static qboolean VEH_TryEject(Vehicle_t *,Entity *,Entity *,int,float *out) { ++traceCalls; VectorSet(out,10,20,30); return allowExit; }
static void SaveNPCGlobals() { ++saves; }
static void SetNPCGlobals(Entity *ent) { NPCS.NPC=ent; }
static void RestoreNPCGlobals() { ++restores; }
static qboolean Jedi_WaitingAmbush(Entity *) { return qfalse; }
static void Jedi_Ambush(Entity *) { CHECK(false); }
static void NPC_UseResponse(Entity *,Entity *,qboolean) { ++responses; }
static void G_DialogueStart(Entity *,const char *,void *) { CHECK(false); }
static void AddRemap(const char *,const char *,float) { CHECK(false); }
static const char *BuildShaderStateConfig() { return ""; }
struct mdxaBone_t { vec3_t origin; };
static int linked[MAX_GENTITIES],updated[MAX_GENTITIES],boltCalls;
static bool ejectDuringMove;
static void BG_GiveMeVectorFromMatrix(mdxaBone_t *matrix,int kind,float *out) { if(kind==ORIGIN) VectorCopy(matrix->origin,out); else VectorSet(out,0,-1,0); }
static void vectoangles(const float *,float *out) { VectorSet(out,0,0,0); }
static void G_SetAngles(Entity *ent,const float *angles) { VectorCopy(angles,ent->r.currentAngles); }
static void NPC_SetAnim(Entity *,int,int,int) { CHECK(false); }
static int BG_GetTime() { return level.time; }
static struct Trap {
 static void LinkEntity(Entity *ent) { CHECK(ent->s.number>=0 && ent->s.number<MAX_GENTITIES); ++linked[ent->s.number]; }
 static void Print(const char *,...) {}
 static void SetConfigstring(int,const char *) { CHECK(false); }
 static int G2API_AddBolt(void *model,int index,const char *tag) { CHECK(model && !index && !std::strcmp(tag,"*driver")); ++boltCalls; return 37; }
 static void G2API_GetBoltMatrix(void *model,int index,int bolt,mdxaBone_t *matrix,const float *angles,const float *origin,int time,void *,const float *) {
  CHECK(model && !index && bolt==37 && angles[PITCH]==0 && angles[ROLL]==0 && time==level.time);
  for(int i=0;i<3;++i) matrix->origin[i]=origin[i]+float(37*(i+1));
 }
} imports,*trap=&imports;
#ifdef VEHICLE_EJECT_ALL
static int attempted[MAX_GENTITIES],forced[MAX_GENTITIES],killed[MAX_GENTITIES],muted[MAX_GENTITIES];
static int ejectOrder[VEH_MAX_PASSENGERS+2],orderCount,callbackMode,damageMutation;
static bool nestedEjectDone;
static void G_MuteSound(int entity,int channel) { CHECK(channel==CHAN_VOICE);++muted[entity]; }
static void G_Damage(Entity *,Entity *,Entity *,const float *,const float *,int,int,int);
#endif
#include "vehicle_functions.h"
#ifdef VEHICLE_SEAT_CONSUMERS
#include "seat_functions.h"
#endif
static qboolean countedEject(Vehicle_t *veh,bgEntity_t *ent,qboolean force) {
 ++ejectCalls;
#ifdef VEHICLE_EJECT_ALL
 const int entity=int(ent-g_entities);CHECK(entity>=0 && entity<MAX_GENTITIES);
 ++attempted[entity];if(force) ++forced[entity];
 CHECK(orderCount<VEH_MAX_PASSENGERS+2);ejectOrder[orderCount++]=entity;
 if(callbackMode==1) return qfalse;
 if(callbackMode==2) return qtrue;
 if(callbackMode==3 && entity==0 && !nestedEjectDone) {
  nestedEjectDone=true;CHECK(countedEject(veh,&g_entities[10],qtrue));
 }
 if(callbackMode==4) {const qboolean result=Eject(veh,ent,force);*ent={};return result;}
#endif
 return Eject(veh,ent,force);
}
static qboolean countedBoard(Vehicle_t *veh,bgEntity_t *ent) { ++boardCalls; return Board(veh,ent); }
static qboolean ejectAll(Vehicle_t *) { CHECK(false); return qfalse; }
static Vehicle_t vehicle;
static vehicleInfo_t info;
static Entity *parent,*trigger;
#ifdef VEHICLE_EJECT_ALL
static void G_Damage(Entity *entity,Entity *,Entity *,const float *,const float *,int damage,int,int mod) {
 CHECK(damage==10000 && mod==MOD_SUICIDE && entity->inuse);
 const int index=int(entity-g_entities);CHECK(index>=0 && index<MAX_GENTITIES);++killed[index];
 if(index==0 && damageMutation) {
  if(damageMutation!=1) CHECK(Eject(&vehicle,&g_entities[10],qtrue));
  g_entities[10]={};
  if(damageMutation==3) {
   g_entities[10].s.number=10;g_entities[10].client=&clients[10];g_entities[10].inuse=true;
   clients[10].ps.m_iVehicleNum=0;
  }
 }
}
#endif
static void reset() {
 std::memset(g_entities,0,sizeof(g_entities)); std::memset(clients,0,sizeof(clients));
 std::memset(g_logicalents,0,sizeof(g_logicalents)); vehicle={};info={};npc={};
 level={100,68,0}; traceCalls=ejectCalls=boardCalls=saves=restores=responses=0;allowExit=true;
 for(int i=0;i<MAX_GENTITIES;++i) {g_entities[i].s.number=i;g_entities[i].health=100;clients[i].pers.connected=CON_CONNECTED;}
 for(int i=0;i<MAX_CLIENTS;++i) {g_entities[i].client=&clients[i];g_entities[i].playerState=&clients[i].ps;g_entities[i].inuse=true;}
 parent=&g_entities[64];parent->client=&clients[64];parent->inuse=true;parent->NPC=&npc;
 parent->client->NPC_class=CLASS_VEHICLE;parent->m_pVehicle=&vehicle;
 parent->targetname=const_cast<char *>("named_vehicle");parent->use=NPC_Use;
 trigger=&g_entities[65];trigger->inuse=true;
 vehicle.m_pParentEntity=parent;vehicle.m_pVehicleInfo=&info;vehicle.m_EjectDir=VEH_EJECT_LEFT;
 parent->playerState=&parent->client->ps;parent->ghoul2=&vehicle;parent->waypoint=777;
 std::memset(linked,0,sizeof(linked));std::memset(updated,0,sizeof(updated));boltCalls=0;ejectDuringMove=false;
#ifdef VEHICLE_EJECT_ALL
 std::memset(attempted,0,sizeof(attempted));std::memset(forced,0,sizeof(forced));
 std::memset(killed,0,sizeof(killed));std::memset(muted,0,sizeof(muted));
 orderCount=callbackMode=damageMutation=0;nestedEjectDone=false;
#endif
 info.type=VH_FIGHTER;info.maxPassengers=4;info.hideRider=true;
 info.ValidateBoard=ValidateBoard;info.Board=countedBoard;info.Eject=countedEject;info.EjectAll=ejectAll;
 info.SetPilot=SetPilot;info.Ghost=Ghost;info.UnGhost=UnGhost;
}
static void passenger(int slot,int entity) {
 vehicle.m_ppPassengers[slot]=&g_entities[entity];++vehicle.m_iNumPassengers;
 g_entities[entity].client->ps.m_iVehicleNum=64;g_entities[entity].client->ps.generic1=slot+1;
}
static void consistent(int expected) {
 int occupied=0;for(auto *ent:vehicle.m_ppPassengers) if(ent) ++occupied;
 CHECK(vehicle.m_iNumPassengers==expected && occupied==expected);
}
static void state(const char *label) {
 std::printf("%s count=%d pilot=%d boarding=%d parentPilot=%d",label,vehicle.m_iNumPassengers,
  vehicle.m_pPilot?vehicle.m_pPilot->s.number:-1,vehicle.m_iBoarding,parent->client->ps.m_iVehicleNum);
 for(int i=0;i<4;++i) std::printf(" seat%d=%d client%d=%d/%d/%d/%d",i,
  vehicle.m_ppPassengers[i]?vehicle.m_ppPassengers[i]->s.number:-1,i,clients[i].ps.m_iVehicleNum,
  clients[i].ps.generic1,g_entities[i].r.ownerNum,clients[i].ps.eFlags);
 std::putchar('\n');
}
static void mapUse(bool activator) {
 reset();G_UseTargets2(trigger,activator?&g_entities[0]:NULL,"named_vehicle");
 CHECK(boardCalls==1 && !vehicle.m_pPilot);consistent(0);
 CHECK(g_entities[0].client->ps.m_iVehicleNum==0);
 CHECK(saves==1 && restores==1 && responses==(activator?1:0));
}
static void validBoard() {
 reset();CHECK(!Board(&vehicle,NULL));
 CHECK(Board(&vehicle,&g_entities[0]));CHECK(vehicle.m_pPilot==&g_entities[0]);
 CHECK(parent->client->ps.m_iVehicleNum==1 && g_entities[0].client->ps.m_iVehicleNum==64);
 info.maxPassengers=2;
 CHECK(Board(&vehicle,&g_entities[1]));CHECK(Board(&vehicle,&g_entities[2]));
 CHECK(!Board(&vehicle,&g_entities[3]));consistent(2);
 CHECK(vehicle.m_ppPassengers[0]==&g_entities[1] && vehicle.m_ppPassengers[1]==&g_entities[2]);
 CHECK(g_entities[1].client->ps.generic1==1 && g_entities[2].client->ps.generic1==2);
 state("valid-board-state");
}
static void cleanup(const std::string &kind) {
 reset();vehicle.m_pPilot=&g_entities[0];passenger(0,1);
 if(kind=="disconnected" || kind=="empty-after-pilot") clients[1].pers.connected=0;
 else if(kind=="no-client") g_entities[1].client=NULL;
 else if(kind=="freed") g_entities[1]={};
 else if(kind=="solid-dead") {g_entities[1].health=0;allowExit=false;}
 else CHECK(false);
 RunPassengerUpdate(&vehicle);
 if(kind=="empty-after-pilot") {
  CHECK(Eject(&vehicle,&g_entities[0],qtrue));CHECK(!Inhabited(&vehicle));
 }
 consistent(0);CHECK(!vehicle.m_ppPassengers[0] && ejectCalls==1);
 RunPassengerUpdate(&vehicle);consistent(0);CHECK(ejectCalls==1);
 if(kind=="freed") CHECK(!g_entities[1].inuse && !g_entities[1].client);
}
static void multiple() {
 reset();vehicle.m_pPilot=&g_entities[0];passenger(0,1);passenger(1,2);passenger(2,3);
 clients[1].pers.connected=0;g_entities[3].client=NULL;
 RunPassengerUpdate(&vehicle);consistent(1);CHECK(ejectCalls==2 && vehicle.m_ppPassengers[1]==&g_entities[2]);
 for(int i=0;i<4;++i) RunPassengerUpdate(&vehicle);
 consistent(1);CHECK(ejectCalls==2 && Inhabited(&vehicle));
}
static void blocked() {
 reset();vehicle.m_pPilot=&g_entities[0];passenger(0,1);
 clients[1].pers.connected=0;clients[1].sess.raceMode=1;clients[1].sess.movementStyle=MV_SWOOP;
 RunPassengerUpdate(&vehicle);consistent(1);CHECK(ejectCalls==1 && vehicle.m_ppPassengers[0]==&g_entities[1]);
 RunPassengerUpdate(&vehicle);consistent(1);CHECK(ejectCalls==2);
}
static void failedExit() {
 reset();vehicle.m_pPilot=&g_entities[0];passenger(0,1);allowExit=false;
 CHECK(!Eject(&vehicle,&g_entities[1],qfalse));consistent(1);CHECK(vehicle.m_ppPassengers[0]==&g_entities[1]);
 CHECK(traceCalls==VEH_EJECT_BOTTOM-VEH_EJECT_LEFT+1);
}
static void pilot() {
 reset();vehicle.m_pPilot=&g_entities[0];passenger(0,1);passenger(1,2);clients[0].pers.connected=0;
 CHECK(Eject(&vehicle,&g_entities[0],qtrue));consistent(1);
 CHECK(vehicle.m_pPilot==&g_entities[1] && vehicle.m_ppPassengers[0]==&g_entities[2]);
 CHECK(clients[1].ps.generic1==0 && clients[2].ps.generic1==1 && parent->client->ps.m_iVehicleNum==2);
 state("promoted-pilot-state");
 RunPassengerUpdate(&vehicle);consistent(1);CHECK(!ejectCalls && Inhabited(&vehicle));
 CHECK(Eject(&vehicle,&g_entities[2],qtrue));consistent(0);CHECK(Inhabited(&vehicle));
 CHECK(Eject(&vehicle,&g_entities[1],qtrue));consistent(0);CHECK(!Inhabited(&vehicle));
}
#ifdef VEHICLE_SEAT_CONSUMERS
static qboolean updateSeat(Vehicle_t *veh,Entity *rider,usercmd_t *cmd) {
 CHECK(rider && rider->client && cmd==&rider->client->pers.cmd);
 int seat=-1;for(int i=0;i<VEH_MAX_PASSENGERS;++i) if(veh->m_ppPassengers[i]==rider) seat=i;
 CHECK(seat>=0 && rider->client->ps.generic1==seat+1);
 ++updated[rider->s.number];
 if(ejectDuringMove) CHECK(Eject(veh,rider,qtrue));
 return !ejectDuringMove;
}
static void seatSetup() {
 reset();CHECK(VEH_MAX_PASSENGERS==10);
 info.maxPassengers=VEH_MAX_PASSENGERS;info.UpdateRider=updateSeat;
 vehicle.m_pPilot=&g_entities[0];parent->client->ps.m_iVehicleNum=1;
 clients[0].ps.m_iVehicleNum=64;
 VectorSet(parent->client->ps.origin,100,200,300);parent->client->ps.viewangles[YAW]=45;
 for(int i=1;i<=VEH_MAX_PASSENGERS;++i) VectorSet(clients[i].ps.origin,-100-i,-200-i,-300-i);
}
static void consumers(const std::string &test) {
 seatSetup();const bool attach=test.find("attach-")==0;
 if(test.find("full")!=std::string::npos) for(int i=0;i<VEH_MAX_PASSENGERS;++i) passenger(i,i+1);
 else if(test.find("dense")!=std::string::npos) for(int i=0;i<3;++i) passenger(i,i+1);
 else if(test.find("sparse-last")!=std::string::npos) passenger(VEH_MAX_PASSENGERS-1,VEH_MAX_PASSENGERS);
 else if(test.find("sparse-1")!=std::string::npos) passenger(1,2);
 else if(test.find("after-eject")!=std::string::npos) {
  for(int i=0;i<3;++i) passenger(i,i+1);
  CHECK(Eject(&vehicle,&g_entities[1],qtrue));consistent(2);
 } else if(test.find("after-promotion")!=std::string::npos) {
  passenger(1,2);passenger(VEH_MAX_PASSENGERS-1,VEH_MAX_PASSENGERS);
  CHECK(Eject(&vehicle,&g_entities[0],qtrue));CHECK(vehicle.m_pPilot==&g_entities[2]);consistent(1);
 } else if(test.find("capacity")!=std::string::npos) {
  info.maxPassengers=2;passenger(1,2);
  vehicle.m_ppPassengers[VEH_MAX_PASSENGERS-1]=reinterpret_cast<Entity *>(std::uintptr_t(1));
 } else CHECK(test.find("empty")!=std::string::npos);
 Entity *slots[VEH_MAX_PASSENGERS];std::memcpy(slots,vehicle.m_ppPassengers,sizeof(slots));
 int generic[VEH_MAX_PASSENGERS];for(int i=0;i<info.maxPassengers;++i) generic[i]=slots[i]?slots[i]->client->ps.generic1:0;
 std::memset(linked,0,sizeof(linked));boltCalls=0;
 if(attach) AttachRiders(&vehicle); else RunPassengerMoves(parent);
 CHECK(!std::memcmp(slots,vehicle.m_ppPassengers,sizeof(slots)));
 for(int i=0;i<info.maxPassengers;++i) if(slots[i]) {
  Entity *ent=slots[i];CHECK(ent->client->ps.generic1==generic[i]);
  if(attach) {
   CHECK(linked[ent->s.number]==1);
   for(int axis=0;axis<3;++axis) {
    const float expected=parent->client->ps.origin[axis]+37*(axis+1);
    CHECK(ent->client->ps.origin[axis]==expected && ent->r.currentOrigin[axis]==expected);
   }
  } else CHECK(updated[ent->s.number]==1);
 }
 if(attach) CHECK(boltCalls==vehicle.m_iNumPassengers+1 && linked[vehicle.m_pPilot->s.number]==1);
 else {int total=0;for(int n:updated) total+=n;CHECK(total==vehicle.m_iNumPassengers);}
 state(test.c_str());
}
static void allPassengerExits() {
 seatSetup();for(int i=0;i<VEH_MAX_PASSENGERS;++i) passenger(i,i+1);
 ejectDuringMove=true;RunPassengerMoves(parent);consistent(0);
 for(int i=1;i<=VEH_MAX_PASSENGERS;++i) CHECK(updated[i]==1 && clients[i].ps.m_iVehicleNum==0);
 CHECK(Inhabited(&vehicle) && vehicle.m_pPilot==&g_entities[0]);
}
static void sparsePilot() {
 seatSetup();passenger(VEH_MAX_PASSENGERS-1,VEH_MAX_PASSENGERS);
 CHECK(Eject(&vehicle,&g_entities[0],qtrue));
 CHECK(vehicle.m_pPilot==&g_entities[VEH_MAX_PASSENGERS]);consistent(0);
 CHECK(clients[VEH_MAX_PASSENGERS].ps.generic1==0 && parent->client->ps.m_iVehicleNum==VEH_MAX_PASSENGERS+1);
 AttachRiders(&vehicle);RunPassengerMoves(parent);
 CHECK(linked[VEH_MAX_PASSENGERS]==1);
}
static void clampCapacity() {
 for(int value:{-100,-1,0,1,VEH_MAX_PASSENGERS,VEH_MAX_PASSENGERS+1,100000}) {
  seatSetup();info.maxPassengers=value;BG_VehicleClampData(&info);
  CHECK(info.maxPassengers==(value<0?0:value>VEH_MAX_PASSENGERS?VEH_MAX_PASSENGERS:value));
 }
}
#endif
#ifdef VEHICLE_EJECT_ALL
static void exitEveryone(const std::string &test) {
 seatSetup();info.EjectAll=EjectAll;
 if(test=="all-empty" || test=="all-no-pilot" || test=="all-race-passenger") {
  vehicle.m_pPilot=NULL;parent->client->ps.m_iVehicleNum=0;clients[0].ps.m_iVehicleNum=0;
 }
 if(test=="all-dense" || test=="all-no-pilot") for(int i=0;i<3;++i) passenger(i,i+1);
 else if(test=="all-full" || test=="all-full-old-pilot" || test=="all-reject-callback" || test=="all-inert-callback") for(int i=0;i<VEH_MAX_PASSENGERS;++i) passenger(i,i+1);
 else if(test=="all-sparse" || test=="all-mutate-next" || test.find("all-damage-")==0) {passenger(1,2);passenger(9,10);}
 else if(test=="all-race-passenger") passenger(9,10);
 else if(test!="all-empty" && test!="all-pilot-only" && test!="all-alias-only" && test!="all-droid" && test!="all-droid-live") passenger(0,1);
 if(test=="all-old-pilot" || test=="all-full-old-pilot") {vehicle.m_pOldPilot=&g_entities[11];clients[11].ps.m_iVehicleNum=64;}
 if(test.find("all-alias")==0) vehicle.m_pOldPilot=vehicle.m_pPilot;
 const bool refusedPilot=test=="all-race-pilot" || test=="all-race-damage" || test=="all-alias-refused" || test=="all-alias-refused-damage";
 if(refusedPilot) {clients[0].sess.raceMode=1;clients[0].sess.movementStyle=MV_SWOOP;}
 if(test=="all-race-passenger") {clients[10].sess.raceMode=1;clients[10].sess.movementStyle=MV_SWOOP;}
 if(test=="all-solid-live" || test=="all-solid-dead") allowExit=false;
 if(test=="all-solid-dead") {g_entities[0].health=0;g_entities[1].health=0;}
 if(test=="all-reject-callback") callbackMode=1;
 if(test=="all-inert-callback") callbackMode=2;
 if(test=="all-mutate-next") callbackMode=3;
 if(test=="all-callback-free") callbackMode=4;
 if(test=="all-alias-damage" || test=="all-alias-refused-damage" || test=="all-race-damage" || test.find("all-damage-")==0 || test=="all-callback-free" || test=="all-droid") info.killRiderOnDeath=1;
 if(test=="all-damage-free") damageMutation=1;
 if(test=="all-damage-detach-free") damageMutation=2;
 if(test=="all-damage-reuse") damageMutation=3;
 if(test=="all-droid" || test=="all-droid-live") {vehicle.m_pDroidUnit=&g_entities[12];g_entities[12].flags=FL_UNDYING;}
 Entity *slots[VEH_MAX_PASSENGERS];std::memcpy(slots,vehicle.m_ppPassengers,sizeof(slots));
 const int originalPassengers=vehicle.m_iNumPassengers;
 int expectedOrder[VEH_MAX_PASSENGERS+2],expectedCount=0;
 if(vehicle.m_pPilot) expectedOrder[expectedCount++]=0;
 if(test=="all-old-pilot" || test=="all-full-old-pilot") expectedOrder[expectedCount++]=11;
 for(int i=0;i<VEH_MAX_PASSENGERS;++i) if(slots[i] && !(damageMutation>=2 && slots[i]==&g_entities[10])) expectedOrder[expectedCount++]=int(slots[i]-g_entities);
 if(test=="all-mutate-next") {CHECK(expectedCount==3);expectedOrder[1]=10;expectedOrder[2]=2;}
 const bool rejected=refusedPilot || test=="all-race-passenger" ||
  test=="all-solid-live" || test=="all-reject-callback" || test=="all-inert-callback";
 const qboolean result=EjectAll(&vehicle);
 CHECK(bool(result)==!rejected);
 CHECK(orderCount==expectedCount && !std::memcmp(ejectOrder,expectedOrder,expectedCount*sizeof(int)));
 if(rejected) {
  if(refusedPilot) {consistent(0);CHECK(vehicle.m_pPilot==&g_entities[0]);}
  else {consistent(originalPassengers);CHECK(!std::memcmp(slots,vehicle.m_ppPassengers,sizeof(slots)));}
  if(test.find("all-alias-refused")==0) CHECK(vehicle.m_pOldPilot==&g_entities[0]);
 } else {consistent(0);CHECK(!vehicle.m_pPilot && !vehicle.m_pOldPilot && !vehicle.m_pDroidUnit);}
 for(int i=0;i<MAX_GENTITIES;++i) {
  CHECK(attempted[i]<=1 && forced[i]==attempted[i] && killed[i]<=1 && muted[i]==killed[i]);
 }
 if(test=="all-alias-damage" || test=="all-race-damage" || test=="all-alias-refused-damage") {CHECK(killed[0]==1 && killed[1]==1);}
 if(test.find("all-damage-")==0) {
  CHECK(killed[0]==1 && killed[2]==1 && !killed[10]);
  if(damageMutation<3) CHECK(!g_entities[10].inuse);
  else CHECK(g_entities[10].inuse && clients[10].ps.m_iVehicleNum==0 && !attempted[10]);
 }
 if(test=="all-droid") CHECK(killed[0]==1 && killed[12]==1 && !(g_entities[12].flags&FL_UNDYING));
 if(test=="all-droid-live") CHECK(!killed[0] && !killed[12] && !(g_entities[12].flags&FL_UNDYING));
 if(test=="all-callback-free") CHECK(!g_entities[0].inuse && !g_entities[1].inuse && !killed[0] && !killed[1]);
 if(!rejected && test!="all-empty" && test!="all-droid" && test!="all-droid-live") {
  for(int i=0;i<VEH_MAX_PASSENGERS;++i) if(slots[i] && !(damageMutation && slots[i]==&g_entities[10])) CHECK(attempted[int(slots[i]-g_entities)]==1);
 }
 state(test.c_str());
}
#endif
int main(int argc,char **argv) {
 CHECK(argc==2);std::string test=argv[1];
 if(test=="map-trigger") mapUse(true);
 else if(test=="map-trigger-no-activator") mapUse(false);
 else if(test=="valid-board") validBoard();
 else if(test=="multiple") multiple();
 else if(test=="blocked-eject") blocked();
 else if(test=="failed-exit") failedExit();
 else if(test=="pilot") pilot();
#ifdef VEHICLE_SEAT_CONSUMERS
 else if(test.find("attach-")==0 || (test.find("move-")==0 && test!="move-eject-all")) consumers(test);
 else if(test=="move-eject-all") allPassengerExits();
 else if(test=="pilot-sparse-last") sparsePilot();
 else if(test=="clamp-capacity") clampCapacity();
#endif
#ifdef VEHICLE_EJECT_ALL
 else if(test.find("all-")==0) exitEveryone(test);
#endif
 else cleanup(test);
 std::printf("%s passed\n",argv[1]);
}
