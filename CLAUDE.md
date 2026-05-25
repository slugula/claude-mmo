# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Project L** — a server-authoritative MMO heavily inspired by Old School RuneScape. Long-term goal: surpass OSRS by overcoming its technical limitations with modern technology. Server-authoritative at **200ms ticks** (vs OSRS's 600ms limit).

For how OSRS systems work, use https://oldschool.runescape.wiki as the reference.
Feature documentation lives in `docs/Features/`.

---

## Dev Commands

**Server (tsx watch, auto-restart):** `npm run server`

**Local PostgreSQL (Docker):** `docker compose up -d`

**Type-check (no emit):**
```
"C:\Program Files\nodejs\node.exe" ".\node_modules\typescript\bin\tsc" --noEmit
```
`npx` is not in PATH on this machine — always use the full node path above. Run after every server-side change.

**Native client build (PowerShell):**
```powershell
# Game client only
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "client-native\build" --config Release --target client-native

# Both client + level editor
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "client-native\build" --config Release
```
Outputs: `client-native\build\Release\client-native.exe` and `client-native\build\Release\level-editor.exe`

No test suite. TypeScript type-checking is the primary correctness gate for server code.

---

## Local Dev Setup

Two processes: PostgreSQL (Docker) + Node server. The native client binary connects directly.

**Server defaults** (no `.env` file needed locally — values are hard-coded as fallbacks):
- `DATABASE_URL` → `postgresql://postgres:dev@localhost:5432/mmo`
- `JWT_SECRET` → `dev-secret-change-in-production`
- `PORT` → `8080`

---

## Tech Stack

### Client — Native C++ / OpenGL 4.6
- **C++20** with CMake ≥ 3.25 + vcpkg (manifest mode)
- **GLFW 3** — window + input
- **GLAD** (GL 4.6 Core) — GL loader
- **GLM** — math
- **Clay** — primary in-game UI layout (HUD, inventory, bank, chat, context menus, minimap, login)
- **Dear ImGui** (docking branch) — debug panel only; WorldOverlays (health bars, hit splats, chat bubbles) stay ImGui permanently
- **cgltf** — glTF model loading (player.glb, tree.gltf)
- **miniaudio** — procedural audio synthesis
- **IXWebSocket** — WebSocket client (mbedtls/bcrypt for TLS)
- **glaze** — JSON parsing (server state messages)
- **stb_image** — PNG loading for water normal map

### Server — Node.js / TypeScript
- **Node.js + Express + `ws`** — HTTP and WebSocket share one port (`server/index.ts`)
- **PostgreSQL + `pg`** — player account persistence
- **JWT + bcryptjs** — authentication

---

## Repository Layout

```
client-native/          ← Native C++/OpenGL game client + level editor (two executables)
  CMakeLists.txt        ← Defines game_core static lib + client-native + level-editor targets
  CMakePresets.json
  vcpkg.json
  src/                  ← C++ source (app, render, world, ui, audio, net, camera, input, editor, shared)
  shaders/              ← GLSL shaders (23 files)
  assets/               ← models (player.glb, tree.gltf), maps (worldMap.json)
  build/                ← CMake build output (gitignored)

server/                 ← Node.js/TypeScript authoritative game server
  index.ts
  GameLoop.ts
  auth/router.ts
  db/

src/shared/             ← TypeScript schema source of truth (types.ts, constants.ts)
src/systems/            ← Pure tick-based game systems (Combat, Movement, Items, etc.)
src/world/              ← Pathfinder, WorldState, Interactables
src/items/              ← ItemRegistry (loads src/data/items.json)
src/npcs/               ← NPCRegistry (loads src/data/npcs.json)
src/data/               ← items.json, npcs.json — entity definitions

tools/                  ← Utility scripts
scripts/                ← Build/export helpers
docs/                   ← Feature documentation (Obsidian vault)
public/                 ← Assets served to tooling
```

The browser client (`src/audio`, `src/engine`, `src/ui`, etc.) has been removed.
`npm run dev` no longer exists. `client-native.exe` is the only game client.
`src/editor/` contains the old browser-based level editor — it is orphaned and not connected to any active build target.

---

## Build Architecture — game_core Static Library

`CMakeLists.txt` compiles a `game_core` static library shared between both executables:

- **`game_core`** — rendering, world, audio, camera, input systems. No networking.
- **`client-native`** links `game_core` + IXWebSocket (networking, UI panels, App entry point)
- **`level-editor`** links `game_core` only (no network; separate EditorApp entry point in `src/editor/`)

Both POST_BUILD steps copy shaders and assets to their respective output directories.

---

## Server Architecture

### Server (`server/`)
The authoritative game world. Runs `GameLoop` which calls `processTick()` every 200ms.

```
server/index.ts               ← HTTP (Express /auth, /health) + WebSocket on same port
server/GameLoop.ts            ← Owns GameState; drives processTick(); periodic checkpoint saves
server/auth/router.ts         ← POST /auth/register, POST /auth/login → JWT (7d expiry)
server/db/client.ts           ← pg Pool singleton
server/db/PlayerRepository.ts ← findByUsername, create, save, updateLastLogin
server/db/schema.sql          ← players table (UUID PK, username, password_hash, player_state JSONB)
```

**Persistence:** Player state saves on disconnect and via periodic checkpoint every 60 seconds. On `addPlayer()` for a returning player, all transient tick-based fields must be reset to `-999` — if a stale `lastChopTick`/`lastAttackTick`/`lastHitTick` survives a server restart the tick counter resets to 0, making `tick - lastXxxTick` permanently negative and locking that skill action forever.

**Interest management:** The broadcast in `server/index.ts` filters each player's patch by Chebyshev distance (square area). View radius defaults to 15, max 25, configurable per client via `{ type: 'setViewRadius' }`. World tiles are NOT broadcast — the client generates the world locally from the same deterministic seed.

**WebSocket close codes:**
- `4001` — auth required / invalid token
- `4002` — duplicate session (account already connected)

### Data Registries
- `src/items/ItemRegistry.ts` — loads `src/data/items.json` at startup; `getItem(id)`, `getAllItems()`
- `src/npcs/NPCRegistry.ts` — loads `src/data/npcs.json`; provides NPC definitions (name, stats, drops table with rates, AI type)

---

## Core Architecture: Pure State Machine

`GameState` is fully immutable. Every tick produces a **brand-new state** via pure functions — nothing mutates state in place. All game logic lives in `src/systems/`.

### Tick System (200ms) — Runs Server-Side

`src/engine/TickSystem.ts` is the **dispatcher** (not a system itself). It pipes state through systems **in this exact order**:

1. `processCombat` — attack resolution, damage, death, XP
2. `processInteractions` — talk, dialogue
3. `processMovement` — path following; MOVE_TO clears `attackTargetId`, `chopTargetX/Y`, etc. Unwalkable destinations are re-routed to the nearest walkable tile via `findWalkableTileNear`.
4. `processItems` — inventory, equip/unequip, drop, pick up, chat
5. `processWoodcutting` — **global step** after all per-player systems; tree depletion/respawn is shared state

**Order matters.** Movement runs after Combat: MOVE_TO cancels combat. Woodcutting runs last because tree health is global.

---

## Schema Source of Truth

`src/shared/types.ts` and `src/shared/constants.ts` are the **canonical** definitions for all game types, actions, and constants.

The native client's `client-native/src/shared/SharedTypes.hpp` is a **hand-written C++ mirror** of these files. When adding new fields to the TypeScript types, also update the C++ mirror and the glaze JSON meta in `SharedTypesJson.hpp`.

**Skills:** `SkillId` = `warrior | defence | hitpoints | woodcutting | mining | gunner`. `VISIBLE_SKILLS` excludes `mining` — `MiningSystem` does not yet exist; `MINE_ROCK` actions currently go unhandled server-side.

---

## Native Client — Key Files

### Core App
| Path | Role |
|---|---|
| `client-native/src/app/App.{hpp,cpp}` | Top-level: render loop, input dispatch, all system wiring. Contains `showClayUi_`/`showImguiUi_` debug toggles. |
| `client-native/src/app/Window.{hpp,cpp}` | GLFW window + input callbacks |
| `client-native/src/camera/GameCamera.{hpp,cpp}` | ArcRotate camera (middle-mouse drag, wheel zoom). Exposes `cameraYaw()` for minimap rotation. |
| `client-native/src/input/Picker.{hpp,cpp}` | Ray-vs-heightfield terrain pick; AABB picks for obstacles/NPCs/items |
| `client-native/src/net/NetworkClient.{hpp,cpp}` | HTTP login + WebSocket + typed send helpers for all `GameAction` subtypes |

### Rendering
| Path | Role |
|---|---|
| `client-native/src/world/TerrainBuilder.{hpp,cpp}` | Welded-corner mesh + Lambert-baked vertex colors |
| `client-native/src/world/ObstacleSystem.{hpp,cpp}` | Trees (glTF) + rocks + chests + fences — instanced draw + screen-space outline |
| `client-native/src/world/WaterRenderer.{hpp,cpp}` | Animated water plane with dual normal maps, caustics, foam, screen-space reflection |
| `client-native/src/world/WaterMesh.{hpp,cpp}` | Builds water VAO/VBO/EBO with per-vertex `shore_weight` |
| `client-native/src/world/EntityRenderer.{hpp,cpp}` | Remote players, NPCs, dropped items (instanced procedural geometry) |
| `client-native/src/world/SkinnedMesh.{hpp,cpp}` | cgltf load + matrix-palette skinned player model (65 joints) |
| `client-native/src/world/SpriteCache.{hpp,cpp}` | Pre-renders item sprites to 32×32 GL textures; used by Clay inventory slots |
| `client-native/src/render/MsaaFramebuffer.{hpp,cpp}` | 4× MSAA FBO + depth resolve texture (required for water SSR) |
| `client-native/src/render/ShadowMap.{hpp,cpp}` | Directional shadow map (PCF 3×3, 2048×2048) |

### Clay UI (primary game UI)
| Path | Role |
|---|---|
| `client-native/src/ui/ClayRenderer.{hpp,cpp}` | Clay backend: translates Clay render commands to ImGui DrawList calls; tracks `s_clayOwned` (UI pointer ownership); exposes `clayIsPointerOverUI()`, `clayMinimapHovered()` |
| `client-native/src/ui/ClayHudPanel.{hpp,cpp}` | Main HUD layout (inventory / skills / equipment tabs); calls `clayHudBuildLayout()` + `clayHudHandleInput()` each frame |
| `client-native/src/ui/ClayBankPanel.{hpp,cpp}` | Bank interface modal |
| `client-native/src/ui/ClayChatLog.{hpp,cpp}` | Scrolling chat panel with inline input |
| `client-native/src/ui/ClayLoginModal.{hpp,cpp}` | Login + join dialogs (Clay layout, ImGui InputText for fields) |
| `client-native/src/ui/ClayContextMenu.{hpp,cpp}` | Right-click action menu (white verb · orange subject · yellow suffix) |
| `client-native/src/ui/ClayContextInfo.{hpp,cpp}` | Top-left hover context info (verb + subject + suffix) |
| `client-native/src/ui/ClayClickFeedback.{hpp,cpp}` | Animated click marker (yellow = walk, red = action) |
| `client-native/src/ui/ClayTooltip.{hpp,cpp}` | Cursor-following multi-line tooltip |
| `client-native/src/ui/MinimapRenderer.{hpp,cpp}` | Circular minimap: base texture from tile groundColors, composite FBO with yaw rotation, entity dots, gold border |

### ImGui (permanent)
| Path | Role |
|---|---|
| `client-native/src/ui/WorldOverlays.{hpp,cpp}` | Hit splats, health bars, overhead chat bubbles (3D-projected, stay ImGui permanently) |
| `client-native/src/ui/Panels.{hpp,cpp}` | Legacy ImGui HUD/Bank/Chat — active only when `showImguiUi_ && !showClayUi_` (debug fallback) |

### Shared / Misc
| Path | Role |
|---|---|
| `client-native/src/ui/NameRegistry.hpp` | Header-only map of entity IDs → display names |
| `client-native/src/audio/AudioEngine.{hpp,cpp}` | miniaudio synth: hit, strike, equip, unequip sounds |
| `client-native/src/shared/SharedTypes.hpp` | C++ mirror of `src/shared/types.ts` |
| `client-native/src/shared/SharedTypesJson.hpp` | Glaze JSON metas for string-backed enums |
| `client-native/src/world/MapGenerator.{hpp,cpp}` | Procedural world generation (Perlin-FBM, used by game client when no map file) |

### Level Editor (`client-native/src/editor/`)
Compiles to `level-editor.exe`. Shares `game_core`; no networking.
| Path | Role |
|---|---|
| `EditorApp.{hpp,cpp}` | Top-level editor: 3D viewport (MSAA FBO), 2D grid, toolbar, undo/redo, map load/save |
| `EditorTool.hpp` | `enum class EditorTool` (PaintTerrain, SculptHeight, PlaceObstacle, PlaceNpc, PlaceSpawn, PaintWalkability, Erase, PaintWater) |
| `UndoStack.{hpp,cpp}` | Snapshot-based undo (full WorldMapFile, max 50 entries) |
| `MinimapRenderer.{hpp,cpp}` | CPU-rendered 256×256 minimap for the editor viewport |
| `EditorPalette.hpp` | 32 OSRS-palette colour swatches |
| `EntityClient.hpp` | cpp-httplib wrapper for future DB CRUD calls |
| `EntityDefs.hpp` | C++ mirror structs for entity DB definitions |

---

## Clay UI Architecture

`clayFrame()` in `App.cpp` is called once per frame. It:
1. Calls `Clay_BeginLayout()` then each panel's build function
2. Calls `Clay_EndLayout()` — after this, `Clay_PointerOver()` is valid for the current frame
3. Updates `s_clayOwned` (pointer ownership) and `s_minimapHovered`
4. Calls `ClayRenderer::render()` which walks render commands and issues ImGui DrawList calls

**Pointer ownership (`claySteals`):** Computed in `renderFrame()` before picking runs. Combines `clayIsPointerOverUI()` (previous frame) with a direct geometric minimap disc check (current frame). When true, world picking, hover outlines, and right-click context menus are all suppressed.

**UI debug toggles** (debug panel only):
- `showClayUi_` (default `true`) — gates all Clay panels + `clayFrame()`
- `showImguiUi_` (default `false`) — gates legacy ImGui bank/chat fallback

---

## Shader Reference

| Shader | Purpose |
|---|---|
| `terrain.{vert,frag}` | Terrain mesh; Lambert lighting, shadow map PCF sampling, fog, HSL palette quantization, AO |
| `water.{vert,frag}` | Animated water; vertex displacement, dual scrolling normal maps, caustics, foam, screen-space reflection |
| `skinned.{vert,frag}` | Matrix-palette skinning for player model (65 joints, 80-joint max) |
| `obstacle.{vert,frag}` | Instanced trees/rocks/NPCs/items; flat-shaded procedural geometry |
| `wireframe.{vert,frag}` | Debug wireframe overlay |
| `outline.{vert,frag}` | Old per-object outline pass (loaded but superseded by screen-space system) |
| `outline_mask.{vert,frag}` | Renders hovered entity silhouette to R8 mask FBO |
| `outline_composite.{vert,frag}` | Composites dilated outline border over scene |
| `shadow_instanced.vert` | Shadow depth pass for instanced obstacles/NPCs |
| `shadow_skinned.vert` | Shadow depth pass for skinned player model |
| `shadow.frag` | Shared depth-only fragment shader |
| `minimap_composite.{vert,frag}` | Minimap FBO: rotates base texture by camera yaw, draws entity dots, circular mask + border |
| `preview.{vert,frag}` | Single-model preview FBO (level editor) |

---

## Settings Persistence

`App.cpp` calls `loadSettings()` on startup and `saveSettings()` when "Save as default" is clicked in the debug panel. Settings are written to `settings.cfg` next to the executable. Covers: fog, AO, lighting (sun yaw/pitch, ambient/diffuse), shadows (darkness/bias/half-extent), HSL palette quantization levels, outline radius/color, hover tile color, water uniforms.

---

## NetworkClient — Typed Send Helpers

`NetworkClient` provides typed wrappers for every `GameAction` the client can send:

| Method | Action |
|---|---|
| `sendMoveTo(x, y)` | `MOVE_TO` |
| `sendChopTree(x, y)` | `CHOP_TREE` |
| `sendAttackNpc(id)` | `ATTACK_NPC` |
| `sendTalkTo(id)` | `TALK_TO` |
| `sendDropItem(slot)` | `DROP_ITEM` |
| `sendMoveSlot(from, to)` | `MOVE_SLOT` |
| `sendEquipItem(slot)` | `EQUIP_ITEM` |
| `sendUnequipItem(slotId)` | `UNEQUIP_ITEM` |
| `sendTakeItem(id)` | `TAKE_ITEM` |
| `sendChat(msg)` | `CHAT` |
| `sendOpenBank()` / `sendCloseBank()` | `OPEN_BANK` / `CLOSE_BANK` |
| `sendDepositItem(id, qty)` | `DEPOSIT_ITEM` |
| `sendDepositAll()` / `sendDepositWorn()` | `DEPOSIT_ALL` / `DEPOSIT_WORN` |
| `sendWithdrawItem(id, qty)` | `WITHDRAW_ITEM` |
| `sendExamine(id)` | `EXAMINE` |
| `loginAndConnect(host, port, user, pass)` | HTTP POST /auth/login → WS connect |
| `registerAndConnect(host, port, user, pass)` | HTTP POST /auth/register → WS connect |

---

## Code Practices

- **Server: no mutation** — game systems return new state objects via spread (`{ ...player, field: value }`). Never mutate the input state.
- **Types first** — add new actions to the `GameAction` discriminated union in `src/shared/types.ts` before implementing. Mirror changes in `SharedTypes.hpp`.
- **System order matters** — do not reorder the five systems in `TickSystem.ts` without understanding the cascade.
- **After server changes** — run the TypeScript type-check; check server console for runtime errors.
- **After client changes** — rebuild `client-native` and run the exe; check the GL debug output in the console.
- **Clay vs ImGui** — new game UI goes in Clay. WorldOverlays (health bars, hit splats, bubbles) stay ImGui permanently — they're 3D-projected world-space elements where Clay provides no benefit.
- **Minimap center** — minimap panel uses `kMmMargin = 24px` from the top-right corner. Any code that computes the minimap disc center from screen coordinates must use the same margin: `centerX = fbW - 24 - kSize/2`, `centerY = 24 + kSize/2`.
