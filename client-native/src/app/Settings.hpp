#pragma once
#include <cstdio>
#include <filesystem>

// Persisted display/rendering settings. Saved as a simple key=value text
// file ("settings.cfg") next to the executable. Both the client and the
// level editor load/save the same file and the same subset of keys.
struct AppSettings {
  // Fog
  bool      fogEnabled  = false;
  float     fogDensity  = 0.015f;
  float     fogStart    = 5.0f;
  float     fogR        = 0.58f;
  float     fogG        = 0.67f;
  float     fogB        = 0.78f;
  // AO
  bool      aoEnabled   = true;
  float     aoStrength  = 0.50f;
  // Lighting
  bool      lightingEnabled = true;
  float     sunYawDeg   = 200.0f;
  float     sunPitchDeg = 58.0f;
  float     ambient     = 0.45f;
  float     diffuse     = 0.55f;
  // Shadows
  bool      shadowsEnabled  = true;
  float     shadowDarkness  = 0.55f;
  float     shadowBias      = 0.0025f;
  float     shadowHalfExtent = 40.0f;
  // Palette
  bool      palette     = true;
  int       paletteHues = 64;
  int       paletteSats = 16;
  int       paletteLums = 48;
  // Outline (client only, ignored by editor)
  float     outlineRadius    = 3.0f;
  float     outlineDepthBias = 0.002f;
  float     outlineColorR    = 0.0f;
  float     outlineColorG    = 0.9f;
  float     outlineColorB    = 0.9f;
  float     outlineColorA    = 0.95f;
  float     hoverTileR       = 1.0f;
  float     hoverTileG       = 0.85f;
  float     hoverTileB       = 0.10f;
  float     hoverTileA       = 1.0f;
};

inline bool saveSettings(const AppSettings& s, const std::filesystem::path& path) {
  FILE* f = std::fopen(path.string().c_str(), "w");
  if (!f) return false;
  std::fprintf(f,
    "fogEnabled=%d fogDensity=%f fogStart=%f fogR=%f fogG=%f fogB=%f\n"
    "aoEnabled=%d aoStrength=%f\n"
    "lightingEnabled=%d sunYawDeg=%f sunPitchDeg=%f ambient=%f diffuse=%f\n"
    "shadowsEnabled=%d shadowDarkness=%f shadowBias=%f shadowHalfExtent=%f\n"
    "palette=%d paletteHues=%d paletteSats=%d paletteLums=%d\n"
    "outlineRadius=%f outlineDepthBias=%f\n"
    "outlineColorR=%f outlineColorG=%f outlineColorB=%f outlineColorA=%f\n"
    "hoverTileR=%f hoverTileG=%f hoverTileB=%f hoverTileA=%f\n",
    (int)s.fogEnabled, s.fogDensity, s.fogStart, s.fogR, s.fogG, s.fogB,
    (int)s.aoEnabled, s.aoStrength,
    (int)s.lightingEnabled, s.sunYawDeg, s.sunPitchDeg, s.ambient, s.diffuse,
    (int)s.shadowsEnabled, s.shadowDarkness, s.shadowBias, s.shadowHalfExtent,
    (int)s.palette, s.paletteHues, s.paletteSats, s.paletteLums,
    s.outlineRadius, s.outlineDepthBias,
    s.outlineColorR, s.outlineColorG, s.outlineColorB, s.outlineColorA,
    s.hoverTileR, s.hoverTileG, s.hoverTileB, s.hoverTileA);
  std::fclose(f);
  return true;
}

inline bool loadSettings(AppSettings& s, const std::filesystem::path& path) {
  FILE* f = std::fopen(path.string().c_str(), "r");
  if (!f) return false;
  int fogE=0, aoE=0, litE=0, shadE=0, palE=0;
  std::fscanf(f,
    "fogEnabled=%d fogDensity=%f fogStart=%f fogR=%f fogG=%f fogB=%f\n"
    "aoEnabled=%d aoStrength=%f\n"
    "lightingEnabled=%d sunYawDeg=%f sunPitchDeg=%f ambient=%f diffuse=%f\n"
    "shadowsEnabled=%d shadowDarkness=%f shadowBias=%f shadowHalfExtent=%f\n"
    "palette=%d paletteHues=%d paletteSats=%d paletteLums=%d\n"
    "outlineRadius=%f outlineDepthBias=%f\n"
    "outlineColorR=%f outlineColorG=%f outlineColorB=%f outlineColorA=%f\n"
    "hoverTileR=%f hoverTileG=%f hoverTileB=%f hoverTileA=%f\n",
    &fogE, &s.fogDensity, &s.fogStart, &s.fogR, &s.fogG, &s.fogB,
    &aoE, &s.aoStrength,
    &litE, &s.sunYawDeg, &s.sunPitchDeg, &s.ambient, &s.diffuse,
    &shadE, &s.shadowDarkness, &s.shadowBias, &s.shadowHalfExtent,
    &palE, &s.paletteHues, &s.paletteSats, &s.paletteLums,
    &s.outlineRadius, &s.outlineDepthBias,
    &s.outlineColorR, &s.outlineColorG, &s.outlineColorB, &s.outlineColorA,
    &s.hoverTileR, &s.hoverTileG, &s.hoverTileB, &s.hoverTileA);
  s.fogEnabled      = (fogE  != 0);
  s.aoEnabled       = (aoE   != 0);
  s.lightingEnabled = (litE  != 0);
  s.shadowsEnabled  = (shadE != 0);
  s.palette         = (palE  != 0);
  std::fclose(f);
  return true;
}
