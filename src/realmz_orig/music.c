#include "prototypes.h"
#include "variables.h"
#include "../MusicManager.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
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

// If the user has placed a "Realmz Music" folder next to the running AppImage
// file, returns the directory prefix (with a trailing slash) that contains it;
// otherwise returns NULL. The AppImage runtime sets $APPIMAGE to the full path of
// the .AppImage file, and its directory is where a user can drop extra music.
// (SDL_GetBasePath() points inside the mounted squashfs, so it can't see files
// sitting beside the AppImage.) Only meaningful for the Linux AppImage build;
// on other platforms $APPIMAGE is unset and this returns NULL.
static const char* external_music_dir(void) {
  static char prefix[MUSIC_PATH_MAX];

  const char* appimage = getenv("APPIMAGE");
  if (!appimage || !appimage[0]) return NULL;

  const char* slash = strrchr(appimage, '/');
  if (!slash) return NULL;

  size_t dirlen = (size_t)(slash - appimage) + 1; // include trailing slash
  if (dirlen >= sizeof(prefix)) return NULL;
  memcpy(prefix, appimage, dirlen);
  prefix[dirlen] = '\0';

  char probe[MUSIC_PATH_MAX];
  snprintf(probe, sizeof(probe), "%sRealmz Music", prefix);
  SDL_PathInfo info;
  if (SDL_GetPathInfo(probe, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
    return prefix;
  }
  return NULL;
}

// Adds every file under "<base>Realmz Music" whose name begins with the scan
// prefix to 'state'. 'base' is a directory prefix ending in a slash; a NULL or
// empty base, or a missing directory, is simply a no-op.
static void scan_music_dir(const char* base, MusicScanState* state) {
  if (!base || !base[0]) return;
  char dir[MUSIC_PATH_MAX];
  snprintf(dir, sizeof(dir), "%sRealmz Music", base);
  SDL_EnumerateDirectory(dir, scan_music_callback, state);
}

// Scans for music files beginning with 'name', picks one at random, and plays it.
// Two locations are searched and merged into a single candidate pool: the music
// bundled next to the executable (inside the AppImage / macOS bundle / beside
// Realmz.exe) and, for the Linux AppImage, a user-supplied "Realmz Music" folder
// placed next to the .AppImage file. Overlapping filenames across the two folders
// are all kept as separate candidates, so the external files supplement rather
// than replace the bundled tracks. If a track from the same group is already
// playing, does nothing. Also handles volume.
void music_play_by_name(const char* name) {
  if (!name) return;

  MusicScanState state;
  state.count = 0;
  state.prefix = name;

  scan_music_dir(SDL_GetBasePath(), &state);
  scan_music_dir(external_music_dir(), &state);

  if (state.count == 0) return;

  MusicManager_SetVolume(musicvolume);
  int idx = (int)(SDL_rand(state.count));
  // The group identity is just the playlist name, so re-triggering the same
  // playlist keeps the current track regardless of which folder it came from.
  MusicManager_PlayIfDifferentGroup(state.paths[idx], name);
}

/******************************* music ************************/
void music(int16_t playlist) {
  if (nomusic) return;
  const char* name = track_name_for_playlist(playlist);
  if (!name) return;
  music_play_by_name(name);
}
