#include "prototypes.h"
#include "variables.h"
#include "../MusicManager.h"

#include <SDL3/SDL.h>
#include <stdio.h>

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

/******************************* music ************************/
void music(int16_t playlist) {
  if (nomusic) return;
  const char* name = track_name_for_playlist(playlist);
  if (!name) return;

  MusicManager_SetVolume(musicvolume);

  const char* base = SDL_GetBasePath();
  char path[1024];
  snprintf(path, sizeof(path), "%sRealmz Music/%s", base ? base : "", name);
  MusicManager_PlayIfDifferent(path);
}
