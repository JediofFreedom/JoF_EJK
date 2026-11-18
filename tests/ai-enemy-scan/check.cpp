#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include "capacity.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while (0)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
typedef int qboolean; typedef float vec3_t[3];
enum {qfalse,qtrue,PITCH=0,YAW=1,ROLL=2,GT_FFA=0,GT_JEDIMASTER,GT_DUEL,GT_POWERDUEL,GT_TEAM,
 TEAM_FREE=0,TEAM_RED,TEAM_BLUE,TEAM_SPECTATOR,PM_NORMAL=0,PM_NOCLIP,PM_INTERMISSION,PM_SPECTATOR,
 SVF_BOT=1,LEVELFLAG_IGNOREINFALLBACK=1,MASK_SOLID=1,MAX_PS_EVENTS=2,
 EV_GLOBAL_SOUND=1,EV_FIRE_WEAPON,EV_ALT_FIRE,EV_SABER_ATTACK,EV_STEP_4,EV_STEP_8,EV_STEP_12,
 EV_STEP_16,EV_FOOTSTEP,EV_FOOTSTEP_METAL,EV_FOOTWADE,EV_JUMP,EV_ROLL};
struct playerState_t {vec3_t origin;int isJediMaster,pm_type,duelInProgress,duelIndex,otherSoundTime,otherSoundLen,footstepTime;};
struct Client {playerState_t ps;struct {int raceMode,sessionTeam;} sess;struct {int connected;char netname[16];} pers;int dangerTime;};
struct gentity_t {Client *client;struct {int number,solid;} s;struct {int svFlags;} r;int health,takedamage;};
struct bot_state_t {int client;gentity_t *currentEnemy;float frame_Enemy_Len;vec3_t origin,eye,viewangles;
 playerState_t cur_ps;int doingFallback,lovednum;struct {char name[16];int level;} loved[4];};
struct Cvar {int integer;};
static Cvar g_newBotAI,g_newBotAITarget,g_friendlyFire,bot_attachments,RMG,bot_pvstype;
static Client clients[MAX_CLIENTS+1];static gentity_t g_entities[MAX_CLIENTS+1];
static bot_state_t botstorage[MAX_CLIENTS+1],*botstates[MAX_CLIENTS+1];
static struct {int gametype,time;Client *clients;} level;
static struct {int eventTime,events[MAX_PS_EVENTS],eventSequence;} gBotEventTracker[MAX_CLIENTS+1];
static int gLevelFlags;
static bool masterPresent,pvs[MAX_CLIENTS+1],visible[MAX_CLIENTS+1],tricked[MAX_CLIENTS+1];
static std::vector<std::string> calls;
static unsigned conversions,rngCalls,rngState;
static void record(const char *name,int first,int second=0) {char s[80];std::snprintf(s,sizeof(s),"%s:%d:%d",name,first,second);calls.emplace_back(s);}
static int bits(float f) {int n;static_assert(sizeof(n)==sizeof(f),"float bits");std::memcpy(&n,&f,sizeof(n));return n;}
static int byOrigin(const float *origin) {for(int i=0;i<=MAX_CLIENTS;++i) if(origin==clients[i].ps.origin) return i;CHECK(false);return -1;}
static int OnSameTeam(gentity_t *a,gentity_t *b) {record("team",a->s.number,b->s.number);return level.gametype==GT_TEAM && a->client->sess.sessionTeam==b->client->sess.sessionTeam;}
static int IsTeamplay() {record("teamplay",level.gametype);return level.gametype==GT_TEAM;}
static int BotMindTricked(int client,int other) {record("mind",client,other);return tricked[other];}
static qboolean G_ThereIsAMaster() {record("master",masterPresent);return masterPresent;}
static int Q_irand(int first,int last) {++rngCalls;rngState=rngState*1664525u+1013904223u;record("rng",first,last);return first+int(rngState%unsigned(last-first+1));}
static void VectorSubtract(const float *a,const float *b,float *v) {for(int i=0;i<3;++i) v[i]=a[i]-b[i];}
static float VectorLength(const float *v) {return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
static struct Trap {static qboolean InPVS(const float *a,const float *) {const int i=byOrigin(a);record("pvs",i);return pvs[i];}} imports,*trap=&imports;
struct trace_t {float fraction;};
static void JP_Trace(trace_t *tr,const float *,const float *mins,const float *maxs,const float *to,int ignore,int mask,qboolean cylinder,int a,int b) {
 CHECK(!mins && !maxs && ignore==-1 && mask==MASK_SOLID && !cylinder && !a && !b);
 const int target=byOrigin(to);record("trace",target);tr->fraction=visible[target]?1.0f:0.5f;
}
#define vectoangles RawVectorAngles
#define InFieldOfVision RawFieldOfVision
#include "primitives.h"
#undef InFieldOfVision
#undef vectoangles
static void vectoangles(const vec3_t a,vec3_t out) {++conversions;RawVectorAngles(a,out);}
static int InFieldOfVision(vec3_t view,float fov,vec3_t angles) {record("fov",bits(angles[0]),bits(angles[1]));return RawFieldOfVision(view,fov,angles);}
#define BotCanHear RawCanHear
#define PassLovedOneCheck RawLovedOneCheck
#include "services.h"
#undef PassLovedOneCheck
#undef BotCanHear
static int BotCanHear(bot_state_t *bs,gentity_t *en,float dist) {record("hear",en->s.number,bits(dist));return RawCanHear(bs,en,dist);}
static int PassLovedOneCheck(bot_state_t *bs,gentity_t *en) {record("love",en->s.number);return RawLovedOneCheck(bs,en);}
namespace baseline {
#include "baseline-scan.h"
}
namespace subject {
#include "subject.h"
}
static void reset() {
 std::memset(clients,0,sizeof(clients));std::memset(g_entities,0,sizeof(g_entities));
 std::memset(botstorage,0,sizeof(botstorage));std::memset(botstates,0,sizeof(botstates));
 std::memset(gBotEventTracker,0,sizeof(gBotEventTracker));
 level={GT_FFA,1000,clients};g_newBotAI={0};g_newBotAITarget={-1};g_friendlyFire={0};bot_attachments={1};RMG={0};bot_pvstype={1};
 masterPresent=false;gLevelFlags=0;
 for(int i=0;i<=MAX_CLIENTS;++i) {
  g_entities[i].s.number=i;g_entities[i].s.solid=1;g_entities[i].health=100;g_entities[i].takedamage=1;
  clients[i].pers.connected=1;std::snprintf(clients[i].pers.netname,sizeof(clients[i].pers.netname),"actor-%d",i);
  pvs[i]=visible[i]=true;tricked[i]=false;
  gBotEventTracker[i].eventTime=level.time+100;gBotEventTracker[i].events[0]=EV_FIRE_WEAPON;
 }
}
static void actor(int id,float x,float y=0,float z=0) {
 CHECK(id>=0 && id<=MAX_CLIENTS);g_entities[id].client=&clients[id];
 clients[id].ps.origin[0]=x;clients[id].ps.origin[1]=y;clients[id].ps.origin[2]=z;
}
struct Result {int winner;unsigned before,after;};
static Result compare(bot_state_t bs) {
 bot_state_t beforeBs=bs;gentity_t beforeEntities[MAX_CLIENTS+1];Client beforeClients[MAX_CLIENTS+1];
 std::memcpy(beforeEntities,g_entities,sizeof(g_entities));std::memcpy(beforeClients,clients,sizeof(clients));
 calls.clear();conversions=rngCalls=0;rngState=0x12F3ABCD;
 const int expected=baseline::ScanForEnemies(&bs);const unsigned eager=conversions,baseRng=rngCalls,baseSeed=rngState;
 const auto expectedCalls=calls;
 CHECK(!std::memcmp(&beforeBs,&bs,sizeof(bs)) && !std::memcmp(beforeEntities,g_entities,sizeof(g_entities)) && !std::memcmp(beforeClients,clients,sizeof(clients)));
 calls.clear();conversions=rngCalls=0;rngState=0x12F3ABCD;
 const int actual=subject::ScanForEnemies(&bs);
 CHECK(expected==actual && calls==expectedCalls && rngCalls==baseRng && rngState==baseSeed);
 CHECK(!std::memcmp(&beforeBs,&bs,sizeof(bs)) && !std::memcmp(beforeEntities,g_entities,sizeof(g_entities)) && !std::memcmp(beforeClients,clients,sizeof(clients)));
 CHECK(conversions<=eager);
 return {actual,eager,conversions};
}
static bot_state_t observer(int id=0) {bot_state_t bs={};bs.client=id;actor(id,0);return bs;}
static unsigned randState=0xE13C047u;
static unsigned random32() {randState=randState*1664525u+1013904223u;return randState;}
static int randomInt(int n) {return int(random32()%unsigned(n));}
static void focused(const std::string &test) {
 reset();bot_state_t bs=observer();
 if(test=="ordered" || test=="reverse" || test=="new" || test=="ties") {
  for(int i=1;i<24;++i) actor(i,float(test=="reverse"?2400-i*100:test=="ties"?100:i*100));
  if(test=="new") g_newBotAI.integer=1;
  Result r=compare(bs);CHECK(r.winner==(test=="reverse"?23:1) && r.before==23);
  CHECK(r.after==(test=="new"?0:test=="reverse"?23:1));
 } else if(test=="jedi" || test=="current-jedi") {
  actor(1,400);actor(2,9000);clients[2].ps.isJediMaster=1;level.gametype=GT_JEDIMASTER;masterPresent=true;
  if(test=="current-jedi") bs.currentEnemy=&g_entities[2];
  Result r=compare(bs);CHECK(r.winner==(test=="current-jedi"?-1:2));
 } else if(test=="health") {
  actor(1,100);actor(2,200);g_entities[2].health=1;g_newBotAI.integer=1;
  Result r=compare(bs);CHECK(r.winner==2 && r.before==2 && !r.after);
 } else if(test=="hysteresis") {
  actor(1,900);actor(2,871);actor(3,872);bs.currentEnemy=&g_entities[1];bs.frame_Enemy_Len=1000;
  CHECK(compare(bs).winner==2);
 } else if(test=="mindtrick") {
  actor(1,400);actor(2,63);tricked[1]=tricked[2]=true;bs.currentEnemy=&g_entities[1];
  CHECK(compare(bs).winner==2);
 } else if(test=="fov") {
  for(float yaw:{-180.0f,-45.01f,-45.0f,-44.99f,0.0f,44.99f,45.0f,45.01f,180.0f,359.99f})
   for(float pitch:{-90.0f,-45.01f,-45.0f,0.0f,45.0f,45.01f,90.0f}) {
    const float y=yaw*float(M_PI/180.0),p=pitch*float(M_PI/180.0);
    actor(1,100*std::cos(y)*std::cos(p),100*std::sin(y)*std::cos(p),100*std::sin(p));
    gBotEventTracker[1].eventTime=0;compare(bs);clients[1].ps.footstepTime=level.time+1;compare(bs);clients[1].ps.footstepTime=0;
   }
 } else if(test=="degenerate") {
  for(auto x:{0.0f,0.000001f,999999.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
   actor(1,x);compare(bs);actor(1,0,0,x);compare(bs);
  }
 } else if(test=="last-index") {
  actor(MAX_CLIENTS,10);CHECK(compare(bs).winner==MAX_CLIENTS);
 } else if(test=="invalid") {
  for(int invalid=0;invalid<13;++invalid) {
   reset();bs=observer();actor(1,100);
   switch(invalid) {
    case 0:g_entities[1].client=NULL;break;case 1:clients[1].sess.raceMode=1;break;case 2:clients[1].ps.pm_type=PM_NOCLIP;break;
    case 3:g_entities[1].health=0;break;case 4:g_entities[1].takedamage=0;break;case 5:clients[1].ps.pm_type=PM_INTERMISSION;break;
    case 6:clients[1].sess.sessionTeam=TEAM_SPECTATOR;break;case 7:clients[1].pers.connected=0;break;case 8:g_entities[1].s.solid=0;break;
    case 9:level.gametype=GT_TEAM;break;case 10:clients[1].ps.duelInProgress=1;clients[1].ps.duelIndex=2;break;
    case 11:bs.doingFallback=1;gLevelFlags=LEVELFLAG_IGNOREINFALLBACK;break;case 12:g_entities[1].r.svFlags=SVF_BOT;g_newBotAITarget.integer=-2;break;
   }
   CHECK(compare(bs).winner==-1);
  }
 } else CHECK(false);
}
static void randomized() {
 unsigned eager=0,lazy=0;
 for(int run=0;run<12000;++run) {
  reset();g_newBotAI.integer=run%2;g_friendlyFire.integer=randomInt(2);level.gametype=randomInt(5);masterPresent=randomInt(2)!=0;
  RMG.integer=randomInt(2);bot_pvstype.integer=randomInt(2);g_newBotAITarget.integer=randomInt(5)==0?-2:-1;
  bot_state_t bs=observer(randomInt(MAX_CLIENTS));bs.cur_ps.isJediMaster=randomInt(5)==0;
  bs.frame_Enemy_Len=float(randomInt(1800));bs.doingFallback=randomInt(12)==0;gLevelFlags=randomInt(2);
  bs.viewangles[0]=float(randomInt(361)-180);bs.viewangles[1]=float(randomInt(1081)-540);
  for(int i=0;i<=MAX_CLIENTS;++i) {
   if(i==bs.client) continue;
   if(randomInt(5)) actor(i,float(randomInt(12001)-6000),float(randomInt(12001)-6000),float(randomInt(2001)-1000));
   clients[i].sess.sessionTeam=randomInt(4);clients[i].sess.raceMode=randomInt(20)==0;
   clients[i].ps.pm_type=randomInt(15)==0?PM_NOCLIP:randomInt(20)==0?PM_SPECTATOR:PM_NORMAL;
   clients[i].ps.isJediMaster=randomInt(17)==0;clients[i].ps.duelInProgress=randomInt(8)==0;clients[i].ps.duelIndex=randomInt(MAX_CLIENTS);
   clients[i].pers.connected=randomInt(8)!=0;clients[i].dangerTime=level.time-randomInt(200);
   clients[i].ps.footstepTime=randomInt(4)==0?level.time+1:0;
   clients[i].ps.otherSoundTime=randomInt(8)==0?level.time+1:0;clients[i].ps.otherSoundLen=randomInt(1500);
   g_entities[i].health=randomInt(201)-30;g_entities[i].takedamage=randomInt(8)!=0;g_entities[i].s.solid=randomInt(8)!=0;g_entities[i].r.svFlags=randomInt(2)?SVF_BOT:0;
   pvs[i]=randomInt(4)!=0;visible[i]=randomInt(3)!=0;tricked[i]=randomInt(4)==0;
   gBotEventTracker[i].eventTime=randomInt(4)==0?level.time-1:level.time+1;
   gBotEventTracker[i].eventSequence=randomInt(2);gBotEventTracker[i].events[0]=randomInt(15);gBotEventTracker[i].events[1]=randomInt(15);
   if(randomInt(3)==0) {botstates[i]=&botstorage[i];botstorage[i].client=i;}
  }
  if(randomInt(3)==0) bs.currentEnemy=&g_entities[randomInt(MAX_CLIENTS+1)];
  if(randomInt(4)==0) {bs.lovednum=1;std::strcpy(bs.loved[0].name,clients[randomInt(MAX_CLIENTS)].pers.netname);bs.loved[0].level=randomInt(3);}
  Result r=compare(bs);eager+=r.before;lazy+=r.after;
  if(g_newBotAI.integer) CHECK(!r.after);
 }
 CHECK(eager>1000 && lazy<eager);
 std::printf("12000 randomized scans: %u -> %u conversions; winners/state/ordered calls identical\n",eager,lazy);
}
static void work() {
 unsigned eager[2]={},lazy[2]={};
 for(int mode=0;mode<2;++mode) for(int frame=0;frame<100;++frame) {
  reset();g_newBotAI.integer=mode;
  for(int i=0;i<24;++i) actor(i,float((i%6)*128+frame*(i%3-1)),float((i/6)*128+frame*(i%5-2)),float((i%3)*16));
  for(int i=0;i<24;++i) {
   bot_state_t bs={};bs.client=i;std::memcpy(bs.eye,clients[i].ps.origin,sizeof(vec3_t));std::memcpy(bs.origin,bs.eye,sizeof(vec3_t));
   Result r=compare(bs);CHECK(r.winner>=0);eager[mode]+=r.before;lazy[mode]+=r.after;
  }
 }
 CHECK(eager[0]==55200 && eager[1]==55200 && lazy[0]<eager[0]/2 && !lazy[1]);
 std::printf("24 moving actors, 2400 scans per AI mode: legacy %u -> %u; new %u -> %u conversions\n",eager[0],lazy[0],eager[1],lazy[1]);
}
int main(int argc,char **argv) {
 CHECK(argc==2);std::string test=argv[1];
 if(test=="randomized") randomized();else if(test=="work") work();else focused(test);
 std::printf("%s passed\n",argv[1]);
}
