#pragma once

#include "app/Settings.hpp"
#include "app/Window.hpp"
#include "audio/AudioEngine.hpp"
#include "camera/GameCamera.hpp"
#include "editor/EntityDefs.hpp"
#include "input/Picker.hpp"
#include "net/NetworkClient.hpp"
#include "render/Mesh.hpp"
#include "render/MsaaFramebuffer.hpp"
#include "render/PostFx.hpp"
#include "render/Shader.hpp"
#include "render/ShadowMap.hpp"
#include "shared/SharedTypes.hpp"
#include "ui/MinimapRenderer.hpp"
#include "ui/Panels.hpp"
#include "ui/WorldOverlays.hpp"
#include "world/EntityRenderer.hpp"
#include "world/ObstacleSystem.hpp"
#include "world/SkinnedMesh.hpp"
#include "world/SpriteCache.hpp"
#include "world/WaterRenderer.hpp"

#include <glad/glad.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>

namespace app {

// Top-level application object. Owns the window, GL context, MSAA target,
// scene resources, camera, picker, and drives the render loop.
class App {
public:
  App() = default;
  ~App();

  App(const App&)            = delete;
  App& operator=(const App&) = delete;

  bool init();
  int  run();

private:
  void renderFrame();
  void initImGui();
  void shutdownImGui();
  void onResize(int width, int height);

  void generateAndBuildTerrain();
  void rebuildWorldFromMap();   // (re)build terrain/obstacles/minimap/water from map_
  // Apply entity defs (names, models, sprites, object defs) from any source.
  void applyEntityDefs(const std::vector<editor::NpcDef>&    npcs,
                       const std::vector<editor::ItemDef>&   items,
                       const std::vector<editor::ObjectDef>& objects,
                       const std::vector<editor::ActionDef>& actions);
  void initHoverMesh();
  void destroyHoverMesh();
  void updateHoverMesh(int tx, int ty, int szX = 1, int szY = 1);

  void renderPlayer(const glm::mat4& viewProj, float dt);
  void processNetworkMessages();
  void drawWorldContextMenu();
  void exportWorldMap();
  void saveSettings();
  void loadSettings();

  // Allocate / reallocate the R8 mask texture + FBO used by the screen-space
  // outline composite. Called on init and on every window resize.
  void initOutlineMaskFbo(int w, int h);
  // Returns true if cursor world position should be sampled for a click
  // action (i.e. a real terrain tile, not an ImGui-owned area).
  bool drawLoginUi();
  void drawJoinModal();

  Window                                   window_;
  std::unique_ptr<render::MsaaFramebuffer> msaa_;
  render::PostFx                           postfx_;
  render::PostFxParams                     postParams_;
  render::Shader                           terrainShader_;
  render::Shader                           wireframeShader_;
  render::Shader                           obstacleShader_;
  render::Shader                           skinnedShader_;
  render::Shader                           outlineShader_;          // (kept, unused after SS-outline)
  render::Shader                           outlineMaskShader_;      // renders silhouette to mask FBO
  render::Shader                           outlineMaskSkinnedShader_; // skinned silhouette (animated objects)
  render::Shader                           outlineCompositeShader_; // composites border over scene
  render::Shader                           shadowInstancedShader_;
  render::Shader                           shadowSkinnedShader_;
  render::ShadowMap                        shadowMap_;
  render::Mesh                             terrainMesh_;
  world::ObstacleSystem                    obstacles_;
  world::WaterRenderer                     waterRenderer_;
  world::WaterUniforms                     waterUniforms_;
  ui::SpriteCache                          spriteCache_;
  world::SkinnedMesh                       playerModel_;
  world::EntityRenderer                    entities_;
  camera::GameCamera                       camera_;

  // DB entity definitions cached at startup (objects + actions) so picking,
  // the context menu, and rendering are data-driven rather than hardcoded.
  std::vector<editor::ObjectDef>           dbObjectDefs_;
  std::vector<editor::ActionDef>           dbActionDefs_;
  std::vector<editor::ItemDef>             dbItemDefs_;

  // Hover indicator — a small dynamic VAO/VBO holding 4 vertices drawn as
  // GL_LINE_LOOP, repositioned each frame to outline the currently
  // hovered tile.
  GLuint                                   hoverVao_     = 0;
  GLuint                                   hoverVbo_     = 0;

  // Screen-space outline mask FBO: a single R8 texture rendered into a
  // colour-only FBO.  Rebuilt on every window resize.  Used by the
  // outline_mask → outline_composite two-pass screen-space outline system.
  GLuint                                   outlineMaskFbo_  = 0;
  GLuint                                   outlineMaskTex_  = 0;
  // Empty VAO used for the fullscreen-triangle composite draw (no buffers).
  GLuint                                   outlineQuadVao_  = 0;

  // Networking
  net::NetworkClient                       network_;
  std::optional<shared::PlayerState>       currLocalPlayer_;
  std::optional<shared::PlayerState>       prevLocalPlayer_;
  // All players this tick (local + remote); kept for chat + future overlay
  // expansion. The map is replaced wholesale on each StateMessage.
  std::unordered_map<std::string, shared::PlayerState> allPlayers_;
  // Per-remote-player interpolation + animation state (prev/curr snapshots
  // for smooth position lerp, independent animation clip/time).
  struct RemoteAnim {
    int   clipIndex = -1;   // index into playerModel_ animations
    float clipTime  = 0.0f;
    float yaw       = 0.0f; // smoothed yaw
    // One-shot animation override (mirrors local oneShotClip_ / oneShotEndsAt_).
    // While oneShotEndsAt is in the future, the one-shot clip overrides clipForPlayer.
    std::string                           oneShotClip;
    std::chrono::steady_clock::time_point oneShotEndsAt{};
    // Tick stamps to detect per-tick action events for this remote player.
    int   seenAttackTick   = -999;
    int   seenChopTick     = -999;
    int   seenMineTick     = -999;
    int   seenFishTick     = -999;
    int   seenHitTick      = -999;
    bool  prevPickupActive = false;  // was pickupItemId non-empty last tick?
  };
  std::unordered_map<std::string, shared::PlayerState> prevRemotePlayers_;
  std::unordered_map<std::string, shared::PlayerState> currRemotePlayers_;
  std::unordered_map<std::string, RemoteAnim>          remoteAnims_;
  std::vector<shared::NPCState>            npcs_;
  std::vector<shared::DroppedItemState>    droppedItems_;
  // "x-y" keys of depleted resource nodes (from the server patch). When this
  // changes, the obstacle instances are rebuilt so depleted tiles swap to their
  // depleted-model variant. unordered_set is included via <unordered_map> deps.
  std::unordered_set<std::string>          depletedTiles_;
  // Per-id previous + current NPC snapshots for Phase 10 interpolation.
  // Rebuilt every state tick; rendered with a lerp in renderFrame.
  std::unordered_map<std::string, shared::NPCState> prevNpcs_;
  std::unordered_map<std::string, shared::NPCState> currNpcs_;
  // Smoothed player yaw — eases toward the facing-derived target each
  // frame so 90-degree turns don't pop.
  float                                    smoothedPlayerYaw_ = 0.0f;
  bool                                     smoothedYawValid_  = false;
  // Per-NPC smoothed yaw keyed by NPC id. Same shortest-arc lerp as the
  // player; entries are added on first sight and pruned when the NPC leaves.
  std::unordered_map<std::string, float>   npcSmoothedYaw_;
  ui::MinimapRenderer                      minimap_;
  float                                    minimapTileRadius_ = 12.f;
  bool                                     cursorOverMinimap_ = false;  // geometric disc check, current frame
  ui::UiHoverState                         uiHover_;
  ui::ChatLog                              chatLog_;
  ui::WorldOverlays                        overlays_;
  bool                                     loginAnnounced_ = false;
  bool                                     bankOpen_       = false;
  // Chest the player walked toward to bank: the bank opens once the
  // server-authoritative position is adjacent. -1 = no pending bank.
  int                                      pendingBankTileX_ = -1;
  int                                      pendingBankTileY_ = -1;
  net::Connection                          lastNetStatus_  = net::Connection::Disconnected;
  // Phase 5e — one-shot player animations. While `oneShotEndsAt_` is in
  // the future, clipForPlayer returns `oneShotClip_` regardless of the
  // movement state. Triggered when lastAttackTick / lastChopTick rises.
  int                                      seenAttackTick_    = -999;
  int                                      seenChopTick_      = -999;
  int                                      seenMineTick_      = -999;
  int                                      seenFishTick_      = -999;
  int                                      seenHitTick_       = -999;
  bool                                     prevPickupActive_  = false; // was pickupItemId non-empty last tick?
  // Per-equip-slot snapshot for detecting equip/unequip events vs the
  // previous PlayerState. Key = equip slot id, value = itemId; missing
  // entries are unequipped.
  std::unordered_map<std::string, std::string> seenEquipped_;
  std::chrono::steady_clock::time_point    oneShotEndsAt_{};
  std::string                              oneShotClip_;
  audio::AudioEngine                       audio_;
  std::chrono::steady_clock::time_point    lastTickTime_{};
  int                                      currentTick_       = 0;
  char                                     loginUser_[64]     = {};
  char                                     loginPass_[64]     = {};
  char                                     loginHost_[64]     = "localhost";
  int                                      loginPort_         = 8080;
  bool                                     loginRegisterMode_ = false;  // false=Login, true=Register
  bool                                     isNewPlayer_       = false;
  char                                     joinNameBuf_[21]   = {};

  std::chrono::steady_clock::time_point    lastFrameTime_{};
  shared::WorldMapFile                     map_;
  // Pass 1 result: terrain tile under the cursor (Möller–Trumbore heightfield pick).
  // Never overridden by entity AABBs — always the raw ground tile.
  // Drives the tile-outline hover square only.
  input::PickResult                        hoveredTile_;
  // Pass 2 result: entity whose AABB the cursor ray actually intersects.
  // Drives outline silhouette, context info, tooltip, left-click, right-click.
  // Kind::None when cursor is over bare terrain or off-world.
  struct HoveredEntity {
    enum class Kind { None, Obstacle, Npc, DroppedItem, RemotePlayer } kind = Kind::None;
    int         tileX = 0;
    int         tileY = 0;
    std::string id;    // npc.id / item.id / player-id; empty for obstacles
    float       rayT  = 0.0f;
  };
  HoveredEntity                            hoveredEntity_;
  // Convenience alias — always equals hoveredEntity_.id when kind==RemotePlayer.
  std::string                              hoveredPlayerId_;
  // Phase 8b-ii — right-click world context menu. We latch the picked tile
  // when the menu is requested so the menu's labels match what was under
  // the cursor at the moment of the click, not whatever the cursor moves
  // over while the menu is open.
  bool                                     ctxMenuRequest_ = false;
  bool                                     ctxMenuTileHit_ = false;
  int                                      ctxMenuTileX_   = 0;
  int                                      ctxMenuTileY_   = 0;
  // Latched remote player id for context menu (valid when ctxMenuRequest_ + ctxMenuPlayerId_ non-empty).
  std::string                              ctxMenuPlayerId_;

  // Deferred world left-click: set in onMouseButton (during pollEvents, where guards
  // are stale), dispatched in renderFrame after clayFrame() refreshes the guards.
  bool                                     pendingWorldLeftClick_ = false;
  float                                    pendingWorldClickX_    = 0.f;
  float                                    pendingWorldClickY_    = 0.f;

  // Click feedback marker — animated expanding circle at cursor pos.
  bool                                     clickFeedbackActive_ = false;
  std::chrono::steady_clock::time_point    clickFeedbackTime_{};
  float                                    clickFeedbackX_  = 0.0f;
  float                                    clickFeedbackY_  = 0.0f;
  int                                      clickFeedbackColor_ = 0; // 0=yellow, 1=red

  uint32_t                                 mapSeed_         = 42;
  float                                    noiseFreq_       = 0.04f;
  float                                    noiseAmp_        = 1.0f;
  int                                      terrainTileW_    = 0;
  int                                      terrainTileH_    = 0;
  int                                      terrainIndexCt_  = 0;
  bool                                     wireframe_       = false;
  // Screen-space outline settings
  float     outlineRadius_    = 3.0f;
  float     outlineDepthBias_ = 0.002f;
  glm::vec4 outlineColor_     = {0.0f, 0.9f, 0.9f, 0.95f};
  glm::vec4 hoverTileColor_   = {1.0f, 0.85f, 0.10f, 1.0f};

  // Fog
  bool                                     fogEnabled_  = false;
  float                                    fogDensity_  = 0.015f;
  float                                    fogStart_    = 5.0f;
  glm::vec3                                fogColor_    = {0.58f, 0.67f, 0.78f};
  // AO
  bool                                     aoEnabled_   = true;
  float                                    aoStrength_  = 0.50f;

  // Phase 7 — HSL palette quantization (per-fragment).
  bool                                     palette_         = true;
  int                                      paletteHues_     = 64;
  int                                      paletteSats_     = 16;
  int                                      paletteLums_     = 48;
  // Phase 6 — directional lighting. Sun is stored as yaw (0..360°, around
  // world +Y) and pitch (0..90° below horizontal); converted to a unit
  // light-travel-direction each frame.
  bool                                     lightingEnabled_ = true;
  float                                    sunYawDeg_       = 200.0f;
  float                                    sunPitchDeg_     = 58.0f;
  float                                    ambient_         = 0.45f;
  float                                    diffuse_         = 0.55f;
  // Phase 6b — shadow map.
  bool                                     shadowsEnabled_  = true;
  float                                    shadowDarkness_  = 0.55f;
  float                                    shadowBias_      = 0.0025f;
  float                                    shadowHalfExtent_ = 40.0f;
  bool                                     imguiInited_     = false;

  // Debug UI layer toggles — visible in the Debug panel.
  // showImguiUi_ gates ImGui game panels (bank, chat); excludes the debug panel itself.
  // showClayUi_  gates the entire clayFrame() call.
  // showClayDebug_ toggles Clay's built-in debug overlay (F1).
  bool                                     showImguiUi_     = false;
  bool                                     showClayUi_      = true;
  bool                                     showClayDebug_   = false;
  // Debug panel visibility — hidden by default in PRODUCTION_BUILD; F12 toggles it.
#ifdef PRODUCTION_BUILD
  bool                                     showDebugPanel_  = false;
#else
  bool                                     showDebugPanel_  = true;
#endif
};

}  // namespace app
