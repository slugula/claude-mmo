# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Snook Online** — a browser-based MMO heavily inspired by Old School RuneScape. Long-term goal: surpass OSRS by overcoming its technical limitations with modern technology. Server-authoritative at **200ms ticks** (vs OSRS's 600ms limit).

For how OSRS systems work, use https://oldschool.runescape.wiki as the reference.
Feature documentation lives in `docs/Features/`.

---

## Dev Commands

**Client (Vite, hot-reload):** `npm run dev`

**Server (tsx watch, auto-restart):** `npm run server`

**Local PostgreSQL (Docker):** `docker compose up -d`

**Type-check (no emit):**
```
"C:\Program Files\nodejs\node.exe" ".\node_modules\typescript\bin\tsc" --noEmit
```
`npx` is not in PATH on this machine — always use the full node path above. Run after every change.

**Build:** `npm run build`

**Native client / level editor (PowerShell):**
```powershell
# Level editor only
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "client-native\build" --config Release --target level-editor

# Game client only
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "client-native\build" --config Release --target client-native

# Both
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "client-native\build" --config Release
```
Output: `client-native\build\Release\level-editor.exe` and `client-native\build\Release\client-native.exe`

No test suite. TypeScript type-checking is the primary correctness gate.

---

## Local Dev Setup

Two processes run side by side. PostgreSQL (Docker) must be up before starting the server.

**Server defaults** (no `.env` file needed locally — values are hard-coded as fallbacks):
- `DATABASE_URL` → `postgresql://postgres:dev@localhost:5432/mmo`
- `JWT_SECRET` → `dev-secret-change-in-production`
- `PORT` → `8080`

**Client defaults** (override via `.env.local`):
- `VITE_AUTH_URL` → `http://localhost:8080/auth`
- `VITE_WS_URL` → `ws://localhost:8080`

---

## Tech Stack

- **Babylon.js v7** — 3D rendering, left-handed coordinate system
- **TypeScript + Vite** — client; no framework, vanilla DOM for UI
- **Node.js + Express + `ws`** — game server; HTTP and WebSocket share one port (`server/index.ts`)
- **PostgreSQL + `pg`** — player account persistence
- **JWT + bcryptjs** — authentication
- **Web Audio API** — procedural audio synthesis, no audio files

---

## Architecture: Two Separate Processes

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

### Client (`src/`)
Rendering and input only — no game logic authority. Receives `ServerStatePatch` every 200ms and interpolates at 60fps.

```
src/shared/types.ts         ← Single source of truth for ALL types and the GameAction union
src/engine/GameEngine.ts    ← Wires scene, camera, input, network, UI; owns render loop
src/engine/TickSystem.ts    ← Pure processTick() — called server-side by GameLoop
src/engine/InputManager.ts  ← Pointer/keyboard → actions enqueued then sent via NetworkClient
src/engine/NetworkClient.ts ← WebSocket wrapper; connect/sendActions/onInit/onState/onClose
```

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

### Render Loop (60fps) — Client-Side

Interpolates between `prevState` and `currentState` using:
```ts
alpha = (now - lastServerTickTime) / TICK_DURATION_MS  // 0..1
```

**Per-tick event detection** inside the 60fps loop:
```ts
if (this.currentState.tick > this.lastHitTick) {
  this.lastHitTick = this.currentState.tick;
  // compare prevState vs currentState — fires exactly once per server tick
}
```
Used for hitsplats, sounds, equip/unequip detection, chop animations.

### Action Dispatch Flow

```
User gesture
  → InputManager.enqueue(action)
  → dispatch() → NetworkClient.sendActions() → WebSocket → server
  → GameLoop.enqueueActions() → processTick() at next 200ms tick
  → ServerStatePatch broadcast (interest-filtered per client)
  → NetworkClient.onState() → GameEngine updates prevState/currentState
  → GameUI.update(state) refreshes DOM panels
```

The `dispatch` function is created in `GameEngine` and threaded into `InputManager` and all UI components.

---

## UI Architecture

All UI is **DOM overlay** (`position: fixed`) over the canvas. No Babylon.js GUI.

- **`src/ui/LoginUI.ts`** — login/register screen shown before WS connects; `showWithError(msg)` re-shows with error pre-filled
- **`src/ui/GameUI.ts`** — root UI; owns the tab bar (Inv | Skills | Equip)
- **`src/ui/ChatLog.ts`** — singleton; yellow = system (`ChatLog.log()`), white = player chat (`ChatLog.chat()`)
- **`src/ui/OverheadChat.ts`** — floating chat bubble, driven by `player.chatMessage` / `player.chatMessageTick`
- **`src/ui/HitSplatManager.ts`** — DOM hit splats, world-projected each frame
- **`src/ui/HealthBarManager.ts`** — DOM health bars above NPCs, world-projected each frame
- **`src/ui/ItemTooltip.ts`** — singleton tooltip on inventory/equipment hover

**World→screen projection** (all DOM overlays use this):
```ts
Vector3.Project(
  new Vector3(worldX, worldY, worldZ),
  Matrix.Identity(),
  scene.getTransformMatrix(),
  scene.activeCamera!.viewport.toGlobal(engine.getRenderWidth(), engine.getRenderHeight())
)
```

For overlays that follow the player, use `this.player.worldPosition` (interpolated mesh position), not `player.tileX / player.tileY`.

---

## Babylon.js Conventions

### Coordinate System
Left-handed. Tile `(x, y)` → `Vector3(x, 0, y)`. Y is up. Player root at Y=0; overhead chat at Y=1.05.

### Mesh Naming — Critical for Raycasting and HighlightLayer
| Entity | Mesh name(s) |
|---|---|
| Ground | `ground` |
| Tree | `tree-trunk-{x}-{y}`, `tree-canopy-{x}-{y}` (InstancedMesh) |
| Rock | `rock-{x}-{y}` (InstancedMesh) |
| NPC | `npc-root-{id}` (no geometry, not pickable) + `npc-{id}` (hitbox) + visual children |
| Dropped item | `item-{droppedItemId}` (root/hitbox) + visual children |
| Player | root = `player-root`, **renderingGroupId = 1** (draws after world at group 0) |

### InstancedMesh (Trees and Rocks)
Trees and rocks use `InstancedMesh` for performance — one draw call per type. Source meshes are hidden at `y = -1000` with `isPickable = false`. **Each instance must explicitly set `isPickable = true`** after `createInstance()` — instances do not inherit the default `true` when the source has `isPickable = false`. Without this, clicks fall through to the ground tile and nothing is dispatched.

`HighlightLayer` is incompatible with `InstancedMesh`. Invisible proxy `Mesh` objects (`hl-trunk`, `hl-canopy`, `hl-rock` in `GameEngine`) are repositioned over the hovered instance each frame to produce the glow.

### HighlightLayer
`hoverHighlight` adds meshes by name. Skip any mesh where `visibility < 0.01` or `getTotalVertices() === 0` to avoid Babylon errors.

---

## Equipment Slot Layout

5×3 CSS grid, human-shaped (`EquipmentUI.ts`):
```
[ —          head       —         ]
[ —          neck       ammo      ]
[ rightHand  body       leftHand  ]   ← Main Hand (right), Off-hand (left)
[ —          legs       —         ]
[ hands      feet       ring      ]
```

---

## Skill Icons

Icons live in `public/icons/skills/{skillId}.png` (served at `/icons/skills/`). The `ICON_IDS` set in `SkillsUI.ts` controls which skills show an image vs a colored placeholder square. Add the PNG first, then add the skill ID to `ICON_IDS`.

---

## Audio

`src/audio/SoundEngine.ts` — fully procedural Web Audio API, no external files. `AudioContext` resumes on first `pointerdown`/`keydown` (browser autoplay policy). Methods: `playHit()`, `playStrike()`, `playEquip()`, `playUnequip()`. All triggered inside the tick-gate block in `GameEngine` by comparing `prevState` vs `currentState`.

---

## Code Practices

- **No mutation** — game systems return new state objects via spread (`{ ...player, field: value }`). Never mutate the input state.
- **Types first** — add new actions to the `GameAction` discriminated union in `types.ts` before implementing. Add new player/NPC fields to their interfaces there too.
- **System order matters** — do not reorder the five systems in `TickSystem.ts` without understanding the cascade.
- **DOM menus** — context menus and slot menus are created fresh on each invocation and removed on dismiss. They are not persistent components.
- **After changes** — check imports, type vs value exports, and missing wires; then run the TypeScript type-check. Use Claude in Chrome tools to check the browser console for runtime errors where possible.
