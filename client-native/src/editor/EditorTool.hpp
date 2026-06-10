#pragma once

namespace editor {

enum class EditorTool {
  PaintTerrain,
  SculptTerrain,    // left-click = raise, right-click = lower
  FlattenTerrain,   // pull brush vertices toward their average height (level a pad)
  PlaceObstacle,
  PlaceWall,         // wall on a tile edge; Q/E rotate 45°
  PlacePillar,       // pillar on a tile corner
  PlaceNPC,
  PlaceSpawn,
  PaintBlocking,    // left-click = block (walkable=false), right-click = unblock (walkable=true)
  PaintOverlay,     // paints a shaped, textured overlay (path/floor/water) on a tile
  Erase,
};

enum class BrushShape { Square, Round };

struct BrushState {
  int        size    = 1;         // 1..64 tiles
  BrushShape shape   = BrushShape::Square;
  float      strength= 0.08f;    // height sculpt strength per frame
};

}  // namespace editor
