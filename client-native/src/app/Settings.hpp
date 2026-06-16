#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <string>

// Persisted display/rendering settings. Saved as a simple key=value text
// file ("settings.cfg") next to the executable. Both the client and the
// level editor load/save the same file (they share the build/Release dir).
//
// Format is line-based "key=value". Unknown keys are ignored; missing keys
// keep their struct default. This makes the format forward/backward
// compatible — new fields can be appended without breaking old files.
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
  float     shadowBias      = 0.0008f;
  float     shadowHalfExtent = 40.0f;
  float     shadowSoftness  = 3.0f;   // PCSS max penumbra radius (texels)
  // Sky
  bool        skyEnabled  = true;
  std::string skyCubemap;             // "" = procedural gradient; else folder name
  float       skyExposure = 1.0f;
  float       skyZenithR = 0.16f,  skyZenithG = 0.34f,  skyZenithB = 0.62f;
  float       skyHorizonR = 0.62f, skyHorizonG = 0.74f, skyHorizonB = 0.86f;
  float       skyGroundR = 0.30f,  skyGroundG = 0.30f,  skyGroundB = 0.34f;
  float       skySunR = 1.0f,      skySunG = 0.96f,     skySunB = 0.88f;
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

  // ---- Water (shared by editor + client) ----
  float       waterShallowR = 0.30f, waterShallowG = 0.70f, waterShallowB = 0.60f;
  float       waterDeepR     = 0.05f, waterDeepG     = 0.20f, waterDeepB     = 0.35f;
  float       waterFoamR     = 0.90f, waterFoamG     = 0.95f, waterFoamB     = 1.00f;
  float       waterWaveSpeed       = 0.40f;
  float       waterWaveHeight      = 0.08f;
  float       waterWaveScale       = 2.00f;
  float       waterNormalStrength  = 0.60f;
  float       waterClarity         = 0.80f;   // reflectStrength
  float       waterCausticIntensity= 0.00f;
  float       waterCausticScale    = 4.00f;
  float       waterCausticSpeed    = 0.30f;
  float       waterFoamWidth       = 0.50f;
  float       waterFoamSpeed       = 0.50f;
  float       waterFoamScale       = 8.00f;
  float       waterParallaxDepth   = 0.04f;
  float       waterOffset          = 0.00f;
  float       waterRefraction      = 0.04f;
  float       waterDepthFade       = 5.00f;
  float       waterShoreDepth      = 0.85f;
  float       waterFoamContact     = 0.30f;
  float       waterSpecular        = 0.70f;
  std::string waterCausticMap;                // relative path, e.g. "assets/water_caustic.png" ("" = none)

  // ---- Bank window position (client only) ----
  // Top-left in screen pixels. -1 = unset → centre on first open.
  float       bankPosX = -1.0f;
  float       bankPosY = -1.0f;

  // ---- Chunked terrain (client only) ----
  // Draw distance in 64-tile render chunks around the player (ring radius).
  int         chunkDrawDistance = 2;
  // Entity sync (interest) radius in tiles requested from the server.
  int         viewRadius        = 15;
  // Level-editor world-mode draw distance (chunks rendered around active cell).
  int         editorDrawDistance = 2;
};

inline bool saveSettings(const AppSettings& s, const std::filesystem::path& path) {
  FILE* f = std::fopen(path.string().c_str(), "w");
  if (!f) return false;
  auto B = [](bool b){ return b ? 1 : 0; };
  std::fprintf(f, "fogEnabled=%d\nfogDensity=%f\nfogStart=%f\nfogR=%f\nfogG=%f\nfogB=%f\n",
               B(s.fogEnabled), s.fogDensity, s.fogStart, s.fogR, s.fogG, s.fogB);
  std::fprintf(f, "aoEnabled=%d\naoStrength=%f\n", B(s.aoEnabled), s.aoStrength);
  std::fprintf(f, "lightingEnabled=%d\nsunYawDeg=%f\nsunPitchDeg=%f\nambient=%f\ndiffuse=%f\n",
               B(s.lightingEnabled), s.sunYawDeg, s.sunPitchDeg, s.ambient, s.diffuse);
  std::fprintf(f, "shadowsEnabled=%d\nshadowDarkness=%f\nshadowBias=%f\nshadowHalfExtent=%f\nshadowSoftness=%f\n",
               B(s.shadowsEnabled), s.shadowDarkness, s.shadowBias, s.shadowHalfExtent, s.shadowSoftness);
  std::fprintf(f, "palette=%d\npaletteHues=%d\npaletteSats=%d\npaletteLums=%d\n",
               B(s.palette), s.paletteHues, s.paletteSats, s.paletteLums);
  std::fprintf(f, "outlineRadius=%f\noutlineDepthBias=%f\n", s.outlineRadius, s.outlineDepthBias);
  std::fprintf(f, "outlineColorR=%f\noutlineColorG=%f\noutlineColorB=%f\noutlineColorA=%f\n",
               s.outlineColorR, s.outlineColorG, s.outlineColorB, s.outlineColorA);
  std::fprintf(f, "hoverTileR=%f\nhoverTileG=%f\nhoverTileB=%f\nhoverTileA=%f\n",
               s.hoverTileR, s.hoverTileG, s.hoverTileB, s.hoverTileA);
  // Water
  std::fprintf(f, "waterShallowR=%f\nwaterShallowG=%f\nwaterShallowB=%f\n",
               s.waterShallowR, s.waterShallowG, s.waterShallowB);
  std::fprintf(f, "waterDeepR=%f\nwaterDeepG=%f\nwaterDeepB=%f\n",
               s.waterDeepR, s.waterDeepG, s.waterDeepB);
  std::fprintf(f, "waterFoamR=%f\nwaterFoamG=%f\nwaterFoamB=%f\n",
               s.waterFoamR, s.waterFoamG, s.waterFoamB);
  std::fprintf(f, "waterWaveSpeed=%f\nwaterWaveHeight=%f\nwaterWaveScale=%f\nwaterNormalStrength=%f\n",
               s.waterWaveSpeed, s.waterWaveHeight, s.waterWaveScale, s.waterNormalStrength);
  std::fprintf(f, "waterClarity=%f\nwaterCausticIntensity=%f\nwaterCausticScale=%f\nwaterCausticSpeed=%f\n",
               s.waterClarity, s.waterCausticIntensity, s.waterCausticScale, s.waterCausticSpeed);
  std::fprintf(f, "waterFoamWidth=%f\nwaterFoamSpeed=%f\nwaterFoamScale=%f\nwaterParallaxDepth=%f\n",
               s.waterFoamWidth, s.waterFoamSpeed, s.waterFoamScale, s.waterParallaxDepth);
  std::fprintf(f, "waterOffset=%f\nwaterRefraction=%f\nwaterDepthFade=%f\nwaterShoreDepth=%f\nwaterFoamContact=%f\nwaterSpecular=%f\n",
               s.waterOffset, s.waterRefraction, s.waterDepthFade, s.waterShoreDepth, s.waterFoamContact, s.waterSpecular);
  std::fprintf(f, "waterCausticMap=%s\n", s.waterCausticMap.c_str());
  std::fprintf(f, "bankPosX=%f\nbankPosY=%f\n", s.bankPosX, s.bankPosY);
  std::fprintf(f, "chunkDrawDistance=%d\n", s.chunkDrawDistance);
  std::fprintf(f, "viewRadius=%d\n", s.viewRadius);
  std::fprintf(f, "editorDrawDistance=%d\n", s.editorDrawDistance);
  // Sky
  std::fprintf(f, "skyEnabled=%d\nskyExposure=%f\nskyCubemap=%s\n",
               B(s.skyEnabled), s.skyExposure, s.skyCubemap.c_str());
  std::fprintf(f, "skyZenithR=%f\nskyZenithG=%f\nskyZenithB=%f\n",
               s.skyZenithR, s.skyZenithG, s.skyZenithB);
  std::fprintf(f, "skyHorizonR=%f\nskyHorizonG=%f\nskyHorizonB=%f\n",
               s.skyHorizonR, s.skyHorizonG, s.skyHorizonB);
  std::fprintf(f, "skyGroundR=%f\nskyGroundG=%f\nskyGroundB=%f\n",
               s.skyGroundR, s.skyGroundG, s.skyGroundB);
  std::fprintf(f, "skySunR=%f\nskySunG=%f\nskySunB=%f\n",
               s.skySunR, s.skySunG, s.skySunB);
  std::fclose(f);
  return true;
}

inline bool loadSettings(AppSettings& s, const std::filesystem::path& path) {
  FILE* f = std::fopen(path.string().c_str(), "r");
  if (!f) return false;

  char line[1024];
  while (std::fgets(line, sizeof(line), f)) {
    // Split on the first '='.
    char* eq = std::strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    const char* key = line;
    char* val = eq + 1;
    // Trim trailing newline/CR/space from value.
    std::size_t vlen = std::strlen(val);
    while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r' || val[vlen-1] == ' '))
      val[--vlen] = '\0';

    auto fF = [&](const char* k, float& dst) { if (std::strcmp(key, k) == 0) { dst = std::strtof(val, nullptr); return true; } return false; };
    auto fI = [&](const char* k, int& dst)   { if (std::strcmp(key, k) == 0) { dst = std::atoi(val); return true; } return false; };
    auto fB = [&](const char* k, bool& dst)  { if (std::strcmp(key, k) == 0) { dst = (std::atoi(val) != 0); return true; } return false; };

    // Try each field in turn (short-circuits on first match).
    if (fB("fogEnabled", s.fogEnabled) || fF("fogDensity", s.fogDensity) ||
        fF("fogStart", s.fogStart) || fF("fogR", s.fogR) || fF("fogG", s.fogG) || fF("fogB", s.fogB) ||
        fB("aoEnabled", s.aoEnabled) || fF("aoStrength", s.aoStrength) ||
        fB("lightingEnabled", s.lightingEnabled) || fF("sunYawDeg", s.sunYawDeg) ||
        fF("sunPitchDeg", s.sunPitchDeg) || fF("ambient", s.ambient) || fF("diffuse", s.diffuse) ||
        fB("shadowsEnabled", s.shadowsEnabled) || fF("shadowDarkness", s.shadowDarkness) ||
        fF("shadowBias", s.shadowBias) || fF("shadowHalfExtent", s.shadowHalfExtent) ||
        fF("shadowSoftness", s.shadowSoftness) ||
        fB("palette", s.palette) || fI("paletteHues", s.paletteHues) ||
        fI("paletteSats", s.paletteSats) || fI("paletteLums", s.paletteLums) ||
        fF("outlineRadius", s.outlineRadius) || fF("outlineDepthBias", s.outlineDepthBias) ||
        fF("outlineColorR", s.outlineColorR) || fF("outlineColorG", s.outlineColorG) ||
        fF("outlineColorB", s.outlineColorB) || fF("outlineColorA", s.outlineColorA) ||
        fF("hoverTileR", s.hoverTileR) || fF("hoverTileG", s.hoverTileG) ||
        fF("hoverTileB", s.hoverTileB) || fF("hoverTileA", s.hoverTileA) ||
        fF("waterShallowR", s.waterShallowR) || fF("waterShallowG", s.waterShallowG) || fF("waterShallowB", s.waterShallowB) ||
        fF("waterDeepR", s.waterDeepR) || fF("waterDeepG", s.waterDeepG) || fF("waterDeepB", s.waterDeepB) ||
        fF("waterFoamR", s.waterFoamR) || fF("waterFoamG", s.waterFoamG) || fF("waterFoamB", s.waterFoamB) ||
        fF("waterWaveSpeed", s.waterWaveSpeed) || fF("waterWaveHeight", s.waterWaveHeight) ||
        fF("waterWaveScale", s.waterWaveScale) || fF("waterNormalStrength", s.waterNormalStrength) ||
        fF("waterClarity", s.waterClarity) || fF("waterCausticIntensity", s.waterCausticIntensity) ||
        fF("waterCausticScale", s.waterCausticScale) || fF("waterCausticSpeed", s.waterCausticSpeed) ||
        fF("waterFoamWidth", s.waterFoamWidth) || fF("waterFoamSpeed", s.waterFoamSpeed) ||
        fF("waterFoamScale", s.waterFoamScale) || fF("waterParallaxDepth", s.waterParallaxDepth) ||
        fF("waterOffset", s.waterOffset) || fF("waterRefraction", s.waterRefraction) ||
        fF("waterDepthFade", s.waterDepthFade) || fF("waterShoreDepth", s.waterShoreDepth) ||
        fF("waterFoamContact", s.waterFoamContact) ||
        fF("waterSpecular", s.waterSpecular) ||
        fF("bankPosX", s.bankPosX) || fF("bankPosY", s.bankPosY) ||
        fI("chunkDrawDistance", s.chunkDrawDistance) ||
        fI("viewRadius", s.viewRadius) ||
        fI("editorDrawDistance", s.editorDrawDistance) ||
        fB("skyEnabled", s.skyEnabled) || fF("skyExposure", s.skyExposure) ||
        fF("skyZenithR", s.skyZenithR) || fF("skyZenithG", s.skyZenithG) || fF("skyZenithB", s.skyZenithB) ||
        fF("skyHorizonR", s.skyHorizonR) || fF("skyHorizonG", s.skyHorizonG) || fF("skyHorizonB", s.skyHorizonB) ||
        fF("skyGroundR", s.skyGroundR) || fF("skyGroundG", s.skyGroundG) || fF("skyGroundB", s.skyGroundB) ||
        fF("skySunR", s.skySunR) || fF("skySunG", s.skySunG) || fF("skySunB", s.skySunB)) {
      continue;
    }
    if (std::strcmp(key, "waterCausticMap") == 0) { s.waterCausticMap = val; continue; }
    if (std::strcmp(key, "skyCubemap") == 0) { s.skyCubemap = val; continue; }
  }
  std::fclose(f);
  return true;
}
