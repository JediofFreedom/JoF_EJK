#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); std::exit(1); } } while (0)
enum qboolean { qfalse, qtrue };
enum { CA_DISCONNECTED, CA_CINEMATIC, CA_CONNECTED, CA_ACTIVE,
       UIMENU_NONE, UIMENU_MAIN, ERR_DISCONNECT, VERSION_1_01, MAX_STRING_CHARS = 1024 };
struct Cvar { int integer; } running = {1}, timedemo = {1};
static Cvar *com_cl_running = &running, *cl_timedemo = &timedemo;
static struct Connection {
  int demorecording, download, demofile;
  char downloadTempName[128], downloadName[128];
  int timeDemoStart, timeDemoFrames, demoplaying;
} clc;
static struct { int state; bool uiStarted; int JKVersion; } cls;
static qboolean cl_connectedToPureServer = qtrue;
static std::map<std::string, std::string> cvars;
static std::vector<std::string> events, output;
static std::vector<int> closed;
static int now = 11000, executed;
static std::string commandBuffer, executedText;
static int Sys_Milliseconds() { return now; }
static void Com_Printf(const char *format, ...) {
  char text[2048]; va_list args; va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args); va_end(args); output.push_back(text);
}
static void Com_DPrintf(const char *, ...) {}
static void Com_Error(int code, const char *, ...) { throw code; }
static void Cvar_Set(const char *name, const char *value) { cvars[name] = value; }
static const char *Cvar_VariableString(const char *name) { return cvars[name].c_str(); }
static void Q_strncpyz(char *dest, const char *src, size_t size) {
  if (size) { std::strncpy(dest, src, size - 1); dest[size - 1] = 0; }
}
static void Cbuf_AddText(const char *text) { commandBuffer += text; }
static void Cbuf_Execute() {
  CHECK(cvars["nextdemo"].empty());
  events.push_back("execute nextdemo"); executedText = commandBuffer; commandBuffer.clear(); ++executed;
}
static void CL_StopRecord_f() { events.push_back("stop recording"); clc.demorecording = 0; }
static void FS_FCloseFile(int file) { closed.push_back(file); }
static char *FS_BuildOSPath(const char *, const char *, const char *) {
  static char path[] = "fixture/download.tmp/"; return path;
}
static void FS_Remove(const char *path) { CHECK(std::string(path) == "fixture/download.tmp"); }
static void UIVM_SetActiveMenu(int menu) {
  CHECK(cls.uiStarted); events.push_back(menu == UIMENU_MAIN ? "main menu" : "close menu");
}
static void SCR_StopCinematic() { events.push_back("stop cinematic"); }
static void S_ClearSoundBuffer() { events.push_back("clear sound buffer"); }
static void S_StopAllSounds() { events.push_back("stop sounds"); }
static void CL_AddReliableCommand(const char *command, qboolean disconnect) {
  CHECK(std::string(command) == "disconnect" && disconnect == qtrue);
}
static void CL_WritePacket() { events.push_back("write packet"); }
static void FS_PureServerSetLoadedPaks(const char *checksums, const char *names) {
  CHECK(!*checksums && !*names); events.push_back("clear pure loaded");
}
static void FS_PureServerSetReferencedPaks(const char *checksums, const char *names) {
  CHECK(!*checksums && !*names); events.push_back("clear pure referenced");
}
static void CL_ClearState() { events.push_back("clear state"); }
static qboolean CL_VideoRecording() { return qfalse; }
static void SCR_UpdateScreen() { CHECK(false); }
static void CL_CloseAVI() { CHECK(false); }
static void CL_UpdateGUID(const char *prefix, int size) { CHECK(!prefix && !size); }
#define Com_Memset std::memset
#include "demo_methods.h"

static void initialize(bool ui = true) {
  cls.state = CA_ACTIVE; cls.uiStarted = ui;
  clc.demoplaying = 1; clc.demofile = 42; clc.timeDemoStart = 1000; clc.timeDemoFrames = 5005;
  cvars["ui_singlePlayerActive"] = "1";
}
static void complete() {
  try { CL_DemoCompleted(); }
  catch (int code) { std::fprintf(stderr, "Demo completion unexpectedly raised error %d\n", code); std::exit(2); }
  CHECK(cls.state == CA_DISCONNECTED && !clc.demofile && !clc.demoplaying);
  CHECK(closed.size() == 1 && closed[0] == 42);
  CHECK(cl_connectedToPureServer == qfalse);
  CHECK(cvars["ui_singlePlayerActive"] == "0");
  CHECK(std::count(events.begin(), events.end(), "stop cinematic") == 1);
  CHECK(std::count(events.begin(), events.end(), "stop sounds") == 1);
  CHECK(std::count(events.begin(), events.end(), "write packet") == 3);
}
static void nextDemo() {
  initialize(); cvars["nextdemo"] = "exec runtime-complete.cfg";
  complete();
  CHECK(executed == 1 && executedText == "exec runtime-complete.cfg\n");
  CHECK(output.size() == 1 && output[0].find("5005 frames, 10.0 seconds: 500.5 fps") != std::string::npos);
  CHECK(std::find(events.begin(), events.end(), "main menu") < std::find(events.begin(), events.end(), "execute nextdemo"));
  CL_NextDemo(); CHECK(executed == 1);
}
static void menu(bool ui, bool zeroElapsed) {
  initialize(ui); cvars["nextdemo"] = "";
  if (zeroElapsed) clc.timeDemoStart = now;
  else cl_timedemo = nullptr;
  complete();
  CHECK(!executed && output.empty());
  CHECK(std::count(events.begin(), events.end(), "main menu") == (ui ? 1 : 0));
  CHECK(std::count(events.begin(), events.end(), "close menu") == (ui ? 1 : 0));
}
static void manualDisconnect() {
  initialize();
  bool threw = false;
  try { CL_Disconnect_f(); }
  catch (int code) { CHECK(code == ERR_DISCONNECT); threw = true; }
  CHECK(threw && !executed && clc.demofile == 42);
  CHECK(cvars["ui_singlePlayerActive"] == "0");
}
int main(int argc, char **argv) {
  CHECK(argc == 2);
  if (!std::strcmp(argv[1], "nextdemo")) nextDemo();
  else if (!std::strcmp(argv[1], "menu")) menu(true, false);
  else if (!std::strcmp(argv[1], "missing_ui")) menu(false, false);
  else if (!std::strcmp(argv[1], "zero_elapsed")) menu(true, true);
  else if (!std::strcmp(argv[1], "manual_disconnect")) manualDisconnect();
  else CHECK(false);
  return 0;
}
