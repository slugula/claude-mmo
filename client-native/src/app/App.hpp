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
#include "render/Shader.hpp"
#include "render/ShadowMap.hpp"
#include "shared/SharedTypes.hpp"
#include "ui/MinimapRenderer.hpp"
#include "ui/Panels.hpp"
#include "ui/WorldOverlays.hpp"
#include "ui/XpTracker.hpp"
#include "world/EntityRenderer.hpp"
#include "world/AttachmentRenderer.hpp"
#include "world/ObstacleSystem.hpp"
#include "world/PoolRenderer.hpp"
#include "world/SkinnedMesh.hpp"
#include "world/WallSystem.hpp"
#include "world/SpriteCache.hpp"
#include "world/WaterRenderer.hpp"
#include "world/OverlayRenderer.hpp"
#include "world/SkyRenderer.hpp"
#include "world/ChunkedTerrain.hpp"

#include <glad/glad.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

struct ImFont;

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
                       const std::vector<editor::ActionDef>& actions,
                       const std::vector<editor::SkillDef>&  skills = {});
  void initHoverMesh();
  void destroyHoverMesh();
  void updateHoverMesh(int tx, int ty, int szX = 1, int szY = 1);

  void renderPlayer(const glm::mat4& viewProj, float dt);
  void processNetworkMessages();
  void drawWorldContextMenu();
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
  render::Shader                           terrainShader_;
  render::Shader                           wireframeShader_;
  render::Shader                           obstacleShader_;
  render::Shader                           skinnedShader_;
  render::Shader                           outlineMaskShader_;      // renders silhouette to mask FBO
  render::Shader                           outlineMaskSkinnedShader_; // skinned silhouette (animated objects)
  render::Shader                           outlineCompositeShader_; // composites border over scene
  render::Shader                           shadowInstancedShader_;
  render::Shader                           shadowSkinnedShader_;
  render::ShadowMap                        shadowMap_;
  world::ChunkedTerrain                    terrain_;        // per-chunk terrain meshes + draw ring
  world::ObstacleSystem                    obstacles_;
  world::PoolRenderer                      pools_;          // 3D water-pool tileset
  world::WallSystem                        walls_;
  world::WaterRenderer                     waterRenderer_;
  world::WaterUniforms                     waterUniforms_;
  world::OverlayRenderer                   overlayRenderer_;
  world::SkyRenderer                       sky_;
  bool                                     skyEnabled_ = true;
  char                                     skyCubemapBuf_[128] = {0};   // F12 import field
  // Sky-driven lighting (Phase 4), refreshed each frame from sky_ config.
  glm::vec3                                skyAmbientUp_{0.16f, 0.34f, 0.62f};
  glm::vec3                                skyAmbientDown_{0.30f, 0.30f, 0.34f};
  glm::vec3                                sunColor_{1.0f, 0.96f, 0.88f};
  ui::SpriteCache                          spriteCache_;
  world::SkinnedMesh                       playerModel_;
  world::EntityRenderer                    entities_;
  world::AttachmentRenderer                attachments_;   // equipped weapon meshes
  camera::GameCamera                       camera_;

  // DB entity definitions cached at startup (objects + actions) so picking,
  // the context menu, and rendering are data-driven rather than hardcoded.
  std::vector<editor::ObjectDef>           dbObjectDefs_;
  std::vector<editor::ActionDef>           dbActionDefs_;
  std::vector<editor::ItemDef>             dbItemDefs_;
  std::unordered_map<std::string, const editor::ItemDef*> itemDefById_;  // points into dbItemDefs_

  // If the object on tile (tx,ty) is a production facility, return the verb
  // (craft action display name, e.g. "Prepare"/"Cook"); otherwise empty.
  std::string facilityVerbAt(int tx, int ty) const;

  // Draw the player's equipped weapon (equipped["rightHand"]) attached to the
  // hand socket. Call immediately after that player's skinned render (so the
  // shared SkinnedMesh pose/modelSpace_ is still valid).
  void drawEquippedWeapon(const shared::PlayerState& p,
                          const glm::mat4& playerModelMatrix,
                          const glm::mat4& viewProj);

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
    int   seenProduceTick  = -999;
    int   seenHitTick      = -999;
    bool  prevPickupActive = false;  // was pickupItemId non-empty last tick?
    bool  seeded           = false;  // baseline stamps set on first sight (no replay)
    // Crossfade between clip transitions (per remote player).
    int   prevClipIndex    = -1;
    float prevClipTime     = 0.0f;
    float blendTime        = 0.0f;
    float blendDur         = 0.0f;
  };
  std::unordered_map<std::string, shared::PlayerState> prevRemotePlayers_;
  std::unordered_map<std::string, shared::PlayerState> currRemotePlayers_;
  // Local-player level-up jingle: seed silently on first state, then play on
  // each rise of lastLevelUpTick (visual VFX is handled in WorldOverlays).
  int  seenLevelUpTickLocal_ = -999999;
  bool levelUpSeeded_        = false;
  // HiDPI UI scale: uiScale_ is the active factor (content scale or override);
  // uiScaleOverride_ is the persisted user value (0 = auto from the monitor).
  float uiScale_         = 1.0f;
  float uiScaleOverride_ = 0.0f;

  // ---- UI font preview (debug panel) -------------------------------------
  // Candidate fonts (bundled + user-dropped + a few system fonts) are all
  // loaded into the ImGui atlas at startup; switching is just a pointer swap.
  struct UiFontOption { std::string label; std::string path; float size; ImFont* font = nullptr; };
  std::vector<UiFontOption> fontOptions_;
  int    activeFontIndex_ = 0;
  float  uiFontScale_     = 1.0f;       // extra HUD text-size multiplier
  void   collectFontOptions_();         // populate fontOptions_ (paths only)
  void   applyActiveFont_();            // push the selected font to ImGui + Clay
  // Performance levers (persisted). Shadow map size applies live; MSAA at start.
  int   shadowMapSize_   = 4096;
  int   msaaSamples_     = 4;
  bool prevLocalFishing_     = false;   // fishTargetX presence last state
  bool fishStartPending_     = false;   // armed on spot-target, fires bloop on first roll
  double seenMiningXp_       = 0.0;     // for ore-break "success" SFX
  double seenFishingXp_      = 0.0;     // for splash "catch" SFX
  std::unordered_set<std::string> seenDepletedTrees_;   // for tree-fall SFX
  bool depletedTreesSeeded_  = false;
  // Per-chunk background music (from init): "cx,cy" -> song file.
  std::unordered_map<std::string, std::string> chunkMusic_;
  int         musicChunkSize_ = 0;
  std::string currentMusicFile_;   // song currently playing (avoids re-triggering)
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
  float                                    minimapTileRadius_ = 24.f;   // default fully zoomed out
  bool                                     cursorOverMinimap_ = false;  // geometric disc check, current frame
  ui::UiHoverState                         uiHover_;
  ui::ChatLog                              chatLog_;
  ui::WorldOverlays                        overlays_;
  ui::XpTracker                            xpTracker_;
  // Per-skill xp seen on the local player, to emit XP drops on increases.
  std::unordered_map<std::string, double>  seenSkillXp_;
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
  int                                      seenProduceTick_   = -999;
  int                                      seenEatTick_       = -999;
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
  // EMA of the real interval between received state snapshots. Node's 200ms
  // setInterval drifts (~205-210ms with spikes), so interpolating against a
  // fixed 200ms made the player freeze a few ms at every tile — a per-tile
  // jerk. Driving interpolation off the measured interval removes it.
  double                                   tickIntervalMs_ =
      static_cast<double>(shared::kTickDurationMs);
  // Shared interpolation fraction [0,1] from the prev→curr snapshot, used
  // identically by local + remote players, NPCs, shadows, minimap, overlays.
  float interpAlpha() const;
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
  bool                                     wireframe_       = false;
  // Draw distance in 64-tile render chunks around the player (debug panel
  // slider; persisted in settings.cfg).
  int                                      chunkDrawDistance_ = 2;
  // Entity sync (interest) radius in tiles, requested from the server via
  // setViewRadius (server clamps to its own max). Persisted in settings.cfg.
  int                                      viewRadius_        = 15;
  // True when the server streams terrain chunks (world.json present); the flat
  // map_ starts all-void and fills in from chunkData messages.
  bool                                     streaming_         = false;
  // Set when one or more chunkData messages arrived this frame; triggers a
  // single coalesced rebuild of the monolithic render systems after draining.
  bool                                     pendingChunkRebuild_ = false;
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
  float                                    shadowBias_      = 0.0008f;
  float                                    shadowHalfExtent_ = 40.0f;
  float                                    shadowSoftness_  = 3.0f;   // PCSS penumbra (texels)
  glm::vec3                                shadowCenter_{};           // shadow frustum focus (follows camera target)
  bool                                     imguiInited_     = false;

  // Debug UI layer toggles — visible in the Debug panel.
  // showImguiUi_ gates ImGui game panels (bank, chat); excludes the debug panel itself.
  // showClayUi_  gates the entire clayFrame() call.
  // showClayDebug_ toggles Clay's built-in debug overlay (F1).
  bool                                     showImguiUi_     = false;
  bool                                     showClayUi_      = true;
  bool                                     showClayDebug_   = false;
  // Debug panel hidden by default; F12 toggles it.
  bool                                     showDebugPanel_  = false;
  int                                      debugCategory_   = 0;   // left-list category
};

}  // namespace app
