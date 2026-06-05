#pragma once

namespace editor {

enum class EditorTool {
  PaintTerrain,
  SculptTerrain,    // left-click = raise, right-click = lower
  PlaceObstacle,
  PlaceWall,         // wall on a tile edge; Q/E rotate 45°
  PlacePillar,       // pillar on a tile corner
  PlaceNPC,
  PlaceSpawn,
  PaintBlocking,    // left-click = block (walkable=false), right-click = unblock (walkable=true)
  PaintWater,       // paints water tiles (non-walkable, rendered as animated water)
  Erase,
};

enum class BrushShape { Square, Round };

struct BrushState {
  int        size    = 1;         // 1..64 tiles
  BrushShape shape   = BrushShape::Square;
  float      strength= 0.08f;    // height sculpt strength per frame
};

}  // namespace editor
