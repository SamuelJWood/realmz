#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void MusicManager_Play(const char* path);
void MusicManager_PlayIfDifferent(const char* path);
// Play 'path' unless the currently playing track's path begins with 'group_prefix'.
// Used to avoid switching tracks when already playing something from the same group.
void MusicManager_PlayIfDifferentGroup(const char* path, const char* group_prefix);
void MusicManager_Stop(void);
void MusicManager_SetVolume(int level); // 0-7 scale

#ifdef __cplusplus
}
#endif
