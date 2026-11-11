#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define FIXTURE_ASAN 1
#endif
#include "cgame/cg_local.h"

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); std::exit(1); } } while (0)
static cgameImport_t imports;
cgameImport_t *trap = &imports;
static centity_t cent;
static std::vector<std::string> effects, sounds, warnings;
static int playedEffects, playedSounds, registeredEffect = 20;
static vec3_t effectOrigin, effectAngles, soundOrigin;
static bool markUnused;
static char *unusedTail;
static size_t unusedSize;
static void FixtureMarkUnused(char *tail, size_t size) {
#ifdef FIXTURE_ASAN
  if (markUnused) {
    unusedTail = tail; unusedSize = size;
    __asan_poison_memory_region(tail, size);
  }
#else
  (void)tail; (void)size;
#endif
}
static int RegisterEffect(const char *name) { effects.push_back(name); return registeredEffect; }
static int RegisterSound(const char *name) { sounds.push_back(name); return 25; }
static void PlayEffect(int id, vec3_t origin, vec3_t angles, int volume, int radius, qboolean portal) {
  CHECK(id == 20 && volume == -1 && radius == -1 && portal == qfalse);
  VectorCopy(origin, effectOrigin); VectorCopy(angles, effectAngles); ++playedEffects;
}
static void StartSound(const vec3_t origin, int entity, int channel, sfxHandle_t sound) {
  CHECK(entity == 7 && channel == CHAN_BODY && sound == 25);
  VectorCopy(origin, soundOrigin); ++playedSounds;
}
static void Print(const char *format, ...) {
  char text[2048]; va_list args; va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args); va_end(args); warnings.push_back(text);
}
int QDECL Com_sprintf(char *dest, int size, const char *format, ...) {
  va_list args; va_start(args, format);
  const int result = std::vsnprintf(dest, size, format, args); va_end(args); return result;
}
#include "client_note.h"

static void same(const vec3_t value, float x, float y, float z) {
  CHECK(std::fabs(value[0] - x) < 0.001f);
  CHECK(std::fabs(value[1] - y) < 0.001f);
  CHECK(std::fabs(value[2] - z) < 0.001f);
}
static void reset() {
  effects.clear(); sounds.clear(); warnings.clear(); playedEffects = playedSounds = 0;
  VectorSet(cent.lerpOrigin, 100, 200, 300); VectorSet(cent.lerpAngles, 0, 90, 0);
  cent.currentState.number = 7;
  imports.FX_RegisterEffect = RegisterEffect; imports.FX_PlayEffectID = PlayEffect;
  imports.S_RegisterSound = RegisterSound; imports.S_StartSound = StartSound;
  Com_Printf = Print;
}
static void note(const std::string &text) {
  CG_ROFF_NotetrackCallback(&cent, text.c_str());
#ifdef FIXTURE_ASAN
  if (unusedTail) {
    __asan_unpoison_memory_region(unusedTail, unusedSize);
    unusedTail = nullptr; unusedSize = 0;
  }
#endif
}
static void valid() {
  reset(); note("effect effects/test");
  CHECK(effects.size() == 1 && effects[0] == "effects/test" && playedEffects == 1);
  same(effectOrigin, 100, 200, 300); same(effectAngles, 0, 90, 0);
  reset(); note("effect effects/test 1+2+3");
  CHECK(playedEffects == 1); same(effectOrigin, 102, 201, 303); same(effectAngles, 0, 90, 0);
  reset(); note("effect effects/test 1 2 3");
  CHECK(playedEffects == 1); same(effectOrigin, 102, 201, 303); same(effectAngles, 0, 90, 0);
  reset(); note("effect effects/test -1+-2+-3");
  CHECK(playedEffects == 1); same(effectOrigin, 98, 199, 297); same(effectAngles, 0, 90, 0);
  reset(); note("effect effects/test 1+2+3 0-0-0");
  CHECK(playedEffects == 1); same(effectOrigin, 101, 198, 303); same(effectAngles, 0, 0, 0);
  reset(); note("effect effects/test 0+0+0 10-20-30");
  CHECK(playedEffects == 1); same(effectOrigin, 100, 200, 300); same(effectAngles, 10, 20, 30);
  reset(); note("sound sound/test.wav");
  CHECK(sounds.size() == 1 && sounds[0] == "sound/test.wav" && playedSounds == 1);
  same(soundOrigin, 100, 200, 300);
  reset(); note("loop sound/test.wav");
  CHECK(effects.empty() && sounds.empty() && !playedEffects && !playedSounds);
  CG_ROFF_NotetrackCallback(nullptr, "effect fx"); CG_ROFF_NotetrackCallback(&cent, nullptr);
  CHECK(effects.empty() && sounds.empty());
  reset(); registeredEffect = 0; note("effect missing");
  CHECK(!playedEffects && effects.size() == 1);
}
static void boundaries() {
  reset(); std::string path(511, 's'); note("sound " + path);
  CHECK(playedSounds == 1 && sounds[0] == path);
  reset(); note(std::string(255, 't') + " argument"); CHECK(!warnings.empty());
  reset(); note("effect fx " + std::string(63, '0') + "+0+0");
  CHECK(playedEffects == 1); same(effectOrigin, 100, 200, 300);
  reset(); note("effect fx 0+0+0 " + std::string(63, '0') + "-0-0");
  CHECK(playedEffects == 1); same(effectAngles, 0, 0, 0);
}
static void oversized(const std::string &which) {
  reset();
  if (which == "oversize_type") note(std::string(256, 't') + " arg");
  else if (which == "oversize_argument") note("sound " + std::string(512, 's'));
  else if (which == "oversize_additional") note("effect fx " + std::string(512, '0'));
  else if (which == "oversize_offset") note("effect fx " + std::string(64, '0') + "+0+0");
  else if (which == "oversize_angle") note("effect fx 0+0+0 " + std::string(64, '0') + "-0-0");
  else CHECK(false);
  CHECK(!playedEffects && !playedSounds);
}
static void truncated(bool angles) {
  for (const char *tail : {"1", "1+2", "1+"}) {
    reset(); markUnused = true;
    if (angles) {
      std::string component = tail;
      for (char &ch : component) if (ch == '+') ch = '-';
      note("effect fx 1+2+3 " + component);
      CHECK(playedEffects == 1); same(effectOrigin, 102, 201, 303);
    } else {
      note(std::string("effect fx ") + tail);
      CHECK(playedEffects == 1); same(effectOrigin, 100, 200, 300);
    }
    same(effectAngles, 0, 90, 0); markUnused = false;
  }
}
int main(int argc, char **argv) {
  CHECK(argc == 2); std::string which = argv[1];
  if (which == "valid") valid();
  else if (which == "boundaries") boundaries();
  else if (which == "truncated_offsets") truncated(false);
  else if (which == "truncated_angles") truncated(true);
  else oversized(which);
  return 0;
}
