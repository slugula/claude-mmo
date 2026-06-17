#include "audio/AudioEngine.hpp"

// miniaudio pulls in windows.h on Windows; suppress the min/max macros so
// std::min works in the callback below.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Enable OGG Vorbis decoding in miniaudio via stb_vorbis. The documented dance:
// include stb_vorbis header-only BEFORE miniaudio, then its implementation
// AFTER miniaudio's implementation. Gives ma_decoder ogg + mp3 + wav support.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace audio {

namespace {
constexpr float kMusicFadeSec = 1.5f;   // fade in/out duration
constexpr float kMusicVolume  = 0.55f;  // music sits under SFX
}  // namespace

struct AudioEngine::Impl {
  ma_device device{};
  bool      deviceInited = false;

  // Streaming music: two slots for crossfade (one fading in, one fading out).
  struct Music {
    ma_decoder dec{};
    bool       valid  = false;
    float      gain   = 0.0f;   // current 0..1
    float      target = 0.0f;   // 0 = fading out, 1 = fading in
  };
  Music        slotA;
  Music        slotB;
  Music*       active     = nullptr;   // points at slotA/slotB, or null
  std::string  activePath;
};

AudioEngine::AudioEngine()  = default;
AudioEngine::~AudioEngine() { shutdown(); }

// miniaudio's callback expects ma_device*. We trampoline from there into
// the AudioEngine instance stashed in pUserData.
static void miniaudioCallback(ma_device* device, void* output,
                              const void* /*input*/, ma_uint32 frameCount) {
  auto* self = static_cast<audio::AudioEngine*>(device->pUserData);
  if (self) {
    // Static cast through the public mixer; access is restricted, so we
    // expose mixInto via a friend free function below.
    self->mixInto(static_cast<float*>(output), frameCount);
  }
}

bool AudioEngine::init() {
  impl_ = std::make_unique<Impl>();

  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format   = ma_format_f32;
  cfg.playback.channels = 1;
  cfg.sampleRate        = sampleRate_;
  cfg.dataCallback      = &miniaudioCallback;
  cfg.pUserData         = this;
  if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
    std::fprintf(stderr, "[Audio] device init failed\n");
    impl_.reset();
    return false;
  }
  impl_->deviceInited = true;
  // The device's actual sample rate may differ from our request.
  sampleRate_ = impl_->device.sampleRate;
  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    std::fprintf(stderr, "[Audio] device start failed\n");
    shutdown();
    return false;
  }

  // ---- Bake the four effects --------------------------------------------
  //
  // Same recipes as src/audio/SoundEngine.ts. startTime offsets are baked
  // into the buffer (effect at t=0 + a second voice at t=0.05 for Equip).
  // playHit: noise(0.08s, lowpass 250Hz) + sine(120->40, decay 0.08s)
  bakeNoise(bufHit_, 0.0f, 0.08f, 250.0f, /*highpass*/false, 0.35f);
  bakeSine (bufHit_, 0.0f, 120.0f, 40.0f, 0.08f, 0.45f, 0.10f);

  // playStrike: sine(700->250, decay 0.05) + noise(0.04, highpass 2000)
  bakeSine (bufStrike_, 0.0f, 700.0f, 250.0f, 0.05f, 0.28f, 0.07f);
  bakeNoise(bufStrike_, 0.0f, 0.04f, 2000.0f, /*highpass*/true, 0.13f);

  // playEquip: sine(1100->800, decay 0.10) + sine(900->650, decay 0.08) @ +0.05s
  bakeSine (bufEquip_, 0.0f,  1100.0f, 800.0f, 0.10f, 0.22f, 0.18f);
  bakeSine (bufEquip_, 0.05f,  900.0f, 650.0f, 0.08f, 0.15f, 0.15f);

  // playUnequip: single sine(650->450, decay 0.08)
  bakeSine (bufUnequip_, 0.0f, 650.0f, 450.0f, 0.08f, 0.18f, 0.16f);

  // playLevelUp: rising major arpeggio (C5-E5-G5-C6), bright + celebratory.
  bakeSine (bufLevelUp_, 0.00f,  523.25f,  523.25f, 0.12f, 0.20f, 0.10f);
  bakeSine (bufLevelUp_, 0.09f,  659.25f,  659.25f, 0.12f, 0.20f, 0.10f);
  bakeSine (bufLevelUp_, 0.18f,  783.99f,  783.99f, 0.12f, 0.20f, 0.10f);
  bakeSine (bufLevelUp_, 0.27f, 1046.50f, 1046.50f, 0.24f, 0.24f, 0.16f);

  ready_ = true;
  return true;
}

void AudioEngine::shutdown() {
  if (impl_ && impl_->deviceInited) {
    ma_device_uninit(&impl_->device);   // stops the callback first
    impl_->deviceInited = false;
    if (impl_->slotA.valid) ma_decoder_uninit(&impl_->slotA.dec);
    if (impl_->slotB.valid) ma_decoder_uninit(&impl_->slotB.dec);
  }
  impl_.reset();
  ready_ = false;
  std::lock_guard<std::mutex> lock(voicesMtx_);
  voices_.clear();
}

void AudioEngine::setMasterVolume(float v) {
  masterVolume_.store(std::clamp(v, 0.0f, 1.0f));
}

void AudioEngine::playHit()     { if (ready_) enqueue(bufHit_);     }
void AudioEngine::playStrike()  { if (ready_) enqueue(bufStrike_);  }
void AudioEngine::playEquip()   { if (ready_) enqueue(bufEquip_);   }
void AudioEngine::playUnequip() { if (ready_) enqueue(bufUnequip_); }
void AudioEngine::playLevelUp() { if (ready_) enqueue(bufLevelUp_); }

// ---- File-based SFX --------------------------------------------------------

// Decode a whole audio file (ogg/mp3/wav) to mono float at `rate`. ma_decoder
// resamples + downmixes for us.
static bool decodeFileMono(const char* path, ma_uint32 rate, std::vector<float>& out) {
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, rate);
  ma_decoder dec;
  if (ma_decoder_init_file(path, &cfg, &dec) != MA_SUCCESS) return false;
  out.clear();
  float chunk[4096];
  for (;;) {
    ma_uint64 read = 0;
    const ma_result r = ma_decoder_read_pcm_frames(&dec, chunk, 4096, &read);
    if (read > 0) out.insert(out.end(), chunk, chunk + read);
    if (r != MA_SUCCESS || read == 0) break;
  }
  ma_decoder_uninit(&dec);
  return !out.empty();
}

bool AudioEngine::loadSfx(const std::string& name, const std::vector<std::string>& files) {
  std::vector<std::vector<float>> bufs;
  for (const auto& f : files) {
    std::vector<float> b;
    if (decodeFileMono(f.c_str(), sampleRate_, b)) bufs.push_back(std::move(b));
    else std::fprintf(stderr, "[Audio] SFX decode failed: %s\n", f.c_str());
  }
  if (bufs.empty()) return false;
  sfx_[name] = std::move(bufs);
  return true;
}

void AudioEngine::playSfx(const std::string& name) {
  if (!ready_) return;
  auto it = sfx_.find(name);
  if (it == sfx_.end() || it->second.empty()) return;
  const auto& variants = it->second;
  std::size_t idx = 0;
  if (variants.size() > 1)
    idx = std::uniform_int_distribution<std::size_t>(0, variants.size() - 1)(rng_);
  enqueue(variants[idx]);   // buffers are stable after load → pointer stays valid
}

// ---- Streaming music -------------------------------------------------------

void AudioEngine::playMusic(const std::string& path) {
  if (!ready_ || path.empty() || !impl_) return;
  std::lock_guard<std::mutex> lock(musicMtx_);
  if (impl_->active && impl_->activePath == path && impl_->active->valid) {
    impl_->active->target = 1.0f;   // already playing — make sure it's audible
    return;
  }
  if (impl_->active && impl_->active->valid) impl_->active->target = 0.0f;  // fade out current

  // Pick the non-active slot to host the new track (evicting any prior outgoing).
  Impl::Music* slot = (impl_->active == &impl_->slotA) ? &impl_->slotB : &impl_->slotA;
  if (slot->valid) { ma_decoder_uninit(&slot->dec); slot->valid = false; }

  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, sampleRate_);
  if (ma_decoder_init_file(path.c_str(), &cfg, &slot->dec) != MA_SUCCESS) {
    std::fprintf(stderr, "[Audio] music load failed: %s\n", path.c_str());
    impl_->active = nullptr;
    impl_->activePath.clear();
    return;
  }
  ma_data_source_set_looping(&slot->dec, MA_TRUE);
  slot->valid  = true;
  slot->gain   = 0.0f;
  slot->target = 1.0f;
  impl_->active     = slot;
  impl_->activePath = path;
}

void AudioEngine::stopMusic() {
  if (!impl_) return;
  std::lock_guard<std::mutex> lock(musicMtx_);
  if (impl_->active) impl_->active->target = 0.0f;   // fade out; slot reclaimed on next play
  impl_->active = nullptr;
  impl_->activePath.clear();
}

void AudioEngine::enqueue(const std::vector<float>& src) {
  std::lock_guard<std::mutex> lock(voicesMtx_);
  // Cap concurrent voices so combat-spam can't snowball.
  if (voices_.size() > 16) voices_.erase(voices_.begin());
  voices_.push_back({ &src, 0 });
}

void AudioEngine::mixInto(float* output, unsigned int frameCount) {
  std::fill(output, output + frameCount, 0.0f);
  const float gain = masterVolume_.load();

  // ---- One-shot voices (synth + SFX) -------------------------------------
  {
    std::lock_guard<std::mutex> lock(voicesMtx_);
    for (auto it = voices_.begin(); it != voices_.end();) {
      auto& v = *it;
      const auto& buf = *v.buf;
      const std::size_t avail = buf.size() - v.pos;
      const std::size_t n = std::min(static_cast<std::size_t>(frameCount), avail);
      for (std::size_t i = 0; i < n; ++i) output[i] += buf[v.pos + i] * gain;
      v.pos += n;
      if (v.pos >= buf.size()) it = voices_.erase(it);
      else                     ++it;
    }
  }

  // ---- Streaming music (looping, crossfaded) -----------------------------
  if (impl_) {
    std::lock_guard<std::mutex> mlock(musicMtx_);
    const float step = (sampleRate_ > 0) ? 1.0f / (kMusicFadeSec * sampleRate_) : 1.0f;
    static thread_local std::vector<float> scratch;
    if (scratch.size() < frameCount) scratch.resize(frameCount);
    for (Impl::Music* m : { &impl_->slotA, &impl_->slotB }) {
      if (!m->valid) continue;
      if (m->gain <= 0.0001f && m->target <= 0.0f) continue;  // silent, fade done
      // Fill the whole block, looping by seeking back to the start on EOF (don't
      // rely solely on the data-source loop flag, which some backends ignore).
      ma_uint64 total = 0;
      for (int guard = 0; total < frameCount && guard < 4; ++guard) {
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&m->dec, scratch.data() + total, frameCount - total, &read);
        total += read;
        if (total < frameCount) {            // hit end of stream → rewind
          if (ma_decoder_seek_to_pcm_frame(&m->dec, 0) != MA_SUCCESS) break;
        }
      }
      const ma_uint64 read = total;
      for (ma_uint64 i = 0; i < read; ++i) {
        if      (m->gain < m->target) m->gain = std::min(m->target, m->gain + step);
        else if (m->gain > m->target) m->gain = std::max(m->target, m->gain - step);
        output[i] += scratch[i] * m->gain * gain * kMusicVolume;
      }
    }
  }
}

// ---- Synth helpers (identical to SoundEngine.ts semantics) ---------------
//
// `out` is grown as needed so multiple bake* calls into the same buffer
// stack additively at the right time offsets. The buffer is mono.

void AudioEngine::bakeSine(std::vector<float>& out,
                           float startTime,
                           float freqStart, float freqEnd, float freqDecayDur,
                           float gainStart, float gainDecayDur) const {
  // Web Audio's exponentialRampToValueAtTime: y(t) = y0 * (y1/y0)^((t-t0)/dur).
  // The osc emits until startTime + gainDecayDur + 0.01s (matching TS).
  const float endTime = startTime + gainDecayDur + 0.01f;
  const std::size_t i0 = static_cast<std::size_t>(startTime * sampleRate_);
  const std::size_t i1 = static_cast<std::size_t>(endTime   * sampleRate_);
  if (out.size() < i1) out.resize(i1, 0.0f);

  // Phase accumulator for the swept-frequency sine.
  double phase = 0.0;
  for (std::size_t i = i0; i < i1; ++i) {
    const float t       = static_cast<float>(i - i0) / sampleRate_;
    const float fT      = std::min(t / freqDecayDur, 1.0f);
    const float freq    = freqStart * std::pow(freqEnd / freqStart, fT);
    const float gT      = std::min(t / gainDecayDur, 1.0f);
    const float gain    = gainStart * std::pow(0.001f / gainStart, gT);
    phase += 2.0 * 3.14159265358979 * freq / sampleRate_;
    out[i] += static_cast<float>(std::sin(phase)) * gain;
  }
}

void AudioEngine::bakeNoise(std::vector<float>& out,
                            float startTime, float duration,
                            float filterFreq, bool highpass,
                            float gainStart) const {
  const std::size_t i0  = static_cast<std::size_t>(startTime * sampleRate_);
  const std::size_t len = static_cast<std::size_t>(duration  * sampleRate_);
  if (out.size() < i0 + len + 1) out.resize(i0 + len + 1, 0.0f);

  std::mt19937 rng(0xC0FFEE);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

  // Very cheap one-pole IIR — accurate enough for "warmer / colder noise".
  // RC = 1 / (2 pi fc); pole alpha = dt / (RC + dt) for lowpass.
  const float dt    = 1.0f / sampleRate_;
  const float rc    = 1.0f / (2.0f * 3.14159265f * filterFreq);
  const float alpha = dt / (rc + dt);

  float lp = 0.0f;
  for (std::size_t i = 0; i < len; ++i) {
    const float n   = dist(rng);
    lp             += alpha * (n - lp);
    const float v   = highpass ? (n - lp) : lp;
    const float gT  = static_cast<float>(i) / static_cast<float>(len);
    const float g   = gainStart * std::pow(0.001f / gainStart, gT);
    out[i0 + i]    += v * g;
  }
}

}  // namespace audio
