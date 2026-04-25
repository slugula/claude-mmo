# CLAUDE.md
Browser-based game heavily inspired by Old School RuneScape (OSRS). Initial scope is an offline playable prototype, but architecture must support adding MMO/server features later. 

The project scope is currently an offline playable prototype, but architecture must support adding MMO/server features later. 

Long-term goal: surpass OSRS by overcoming its technical limitations with modern technology.

## Documentation

Access all project and feature documentation here: C:\Users\alexa\Documents\Claude Games\OSRS Prototype\docs

Initial Prototype scope accessible here:  C:\Users\alexa\Documents\Claude Games\OSRS Prototype\docs\Features\Prototype Scope.md

If prompted to investigate how OldSchool RuneScape works use: https://oldschool.runescape.wiki as a reference.

## Major Project Goals
1. Create an OSRS "engine" built with modern technologies — overcome OSRS's technical limitations
2. Nail the OSRS gameplay feel — an OSRS player should migrate effortlessly
3. Server-authoritative game tick system — OSRS uses 600ms ticks (a known limitation); this project targets 200ms ticks (5/sec) (See: https://oldschool.runescape.wiki/w/Game_tick)
4. Core OSRS game systems: Skills, XP/Leveling, Grid-based movement, Mouse-based input, 28-slot inventory, Items
    1. Skills (See: https://oldschool.runescape.wiki/w/Skills)
    2. Experience and Leveling (See: https://oldschool.runescape.wiki/w/Experience)
    3. Grid-based movement (See: https://oldschool.runescape.wiki/w/Game_square)
    4. Mouse-based Input (See: https://oldschool.runescape.wiki/w/Clickbox)
    4. A static 28-slot Inventory systems (See: https://oldschool.runescape.wiki/w/Inventory)
    5. Items (See: https://oldschool.runescape.wiki/w/Items)

## Testing

- After writing/editing code, check for obvious errors (imports, type vs value exports, missing wires) before declaring done. 
- Use the Claude in Chrome tools to check the console for runtime errors if possible. At minimum, re-read the changed files and trace the import graph for issues.

---

## Dev Commands

**Dev server:** `npm run dev` (Vite, hot-reload)

**Type-check (no emit):**
```
"C:\Program Files\nodejs\node.exe" ".\node_modules\typescript\bin\tsc" --noEmit
```
`npx` is not in PATH on this machine — always use the full node path above.

**Build:** `npm run build`

No test suite exists. TypeScript type-checking is the primary correctness gate. Always run it after changes.

---

## Tech Stack

- **Babylon.js v7** — 3D rendering (left-handed coordinate system)
- **TypeScript + Vite** — no framework, vanilla DOM for UI
- **Web Audio API** — procedural audio synthesis, no audio files
- **No backend** — fully offline prototype; architecture must not preclude adding a server later

---

## Core Architecture

### Pure State Machine

`GameState` is a fully immutable, serializable object. Every game tick produces a **brand-new state** via pure functions — nothing mutates state in place. This is the most important architectural invariant. All game logic lives in pure system functions under `src/systems/`.

```
src/shared/types.ts       ← single source of truth for ALL types
src/engine/TickSystem.ts  ← drives state forward every 200ms
src/engine/GameEngine.ts  ← wires everything; owns the render loop
src/engine/InputManager.ts ← collects player input into an action queue
```

### Tick System (200ms)

Each tick, `TickSystem` drains `InputManager.pendingActions[]` and pipes the same `actions` array through five pure systems **in this exact order**:

1. `processCombat` — attack resolution, damage, death, XP
2. `processInteractions` — talk, dialogue
3. `processMovement` — path following, MOVE_TO overrides combat/talk
4. `processNPCs` / `processRespawns` — NPC AI, wander, respawn queue
5. `processItems` — inventory, equip/unequip, drop, pick up, chat

No system mutates the `actions` array. Each system receives the player state output from the previous one. After all systems, `onTick(prev, next)` fires and `GameUI.update(next)` refreshes the UI.

### Render Loop (60fps)

`GameEngine.startRenderLoop()` runs at 60fps and interpolates visuals between ticks using:
```ts
alpha = (now - lastTickTime) / TICK_DURATION_MS  // 0..1
```

The loop holds both `prevState` and `currentState`. Entity positions are lerped between the two for smooth movement even though the game logic only advances at 200ms.

**Per-tick event detection** — to fire once per tick inside the 60fps loop:
```ts
if (this.currentState.tick > this.lastHitTick) {
  this.lastHitTick = this.currentState.tick;
  // safe to compare prevState vs currentState here
}
```
Used for hitsplats, sounds, equip/unequip detection.

### Action Dispatch Flow

```
User gesture
  → InputManager.enqueue(action)
  → TickSystem.drainActions() at next 200ms tick
  → pure system functions produce new GameState
  → GameEngine.setState() updates prevState/currentState
  → GameUI.update(state) refreshes DOM panels
```

All dispatch paths — inventory clicks, equipment clicks, chat input — funnel through the same `dispatch = (action) => inputManager.enqueue(action)` function created in `GameEngine` and passed down to all UI components.

---

## UI Architecture

All UI is **DOM overlay** (fixed `position: fixed` HTML elements projected over the canvas). There is no Babylon.js GUI.

- **`src/ui/GameUI.ts`** — root UI, owns the tab bar (Inv | Skills | Equip). Calls `ChatLog.init(dispatch)` — must be constructed after `InputManager`.
- **`src/ui/ChatLog.ts`** — singleton. Yellow = system messages (`ChatLog.log()`), white = player chat (`ChatLog.chat()`). Keyboard listener on `window`.
- **`src/ui/OverheadChat.ts`** — floating chat bubble above player, driven by `player.chatMessage` / `player.chatMessageTick` in state.
- **`src/ui/HitSplatManager.ts`** — DOM hit splats, world-projected each frame.
- **`src/ui/HealthBarManager.ts`** — DOM health bars above NPCs, world-projected each frame.
- **`src/ui/ItemTooltip.ts`** — singleton tooltip, shown on inventory/equipment hover.

**World→screen projection** (used by all DOM overlays):
```ts
Vector3.Project(
  new Vector3(worldX, worldY, worldZ),
  Matrix.Identity(),
  scene.getTransformMatrix(),
  scene.activeCamera!.viewport.toGlobal(engine.getRenderWidth(), engine.getRenderHeight())
)
```

For overlays that follow the player, use `this.player.worldPosition` (the interpolated mesh position), not `player.tileX / player.tileY`.

---

## Babylon.js Conventions

### Coordinate System
Left-handed. World tiles map 1:1 to Babylon X/Z (tile `32,32` → `Vector3(32, y, 32)`). Y is up. Player root sits at Y=0; overhead chat is placed at Y=1.05.

### Mesh Naming — critical for raycasting and HighlightLayer
| Entity | Mesh name(s) |
|---|---|
| Ground | `ground` |
| Tree | `tree-trunk-{x}-{y}`, `tree-canopy-{x}-{y}` |
| Rock | `rock-{x}-{y}` |
| NPC | `npc-root-{id}` (no geometry, not pickable) + `npc-{id}` (invisible hitbox) + visual children |
| Dropped item | `item-{droppedItemId}` (root/hitbox) + visual children |
| Player | root = `player-root`, **renderingGroupId = 1** (draws after all world objects at group 0) |

Tree/rock meshes share a `world-root` parent — never highlight the parent or you'll light up the entire world.

### HighlightLayer
`hoverHighlight` in `GameEngine` adds individual meshes by exact name. Skip meshes where `visibility < 0.01` or `getTotalVertices() === 0` to avoid Babylon errors.

---

## Equipment Slot Layout

5×3 CSS grid, human-shaped. Slot order matters for the `GRID` constant in `EquipmentUI.ts`:
```
[ —          head       —         ]
[ —          neck       ammo      ]
[ rightHand  body       leftHand  ]   ← Main Hand (right), Off-hand (left)
[ —          legs       —         ]
[ hands      feet       ring      ]
```
Grid CSS: `repeat(3, calc((100% - 9px) / 4))` — same formula as the 4-column inventory so slot sizes match.

---

## Audio

`src/audio/SoundEngine.ts` — fully procedural Web Audio API, no external files. `AudioContext` starts suspended and resumes on first `pointerdown` or `keydown` (browser autoplay policy). Four public methods: `playHit()`, `playStrike()`, `playEquip()`, `playUnequip()`. All are triggered inside the tick-gate block in `GameEngine` by comparing `prevState` vs `currentState`.

---

## Code Practices

- **No mutation** — all game systems return new state objects via spread (`{ ...player, field: value }`). Never mutate the state passed in.
- **No external assets** — sounds are synthesised with Web Audio API; item sprites are drawn procedurally on `<canvas>` in `src/items/ItemSprites.ts`.
- **Types first** — add new actions to the `GameAction` discriminated union in `types.ts` before implementing them. Add new player/NPC fields to the relevant interface there too.
- **System order matters** — `processMovement` runs after `processCombat` intentionally: a MOVE_TO action cancels combat by clearing `attackTargetId`. Do not reorder systems without understanding the cascade.
- **DOM menus** — right-click context menus and slot menus are created fresh on each invocation and removed on dismiss. They are not persistent components.