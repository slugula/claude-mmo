#pragma once

#include "app/Window.hpp"
#include "camera/GameCamera.hpp"
#include "editor/EditorPalette.hpp"
#include "editor/EditorTool.hpp"
#include "editor/MinimapRenderer.hpp"
#include "editor/UndoStack.hpp"
#include "input/Picker.hpp"
#include "render/Mesh.hpp"
#include "render/MsaaFramebuffer.hpp"
#include "render/Shader.hpp"
#include "render/ShadowMap.hpp"
#include "shared/SharedTypes.hpp"
#include "world/EntityRenderer.hpp"
#include "world/ObstacleSystem.hpp"
#include "world/TerrainBuilder.hpp"
#include "world/WaterRenderer.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace editor {

class EditorApp {
public:
  EditorApp() = default;
  ~EditorApp();

  EditorApp(const EditorApp&)            = delete;
  EditorApp& operator=(const EditorApp&) = delete;

  bool init();
  int  run();

private:
  // ---- Initialisation helpers
  void initImGui();
  void shutdownImGui();
  void initNewMap(int w, int h);
  void rebuildTerrainGL();         // full terrain mesh rebuild + GPU upload
  void rebuildObstacles();

  // ---- Frame rendering
  void renderFrame(float dt);
  void render3DViewport(float dt);
  void draw3DViewportWindow();   // ImGui window that hosts the FBO image + 3D interaction
  void drawToolbar();
  void drawProperties();
  void drawGridView();
  void drawMinimapWindow();
  void drawMenuBar();

  // ---- Editing
  void applyToolAt(int tx, int ty, float dt,
                   bool& dirtyTerrain, bool& dirtyObstacles,
                   bool& dirtyMinimap,  bool& dirtyWater);
  void applyBrush(int cx, int cy, float dt);  // dispatches to applyToolAt for each tile in brush
  int  clampTile(int v, int max) const;

  // ---- Blocked-tile 3D overlay
  void initBlockedOverlay();
  void rebuildBlockedOverlay();

  // ---- Terrain incremental update
  // Rebuild vertex colours for the tile range [x0..x1) [y0..y1) and
  // re-upload just those vertex attributes to the GPU.
  void repaintVertexColors(int x0, int y0, int x1, int y1);
  // Rebuild normals for the vertex range and re-upload.
  void resculptNormals(int x0, int y0, int x1, int y1);

  // ---- File I/O
  void newMapDialog();
  void openFileDialog();
  void saveCurrentFile();
  void saveAsDialog();
  std::wstring winOpenDialog();   // returns path or empty
  std::wstring winSaveDialog();   // returns path or empty

  // ---- Undo helpers
  void pushUndo();  // snapshot current map + npcs

  // ---- Hover mesh (yellow outline)
  void initHoverMesh();
  void destroyHoverMesh();
  void updateHoverMesh(int tx, int ty, int szX = 1, int szY = 1);

  // ---- Resize map
  void resizeMap(int newW, int newH);

  // ---- Utilities
  float tileWorldY(int tx, int ty) const;
  void  setObstacleAtTile(int tx, int ty, shared::ObstacleType obs);

  // ---- Water
  // Deform vertex heights in a ±2 tile radius around the placed water tile so
  // the terrain slopes smoothly down into the water surface.
  void bakeWaterBank(int tx, int ty);
  // Water settings UI block (called from drawProperties).
  void drawWaterSettings();

  // ---- GL / window
  app::Window                                    window_;
  std::unique_ptr<render::MsaaFramebuffer>       viewport3dFbo_;
  int                                            viewport3dW_ = 800;
  int                                            viewport3dH_ = 600;

  render::Shader  terrainShader_;
  render::Shader  wireframeShader_;
  render::Shader  obstacleShader_;
  render::Shader  shadowInstancedShader_;
  render::ShadowMap shadowMap_;
  render::Mesh    terrainMesh_;
  world::ObstacleSystem obstacles_;
  world::EntityRenderer entities_;   // NPC stand-ins
  camera::GameCamera    camera_;
  world::WaterRenderer  waterRenderer_;
  world::WaterUniforms  waterUniforms_;

  // Raw GPU buffers for incremental terrain updates.
  // These are the VBOs owned by terrainMesh_; we cache them for SubData calls.
  // Actually we just rebuild the full mesh on structural changes; for paint
  // and sculpt we use targeted glBufferSubData via Mesh helpers.
  // (TerrainBuilder returns the full mesh data; we store a CPU copy.)
  world::TerrainMeshData terrainData_;

  // Hover indicator.
  GLuint hoverVao_       = 0;
  GLuint hoverVbo_       = 0;
  int    hoverVertCount_ = 4;   // vertices currently stored (4 = square LINE_LOOP, N*8 = round GL_LINES)
  bool   hoverIsRound_   = false;

  // Blocked-tile X overlay (3D view).
  GLuint blockedVao_       = 0;
  GLuint blockedVbo_       = 0;
  int    blockedLineCount_ = 0;  // # of line segment vertices (4 per tile)
  bool   blockedGLInited_  = false;

  // ---- Map state
  shared::WorldMapFile          map_;
  std::vector<shared::NpcSpawn> npcSpawns_;
  std::string                   currentFilePath_;  // empty = unsaved

  // ---- Editor state
  EditorTool    activeTool_     = EditorTool::PaintTerrain;
  EditorTool    prevTool_       = EditorTool::PaintTerrain;
  BrushState    brush_;
  int           hoveredTileX_   = -1;
  int           hoveredTileY_   = -1;
  bool          mouseHeldGrid_  = false;   // drag in 2D view
  bool          mouseHeld3D_    = false;   // drag in 3D viewport

  // Sub-selection within tools
  shared::ObstacleType  obstacleSubtype_ = shared::ObstacleType::tree;
  std::string           npcSubtype_      = "chicken";

  // Active terrain colour (PaintTerrain tool)
  float paletteR_ = 0.49f, paletteG_ = 0.78f, paletteB_ = 0.31f;

  // Overlays (2D grid view / 3D view)
  bool showHeightOverlay_         = false;
  bool showWalkabilityOverlay_    = false;
  bool showGridmapOverlay_        = false;
  // True when overlay was auto-enabled by tool selection (so it auto-disables on tool change)
  bool overlayHeightAuto_         = false;
  bool overlayWalkabilityAuto_    = false;

  // 2D grid view pan + zoom
  float gridOffX_  = 0.0f;
  float gridOffY_  = 0.0f;
  float gridZoom_  = 8.0f;   // pixels per tile (2..32)

  // Map resize dialog state
  bool showResizeDialog_ = false;
  int  resizeW_          = 64;
  int  resizeH_          = 64;

  // New map dialog state
  bool showNewMapDialog_ = false;
  int  newMapW_          = 64;
  int  newMapH_          = 64;

  // ImGui
  bool imguiInited_ = false;

  // Lighting (passed to terrain/obstacle shaders)
  float sunYawDeg_   = 200.0f;
  float sunPitchDeg_ = 58.0f;
  float ambient_     = 0.45f;
  float diffuse_     = 0.55f;
  bool  lightingEnabled_ = true;
  bool  shadowsEnabled_  = false;  // disabled in editor by default
  float shadowHalfExtent_ = 40.0f;

  // Palette quantisation
  bool palette_    = true;
  int  paletteHues_= 64;
  int  paletteSats_= 16;
  int  paletteLums_= 48;

  // Undo/redo
  UndoStack    undo_;
  bool         undoPending_ = false;  // push at mouse-up
  bool         hadStroke_   = false;  // was brushing this frame?

  // Minimap
  MinimapRenderer minimap_;

  std::chrono::steady_clock::time_point lastFrameTime_{};
};

}  // namespace editor
