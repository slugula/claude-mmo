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
  { "pool_corner",     "assets/models/pool_InnerCorner.glb" }, // 2 adjacent terrain (SE)
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
constexpr int kBaseThreeSides = 3;   // pool_ThreeSides: single OPEN (water) side faces W
constexpr int kBaseChannel    = 0;   // pool_TwoSides:   walls on the N/S axis
constexpr int kBaseCorner     = 1;   // pool_InnerCorner: walls on S+E (corner ref = E)

inline int rotFor(int dir, int base) { return (kRotSign * (dir - base)) & 3; }

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

void PoolRenderer::setModelResolver(
    std::function<std::filesystem::path(const std::string&)> r) {
  if (modelsInited_) return;
  models_.init(r, "assets/models/_placeholder_object.gltf");
  for (const auto& m : kModels) models_.ensure(m.id, m.path, 1, 1);
  modelsInited_ = true;
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

      const float rotY = static_cast<float>(p.rot & 3) * 1.57079632679f;
      instances_[p.id].push_back(ModelLibrary::Instance{
          static_cast<float>(tx), cy, static_cast<float>(ty), rotY, n.x, n.y, n.z });
    }
  }
}

void PoolRenderer::render(render::Shader& s) {
  for (auto& [id, insts] : instances_)
    if (!insts.empty()) models_.drawStaticInstanced(s, id, insts);
}

void PoolRenderer::renderDepth(render::Shader& s) {
  for (auto& [id, insts] : instances_)
    if (!insts.empty()) models_.drawStaticInstanced(s, id, insts);
}

}  // namespace world
