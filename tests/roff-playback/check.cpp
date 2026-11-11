#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "qcommon/q_shared.h"
#include "game/g_public.h"
static int liveROFFEntities;
#include "roff_declarations.h"

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); std::exit(1); } } while (0)
static struct { int time; } svs = {1000};
static sharedEntity_t serverEntity;
static trajectory_t clientPosition, clientRotation;
static vec3_t clientOrigin = {11, 22, 33}, clientAngles = {0, 90, 0};
static int angleCalls;
static bool missingEntity;
struct Note { bool client; int entity; std::string text; };
static std::vector<Note> notes;
static std::vector<std::string> warnings;
static sharedEntity_t *SV_GentityNum(int entity) { return missingEntity ? nullptr : &serverEntity; }
static void Com_Printf(const char *format, ...) {
  char text[2048]; va_list args; va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args); va_end(args);
  warnings.push_back(text);
}
static trajectory_t *CGVM_GetOriginTrajectory(int) { return &clientPosition; }
static trajectory_t *CGVM_GetAngleTrajectory(int) { return &clientRotation; }
static void CGVM_GetOrigin(int, vec3_t output) { VectorCopy(clientOrigin, output); }
static void CGVM_GetAngles(int, vec3_t output) { ++angleCalls; VectorCopy(clientAngles, output); }
static void CGVM_ROFF_NotetrackCallback(int entity, const char *note) { notes.push_back({true, entity, note}); }
static void GVM_ROFF_NotetrackCallback(int entity, const char *note) { notes.push_back({false, entity, note}); }
#include "roff_methods.h"

static void same(const vec3_t actual, float a, float b, float c) {
  CHECK(std::fabs(actual[0] - a) < 0.001f);
  CHECK(std::fabs(actual[1] - b) < 0.001f);
  CHECK(std::fabs(actual[2] - c) < 0.001f);
}
static void prepare(CROFFSystem::CROFF &roff) {
  roff.mROFFEntries = 1; roff.mFrameTime = 100; roff.mLerp = 10;
  roff.mNumNoteTracks = 0;
  roff.mMoveRotateList = new CROFFSystem::TROFF2Entry[1]();
  VectorSet(roff.mMoveRotateList[0].mOriginOffset, 1, 2, 3);
  VectorSet(roff.mMoveRotateList[0].mRotateOffset, 4, 5, 6);
  roff.mMoveRotateList[0].mStartNote = -1;
}
static CROFFSystem::SROFFEntity entity(bool client) {
  CROFFSystem::SROFFEntity result = {};
  result.mEntID = 7; result.mROFFID = 1; result.mIsClient = client ? qtrue : qfalse;
  result.mNextROFFTime = svs.time;
  return result;
}
static void clientApply() {
  CROFFSystem system; CROFFSystem::CROFF roff("fixture", 1); prepare(roff);
  auto ent = entity(true);
  CHECK(system.ApplyROFF(&ent, &roff));
  CHECK(clientPosition.trType == TR_LINEAR && clientRotation.trType == TR_LINEAR);
  same(clientPosition.trBase, 11, 22, 33); same(clientRotation.trBase, 0, 90, 0);
  same(clientPosition.trDelta, 10, 20, 30); same(clientRotation.trDelta, 40, 50, 60);
  CHECK(ent.mROFFFrame == 1 && ent.mNextROFFTime == 1100);
  CHECK(system.ApplyROFF(&ent, &roff)); // Not due: leave the trajectories unchanged.
  svs.time = 1100;
  CHECK(!system.ApplyROFF(&ent, &roff));
  CHECK(clientPosition.trType == TR_STATIONARY && clientRotation.trType == TR_STATIONARY);
  same(clientPosition.trDelta, 0, 0, 0); same(clientRotation.trDelta, 0, 0, 0);
}
static void clientClear() {
  CROFFSystem system; auto ent = entity(true);
  CHECK(system.ClearLerp(&ent));
  CHECK(clientPosition.trType == TR_STATIONARY && clientRotation.trType == TR_STATIONARY);
  same(clientPosition.trBase, 11, 22, 33); same(clientRotation.trBase, 0, 90, 0);
}
static void clientTranslation() {
  CROFFSystem system; CROFFSystem::CROFF roff("fixture", 1); prepare(roff);
  CHECK(system.Play(7, 1, qtrue, qtrue));
  CHECK(angleCalls == 1);
  CHECK(system.mROFFEntList.size() == 1);
  same(system.mROFFEntList[0]->mStartAngles, 0, 90, 0);
  VectorSet(roff.mMoveRotateList[0].mOriginOffset, 1, 0, 0);
  CHECK(system.ApplyROFF(system.mROFFEntList[0], &roff));
  same(clientPosition.trDelta, 0, 10, 0);
  CHECK(system.PurgeEnt(7, qtrue));
  CHECK(system.mROFFEntList.empty());
}
static void noteLines() {
  CROFFSystem system; auto ent = entity(false);
  std::string valid = "\n\tfirst command\r\nsecond command\n";
  system.ProcessNote(&ent, &valid[0]);
  CHECK(notes.size() == 2 && notes[0].text == "first command" && notes[1].text == "second command");
  CHECK(!notes[0].client && notes[0].entity == 7);
  notes.clear();
  std::string boundary(1023, 'a'); system.ProcessNote(&ent, &boundary[0]);
  CHECK(notes.size() == 1 && notes[0].text == boundary);
  for (size_t length : {size_t(1024), size_t(4000)}) {
    notes.clear(); warnings.clear();
    std::string oversized(length, 'b'); oversized += "\nvalid continuation\n";
    system.ProcessNote(&ent, &oversized[0]);
    CHECK(notes.size() == 1 && notes[0].text == "valid continuation");
    CHECK(warnings.size() == 1);
  }
#ifndef DEDICATED
  notes.clear(); ent.mIsClient = qtrue;
  system.ProcessNote(&ent, &valid[0]);
  CHECK(notes.size() == 2 && notes[0].client && notes[0].entity == 7);
#endif
}
static void serverPlayback() {
  CROFFSystem system; CROFFSystem::CROFF roff("fixture", 1); prepare(roff);
  VectorSet(serverEntity.r.currentOrigin, 100, 200, 300);
  VectorSet(serverEntity.r.currentAngles, 10, 20, 30);
  CHECK(system.Play(7, 1, qfalse, qfalse));
  CHECK(serverEntity.r.mIsRoffing);
  auto ent = system.mROFFEntList[0];
  CHECK(system.ApplyROFF(ent, &roff));
  same(serverEntity.s.pos.trBase, 100, 200, 300);
  same(serverEntity.s.apos.trBase, 10, 20, 30);
  same(serverEntity.s.pos.trDelta, 10, 20, 30);
  CHECK(serverEntity.next_roff_time == 1100);
  svs.time = 1100;
  CHECK(!system.ApplyROFF(ent, &roff) && !serverEntity.r.mIsRoffing);
  CHECK(system.PurgeEnt(7, qfalse));
  missingEntity = true;
  auto missing = entity(false);
  CHECK(!system.ApplyROFF(&missing, &roff) && !system.ClearLerp(&missing));
  CHECK(!system.Play(7, 1, qfalse, qfalse));
}
static void dedicatedClient(bool clear) {
  CROFFSystem system;
  auto roff = new CROFFSystem::CROFF("fixture", 1); prepare(*roff);
  system.mROFFList[1] = roff;
  CHECK(system.Play(7, 1, qfalse, qtrue));
  if (clear) {
    CHECK(!system.ClearLerp(system.mROFFEntList[0]));
    delete system.mROFFEntList[0];
    system.mROFFEntList.clear();
  } else {
    system.UpdateEntities(qtrue);
  }
  CHECK(system.mROFFEntList.empty());
  CHECK(!angleCalls);
}
static void noteAllocation() {
  CROFFSystem system;
  auto roff = new CROFFSystem::CROFF("fixture", 1);
  CROFFSystem::TROFF2Header header = {};
  std::memcpy(header.mHeader, "ROFF", 4);
  header.mVersion = 2; header.mCount = 1; header.mFrameRate = 100; header.mNumNotes = 2;
  CROFFSystem::TROFF2Entry entry = {};
  entry.mStartNote = 0; entry.mNumNotes = 2;
  const char rawNotes[] = "sound sound/test.wav\0effect effects/test\0";
  std::vector<unsigned char> data(sizeof(header) + sizeof(entry) + sizeof(rawNotes));
  std::memcpy(data.data(), &header, sizeof(header));
  std::memcpy(data.data() + sizeof(header), &entry, sizeof(entry));
  std::memcpy(data.data() + sizeof(header) + sizeof(entry), rawNotes, sizeof(rawNotes));
  CHECK(system.InitROFF2(data.data(), roff));
  CHECK(roff->mNumNoteTracks == 2);
  CHECK(std::string(roff->mNoteTrackIndexes[0]) == "sound sound/test.wav");
  CHECK(std::string(roff->mNoteTrackIndexes[1]) == "effect effects/test");
  delete roff; // Must match the real InitROFF2 new[] allocation.
}
static void restartOwnership() {
  CROFFSystem system;
  for (int cycle = 0; cycle < 3; ++cycle) {
    auto roff = new CROFFSystem::CROFF("fixture", 1); prepare(*roff);
    system.mROFFList[1] = roff; system.mID = 1;
    CHECK(system.Play(7, 1, qfalse, qfalse));
    CHECK(system.Play(8, 1, qfalse, qfalse));
    CHECK(system.Play(7, 1, qfalse, qtrue));
    CHECK(liveROFFEntities == 3);
    // Teardown must release its owned state without accessing a dead VM/entity.
    missingEntity = true;
    CHECK(system.Restart());
    CHECK(system.mROFFEntList.empty() && system.mROFFList.empty());
    CHECK(system.mID == 0 && liveROFFEntities == 0);
    missingEntity = false;
    CHECK(system.Restart());
  }
}
int main(int argc, char **argv) {
  CHECK(argc == 2);
  if (!std::strcmp(argv[1], "client_apply")) clientApply();
  else if (!std::strcmp(argv[1], "client_clear")) clientClear();
  else if (!std::strcmp(argv[1], "client_translation")) clientTranslation();
  else if (!std::strcmp(argv[1], "notes")) noteLines();
  else if (!std::strcmp(argv[1], "server")) serverPlayback();
  else if (!std::strcmp(argv[1], "dedicated_apply")) dedicatedClient(false);
  else if (!std::strcmp(argv[1], "dedicated_clear")) dedicatedClient(true);
  else if (!std::strcmp(argv[1], "note_allocation")) noteAllocation();
  else if (!std::strcmp(argv[1], "restart_ownership")) restartOwnership();
  else CHECK(false);
  return 0;
}
