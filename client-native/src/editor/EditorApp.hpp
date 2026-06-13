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
#include "editor/WorldAssembly.hpp"   // CellKey, assembleWorld, sliceChunk
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
#include "world/ChunkedTerrain.hpp"
#include "world/WallSystem.hpp"
#include "world/WaterRenderer.hpp"
#include "world/OverlayRenderer.hpp"
#include "world/AttachmentRenderer.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

// Primary editor workspaces, switched via the left mode rail. Map shows the
// docked map-editing layout (toolbar / 3D / 2D grid / properties / minimap);
// World and Database each fill the content area with their dedicated view.
enum class EditorMode { Map, World, Database };

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
  void drawModeRail(float railW);  // vertical Map/World/Database switcher
  void setMode(EditorMode m);      // switch workspace (handles lazy DB load)

  // ---- Editing
  void applyToolAt(int tx, int ty, float dt, bool rightClick,
                   bool& dirtyTerrain, bool& dirtyObstacles,
                   bool& dirtyMinimap,  bool& dirtyWater);
  void applyBrush(int cx, int cy, float dt, bool rightClick = false);  // dispatches to applyToolAt for each tile in brush
  void applyFlatten(int cx, int cy);   // pull brush vertices toward their average height
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
  world::ChunkedTerrain terrain_;          // per-chunk terrain meshes + draw ring (both modes)
  world::ObstacleSystem obstacles_;
  world::WallSystem     walls_;
  world::EntityRenderer entities_;   // NPC stand-ins
  camera::GameCamera    camera_;
  world::WaterRenderer  waterRenderer_;
  world::WaterUniforms  waterUniforms_;
  world::OverlayRenderer overlayRenderer_;

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
  EditorMode    mode_           = EditorMode::Map;
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
  int                   placeRotation_   = 0;        // 0..3 quarter-turns (Q=CCW, E=CW) for placed objects
  int                   wallOrient_      = 0;        // 0..7 (45°) for PlaceWall (Q/E)
  int                   pillarOrient_    = 0;        // 0/2/4/6 = tile corners (Q/E 90° steps)
  std::string           wallSubtype_     = "wall";   // wall variant id (mesh attach later)
  std::string           pillarSubtype_   = "pillar"; // pillar variant id

  // PaintOverlay tool state
  int                   overlayShape_    = 0;        // 0..11 (OverlayShapes.hpp)
  int                   overlayMaterial_ = 1;        // index into world::overlayMaterials() (1 = dirt_path)
  int                   overlayRotation_ = 0;        // 0..3 quarter-turns (Q/E)
  std::size_t           overlayHash_     = SIZE_MAX; // last-built overlayTiles signature

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

  // ---- World View (multi-chunk overworld manifest; EditorWorldView.cpp) ----
  // A world grid where each cell holds one 64×64 chunk map. Assign existing
  // map files (or create blank chunks) to cells, move/erase assignments, set
  // the world spawn, and open a cell's map for editing. While the open map is
  // assigned to a cell, its neighbors render as read-only ghosts for seam
  // authoring.
  void drawWorldView();
  void worldNewManifest();
  void worldOpenManifest();
  void worldSaveManifest();
  void worldOpenChunk(int cx, int cy);                 // focus a cell for editing (world mode)
  void worldAssignCell(int cx, int cy, const std::string& mapFile);
  void worldEraseCell(int cx, int cy);
  void worldEnsureManifestLoaded();                    // lazy one-time world.json autoload
  void worldDestroyThumbs();                           // free thumbnail GL textures
  GLuint worldThumbnail(const std::string& mapFile);   // cached per-mapFile texture
  std::filesystem::path worldDir() const;              // manifest directory
  shared::WorldChunkRef* worldCellAt(int cx, int cy);

  // ---- World editing mode ----
  // In world mode, map_ is the ASSEMBLED global world (all assigned chunks
  // merged); edits mark per-cell dirty and Ctrl+S slices each dirty cell back
  // to its file. Single-map mode (worldMode_ == false) edits one file as before.
  bool                       worldMode_   = false;
  int                        chunkSize_   = 64;
  std::set<CellKey>          assignedCells_;     // cells with map data
  std::set<CellKey>          dirtyCells_;        // cells edited since last save
  CellKey                    activeCell_  = {0, 0};   // focus cell (camera + draw ring centre)
  int                        editorDrawDistance_ = 2; // chunks rendered around active (persisted)

  void enterWorldMode(const std::string& manifestPath);   // assemble + switch to world mode
  void worldFocusCell(int cx, int cy);                    // recenter on a cell (no reload)
  void worldSaveDirtyChunks();                            // slice dirty cells -> files
  void markCellDirtyAtTile(int gx, int gy);               // tile/vertex edit -> owning cell(s)
  void markTerrainDirtyRegion(int x0, int y0, int x1, int y1);  // ChunkedTerrain dirty for a rect
  int  activeCenterTileX() const;                         // tile-space centre for the draw ring
  int  activeCenterTileY() const;

  shared::WorldManifest worldManifest_;
  std::string  worldManifestPath_;        // empty = no world loaded
  bool         worldDirty_      = false;
  bool         showWorldView_   = false;
  bool         worldAutoloaded_ = false;  // tried loading public/maps/world.json once
  float        worldOffX_ = 0.0f, worldOffY_ = 0.0f;
  float        worldZoom_ = 96.0f;        // pixels per world cell (32..256)
  int          worldDragCx_ = INT_MIN, worldDragCy_ = INT_MIN;  // drag-move source cell
  std::unordered_map<std::string, GLuint> worldThumbs_;  // mapFile → 64×64 texture (0 = failed)

  // ---- Database editor window
  void drawDatabaseWindow();
  void dbLoadAll();              // fetch all entities from server
  void dbDrawItemsTab();
  void dbDrawNPCsTab();
  void dbDrawObjectsTab();
  void dbDrawActionsTab();
  void dbDrawSkillsTab();

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

  // ---- Held-weapon grip preview (Items tab) ----
  // Renders the player model holding the edited item's equipped weapon so grip
  // pos/rot/scale can be tuned visually, then saved to the DB.
  world::SkinnedMesh          playerPreview_;          // player.glb for grip preview
  world::AttachmentRenderer   gripAttach_;             // draws the weapon at the grip
  bool                        playerPreviewTried_ = false;
  bool                        gripPreview_   = true;   // show in-hand vs spinning weapon
  int                         gripClipIndex_ = -1;     // player clip used in the preview
  // Manual orbit for the grip preview (drag on the image; no auto-spin).
  float                       gripYaw_       = 2.2f;   // azimuth (radians)
  float                       gripPitch_     = 0.20f;  // elevation (radians)
  bool                        gripDragging_  = false;
  void                        dbDrawGripPreview(const ItemDef& d, const glm::mat4& viewProj, float dt);

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
  std::vector<SkillDef>  dbSkills_;

  // Selected + edit copies
  int       dbSelItem_   = -1;
  int       dbSelNPC_    = -1;
  int       dbSelObject_ = -1;
  int       dbSelAction_ = -1;
  int       dbSelSkill_  = -1;
  ItemDef   dbEditItem_;
  NpcDef    dbEditNPC_;
  ObjectDef dbEditObject_;
  ActionDef dbEditAction_;
  SkillDef  dbEditSkill_;
  bool      dbEditIsNew_ = false;

  std::chrono::steady_clock::time_point lastFrameTime_{};
};

}  // namespace editor
