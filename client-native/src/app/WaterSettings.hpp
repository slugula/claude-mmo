#pragma once
// App-layer glue between persisted AppSettings (key=value file) and the
// runtime world::WaterUniforms struct. Kept out of the world layer so
// WaterRenderer has no dependency on the settings file format.

#include "app/Settings.hpp"
#include "world/WaterRenderer.hpp"

// Copy persisted water settings → runtime uniforms.
inline void applyWaterSettings(const AppSettings& s, world::WaterUniforms& u) {
  u.shallowColor      = { s.waterShallowR, s.waterShallowG, s.waterShallowB };
  u.deepColor         = { s.waterDeepR,    s.waterDeepG,    s.waterDeepB };
  u.foamColor         = { s.waterFoamR,    s.waterFoamG,    s.waterFoamB };
  u.waveSpeed         = s.waterWaveSpeed;
  u.waveHeight        = s.waterWaveHeight;
  u.waveScale         = s.waterWaveScale;
  u.normalStrength    = s.waterNormalStrength;
  u.reflectStrength   = s.waterClarity;
  u.causticIntensity  = s.waterCausticIntensity;
  u.causticScale      = s.waterCausticScale;
  u.causticSpeed      = s.waterCausticSpeed;
  u.foamWidth         = s.waterFoamWidth;
  u.foamSpeed         = s.waterFoamSpeed;
  u.foamScale         = s.waterFoamScale;
  u.parallaxDepth     = s.waterParallaxDepth;
  u.waterOffset       = s.waterOffset;
  u.refractionStrength= s.waterRefraction;
  u.depthFade         = s.waterDepthFade;
  u.shoreDepth        = s.waterShoreDepth;
  u.foamContactWidth  = s.waterFoamContact;
  u.specularStrength  = s.waterSpecular;
  u.causticMapPath    = s.waterCausticMap;
}

// Copy runtime uniforms → persisted water settings.
inline void storeWaterSettings(const world::WaterUniforms& u, AppSettings& s) {
  s.waterShallowR = u.shallowColor.r; s.waterShallowG = u.shallowColor.g; s.waterShallowB = u.shallowColor.b;
  s.waterDeepR    = u.deepColor.r;    s.waterDeepG    = u.deepColor.g;    s.waterDeepB    = u.deepColor.b;
  s.waterFoamR    = u.foamColor.r;    s.waterFoamG    = u.foamColor.g;    s.waterFoamB    = u.foamColor.b;
  s.waterWaveSpeed        = u.waveSpeed;
  s.waterWaveHeight       = u.waveHeight;
  s.waterWaveScale        = u.waveScale;
  s.waterNormalStrength   = u.normalStrength;
  s.waterClarity          = u.reflectStrength;
  s.waterCausticIntensity = u.causticIntensity;
  s.waterCausticScale     = u.causticScale;
  s.waterCausticSpeed     = u.causticSpeed;
  s.waterFoamWidth        = u.foamWidth;
  s.waterFoamSpeed        = u.foamSpeed;
  s.waterFoamScale        = u.foamScale;
  s.waterParallaxDepth    = u.parallaxDepth;
  s.waterOffset           = u.waterOffset;
  s.waterRefraction       = u.refractionStrength;
  s.waterDepthFade        = u.depthFade;
  s.waterShoreDepth       = u.shoreDepth;
  s.waterFoamContact      = u.foamContactWidth;
  s.waterSpecular         = u.specularStrength;
  s.waterCausticMap       = u.causticMapPath;
}
