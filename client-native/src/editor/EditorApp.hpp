#pragma once

#include "app/Settings.hpp"
#include "app/Window.hpp"
#include "camera/GameCamera.hpp"
#include "editor/EditorPalette.hpp"
#include "editor/EditorTool.hpp"
#include "editor/EntityClient.hpp"
#include "editor/EntityDefs.hpp"
#include "editor/MinimapRenderer.hpp"
#include "editor/UndoStack.hpp"
#include "input/Picker.hpp"
#include "render/Mesh.hpp"
#include "render/MsaaFramebuffer.hpp"
#include "render/Shader.hpp"
#include "render/ShadowMap.hpp"
#include "shared/SharedTypes.hpp"
#include "world/EntityRenderer.hpp"
#include "world/GltfLoader.hpp"
#include "world/GltfModel.hpp"
#include "world/ObstacleSystem.hpp"
#include "world/SkinnedMesh.hpp"
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
  void drawProperties();         // right-hand docked panel (palette, object/NPC type, file info)
  void drawPreferencesWindow();  // floating Preferences window (Edit → Preferences)
  void drawGridView();
  void drawMinimapWindow();
  void drawMenuBar();

  // ---- Editing
  void applyToolAt(int tx, int ty, float dt, bool rightClick,
                   bool& dirtyTerrain, bool& dirtyObstacles,
                   bool& dirtyMinimap,  bool& dirtyWater);
  void applyBrush(int cx, int cy, float dt, bool rightClick = false);  // dispatches to applyToolAt for each tile in brush
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
  void openRecentFile(const std::string& path);
  void saveCurrentFile();
  void saveAsDialog();
  std::wstring winOpenDialog();   // returns path or empty
  std::wstring winSaveDialog();   // returns path or empty
  void updateWindowTitle();       // refresh GLFW title (filename + dirty marker)
  void addRecentFile(const std::string& path);
  void loadRecentFiles();
  void saveRecentFiles();

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
  void  setObstacleAtTile(int tx, int ty, const std::string& obs);

  // ---- Water
  // Deform vertex heights in a ±2 tile radius around the placed water tile so
  // the terrain slopes smoothly down into the water surface.
  void bakeWaterBank(int tx, int ty);
  // Water settings UI block (called from drawPreferencesWindow).
  void drawWaterSettings();

  // ---- Settings persistence
  void saveSettings();
  void loadSettings();

  // ---- GL / window
  app::Window                                    window_;
  std::unique_ptr<render::MsaaFramebuffer>       viewport3dFbo_;
  int                                            viewport3dW_ = 800;
  int                                            viewport3dH_ = 600;

  render::Shader  terrainShader_;
  render::Shader  wireframeShader_;
  render::Shader  obstacleShader_;
  render::Shader  skinnedShader_;          // for animated obstacle models (fishing spots etc.)
  render::Shader  shadowInstancedShader_;
  render::ShadowMap shadowMap_;
  render::Mesh    terrainMesh_;
  world::ObstacleSystem obstacles_;
  world::SkinnedMesh    fishingSpotMesh_;  // animated fishing spot
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
  bool                          dirty_          = false;
  std::vector<std::string>      recentFiles_;   // ordered most-recently-used first

  // ---- Editor state
  EditorTool    activeTool_     = EditorTool::PaintTerrain;
  EditorTool    prevTool_       = EditorTool::PaintTerrain;
  BrushState    brush_;
  int           hoveredTileX_   = -1;
  int           hoveredTileY_   = -1;
  bool          mouseHeldGrid_    = false;   // drag in 2D view
  bool          mouseHeld3D_      = false;   // drag in 3D viewport
  bool          middleClickIn3D_  = false;   // middle-click started in 3D viewport (locks orbit there)

  // Sub-selection within tools
  std::string           obstacleSubtype_ = "tree";  // DB object ID of selected obstacle type
  std::string           npcSubtype_      = "chicken";

  // Active terrain colour (PaintTerrain tool)
  float paletteR_ = 0.49f, paletteG_ = 0.78f, paletteB_ = 0.31f;

  // Overlays (2D grid view / 3D view)
  bool showHeightOverlay_         = false;
  bool showWalkabilityOverlay_    = false;
  bool showGridmapOverlay_        = false;
  bool showWireframe_             = false;
  bool resetLayout_               = false;  // triggers DockBuilder reset next frame
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

  // Preferences window state
  bool showPrefsWindow_  = false;
  int  prefsCategory_    = 0;  // 0=Water, 1=Lighting, 2=Fog, 3=AO, 4=Rendering

  // New map dialog state
  bool showNewMapDialog_ = false;
  int  newMapW_          = 64;
  int  newMapH_          = 64;

  // ImGui
  bool imguiInited_ = false;

  // Fog
  bool      fogEnabled_  = false;
  float     fogDensity_  = 0.015f;
  float     fogStart_    = 5.0f;
  glm::vec3 fogColor_    = {0.58f, 0.67f, 0.78f};
  // AO
  bool      aoEnabled_   = true;
  float     aoStrength_  = 0.50f;

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

  // ---- Database editor window
  void drawDatabaseWindow();
  void dbLoadAll();              // fetch all entities from server
  void dbDrawItemsTab();
  void dbDrawNPCsTab();
  void dbDrawObjectsTab();
  void dbDrawActionsTab();

  // Offscreen FBO for the 3D model preview inside the DB window.
  GLuint dbPreviewFbo_  = 0;
  GLuint dbPreviewTex_  = 0;
  GLuint dbPreviewRbo_  = 0;   // depth renderbuffer
  void   dbInitPreviewFbo();
  void   dbDestroyPreviewFbo();
  void   dbRenderPreview(float dt);   // renders into dbPreviewFbo_, angle auto-spins
  void   dbLoadPreviewModel(const std::string& modelPath, bool forceReload = false);

  // Per-primitive GPU resources for the preview model.
  struct DbPreviewPrim {
    GLuint  vao     = 0;
    GLuint  vboPos  = 0;
    GLuint  vboNorm = 0;
    GLuint  vboCol  = 0;
    GLuint  ebo     = 0;
    GLsizei indexCount = 0;
    glm::vec4 color    = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
  };
  render::Shader              dbPreviewShader_;
  std::vector<DbPreviewPrim>  dbPreviewPrims_;
  std::string                 dbPreviewLoadedPath_;
  glm::vec3                   dbPreviewCenter_ = glm::vec3(0.f);
  float                       dbPreviewRadius_ = 1.0f;
  // Animated preview (used when the model has glTF animation clips)
  world::SkinnedMesh          dbPreviewSkinned_;
  bool                        dbPreviewHasAnim_  = false;
  std::vector<std::string>    dbPreviewClips_;    // clip names from the loaded model
  glm::vec3                   dbPreviewRot_ = glm::vec3(0.f);  // euler degrees applied in preview

  EntityClient         dbClient_;
  bool                 showDbWindow_  = false;
  bool                 dbLoaded_      = false;
  int                  dbTab_         = 0;  // 0=Items 1=NPCs 2=Objects 3=Actions
  std::string          dbStatus_;           // last save/error message
  float                dbPreviewAngle_ = 0.0f;

  // Lists (fetched from server)
  std::vector<ItemDef>   dbItems_;
  std::vector<NpcDef>    dbNPCs_;
  std::vector<ObjectDef> dbObjects_;
  std::vector<ActionDef> dbActions_;

  // Selected + edit copies
  int       dbSelItem_   = -1;
  int       dbSelNPC_    = -1;
  int       dbSelObject_ = -1;
  int       dbSelAction_ = -1;
  ItemDef   dbEditItem_;
  NpcDef    dbEditNPC_;
  ObjectDef dbEditObject_;
  ActionDef dbEditAction_;
  bool      dbEditIsNew_ = false;

  std::chrono::steady_clock::time_point lastFrameTime_{};
};

}  // namespace editor
