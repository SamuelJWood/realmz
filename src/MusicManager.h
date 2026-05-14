#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void MusicManager_Play(const char* path);
void MusicManager_PlayIfDifferent(const char* path);
void MusicManager_Stop(void);
void MusicManager_SetVolume(int level); // 0-7 scale

#ifdef __cplusplus
}
#endif
