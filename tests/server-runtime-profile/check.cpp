#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
typedef int qboolean;
enum {qfalse,qtrue,CS_CONNECTED=2,NA_BOT=1,CVAR_SERVERINFO=1,CVAR_SYSTEMINFO=2,CS_SERVERINFO=1,CS_SYSTEMINFO=2,
 GAME_RUN_FRAME=1,GAME_INIT,GAME_SHUTDOWN,BOTAI_START_FRAME,VM_GAME=0,ERR_DROP=1};
struct cvar_t {int integer;float value;char *string;};
static cvar_t profileCvar={0,0,nullptr};
static std::vector<std::string> operations,printed;
static std::map<std::string,void(*)()> commands;
static std::uint64_t clockReads,clockTicks;
struct TestClock {
 using time_point=std::chrono::steady_clock::time_point;
 static time_point now() {++clockReads;clockTicks+=100;return time_point(std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(clockTicks)));}
};
static void Com_Printf(const char *fmt,...) {char out[1024];va_list args;va_start(args,fmt);std::vsnprintf(out,sizeof(out),fmt,args);va_end(args);printed.emplace_back(out);operations.emplace_back("print");}
static cvar_t *Cvar_Get(const char *name,const char *value,int flags,const char *description="") {
 if(!std::strcmp(name,"sv_runtimeProfile")) {CHECK(!std::strcmp(value,"0") && !flags && *description);return &profileCvar;}
 CHECK(!std::strcmp(name,"gamename"));static cvar_t gamename={0,0,const_cast<char *>("")};return &gamename;
}
static void Cvar_CheckRange(cvar_t *cv,float low,float high,qboolean integral) {CHECK(cv==&profileCvar && low==0 && high==1 && integral);cv->integer=std::max(0,std::min(1,cv->integer));}
static void Cmd_AddCommand(const char *name,void(*fn)(),const char *) {CHECK(commands.emplace(name,fn).second);}
#include "profile_body.h"
#define CVAR(name) static cvar_t name##Value;static cvar_t *name=&name##Value
CVAR(sv_killserver);CVAR(sv_maxclients);CVAR(sv_hibernateTime);CVAR(sv_hibernateFPS);CVAR(com_sv_running);
CVAR(sv_fps);CVAR(com_timescale);CVAR(com_dedicated);CVAR(com_speeds);
struct client_t {int state;struct {struct {int type;} remoteAddress;} netchan;};
static client_t clients[32];
static struct {int initialized,gameStarted,time,nextSnapshotEntities,numSnapshotEntities;client_t *clients;int *snapshotEntities,gvmIsLegacy;
 struct {int enabled,lastTimeDisconnected;} hibernation;} svs;
static struct {int time,timeResidual,restartTime;} sv;
static int cvar_modifiedFlags,time_game,bot_enable;
static bool paused,com_errorEntered;
static int gameAction,botCalls,gameCalls,initCalls,shutdownCalls;
struct vm_t {bool isLegacy;};static vm_t vm;static vm_t *gvm;
static void log(const std::string &s) {operations.emplace_back(s);clockTicks+=1000;}
struct VMSwap {explicit VMSwap(vm_t *v) {CHECK(v==gvm);log("vm-enter");}~VMSwap() {log("vm-exit");}};
static void GVM_InitGame(int,int,int);static void GVM_ShutdownGame(int);static void GVM_RunFrame(int);static int GVM_BotAIStartFrame(int);
static void SV_BotFrame(int);static void SV_Shutdown(char *);static void SV_ShutdownGameProgs();static void SV_UnbindGame();
static void RunGame(int time) {++gameCalls;log("game:"+std::to_string(time));
 if(gameAction==1) SV_RuntimeProfileReset();
 else if(gameAction==2) throw 42;
 else if(gameAction==3) profileCvar.integer=0;
 else if(gameAction==4) profileCvar.integer=1;
}
static void InitGame(int time,int seed,int restart) {CHECK(time==sv.time && seed==123);++initCalls;log("init:"+std::to_string(restart));}
static void ShutdownGame(int restart) {++shutdownCalls;log("game-shutdown:"+std::to_string(restart));}
static int BotAI(int time) {++botCalls;log("bot:"+std::to_string(time));return 7;}
static struct {void (*RunFrame)(int);void (*InitGame)(int,int,int);void (*ShutdownGame)(int);int(*BotAIStartFrame)(int);} exports={RunGame,InitGame,ShutdownGame,BotAI},*ge=&exports;
static int VM_Call(vm_t *v,int action,int first,int second=0,int third=0) {
 CHECK(v==gvm);if(action==GAME_RUN_FRAME) RunGame(first);else if(action==GAME_INIT) InitGame(first,second,third);
 else if(action==GAME_SHUTDOWN) ShutdownGame(first);else {CHECK(action==BOTAI_START_FRAME);return BotAI(first);}return 0;
}
static qboolean SV_CheckPaused() {log("pause-check");return paused;}
static int Sys_Milliseconds() {log("sys-clock");return 200;}
static const char *va(const char *fmt,...) {static char out[256];va_list args;va_start(args,fmt);std::vsnprintf(out,sizeof(out),fmt,args);va_end(args);return out;}
static void Cvar_Set(const char *name,const char *value) {
 log(std::string("set:")+name+":"+value);int number=std::atoi(value);
 if(!std::strcmp(name,"sv_running")) com_sv_running->integer=number;
 else if(!std::strcmp(name,"sv_killserver")) sv_killserver->integer=number;
 else if(!std::strcmp(name,"sv_fps")) sv_fps->integer=number;
 else if(!std::strcmp(name,"timescale")) com_timescale->value=std::strtof(value,nullptr);
}
static const char *Cvar_VariableString(const char *) {return "mp/ffa3";}
static const char *Cvar_InfoString(int) {return "server-info";}
static const char *Cvar_InfoString_Big(int) {return "system-info";}
static void Cvar_Unset(cvar_t *) {log("unset");}
static void Cbuf_AddText(const char *text) {log(std::string("command:")+text);}
static void SV_SetConfigstring(int which,const char *) {log("config:"+std::to_string(which));}
static void SV_CalcPings() {log("pings");}
static void SV_CheckTimeouts() {log("timeouts");}
static void SV_SendClientMessages() {log("messages");}
static void SV_CheckCvars() {log("cvars");}
static void SV_MasterHeartbeat() {log("heartbeat");}
static struct Renderer {static void G2API_SetTime(int time,int zero) {CHECK(!zero);log("g2:"+std::to_string(time));}} renderer,*re=&renderer;
static void SV_FinalMessage(char *) {log("final-message");}
static void SV_RemoveOperatorCommands() {log("remove-operator");}
static void SV_MasterShutdown() {log("master-shutdown");}
static void SV_ChallengeShutdown() {log("challenge-shutdown");}
static void SV_ClearServer() {log("clear-server");sv={};}
static void CM_ClearMap() {log("clear-map");}
static void Z_Free(void *p) {CHECK(p==clients);log("free-clients");}
static void Com_Memset(void *p,int value,std::size_t n) {std::memset(p,value,n);}
static void CL_Disconnect(qboolean value) {CHECK(!value);log("disconnect");}
static void VM_Free(vm_t *v) {CHECK(v==gvm);log("free-vm");}
static vm_t *VM_Restart(vm_t *v) {CHECK(v==gvm);log("restart-vm");return v;}
static void SV_BindGame() {gvm=&vm;log("bind-game");}
static void SV_InitGame(qboolean restart) {GVM_InitGame(sv.time,123,restart);}
static void Com_Error(int,const char *) {throw 99;}
#include "wrappers.h"
static void reset(bool enabled,bool legacy=false,bool dedicated=false) {
 SV_RuntimeProfileReset();profileCvar.integer=enabled;operations.clear();printed.clear();clockReads=clockTicks=0;
 std::memset(clients,0,sizeof(clients));svs={};svs.clients=clients;svs.gameStarted=svs.initialized=1;svs.numSnapshotEntities=1024;
 sv={100,0,0};vm={legacy};gvm=&vm;paused=com_errorEntered=false;bot_enable=1;gameAction=botCalls=gameCalls=initCalls=shutdownCalls=0;
 sv_killserverValue={0,0,nullptr};sv_maxclientsValue={32,32,nullptr};sv_hibernateTimeValue={0,0,nullptr};sv_hibernateFPSValue={2,2,nullptr};
 com_sv_runningValue={1,1,nullptr};sv_fpsValue={40,40,nullptr};com_timescaleValue={1,1,nullptr};com_dedicatedValue={dedicated?1:0,0,nullptr};com_speedsValue={0,0,nullptr};
 cvar_modifiedFlags=time_game=0;
}
static const SVRuntimeSample &sample(SVRuntimeStage stage) {return samples[static_cast<unsigned>(stage)];}
static void count(SVRuntimeStage stage,unsigned expected) {CHECK(sample(stage).calls==expected);if(expected) CHECK(sample(stage).totalNs>0 && sample(stage).maxNs>0);}
static void allEmpty() {for(const auto &s:samples) CHECK(!s.calls && !s.totalNs && !s.maxNs);}
static void ordinary(bool legacy,bool dedicated,bool catchup) {
 reset(false,legacy,dedicated);const int msec=catchup?80:25;SV_Frame(msec);
 const auto expected=operations;const int expectedGames=gameCalls,expectedBots=botCalls,expectedTime=sv.time,expectedResidual=sv.timeResidual;
 CHECK(!clockReads && expectedGames==(catchup?3:1) && expectedBots==1);allEmpty();
 reset(true,legacy,dedicated);SV_Frame(msec);CHECK(operations==expected && gameCalls==expectedGames && botCalls==expectedBots && sv.time==expectedTime && sv.timeResidual==expectedResidual);
 count(SVRuntimeStage::Frame,1);count(SVRuntimeStage::BotAI,1);count(SVRuntimeStage::Game,expectedGames);count(SVRuntimeStage::ClientMessages,1);
 CHECK(clockReads==2u*(3+expectedGames));CHECK(printed.empty());
}
static void early(const std::string &test) {
 reset(true);
 if(test=="stopped") com_sv_running->integer=0;
 else if(test=="paused") paused=true;
 else if(test=="kill") sv_killserver->integer=1;
 else if(test=="no-vm") {gvm=nullptr;GVM_RunFrame(100);CHECK(!clockReads);allEmpty();return;}
 else if(test=="no-bots") {bot_enable=0;SV_BotFrame(100);CHECK(!clockReads && !botCalls);allEmpty();return;}
 else if(test=="no-game") {svs.gameStarted=0;SV_BotFrame(100);CHECK(!clockReads && !botCalls);allEmpty();return;}
 else if(test=="deferred-restart") sv.restartTime=sv.time;
 else CHECK(false);
 SV_Frame(25);
 CHECK(!gameCalls);
 if(test=="deferred-restart") {count(SVRuntimeStage::Frame,1);count(SVRuntimeStage::BotAI,1);count(SVRuntimeStage::ClientMessages,0);CHECK(clockReads==4);}
 else {CHECK(!clockReads);allEmpty();}
}
static void lifecycle(const std::string &test) {
 reset(true,test=="restart-legacy");SV_Frame(25);CHECK(printed.empty());const auto before=clockReads;
 if(test=="commands") {
  commands.at("sv_runtimeProfileReport")();CHECK(printed.size()==5 && clockReads==before);count(SVRuntimeStage::Frame,1);
  CHECK(printed[1].find("sv-frame calls=1")!=std::string::npos && printed[3].find("game-tick calls=1")!=std::string::npos);
  commands.at("sv_runtimeProfileReset")();allEmpty();CHECK(clockReads==before);return;
 }
 if(test=="restart" || test=="restart-legacy") {
  SV_RestartGame();CHECK(initCalls==1 && shutdownCalls==1 && gvm==&vm && printed.size()==5);allEmpty();
  CHECK(clockReads==before);SV_Frame(25);count(SVRuntimeStage::Game,1);return;
 }
 if(test=="map") {
  {SVRuntimeProfileScope old(SVRuntimeStage::Frame);SV_ShutdownGameProgs();CHECK(!gvm);SV_BindGame();SV_InitGame(qfalse);GVM_RunFrame(sv.time);}
  CHECK(printed.size()==5 && initCalls==1 && shutdownCalls==1);count(SVRuntimeStage::Frame,0);count(SVRuntimeStage::Game,1);CHECK(clockReads==before+3);return;
 }
 if(test=="shutdown" || test=="shutdown-error") {
  com_errorEntered=test=="shutdown-error";SV_Shutdown(const_cast<char *>("test"));
  CHECK(printed.size()==5 && !com_sv_running->integer && !gvm && shutdownCalls==1 && clockReads==before);allEmpty();
  CHECK(std::find(operations.begin(),operations.end(),"final-message")==operations.end() || !com_errorEntered);
  const auto output=printed;SV_Shutdown(const_cast<char *>("again"));CHECK(printed==output);return;
 }
 CHECK(false);
}
static void transition(const std::string &test) {
 reset(test!="enable-midframe");gameAction=test=="reset-midframe"?1:test=="exception"?2:test=="disable-midframe"?3:4;
 try {SV_Frame(25);CHECK(test!="exception");}catch(int n) {CHECK(test=="exception" && n==42);}
 if(test=="reset-midframe") {count(SVRuntimeStage::Frame,0);count(SVRuntimeStage::BotAI,0);count(SVRuntimeStage::Game,0);count(SVRuntimeStage::ClientMessages,1);CHECK(clockReads==6);}
 else if(test=="exception") {count(SVRuntimeStage::Frame,1);count(SVRuntimeStage::BotAI,1);count(SVRuntimeStage::Game,1);count(SVRuntimeStage::ClientMessages,0);CHECK(clockReads==6);}
 else if(test=="disable-midframe") {count(SVRuntimeStage::Frame,0);count(SVRuntimeStage::BotAI,1);count(SVRuntimeStage::Game,0);count(SVRuntimeStage::ClientMessages,0);CHECK(clockReads==4);}
 else {count(SVRuntimeStage::Frame,0);count(SVRuntimeStage::BotAI,0);count(SVRuntimeStage::Game,0);count(SVRuntimeStage::ClientMessages,1);CHECK(clockReads==2);}
}
int main(int argc,char **argv) {
 CHECK(argc==2);const std::string test=argv[1];
 CHECK(profileCvar.integer==0);SV_RuntimeProfileInit();CHECK(!clockReads && profileCvar.integer==0 && commands.size()==2);
 if(test=="listen" || test=="dedicated" || test=="legacy" || test=="catchup") ordinary(test=="legacy",test=="dedicated",test=="catchup");
 else if(test=="commands" || test=="restart" || test=="restart-legacy" || test=="map" || test=="shutdown" || test=="shutdown-error") lifecycle(test);
 else if(test=="reset-midframe" || test=="exception" || test=="disable-midframe" || test=="enable-midframe") transition(test);
 else if(test=="cli") {reset(true);SV_RuntimeProfileInit();CHECK(profileCvar.integer==1 && commands.size()==2 && !clockReads);}
 else early(test);
 std::printf("%s passed\n",argv[1]);
}
