#include "audio/AudioEngine.hpp"

// miniaudio pulls in windows.h on Windows; suppress the min/max macros so
// std::min works in the callback below.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace audio {

struct AudioEngine::Impl {
  ma_device device{};
  bool      deviceInited = false;
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

  ready_ = true;
  return true;
}

void AudioEngine::shutdown() {
  if (impl_ && impl_->deviceInited) {
    ma_device_uninit(&impl_->device);
    impl_->deviceInited = false;
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

void AudioEngine::enqueue(const std::vector<float>& src) {
  std::lock_guard<std::mutex> lock(voicesMtx_);
  // Cap concurrent voices so combat-spam can't snowball.
  if (voices_.size() > 16) voices_.erase(voices_.begin());
  voices_.push_back({ &src, 0 });
}

void AudioEngine::mixInto(float* output, unsigned int frameCount) {
  std::fill(output, output + frameCount, 0.0f);
  std::lock_guard<std::mutex> lock(voicesMtx_);
  const float gain = masterVolume_.load();
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
