#include "prototypes.h"
#include "variables.h"
#include "../MusicManager.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

extern char nomusic;
extern short musicvolume;

static const char* track_name_for_playlist(int16_t playlist) {
  switch (playlist) {
    case 5:  return "Create Music";
    case 6:  return "Items Music";
    case 7:  return "Treasure Music";
    case 8:  return "Shop Music";
    case 9:  return "Camp Music";
    case 10: return "Temple Music";
    case 11: return "Battle Music";
    default: return NULL;
  }
}

// --- Directory scanning for prefix-matched music files ---

#define MUSIC_SCAN_MAX 64
#define MUSIC_PATH_MAX 1024

typedef struct {
  char paths[MUSIC_SCAN_MAX][MUSIC_PATH_MAX];
  int count;
  const char* prefix;
} MusicScanState;

static SDL_EnumerationResult scan_music_callback(void* userdata, const char* dirname, const char* fname) {
  MusicScanState* state = (MusicScanState*)userdata;
  if (state->count >= MUSIC_SCAN_MAX) return SDL_ENUM_CONTINUE;
  if (fname[0] == '.') return SDL_ENUM_CONTINUE;
  if (strncmp(fname, state->prefix, strlen(state->prefix)) == 0) {
    snprintf(state->paths[state->count], MUSIC_PATH_MAX, "%s/%s", dirname, fname);
    state->count++;
  }
  return SDL_ENUM_CONTINUE;
}

// Scans the "Realmz Music" directory for files beginning with 'name', picks one at
// random, and plays it. If a file from the same prefix group is already playing,
// does nothing. Also handles volume.
void music_play_by_name(const char* name) {
  if (!name) return;
  const char* base = SDL_GetBasePath();
  const char* base_str = base ? base : "";

  char dir[MUSIC_PATH_MAX];
  snprintf(dir, sizeof(dir), "%sRealmz Music", base_str);

  char group_prefix[MUSIC_PATH_MAX];
  snprintf(group_prefix, sizeof(group_prefix), "%sRealmz Music/%s", base_str, name);

  MusicScanState state;
  state.count = 0;
  state.prefix = name;
  SDL_EnumerateDirectory(dir, scan_music_callback, &state);

  if (state.count == 0) return;

  MusicManager_SetVolume(musicvolume);
  int idx = (int)(SDL_rand(state.count));
  MusicManager_PlayIfDifferentGroup(state.paths[idx], group_prefix);
}

/******************************* music ************************/
void music(int16_t playlist) {
  if (nomusic) return;
  const char* name = track_name_for_playlist(playlist);
  if (!name) return;
  music_play_by_name(name);
}
