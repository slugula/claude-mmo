#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace audio {

// Port of src/audio/SoundEngine.ts to native C++ via miniaudio. The four
// sounds (hit / strike / equip / unequip) are pre-baked into PCM buffers
// at init time using the same sine + filtered-noise recipes as the TS
// version; runtime "play" appends a voice that the audio callback mixes.
//
// Mono float32 at the device sample rate. The callback runs on miniaudio's
// audio thread — voice mutation is mutex-guarded.
class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();

  AudioEngine(const AudioEngine&)            = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  // Starts the miniaudio device + bakes the effect buffers. Returns false
  // if the device can't be opened — caller should treat audio as optional.
  bool init();
  void shutdown();
  bool isReady() const { return ready_; }

  void playHit();
  void playStrike();
  void playEquip();
  void playUnequip();
  void playLevelUp();

  // ---- File-based SFX ----------------------------------------------------
  // Register a named SFX with one or more variant files (absolute paths). When
  // a name has multiple variants, playSfx() picks one at random. Decoded to the
  // device format (mono, device rate) once at load. Safe to call before the
  // device is ready; decoding is independent of playback.
  bool loadSfx(const std::string& name, const std::vector<std::string>& files);
  void playSfx(const std::string& name);

  // ---- Streaming music ---------------------------------------------------
  // Play a looping music track (absolute .ogg/.mp3/.wav path), crossfading from
  // whatever is currently playing. Passing the path that's already active is a
  // no-op (keeps it playing). stopMusic() fades the current track out.
  void playMusic(const std::string& path);
  void stopMusic();

  // Global gain multiplier applied in the audio callback.
  void  setMasterVolume(float v);
  float masterVolume() const { return masterVolume_.load(); }

private:
  // PCM buffer + active play head. The vector of buffers is immutable after
  // init; only `voices_` mutates at runtime.
  struct Voice {
    const std::vector<float>* buf  = nullptr;
    std::size_t               pos  = 0;
  };

  // Synth helpers — write into `out`. `out` is grown as needed; sampleRate
  // is fixed at init time.
  void bakeSine(std::vector<float>& out,
                float startTime, float freqStart, float freqEnd,
                float freqDecayDur, float gainStart, float gainDecayDur) const;
  void bakeNoise(std::vector<float>& out,
                 float startTime, float duration,
                 float filterFreq, bool highpass,
                 float gainStart) const;

public:
  // Called from the audio thread. Public so the C-style miniaudio
  // callback in the .cpp can dispatch into it without needing friendship.
  void        mixInto(float* output, unsigned int frameCount);
private:

  void enqueue(const std::vector<float>& src);

  // Pre-baked buffers (one per effect).
  std::vector<float> bufHit_;
  std::vector<float> bufStrike_;
  std::vector<float> bufEquip_;
  std::vector<float> bufUnequip_;
  std::vector<float> bufLevelUp_;

  std::mutex          voicesMtx_;
  std::vector<Voice>  voices_;

  // File-based SFX: name -> decoded variant buffers (mono, device rate). Loaded
  // once; stable thereafter so Voice pointers into them stay valid.
  std::unordered_map<std::string, std::vector<std::vector<float>>> sfx_;
  std::mt19937 rng_{ std::random_device{}() };   // variant pick (main thread)

  // Streaming music state (ma_decoder lives in Impl). musicMtx_ guards the music
  // tracks shared with the audio callback.
  std::mutex musicMtx_;

  std::atomic<float>  masterVolume_{0.5f};
  bool                ready_      = false;
  unsigned int        sampleRate_ = 44100;

  // Opaque miniaudio handles — pointer so we don't pull miniaudio.h into
  // the header.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace audio
