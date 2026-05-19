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
# Game client
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "client-native\build" --config Release --target client-native

# Both client + level editor
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "client-native\build" --config Release
```
Output: `client-native\build\Release\client-native.exe`

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
- **Dear ImGui** (docking branch) — all in-game UI
- **cgltf** — glTF model loading (player.glb, tree.gltf)
- **miniaudio** — procedural audio synthesis
- **IXWebSocket** — WebSocket client (mbedtls/bcrypt for TLS)
- **glaze** — JSON parsing (server state messages)

### Server — Node.js / TypeScript
- **Node.js + Express + `ws`** — HTTP and WebSocket share one port (`server/index.ts`)
- **PostgreSQL + `pg`** — player account persistence
- **JWT + bcryptjs** — authentication

---

## Repository Layout

```
client-native/          ← Native C++/OpenGL client (the only client)
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  src/                  ← C++ source (app, render, world, ui, audio, net, camera, input)
  shaders/              ← GLSL shaders
  assets/               ← models, maps
  build/                ← CMake build output (gitignored)

server/                 ← Node.js/TypeScript authoritative game server (UNCHANGED)
  index.ts
  GameLoop.ts
  auth/router.ts
  db/

src/shared/             ← TypeScript schema source of truth (types.ts, constants.ts)
src/editor/             ← Level editor (deferred — browser-based, revisit later)

tools/                  ← Utility scripts
scripts/                ← Build/export helpers
docs/                   ← Feature documentation
public/                 ← Assets served to the server or used by tooling
```

The browser client (`src/audio`, `src/engine`, `src/ui`, etc.) has been removed.
`npm run dev` no longer exists. `client-native.exe` is the only game client.

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

---

## Core Architecture: Pure State Machine

`GameState` is fully immutable. Every tick produces a **brand-new state** via pure functions — nothing mutates state in place. All game logic lives in `src/systems/`.

### Tick System (200ms) — Runs Server-Side

`processTick` pipes state through these systems **in this exact order**:

1. `processCombat` — attack resolution, damage, death, XP
2. `processInteractions` — talk, dialogue
3. `processMovement` — path following; MOVE_TO clears `attackTargetId`, `chopTargetX/Y`, etc.
4. `processItems` — inventory, equip/unequip, drop, pick up, chat
5. `processWoodcutting` — **global step** after all per-player systems; tree depletion/respawn is shared state

**Order matters.** Movement runs after Combat: MOVE_TO cancels combat. Woodcutting runs last because tree health is global.

---

## Schema Source of Truth

`src/shared/types.ts` and `src/shared/constants.ts` are the **canonical** definitions for all game types, actions, and constants.

The native client's `client-native/src/shared/SharedTypes.hpp` is a **hand-written C++ mirror** of these files. When adding new fields to the TypeScript types, also update the C++ mirror and the glaze JSON meta in `SharedTypesJson.hpp`.

---

## Native Client — Key Files

| Path | Role |
|---|---|
| `client-native/src/app/App.{hpp,cpp}` | Top-level: render loop, all system wiring |
| `client-native/src/app/Window.{hpp,cpp}` | GLFW window + input callbacks |
| `client-native/src/camera/GameCamera.{hpp,cpp}` | ArcRotate camera (middle-mouse, wheel zoom) |
| `client-native/src/input/Picker.{hpp,cpp}` | Ray-vs-heightfield + cylinder obstacle picking |
| `client-native/src/net/NetworkClient.{hpp,cpp}` | HTTP login + WebSocket + message queue |
| `client-native/src/world/MapGenerator.{hpp,cpp}` | Procedural world (Perlin-FBM, 64×64) |
| `client-native/src/world/TerrainBuilder.{hpp,cpp}` | Welded-corner mesh + Lambert-baked vertex colors |
| `client-native/src/world/ObstacleSystem.{hpp,cpp}` | Trees (glTF 1×1) + rocks + fences, instanced + stencil outline |
| `client-native/src/world/WaterRenderer.{hpp,cpp}` | Water plane + foam depth intersection |
| `client-native/src/world/EntityRenderer.{hpp,cpp}` | Remote players, NPCs, dropped items |
| `client-native/src/world/SkinnedMesh.{hpp,cpp}` | cgltf load + matrix-palette skinned player model |
| `client-native/src/render/MsaaFramebuffer.{hpp,cpp}` | 4× MSAA FBO + depth resolve texture for water |
| `client-native/src/render/ShadowMap.{hpp,cpp}` | Directional shadow map (PCF 3×3) |
| `client-native/src/ui/Panels.{hpp,cpp}` | ImGui panels: HUD, Bank, ChatLog |
| `client-native/src/ui/WorldOverlays.{hpp,cpp}` | Hit splats, health bars, overhead chat |
| `client-native/src/audio/AudioEngine.{hpp,cpp}` | miniaudio synth: hit, strike, equip, unequip |
| `client-native/src/shared/SharedTypes.hpp` | C++ mirror of `src/shared/types.ts` |
| `client-native/src/shared/SharedTypesJson.hpp` | Glaze JSON metas for string-backed enums |

---

## Code Practices

- **Server: no mutation** — game systems return new state objects via spread (`{ ...player, field: value }`). Never mutate the input state.
- **Types first** — add new actions to the `GameAction` discriminated union in `src/shared/types.ts` before implementing. Mirror changes in `SharedTypes.hpp`.
- **System order matters** — do not reorder the five systems in `TickSystem.ts` without understanding the cascade.
- **After server changes** — run the TypeScript type-check; check server console for runtime errors.
- **After client changes** — rebuild `client-native` and run the exe; check the GL debug output in the console.
