#include "world/PoolRenderer.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace world {

namespace {

// Pool model ids → asset paths. The two 2-side pieces are supplied by the
// artist; until present they fall back to the placeholder cube.
struct PoolModel { const char* id; const char* path; };
constexpr PoolModel kModels[] = {
  { "pool_oneTile",    "assets/models/pool_OneTile.glb" },     // 4 terrain sides
  { "pool_center",     "assets/models/pool_center.glb" },      // 0 terrain sides
  { "pool_oneSide",    "assets/models/pool_OneSide.glb" },     // 1 terrain side
  { "pool_threeSides", "assets/models/pool_ThreeSides.glb" },  // 3 terrain sides
  { "pool_channel",    "assets/models/pool_TwoSides.glb" },    // 2 opposite terrain (N/S)
  { "pool_corner",     "assets/models/pool_OuterCorner.glb" }, // 2 adjacent terrain (L-wall, N+E)
  { "pool_inner",      "assets/models/pool_InnerCorner.glb" }, // diagonal concave-corner pillar (SE)
};

// ---- Orientation tuning ----------------------------------------------------
// Cardinal index: 0 = N (tileY-1), 1 = E (tileX+1), 2 = S (tileY+1), 3 = W.
// A mesh's "reference side" aims at direction kBase* at rotationQuarter 0; we
// rotate in 90° steps to point it at the actual neighbour. Project convention
// is Blender −Y = south, and the carved walls are on −Y, so the single WALL of
// pool_oneSide faces S (2) at q0, etc. If a piece renders rotated wrong, change
// its kBase* (0..3). If EVERY piece is mirrored, flip kRotSign to -1.
// rotY about +Y is CCW in world space while these indices go clockwise, so the
// sign is -1. Bases below match the artist's authored orientations.
constexpr int kRotSign        = -1;
constexpr int kBaseOneSide    = 2;   // pool_OneSide:    single WALL faces S
constexpr int kBaseThreeSides = 0;   // pool_ThreeSides: 90deg CCW from authored open=W (artist-verified)
constexpr int kBaseChannel    = 0;   // pool_TwoSides:   walls on the N/S axis
constexpr int kBaseCorner     = 0;   // pool_OuterCorner: L-wall on N+E (corner ref = N)
// Diagonal index for inner-corner pillars: 0=NE, 1=SE, 2=SW, 3=NW (same CW
// sense as the cardinals). pool_InnerCorner fills the SE corner at q0.
constexpr int kBaseInnerDiag  = 1;

inline int rotFor(int dir, int base) { return (kRotSign * (dir - base)) & 3; }

// Parse "#rrggbb" → linear-ish RGB [0,1]; falls back to mid-grey.
glm::vec3 hexRgb(const std::string& s) {
  if (s.size() < 7 || s[0] != '#') return glm::vec3(0.5f);
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  return glm::vec3(
      (nib(s[1]) * 16 + nib(s[2])) / 255.0f,
      (nib(s[3]) * 16 + nib(s[4])) / 255.0f,
      (nib(s[5]) * 16 + nib(s[6])) / 255.0f);
}

struct Pick { const char* id; int rot; };

// Choose mesh + quarter-rotation from which cardinal sides are terrain (true).
Pick pickPool(bool tN, bool tE, bool tS, bool tW) {
  const int count = (tN ? 1 : 0) + (tE ? 1 : 0) + (tS ? 1 : 0) + (tW ? 1 : 0);
  if (count == 0) return { "pool_center",  0 };
  if (count == 4) return { "pool_oneTile", 0 };
  if (count == 1) {                       // wall faces the single terrain side
    const int d = tN ? 0 : tE ? 1 : tS ? 2 : 3;
    return { "pool_oneSide", rotFor(d, kBaseOneSide) };
  }
  if (count == 3) {                       // open side faces the single water side
    const int d = !tN ? 0 : !tE ? 1 : !tS ? 2 : 3;
    return { "pool_threeSides", rotFor(d, kBaseThreeSides) };
  }
  // count == 2 — channel (opposite) or corner (adjacent).
  if (tN && tS) return { "pool_channel", rotFor(0, kBaseChannel) };
  if (tE && tW) return { "pool_channel", rotFor(1, kBaseChannel) };
  for (int d = 0; d < 4; ++d) {
    auto T = [&](int k) { return k == 0 ? tN : k == 1 ? tE : k == 2 ? tS : tW; };
    if (T(d) && T((d + 1) & 3)) return { "pool_corner", rotFor(d, kBaseCorner) };
  }
  return { "pool_oneTile", 0 };
}

}  // namespace

void PoolRenderer::reloadModels() {
  models_.clearEntries();
  for (const auto& m : kModels) {
    models_.ensure(m.id, m.path, 1, 1);
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(resolver_(m.path), ec);
    if (!ec) mtimes_[m.id] = t;
  }
}

void PoolRenderer::setModelResolver(
    std::function<std::filesystem::path(const std::string&)> r) {
  if (modelsInited_) return;
  resolver_ = r;
  models_.init(r, "assets/models/_placeholder_object.gltf");
  reloadModels();
  modelsInited_ = true;
}

bool PoolRenderer::pollReloadIfChanged() {
  if (!modelsInited_ || !resolver_) return false;
  const auto now = std::chrono::steady_clock::now();
  if (now - lastPoll_ < std::chrono::milliseconds(500)) return false;
  lastPoll_ = now;

  bool changed = false;
  for (const auto& m : kModels) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(resolver_(m.path), ec);
    if (ec) continue;
    const auto it = mtimes_.find(m.id);
    if (it == mtimes_.end() || it->second != t) changed = true;
  }
  if (changed) reloadModels();
  return changed;
}

void PoolRenderer::rebuildFromMap(const shared::WorldMapFile& map) {
  instances_.clear();
  const int W = map.width, H = map.height;
  if (W <= 0 || H <= 0) return;

  std::vector<std::vector<bool>> water(
      static_cast<std::size_t>(H), std::vector<bool>(static_cast<std::size_t>(W), false));
  for (const auto& ov : map.overlayTiles)
    if (ov.materialId == shared::kWaterMaterialId &&
        ov.tileX >= 0 && ov.tileY >= 0 && ov.tileX < W && ov.tileY < H)
      water[ov.tileY][ov.tileX] = true;

  auto isWater = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H && water[y][x];
  };

  const auto& vh = map.vertexHeights;
  const bool vhok = static_cast<int>(vh.size()) == (W + 1) * (H + 1);
  auto cH = [&](int vc, int vr) {
    return vhok ? vh[static_cast<std::size_t>(vr) * (W + 1) + vc] * shared::kMaxTerrainH : 0.0f;
  };

  // Average groundColor of the (up to 8) surrounding NON-water tiles, so the
  // pool blends with the terrain it's carved into instead of reading as grey.
  auto surroundColor = [&](int tx, int ty) -> glm::vec3 {
    glm::vec3 sum(0.0f);
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        const int nx = tx + dx, ny = ty + dy;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
        if (water[ny][nx]) continue;
        sum += hexRgb(map.tiles[ny][nx].groundColor);
        ++n;
      }
    if (n > 0) return sum / static_cast<float>(n);
    return hexRgb(map.tiles[ty][tx].groundColor);  // surrounded by water → own tile
  };

  // Diagonal (dx,dy) + its two flanking cardinals, for concave inner corners.
  struct Diag { int dx, dy, fx0, fy0, fx1, fy1; };
  const Diag diags[4] = {
    {  1, -1,  0, -1,  1,  0 },   // NE  (flanks N, E)
    {  1,  1,  1,  0,  0,  1 },   // SE  (flanks E, S)
    { -1,  1,  0,  1, -1,  0 },   // SW  (flanks S, W)
    { -1, -1, -1,  0,  0, -1 },   // NW  (flanks W, N)
  };

  for (int ty = 0; ty < H; ++ty) {
    for (int tx = 0; tx < W; ++tx) {
      if (!water[ty][tx]) continue;
      // A side is "terrain" when the neighbour isn't water (out of bounds too).
      const bool tN = !isWater(tx, ty - 1);
      const bool tE = !isWater(tx + 1, ty);
      const bool tS = !isWater(tx, ty + 1);
      const bool tW = !isWater(tx - 1, ty);
      const Pick p = pickPool(tN, tE, tS, tW);

      const float hSW = cH(tx,     H - ty);
      const float hSE = cH(tx + 1, H - ty);
      const float hNW = cH(tx,     H - ty - 1);
      const float hNE = cH(tx + 1, H - ty - 1);
      const float cy = (hSW + hSE + hNW + hNE) * 0.25f;
      // Up-normal from corner slopes (matches EntityRenderer::tileUpNormal) so
      // the pool seats flush on sloped terrain.
      const float dhdx = ((hSE + hNE) - (hSW + hNW)) * 0.5f;
      const float dhdz = ((hNW + hNE) - (hSW + hSE)) * 0.5f;
      const glm::vec3 n = glm::normalize(glm::vec3(-dhdx, 1.0f, -dhdz));
      const glm::vec3 c = surroundColor(tx, ty);

      auto add = [&](const char* id, int rotQuarter) {
        const float rotY = static_cast<float>(rotQuarter & 3) * 1.57079632679f;
        instances_[id].push_back(ModelLibrary::Instance{
            static_cast<float>(tx), cy, static_cast<float>(ty), rotY,
            n.x, n.y, n.z, c.r, c.g, c.b,
            hSW - cy, hSE - cy, hNW - cy, hNE - cy });   // corner deltas for the warp
      };

      add(p.id, p.rot);

      // Concave inner corners: a diagonal neighbour is terrain while BOTH of its
      // flanking cardinals are water — drop an inner pillar to fill the nook.
      for (int d = 0; d < 4; ++d) {
        const Diag& dg = diags[d];
        const bool diagTerrain = !isWater(tx + dg.dx, ty + dg.dy);
        const bool flank0Water = isWater(tx + dg.fx0, ty + dg.fy0);
        const bool flank1Water = isWater(tx + dg.fx1, ty + dg.fy1);
        if (diagTerrain && flank0Water && flank1Water)
          add("pool_inner", rotFor(d, kBaseInnerDiag));
      }
    }
  }
}

void PoolRenderer::render(render::Shader& s) {
  s.setFloat("u_poolWarp", 1.0f);   // bilinear height warp for pool meshes
  for (auto& [id, insts] : instances_)
    if (!insts.empty()) models_.drawStaticInstanced(s, id, insts);
  s.setFloat("u_poolWarp", 0.0f);   // restore for the next obstacle/wall draws
}

void PoolRenderer::renderDepth(render::Shader& s) {
  for (auto& [id, insts] : instances_)
    if (!insts.empty()) models_.drawStaticInstanced(s, id, insts);
}

}  // namespace world
