#pragma once

namespace editor {

enum class EditorTool {
  PaintTerrain,
  SculptRaise,
  SculptLower,
  PlaceObstacle,
  PlaceNPC,
  PlaceSpawn,
  PaintWalkable,    // marks tiles walkable (clears obstacle + sets walkable=true)
  PaintBlocked,     // marks tiles non-walkable WITHOUT placing an obstacle
  Erase,
};

enum class BrushShape { Square, Round };

struct BrushState {
  int        size    = 1;         // 1..64 tiles
  BrushShape shape   = BrushShape::Square;
  float      strength= 0.08f;    // height sculpt strength per frame
};

}  // namespace editor
