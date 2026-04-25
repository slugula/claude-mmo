# Artist Asset Swap Guide

### The short answer: not quite plug-and-play, but close

Every visual in the game is currently **procedural** — meshes built from primitives in code, textures drawn on `<canvas>`, sounds synthesized via Web Audio. An artist swap means replacing those code paths with file loaders. The architecture is ready for it; it just needs wiring per asset type.

---

## 3D Models — Player, NPCs, Trees, Rocks

**Recommended format: GLB (binary GLTF 2.0)**
- Single file (textures embedded), wide tool support (Blender, Maya, 3ds Max all export it natively)
- Babylon.js loads it with one call and handles the right-hand → left-hand coordinate flip automatically
- Supports skeletal animation — walk cycles, attack animations, idle — referenced by clip name in code

**OBJ** works for static props with no animation. Avoid FBX — Babylon doesn't load it natively and it requires a converter step.

**Scale & pivot contract the artist needs to know:**
- 1 unit = 1 tile (roughly 1 metre)
- Pivot at foot level, bottom-center (Y = 0 at ground)
- Player/NPC standing height should be ~1 unit tall
- Trees sit at roughly 0.9 units tall; rocks at ~0.3 units

**What changes in code:**
`src/entities/Player.ts`, `src/entities/NPCEntity.ts` — replace `buildAvatar()` / `buildBody()` with `SceneLoader.ImportMeshAsync('model.glb')`. Animation clips (e.g. `"walk"`, `"idle"`, `"attack"`) get looked up by name.

`src/world/World.ts` — replace `buildTree()` and `buildRock()` with loaded GLB instances. The ground plane stays code-generated but its material texture swaps to a loaded PNG/KTX2.

**Mesh naming matters** — raycasting, hover highlights, and health bars all find meshes by name (e.g. `npc-{id}`, `tree-trunk-{x}-{y}`). Either keep those names on root meshes or do a small rename pass in code.

---

## 2D Sprites — Inventory Items

**Recommended format: PNG sprite sheet**
- One sheet per category (weapons, food, armor, misc) at a consistent cell size (e.g. 32×32 or 40×40 px)
- Transparent background

**What changes in code:**
`src/items/ItemSprites.ts` currently draws every item sprite procedurally on a `<canvas>`. Replace with a `drawImage()` call slicing from the sheet by `itemId`. The rest of the inventory UI (`InventoryUI.ts`, `EquipmentUI.ts`) already uses the canvas output — no changes needed there.

---

## Textures — Ground, UI Borders

**Recommended formats:**
- **PNG** — transparency, lossless, safe default
- **KTX2 / Basis Universal** — GPU-compressed, loads faster and uses less VRAM; Babylon.js has built-in support via `KhronosTextureContainer2`
- **JPEG** — fine for fully opaque ground/terrain tiles with no alpha

Ground texture currently tiled via `uScale`/`vScale` — a tileable PNG drops in directly.

---

## Audio

**Recommended formats: OGG + MP3 fallback**
- OGG is preferred (smaller, free codec); MP3 for Safari compatibility
- WAV for short UI sounds where quality matters and filesize is acceptable

**What changes in code:**
`src/audio/SoundEngine.ts` — replace each synthesized method (`playHit()`, `playStrike()`, `playEquip()`, `playUnequip()`) with `new Sound('name', 'path/to/file.ogg', scene, null, { autoplay: false })` and `.play()`. The call sites in `GameEngine.ts` don't change at all.

---

## Loading & Asset Pipeline

Add a `src/assets/` folder and a loader module. For production, run assets through:
- **Blender → GLB export** (File → Export → glTF 2.0, select Binary)
- **toktx or Basis Universal encoder** for KTX2 textures (optional but worth it)
- **ffmpeg** to convert WAV → OGG: `ffmpeg -i hit.wav -c:a libvorbis hit.ogg`

Vite (the current build tool) serves files from `/public/` at the root URL — drop assets in `public/assets/` and reference them as `/assets/model.glb`. No import statements needed for binary files.

---

## Summary of files an artist swap touches

| What | File | Change |
|---|---|---|
| Player model | `src/entities/Player.ts` | Replace `buildAvatar()` with GLB loader |
| NPC models | `src/entities/NPCEntity.ts` | Replace `buildBody()` with GLB loader |
| Trees / rocks | `src/world/World.ts` | Replace `buildTree()` / `buildRock()` with GLB instances |
| Ground texture | `src/world/World.ts` | Swap `DynamicTexture` for `new Texture(url)` |
| Item sprites | `src/items/ItemSprites.ts` | Swap procedural canvas draw for sprite sheet slice |
| Sounds | `src/audio/SoundEngine.ts` | Swap Web Audio nodes for `Sound` file loads |

Everything else — game logic, UI layout, networking, tick system — is untouched.
