#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "menudef.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
typedef int qboolean;
enum { qfalse, qtrue, MAX_QPATH = 64, WINDOW_HASFOCUS = 1, WINDOW_VISIBLE = 2, A_MOUSE1 = 11, A_ENTER, A_MOUSE2, A_MOUSE3, A_MWHEELDOWN, A_MWHEELUP, A_CURSOR_LEFT };
struct rectDef_t { float x,y,w,h; };
struct itemDef_t;
#include "types.h"
struct itemDef_t { float special; const char *cvar; struct { multiDef_t *multi; } typeData; struct { rectDef_t rect; int flags; } window; void *parent; };
struct menuDef_t {
    int itemCount; itemDef_t *items[8]; const char *onOpen, *soundName;
    struct { int flags; } window; int appearanceTime;
};
static menuDef_t *focusedMenu, *scriptMenu;
static menuDef_t *Menu_GetFocused() { return focusedMenu; }
static struct {
    qboolean inGameLoad;
    int playerSpeciesCount, playerSpeciesIndex, playerSpeciesMax, languageCount;
    playerSpeciesInfo_t *playerSpecies;
    struct { int resolutionCount; const char *resolutions[8]; } uiDC;
} uiInfo;
static qboolean uiSpeciesLoaded, uiSpeciesBuilding;
static unsigned int uiSpeciesGeneration = 1;
static int uiSpeciesBrowserCount;
static int builds, directoryReads, browserUpdates;
static bool emptyCatalog;
static std::map<std::string, std::string> cvars;
static std::deque<std::string> strings;
static struct { int integer; } ui_selectedModelIndex, ui_char_model, ui_char_color_red, ui_char_color_green, ui_char_color_blue;
static int Q_stricmp(const char *a, const char *b) {
    std::string x(a), y(b);
    for(char &c:x)c=char(std::tolower(static_cast<unsigned char>(c)));
    for(char &c:y)c=char(std::tolower(static_cast<unsigned char>(c)));
    return x.compare(y);
}
static void Q_strncpyz(char *out, const char *in, size_t length) { std::snprintf(out,length,"%s",in); }
static void Q_strcat(char *out, size_t length, const char *in) { CHECK(std::strlen(out)+std::strlen(in)<length);std::strcat(out,in); }
static char *Q_strupr(char *text) { for(char *p=text;*p;++p)*p=char(std::toupper(static_cast<unsigned char>(*p))); return text; }
static char *va(const char *format, ...) {
    static char buffers[8][1024]; static unsigned next; char *out=buffers[next++%8];
    va_list args; va_start(args,format); std::vsnprintf(out,1024,format,args); va_end(args); return out;
}
static void Com_sprintf(char *out, size_t size, const char *format, ...) {
    va_list args;va_start(args,format);std::vsnprintf(out,size,format,args);va_end(args);
}
static const char *String_Alloc(const char *text) { strings.emplace_back(text); return strings.back().c_str(); }
static qboolean String_Parse(char **input, const char **out) {
    char *p=*input;while(*p && std::isspace(static_cast<unsigned char>(*p)))++p;
    if(!*p){*input=p;return qfalse;}
    char *start=p;
    if(*p==';')++p;else while(*p && *p!=';' && !std::isspace(static_cast<unsigned char>(*p)))++p;
    strings.emplace_back(start,p-start);*out=strings.back().c_str();*input=p;return qtrue;
}
static char *UI_Cvar_VariableString(const char *name) {
    static char buffers[16][1024]; static unsigned next; char *out=buffers[next++%16];
    Q_strncpyz(out,cvars[name].c_str(),1024);return out;
}
static int UI_HeadIndexForModel(const char *) { return -1; }
static void UI_ProfileStage(const char *, int) {}
static void UI_UpdateSpeciesBrowser() { ++browserUpdates;uiSpeciesBrowserCount=uiInfo.playerSpeciesCount; }
static void UI_FreeAllSpecies();
static void UI_BuildPlayerSpeciesCatalog(qboolean) {
    ++builds; ++directoryReads;
    uiInfo.playerSpeciesIndex=0;uiInfo.playerSpeciesMax=2;uiInfo.playerSpeciesCount=emptyCatalog?0:2;
    uiInfo.playerSpecies=static_cast<playerSpeciesInfo_t *>(std::calloc(2,sizeof(playerSpeciesInfo_t)));
    for(int i=0;i<uiInfo.playerSpeciesCount;++i) {
        auto &s=uiInfo.playerSpecies[i];Q_strncpyz(s.Name,i?"alien":"human",sizeof(s.Name));
        s.SkinHeadCount=s.SkinTorsoCount=s.SkinLegCount=s.ColorCount=1;
        s.SkinHead=static_cast<skinName_t *>(std::calloc(1,sizeof(skinName_t)));
        s.SkinTorso=static_cast<skinName_t *>(std::calloc(1,sizeof(skinName_t)));
        s.SkinLeg=static_cast<skinName_t *>(std::calloc(1,sizeof(skinName_t)));
        s.Color=static_cast<playerColor_t *>(std::calloc(1,sizeof(playerColor_t)));
        Q_strncpyz(s.SkinHead[0].name,"head_a1",SKIN_LENGTH);
    }
}
struct pc_token_t { char string[1024]; };
static struct {
    static int Milliseconds() { return 0; }
    static void Cvar_Set(const char *name,const char *value) { CHECK(value);cvars[name]=value; }
    static void Cvar_SetValue(const char *name,float value) { cvars[name]=std::to_string(value); }
    static void Cvar_Update(void *) {}
    static int PC_ReadToken(int,pc_token_t *token) { std::strcpy(token->string,"feeder");return 1; }
    static void SE_GetLanguageName(int,char *) {}
} services;
static decltype(services) *trap=&services;
static struct {
    float cursorx=10,cursory=10;
    void (*runScript)(char **);
    static void startBackgroundTrack(const char *,const char *,qboolean) {}
    static void getCVarString(const char *name,char *out,int size) { Q_strncpyz(out,cvars[name].c_str(),size); }
    static float getCVarValue(const char *) { return 0; }
    static void setCVar(const char *name,const char *value) { CHECK(value);cvars[name]=value; }
    static qboolean feederSelection(float,int,itemDef_t *) { return qtrue; }
} display;
static decltype(display) *DC=&display;
static void Item_ValidateTypeData(itemDef_t *item) { CHECK(item->typeData.multi); }
static const char *UI_LanguageDisplayName(const char *name) { return name; }
static void UI_SortLanguageOptions(multiDef_t *) {}
static qboolean PC_String_Parse(int,const char **) { return qfalse; }
static void PC_SourceError(int,const char *,...) {}
static void Item_ClearCapture() {}
static void Display_CloseCinematics() {}
struct commandDef_t { const char *name; qboolean (*handler)(itemDef_t *,char **); };
static commandDef_t commandList[1];
static int scriptCommandCount;
#include "script.h"
#include "lifecycle.h"
#include "character.h"
#define UI_BUILD 1
#include "shared.h"
#include "count.h"
static void init(qboolean inGameLoad, qboolean lazySpecies = qtrue) {
    uiInfo.inGameLoad=inGameLoad;
#include "init.h"
}
static menuDef_t *nestedMenu;
static void runScript(char **args) {
    const char *command=NULL;CHECK(String_Parse(args,&command));
    if(!std::strcmp(command,"nested")) {
        menuDef_t *outer=Menu_GetScriptMenu();CHECK(outer && outer!=nestedMenu);
        Menus_Activate(nestedMenu);CHECK(Menu_GetScriptMenu()==outer);
    } else {
        CHECK(!std::strcmp(command,"getcharcvars"));UI_GetCharacterCvars();
    }
}
int main() {
    display.runScript=runScript;
    cvars["model"]="alien/head_a1|torso_b2|lower_c3";
    cvars["ui_char_model"]="alien";cvars["char_color_red"]="123";
    itemDef_t normalControl={};normalControl.special=FEEDER_Q3HEADS;
    itemDef_t speciesControl={};speciesControl.special=FEEDER_PLAYER_SPECIES;
    menuDef_t normal={};normal.itemCount=1;normal.items[0]=&normalControl;normal.onOpen="uiScript getcharcvars;";
    menuDef_t custom={};custom.itemCount=1;custom.items[0]=&speciesControl;custom.onOpen="uiScript getcharcvars;";
    for(qboolean inGame:{qfalse,qtrue}) {
        init(inGame);const int initial=builds;
        // onOpen may run while another menu still appears focused. Its actual
        // script parent decides whether a catalogue is required.
        focusedMenu=&custom;Menus_Activate(&normal);
        CHECK(builds==initial && !Menu_GetScriptMenu());
        CHECK(cvars["ui_char_model"]=="alien" && cvars["ui_char_skin_legs"]=="lower_c3");
        focusedMenu=&normal;UI_GetCharacterCvars();CHECK(builds==initial);
        Menus_Activate(&custom);
        CHECK(builds==initial+1 && uiInfo.playerSpeciesIndex==1 && !Menu_GetScriptMenu());
        CHECK(cvars["ui_char_skin_head"]=="head_a1" && cvars["ui_char_skin_torso"]=="torso_b2");
    }
    for(int feeder:{FEEDER_PLAYER_SPECIES,FEEDER_PLAYER_SPECIES_BROWSER,FEEDER_PLAYER_SKIN_HEAD,FEEDER_PLAYER_SKIN_TORSO,FEEDER_PLAYER_SKIN_LEGS,FEEDER_COLORCHOICES}) {
        init(qtrue);const int initial=builds;speciesControl.special=float(feeder);
        focusedMenu=&normal;Menus_Activate(&custom);
        CHECK(builds==initial+1 && uiInfo.playerSpeciesIndex==1);
    }
    speciesControl.special=FEEDER_PLAYER_SPECIES;
    init(qtrue);const int nestedBefore=builds;
    nestedMenu=&custom;normal.onOpen="uiScript nested; uiScript getcharcvars;";
    focusedMenu=&normal;Menus_Activate(&normal);
    CHECK(builds==nestedBefore+1 && !Menu_GetScriptMenu());
    normal.onOpen="uiScript getcharcvars;";
    cvars["model"]="stormtrooper/default";init(qtrue);const int normalBefore=builds;
    focusedMenu=&custom;Menus_Activate(&normal);
    CHECK(builds==normalBefore && cvars["ui_char_model"]=="stormtrooper/default");
    cvars["model"]="alien/head_a1|torso_b2|lower_c3";
    focusedMenu=NULL;
    for(qboolean inGame:{qfalse,qtrue}) {
        init(inGame); const int initial=builds;
        multiDef_t explicitOptions={};explicitOptions.count=1;explicitOptions.strDef=qtrue;
        explicitOptions.cvarList[0]="Custom alien";explicitOptions.cvarStr[0]="alien";
        itemDef_t explicitItem={float(FEEDER_PLAYER_SPECIES),"ui_char_model",{&explicitOptions},{{0,0,100,20},WINDOW_HASFOCUS}};
        CHECK(Item_Multi_CountSettings(&explicitItem)==1 && builds==initial);
        CHECK(!std::strcmp(Item_Multi_Setting(&explicitItem),"Custom alien"));
        multiDef_t options={};itemDef_t item={float(FEEDER_PLAYER_SPECIES),"ui_char_model",{&options},{{0,0,100,20},WINDOW_HASFOCUS}};
        CHECK(ItemParse_cvarStrList(&item,1));
        CHECK(builds==initial && options.count==0 && uiInfo.playerSpeciesCount==0);
        CHECK(cvars["ui_char_model"]=="alien" && cvars["ui_char_skin_head"]=="head_a1");
        CHECK(Item_Multi_CountSettings(&item)==2);
        CHECK(builds==initial+1 && directoryReads==builds && uiInfo.playerSpeciesIndex==1);
        CHECK(!std::strcmp(Item_Multi_Setting(&item),"@MENUS_ALIEN"));
        CHECK(Item_Multi_FindCvarByValue(&item)==1);
        CHECK(Item_Multi_HandleKey(&item,A_MOUSE1));CHECK(cvars["ui_char_model"]=="human");
        for(int id:{FEEDER_PLAYER_SPECIES,FEEDER_PLAYER_SPECIES_BROWSER,FEEDER_PLAYER_SKIN_HEAD,FEEDER_PLAYER_SKIN_TORSO,FEEDER_PLAYER_SKIN_LEGS,FEEDER_COLORCHOICES})
            CHECK(speciesCount(float(id))==(id==FEEDER_PLAYER_SPECIES||id==FEEDER_PLAYER_SPECIES_BROWSER?2:1));
        CHECK(builds==initial+1);
        UI_GetCharacterCvars();CHECK(cvars["ui_char_model"]=="alien" && uiInfo.playerSpeciesIndex==1);
        CHECK(cvars["ui_char_skin_torso"]=="torso_b2" && cvars["ui_char_skin_legs"]=="lower_c3");
        UI_BuildPlayerModel_List(qtrue);CHECK(builds==initial+2);
        CHECK(Item_Multi_CountSettings(&item)==2 && builds==initial+2);
    }
    for(qboolean inGame:{qfalse,qtrue}) {
        const int initial=builds;init(inGame,qfalse);
        CHECK(builds==initial+1 && uiInfo.playerSpeciesCount==2 && uiInfo.playerSpeciesIndex==1);
        CHECK(cvars["ui_char_model"]=="alien" && cvars["ui_char_skin_head"]=="head_a1");
        multiDef_t options={};itemDef_t item={float(FEEDER_PLAYER_SPECIES),"ui_char_model",{&options},{{0,0,100,20},WINDOW_HASFOCUS}};
        CHECK(ItemParse_cvarStrList(&item,1));CHECK(Item_Multi_CountSettings(&item)==2 && builds==initial+1);
    }
    cvars["model"]="alien";
    for(qboolean lazy:{qfalse,qtrue}) {
        init(qtrue,lazy);UI_GetCharacterCvars();
        CHECK(cvars["ui_char_model"]=="alien" && uiInfo.playerSpeciesIndex==1);
    }
    UI_FreeAllSpecies();const int before=builds;UI_BuildPlayerModel_List(qtrue);CHECK(builds==before);
    emptyCatalog=true;CHECK(!UI_EnsurePlayerSpecies());CHECK(builds==before+1);
    CHECK(UI_CurrentPlayerSpecies()->SkinHeadCount==0 && !UI_CurrentPlayerSpecies()->Name[0]);
    multiDef_t options={};itemDef_t item={float(FEEDER_PLAYER_SPECIES),"ui_char_model",{&options},{{0,0,100,20},WINDOW_HASFOCUS}};
    CHECK(Item_Multi_CountSettings(&item)==0);CHECK(!Item_Multi_HandleKey(&item,A_MOUSE1));
    CHECK(builds==before+1);UI_FreeAllSpecies();UI_FreeAllSpecies();
    std::puts("Lazy UI species lifecycle and consumers passed.");
}
