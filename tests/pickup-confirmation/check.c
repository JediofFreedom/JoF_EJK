#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define MAX_CLIENTS 32
#define MAX_INFO_STRING 1024
#define CS_SERVERINFO 0
#define CHAN_AUTO 0
typedef int qboolean;
enum { IT_HEALTH=1, IT_ARMOR, IT_AMMO, IT_HOLDABLE, IT_WEAPON };
#include "../../codemp/game/bg_pickup.h"
typedef struct { int giType; const char *pickup_sound; } item_t;
typedef struct { struct { int number, modelindex; } s; item_t *item; } gentity_t;
static item_t bg_itemlist[] = {{0,0},{IT_HEALTH,"health"},{IT_ARMOR,"armor"},{IT_AMMO,"ammo"},{IT_HOLDABLE,"bacta"},{IT_WEAPON,"weapon"}};
static int bg_numItems = ARRAY_LEN(bg_itemlist);
static struct { int integer; } g_pickupConfirm = {1}, cg_pickupConfirm = {1};
typedef struct { struct { int clientNum; } ps; } snapshot_t;
static snapshot_t snapshot;
static struct {
 int time, itemPickup, itemPickupTime, pickupQueue[16], pickupQueueHead, pickupQueueCount, clientNum;
 snapshot_t *snap;
} cg;
static const char *serverVersion = "1", *clientVersion = "1", *argument = "1", *extra = "";
static char messages[32][64];
static int messageCount, soundCount, feedbackCount, feedback[32];
static const char *CG_ConfigString(int n) { return serverVersion; }
static const char *Info_ValueForKey(const char *info, const char *key) { return info; }
static const char *CG_Argv(int n) { return n == 1 ? argument : extra; }
static void GetUserinfo(int n, char *out, int size) { snprintf(out,size,"%s",clientVersion); }
static void SendServerCommand(int n,const char *text) { CHECK(n==0); CHECK(messageCount<32); strcpy(messages[messageCount++],text); }
static int RegisterSound(const char *s) { CHECK(s && *s); return 1; }
static void StartSound(int s,int channel) { ++soundCount; }
static char *va(const char *fmt,...) { static char buf[64]; va_list args; va_start(args,fmt); vsnprintf(buf,sizeof(buf),fmt,args); va_end(args); return buf; }
static struct {
 void (*GetUserinfo)(int,char*,int);
 void (*SendServerCommand)(int,const char*);
 int (*S_RegisterSound)(const char*);
 void (*S_StartLocalSound)(int,int);
} imports = {GetUserinfo,SendServerCommand,RegisterSound,StartSound}, *trap=&imports;
static void CG_QueuePickupNotification(int);
static void CG_ItemPickup(int index) { feedback[feedbackCount++]=index; CG_QueuePickupNotification(index); }
#include "actual.h"
int main(void) {
 gentity_t collector={{0,0},NULL}, item={{50,2},&bg_itemlist[2]};
 int i;
 cg.snap=&snapshot; cg.time=1000;
 /* Two accepted items retain separate reliable messages, regardless of event
    ring contents or item entities disappearing before the client receives them. */
 G_ConfirmItemPickup(&item,&collector);
 item.s.modelindex=1; item.item=&bg_itemlist[1];
 G_ConfirmItemPickup(&item,&collector);
 CHECK(messageCount==2);
 for(i=0;i<messageCount;i++) { argument=strchr(messages[i],' ')+1; CG_ConfirmedPickup_f(); }
 CHECK(soundCount==2 && feedbackCount==2 && feedback[0]==2 && feedback[1]==1);
 CHECK(cg.itemPickup==2 && cg.pickupQueueCount==1);
 cg.time=1749; CG_AdvancePickupQueue(); CHECK(cg.itemPickup==2);
 cg.time=1750; CG_AdvancePickupQueue(); CHECK(cg.itemPickup==1 && cg.pickupQueueCount==0);
 /* Consecutive identical accepted items are distinct, not time-deduplicated. */
 argument="1"; CG_ConfirmedPickup_f(); CG_ConfirmedPickup_f();
 CHECK(feedbackCount==4 && soundCount==4);
 argument="-1"; CG_ConfirmedPickup_f(); argument="99999999999999999999"; CG_ConfirmedPickup_f();
 argument="1x"; CG_ConfirmedPickup_f(); argument="0"; CG_ConfirmedPickup_f();
 argument="5"; CG_ConfirmedPickup_f(); argument="1"; extra="bad"; CG_ConfirmedPickup_f(); extra="";
 CHECK(feedbackCount==4);
 snapshot.ps.clientNum=1; CG_ConfirmedPickup_f(); CHECK(feedbackCount==4); snapshot.ps.clientNum=0;
 serverVersion=""; CG_ConfirmedPickup_f(); CHECK(feedbackCount==4 && !CG_UsesPickupConfirmation());
 serverVersion="2"; CHECK(!CG_UsesPickupConfirmation()); serverVersion="1";
 clientVersion=""; G_ConfirmItemPickup(&item,&collector); CHECK(messageCount==2);
 clientVersion="2"; G_ConfirmItemPickup(&item,&collector); CHECK(messageCount==2);
 clientVersion="1"; item.item=&bg_itemlist[5]; G_ConfirmItemPickup(&item,&collector); CHECK(messageCount==2);
 item.item=&bg_itemlist[1]; collector.s.number=MAX_CLIENTS; G_ConfirmItemPickup(&item,&collector); CHECK(messageCount==2);
 for(i=0;i<40;i++) CG_QueuePickupNotification(1);
 CHECK(cg.pickupQueueCount==16);
 for(i=0;i<16;i++) { cg.time+=750; CG_AdvancePickupQueue(); }
 CHECK(cg.pickupQueueCount==0);
 puts("PASS: paired confirmation, two-item feedback and HUD queue, repeat items, malformed commands, legacy/version fallback, spectators, bounded backlog.");
 return 0;
}
