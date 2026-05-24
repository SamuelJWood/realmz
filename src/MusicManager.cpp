#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "MusicManager.h"

#include <xmp.h>
#include <SDL3/SDL.h>
#include <phosg/Strings.hh>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static phosg::PrefixedLogger mm_log("[MusicManager] ");

static constexpr int MUSIC_SAMPLE_RATE = 48000;

// Volume mapping: game uses 0-7, XMP uses 0-200 (100 = normal)
static int xmp_volume_for_game(int level) {
  return level * 200 / 7;
}

static bool is_mp3_path(const std::string& path) {
  if (path.size() < 4) return false;
  std::string ext = path.substr(path.size() - 4);
  for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
  return ext == ".mp3";
}

class MusicManager {
  xmp_context ctx = nullptr;
  SDL_AudioDeviceID device_id = 0;
  SDL_AudioStream* stream = nullptr;
  std::thread decode_thread;
  std::mutex mtx;
  std::condition_variable cv;
  std::string pending_path;   // guarded by mtx
  std::string current_path;   // guarded by mtx
  std::atomic<bool> running{false};
  std::atomic<int> volume_pct{100};  // 0-200 scale
  std::string current_group;         // guarded by mtx

  // MP3 decoder state (accessed only from decode thread)
  std::vector<uint8_t> mp3_data;
  size_t mp3_pos{0};
  mp3dec_t mp3_dec{};

  void decode_loop() {
    std::string loaded_path;
    bool loaded = false;
    bool playing_mp3 = false;

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
        // Unload previous track
        if (loaded) {
          if (!playing_mp3) {
            xmp_end_player(this->ctx);
            xmp_release_module(this->ctx);
          }
          this->mp3_data.clear();
          loaded = false;
        }
        SDL_ClearAudioStream(this->stream);

        playing_mp3 = is_mp3_path(next_path);

        if (playing_mp3) {
          // Read the file into memory
          FILE* f = fopen(next_path.c_str(), "rb");
          if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0) {
              this->mp3_data.resize((size_t)sz);
              fread(this->mp3_data.data(), 1, (size_t)sz, f);
            }
            fclose(f);
          }

          if (!this->mp3_data.empty()) {
            // Probe the first frame to determine sample rate and channels
            mp3dec_t probe;
            mp3dec_init(&probe);
            mp3d_sample_t probe_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
            mp3dec_frame_info_t info;
            mp3dec_decode_frame(&probe, this->mp3_data.data(), (int)this->mp3_data.size(), probe_pcm, &info);

            if (info.frame_bytes > 0 && info.hz > 0) {
              // Configure stream input to match MP3 format
              SDL_AudioSpec mp3_spec;
              mp3_spec.format = SDL_AUDIO_S16LE;
              mp3_spec.channels = info.channels;
              mp3_spec.freq = info.hz;
              SDL_SetAudioStreamFormat(this->stream, &mp3_spec, nullptr);

              // Initialize the actual playback decoder from the beginning
              this->mp3_pos = 0;
              mp3dec_init(&this->mp3_dec);
              loaded = true;
              loaded_path = next_path;
              mm_log.info_f("Playing MP3: {} ({}Hz, {}ch)", next_path, info.hz, info.channels);
            } else {
              mm_log.warning_f("Failed to probe MP3: {}", next_path);
              this->mp3_data.clear();
            }
          } else {
            mm_log.warning_f("Failed to read MP3 file: {}", next_path);
          }

        } else {
          // XMP module track — reset stream input format to standard XMP output
          SDL_AudioSpec xmp_spec;
          xmp_spec.format = SDL_AUDIO_S16LE;
          xmp_spec.channels = 2;
          xmp_spec.freq = MUSIC_SAMPLE_RATE;
          SDL_SetAudioStreamFormat(this->stream, &xmp_spec, nullptr);

          if (xmp_load_module(this->ctx, next_path.c_str()) == 0) {
            xmp_start_player(this->ctx, MUSIC_SAMPLE_RATE, 0);
            xmp_set_player(this->ctx, XMP_PLAYER_VOLUME, this->volume_pct.load());
            loaded = true;
            loaded_path = next_path;
            mm_log.info_f("Playing module: {}", next_path);
          } else {
            mm_log.warning_f("Failed to load module: {}", next_path);
          }
        }

        {
          std::lock_guard lk(this->mtx);
          this->current_path = loaded ? loaded_path : "";
        }
      }

      if (!this->running) break;
      if (!loaded) continue;

      // Keep stream buffer filled (~200ms ahead)
      const int target_bytes = MUSIC_SAMPLE_RATE * 4 / 5;

      if (playing_mp3) {
        while (this->running && SDL_GetAudioStreamAvailable(this->stream) < target_bytes) {
          if (this->mp3_pos >= this->mp3_data.size()) {
            // Loop: reset to beginning
            this->mp3_pos = 0;
            mp3dec_init(&this->mp3_dec);
          }

          mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
          mp3dec_frame_info_t frame_info;
          int samples = mp3dec_decode_frame(
              &this->mp3_dec,
              this->mp3_data.data() + this->mp3_pos,
              (int)(this->mp3_data.size() - this->mp3_pos),
              pcm,
              &frame_info);

          if (frame_info.frame_bytes > 0) {
            this->mp3_pos += (size_t)frame_info.frame_bytes;
          }

          if (samples > 0) {
            // Apply volume scaling
            int vol = this->volume_pct.load();
            if (vol != 200) {
              int n = samples * frame_info.channels;
              for (int i = 0; i < n; i++) {
                pcm[i] = (mp3d_sample_t)((int)pcm[i] * vol / 200);
              }
            }
            SDL_PutAudioStreamData(this->stream, pcm,
                samples * frame_info.channels * (int)sizeof(mp3d_sample_t));
          } else if (frame_info.frame_bytes == 0) {
            // No decodable frame found — end of stream, loop
            this->mp3_pos = 0;
            mp3dec_init(&this->mp3_dec);
          }
        }

      } else {
        // XMP decode
        xmp_set_player(this->ctx, XMP_PLAYER_VOLUME, this->volume_pct.load());
        while (this->running && SDL_GetAudioStreamAvailable(this->stream) < target_bytes) {
          struct xmp_frame_info fi;
          int ret = xmp_play_frame(this->ctx);
          if (ret != 0) {
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
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (loaded && !playing_mp3) {
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
    this->current_group = "";
    this->pending_path = path ? path : "";
    this->cv.notify_all();
  }

  void play_if_different(const char* path) {
    if (!this->stream) return;
    std::lock_guard lk(this->mtx);
    const std::string new_path = path ? path : "";
    if (new_path == this->pending_path || new_path == this->current_path) return;
    this->pending_path = new_path;
    this->cv.notify_all();
  }

  void play_if_different_group(const char* path, const char* group_prefix) {
    if (!this->stream) return;
    std::lock_guard lk(this->mtx);
    const std::string prefix = group_prefix ? group_prefix : "";
    if (!prefix.empty() && this->current_group == prefix) return;
    this->current_group = prefix;
    this->pending_path = path ? path : "";
    this->cv.notify_all();
  }

  void stop() {
    this->play(nullptr);
  }

  void set_volume(int level) {
    this->volume_pct = xmp_volume_for_game(level);
  }
};

extern "C" {

void MusicManager_Play(const char* path) {
  MusicManager::instance().play(path);
}

void MusicManager_PlayIfDifferent(const char* path) {
  MusicManager::instance().play_if_different(path);
}

void MusicManager_PlayIfDifferentGroup(const char* path, const char* group_prefix) {
  MusicManager::instance().play_if_different_group(path, group_prefix);
}

void MusicManager_Stop(void) {
  MusicManager::instance().stop();
}

void MusicManager_SetVolume(int level) {
  MusicManager::instance().set_volume(level);
}

} // extern "C"
