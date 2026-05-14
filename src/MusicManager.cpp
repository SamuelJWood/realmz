#include "MusicManager.h"

#include <xmp.h>
#include <SDL3/SDL.h>
#include <phosg/Strings.hh>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

static phosg::PrefixedLogger mm_log("[MusicManager] ");

static constexpr int MUSIC_SAMPLE_RATE = 48000;

// Volume mapping: game uses 0-7, XMP uses 0-200 (100 = normal)
static int xmp_volume_for_game(int level) {
  return level * 200 / 7;
}

class MusicManager {
  xmp_context ctx = nullptr;
  SDL_AudioDeviceID device_id = 0;
  SDL_AudioStream* stream = nullptr;
  std::thread decode_thread;
  std::mutex mtx;
  std::condition_variable cv;
  std::string pending_path;  // guarded by mtx; set by play(), consumed by thread
  std::string current_path;  // guarded by mtx; tracks what's loaded
  std::atomic<bool> running{false};
  std::atomic<int> volume_pct{100};

  void decode_loop() {
    std::string loaded_path;
    bool loaded = false;

    while (this->running) {
      std::string next_path;
      {
        std::unique_lock lk(this->mtx);
        this->cv.wait_for(lk, std::chrono::milliseconds(50), [&] {
          return !this->pending_path.empty() || !this->running;
        });
        if (!this->pending_path.empty()) {
          next_path = std::move(this->pending_path);
        }
      }

      if (!next_path.empty()) {
        if (loaded) {
          xmp_end_player(this->ctx);
          xmp_release_module(this->ctx);
          loaded = false;
        }
        SDL_ClearAudioStream(this->stream);

        if (xmp_load_module(this->ctx, next_path.c_str()) == 0) {
          xmp_start_player(this->ctx, MUSIC_SAMPLE_RATE, 0);
          xmp_set_player(this->ctx, XMP_PLAYER_VOLUME, this->volume_pct.load());
          loaded = true;
          loaded_path = next_path;
          mm_log.info_f("Playing: {}", next_path);
        } else {
          mm_log.warning_f("Failed to load module: {}", next_path);
          loaded_path.clear();
        }

        {
          std::lock_guard lk(this->mtx);
          this->current_path = loaded ? loaded_path : "";
        }
      }

      if (!this->running) break;
      if (!loaded) continue;

      // Update volume in case it changed
      xmp_set_player(this->ctx, XMP_PLAYER_VOLUME, this->volume_pct.load());

      // Keep stream buffer filled (target ~200ms ahead)
      const int target_bytes = MUSIC_SAMPLE_RATE * 4 / 5; // 200ms of stereo 16-bit
      while (this->running && SDL_GetAudioStreamAvailable(this->stream) < target_bytes) {
        struct xmp_frame_info fi;
        int ret = xmp_play_frame(this->ctx);
        if (ret != 0) {
          // End of module — loop back
          xmp_restart_module(this->ctx);
          continue;
        }
        xmp_get_frame_info(this->ctx, &fi);
        if (fi.buffer_size > 0) {
          SDL_PutAudioStreamData(this->stream, fi.buffer, fi.buffer_size);
        } else {
          break;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (loaded) {
      xmp_end_player(this->ctx);
      xmp_release_module(this->ctx);
    }
  }

public:
  static MusicManager& instance() {
    static MusicManager inst;
    return inst;
  }

  MusicManager() {
    this->ctx = xmp_create_context();

    SDL_Init(SDL_INIT_AUDIO);
    this->device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (this->device_id == 0) {
      mm_log.warning_f("Failed to open audio device: {}", SDL_GetError());
      return;
    }

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 2;
    spec.freq = MUSIC_SAMPLE_RATE;
    this->stream = SDL_CreateAudioStream(&spec, &spec);
    if (!this->stream) {
      mm_log.warning_f("Failed to create audio stream: {}", SDL_GetError());
      return;
    }
    SDL_BindAudioStream(this->device_id, this->stream);

    this->running = true;
    this->decode_thread = std::thread([this] { this->decode_loop(); });
  }

  ~MusicManager() {
    this->running = false;
    this->cv.notify_all();
    if (this->decode_thread.joinable()) {
      this->decode_thread.join();
    }
    if (this->stream) {
      SDL_DestroyAudioStream(this->stream);
    }
    if (this->device_id) {
      SDL_CloseAudioDevice(this->device_id);
    }
    if (this->ctx) {
      xmp_free_context(this->ctx);
    }
  }

  void play(const char* path) {
    if (!this->stream) return;
    std::lock_guard lk(this->mtx);
    this->pending_path = path ? path : "";
    this->cv.notify_all();
  }

  void play_if_different(const char* path) {
    if (!this->stream) return;
    std::lock_guard lk(this->mtx);
    const std::string new_path = path ? path : "";
    // Skip if this path is already queued or currently playing
    if (new_path == this->pending_path || new_path == this->current_path) return;
    this->pending_path = new_path;
    this->cv.notify_all();
  }

  void stop() {
    this->play(nullptr);
  }

  void set_volume(int level) {
    this->volume_pct = xmp_volume_for_game(level);
    // The decode loop applies the new volume on next iteration
  }
};

extern "C" {

void MusicManager_Play(const char* path) {
  MusicManager::instance().play(path);
}

void MusicManager_PlayIfDifferent(const char* path) {
  MusicManager::instance().play_if_different(path);
}

void MusicManager_Stop(void) {
  MusicManager::instance().stop();
}

void MusicManager_SetVolume(int level) {
  MusicManager::instance().set_volume(level);
}

} // extern "C"
