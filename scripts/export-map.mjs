/**
 * export-map.mjs
 *
 * Usage: npm run export-map
 *
 * 1. Calls Aseprite CLI to export each layer of T_World_Map.aseprite as a
 *    separate PNG into public/maps/layers/.
 * 2. Reads the per-layer PNGs with pngjs and samples the center pixel of
 *    each 3×3 tile cell.
 * 3. Writes public/maps/worldMap.json — the authoritative tile map consumed
 *    by the server at startup and sent to clients on connect.
 *
 * Layer processing order (later layers win on the same property):
 *   Land      → groundColor + base type (grass / cliff)
 *   Water     → type=water, walkable=false
 *   Walls     → type=wall (or door if #d95763), walkable determined by type
 *   Objects   → obstacle=chest if #df7126
 *   Trees     → obstacle=tree, walkable=false, blocksRanged=true
 *   Rocks     → obstacle=rock, walkable=false, blocksRanged=false
 *   Fishing Spots → obstacle=fishing_spot, walkable=false
 */

import { execSync }   from 'child_process';
import { readFileSync, writeFileSync, mkdirSync, rmSync, readdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { PNG } from 'pngjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT      = join(__dirname, '..');

const ASEPRITE   = 'D:\\Games\\steamapps\\common\\Aseprite\\Aseprite.exe';
const SOURCE     = join(ROOT, 'src', 'assets', 'maps', 'T_World_Map.aseprite');
// Fall back to Desktop path if not yet copied into the project
const SOURCE_ALT = 'C:\\Users\\alexa\\Desktop\\Art\\mmo\\T_World_Map.aseprite';
const LAYERS_DIR = join(ROOT, 'public', 'maps', 'layers');
const OUT_JSON   = join(ROOT, 'public', 'maps', 'worldMap.json');

const TILE_PX    = 3;   // pixels per tile in the source image
const GRID_W     = 64;
const GRID_H     = 64;

// ---- Special colors (lowercase hex, no alpha) ----
const COLOR_CLIFFSIDE = '#45283c';
const COLOR_DOORWAY   = '#d95763';
const COLOR_CHEST     = '#df7126';
const COLOR_WATER     = '#cbdbfc';

// ---- Helpers ----

function toHex(r, g, b) {
  return '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0')).join('');
}

function colorsMatch(r, g, b, hex) {
  const ref = parseInt(hex.slice(1), 16);
  const rr = (ref >> 16) & 0xff;
  const rg = (ref >>  8) & 0xff;
  const rb =  ref        & 0xff;
  return r === rr && g === rg && b === rb;
}

/** Returns { r, g, b, a } for the center pixel of tile (tx, ty). */
function sampleCenter(png, tx, ty) {
  const px = tx * TILE_PX + 1;
  const py = ty * TILE_PX + 1;
  const idx = (py * png.width + px) * 4;
  return {
    r: png.data[idx],
    g: png.data[idx + 1],
    b: png.data[idx + 2],
    a: png.data[idx + 3],
  };
}

/** Returns true if ANY of the 9 pixels in the tile cell has alpha > 0. */
function tileHasPixel(png, tx, ty) {
  const ox = tx * TILE_PX;
  const oy = ty * TILE_PX;
  for (let dy = 0; dy < TILE_PX; dy++) {
    for (let dx = 0; dx < TILE_PX; dx++) {
      const idx = ((oy + dy) * png.width + (ox + dx)) * 4;
      if (png.data[idx + 3] > 0) return true;
    }
  }
  return false;
}

function loadPNG(path) {
  const buf = readFileSync(path);
  return PNG.sync.read(buf);
}

// ---- Step 1: Export layers via Aseprite CLI ----

let sourceFile = SOURCE;
try { readFileSync(SOURCE); } catch { sourceFile = SOURCE_ALT; }

console.log(`[export-map] Source: ${sourceFile}`);
mkdirSync(LAYERS_DIR, { recursive: true });

// Delete existing layer PNGs before export so stale files don't survive when
// Aseprite skips writing empty/transparent layers.
try {
  for (const f of readdirSync(LAYERS_DIR)) {
    if (f.endsWith('.png')) rmSync(join(LAYERS_DIR, f));
  }
} catch { /* dir was empty or missing — fine */ }

const aseCmd = `"${ASEPRITE}" -b "${sourceFile}" --save-as "${LAYERS_DIR}\\{layer}.png"`;
console.log(`[export-map] Running Aseprite CLI…`);
try {
  execSync(aseCmd, { stdio: 'inherit' });
} catch (err) {
  console.error('[export-map] Aseprite export failed:', err.message);
  process.exit(1);
}
console.log(`[export-map] Layers exported to ${LAYERS_DIR}`);

// ---- Step 2: Load each layer PNG ----

const layerNames = ['Land', 'Water', 'Walls', 'Objects', 'Trees', 'Rocks', 'Fishing Spots'];
const layers = {};
for (const name of layerNames) {
  const path = join(LAYERS_DIR, `${name}.png`);
  try {
    layers[name] = loadPNG(path);
    console.log(`[export-map] Loaded layer: ${name} (${layers[name].width}×${layers[name].height})`);
  } catch {
    console.warn(`[export-map] Warning: layer PNG not found — ${path}`);
    layers[name] = null;
  }
}

// ---- Step 3: Build tile map ----

const tiles = [];

for (let ty = 0; ty < GRID_H; ty++) {
  const row = [];
  for (let tx = 0; tx < GRID_W; tx++) {

    let walkable     = true;
    let type         = 'grass';
    let obstacle     = 'none';
    let blocksRanged = false;
    let groundColor  = '#7ec850';   // fallback grass green

    // Process layers top → bottom.  Each layer can claim a tile; Water is last
    // and only fills tiles that no other layer has claimed.
    // Fishing Spots are the exception — they sit ON water, so they don't count
    // as "ground content" and do not prevent the Water layer from applying.

    let hasGroundContent = false;

    // 1. Fishing Spots (topmost) — marks obstacle but leaves type/walkable for Water to set
    if (layers['Fishing Spots'] && tileHasPixel(layers['Fishing Spots'], tx, ty)) {
      obstacle     = 'fishing_spot';
      walkable     = false;
      blocksRanged = false;
      // hasGroundContent intentionally NOT set — water still applies beneath
    }

    // 2. Rocks
    if (layers['Rocks'] && tileHasPixel(layers['Rocks'], tx, ty)) {
      obstacle         = 'rock';
      walkable         = false;
      blocksRanged     = false;
      hasGroundContent = true;
    }

    // 3. Trees
    if (layers['Trees'] && tileHasPixel(layers['Trees'], tx, ty)) {
      obstacle         = 'tree';
      walkable         = false;
      blocksRanged     = true;
      hasGroundContent = true;
    }

    // 4. Objects — chest
    if (layers['Objects'] && tileHasPixel(layers['Objects'], tx, ty)) {
      const { r, g, b } = sampleCenter(layers['Objects'], tx, ty);
      if (colorsMatch(r, g, b, COLOR_CHEST)) {
        obstacle     = 'chest';
        walkable     = false;
        blocksRanged = true;
      }
      hasGroundContent = true;
    }

    // 5. Walls — wall or doorway
    if (layers['Walls'] && tileHasPixel(layers['Walls'], tx, ty)) {
      const { r, g, b } = sampleCenter(layers['Walls'], tx, ty);
      if (colorsMatch(r, g, b, COLOR_DOORWAY)) {
        type         = 'door';
        walkable     = true;
        blocksRanged = false;
      } else {
        type         = 'wall';
        walkable     = false;
        blocksRanged = true;
      }
      hasGroundContent = true;
    }

    // 6. Land — ground color and cliff detection.
    // Ignore #cbdbfc (canvas background) — it is not painted ground.
    if (layers['Land']) {
      const { r, g, b, a } = sampleCenter(layers['Land'], tx, ty);
      if (a > 0 && !colorsMatch(r, g, b, COLOR_WATER)) {
        groundColor      = toHex(r, g, b);
        hasGroundContent = true;
        if (colorsMatch(r, g, b, COLOR_CLIFFSIDE)) {
          type         = 'cliff';
          walkable     = false;
          blocksRanged = true;
        }
      }
    }

    // 7. Water (bottommost) — flood-fills every tile not claimed by a ground layer.
    // Fishing spots are the one case that coexists with water.
    if (!hasGroundContent) {
      type        = 'water';
      walkable    = false;
      blocksRanged = false;
      groundColor = COLOR_WATER;
    }

    row.push({ x: tx, y: ty, walkable, type, obstacle, blocksRanged, groundColor });
  }
  tiles.push(row);
}

// ---- Step 3b: Build per-pixel terrain map (192×192) ----
//
// Each entry is a packed 32-bit integer:
//   (typeCode * 2^24) | (r * 2^16) | (g * 2^8) | b
//
// Type codes:
//   0 = grass / generic ground
//   1 = water
//   2 = cliff
//   3 = wall
//   4 = door
//
// Pixel classification uses the same layer priority as the tile map:
// Rocks/Trees/Objects/Walls/Land set ground content; water fills the rest.
// Color is sampled from the Land layer at this exact pixel (full fidelity).

const TYPE_GRASS = 0, TYPE_WATER = 1, TYPE_CLIFF = 2, TYPE_WALL = 3, TYPE_DOOR = 4;

const PW = GRID_W * TILE_PX; // 192
const PH = GRID_H * TILE_PX; // 192

const pixelsBuf = new Array(PW * PH);

for (let py = 0; py < PH; py++) {
  for (let px = 0; px < PW; px++) {
    const idx = (py * PW + px) * 4;

    // Default: grass green
    let typeCode = TYPE_GRASS;
    let pr = 0x7e, pg = 0xc8, pb = 0x50;

    // 1. Sample Land layer for color and cliff detection
    if (layers['Land']) {
      const la = layers['Land'].data[idx + 3];
      const lr = layers['Land'].data[idx];
      const lg = layers['Land'].data[idx + 1];
      const lb = layers['Land'].data[idx + 2];
      if (la > 0 && !colorsMatch(lr, lg, lb, COLOR_WATER)) {
        pr = lr; pg = lg; pb = lb;
        if (colorsMatch(lr, lg, lb, COLOR_CLIFFSIDE)) typeCode = TYPE_CLIFF;
      }
    }

    // 2. Walls layer overrides type (but keep Land color for ground under walls)
    if (layers['Walls'] && layers['Walls'].data[idx + 3] > 0) {
      const wr = layers['Walls'].data[idx];
      const wg = layers['Walls'].data[idx + 1];
      const wb = layers['Walls'].data[idx + 2];
      typeCode = colorsMatch(wr, wg, wb, COLOR_DOORWAY) ? TYPE_DOOR : TYPE_WALL;
    }

    // 3. Determine if this pixel has any ground content (to decide water)
    let hasGroundPixel = false;
    for (const layerName of ['Rocks', 'Trees', 'Objects', 'Walls']) {
      if (layers[layerName] && layers[layerName].data[idx + 3] > 0) {
        hasGroundPixel = true;
        break;
      }
    }
    if (!hasGroundPixel && layers['Land']) {
      const la = layers['Land'].data[idx + 3];
      const lr = layers['Land'].data[idx];
      const lg = layers['Land'].data[idx + 1];
      const lb = layers['Land'].data[idx + 2];
      if (la > 0 && !colorsMatch(lr, lg, lb, COLOR_WATER)) hasGroundPixel = true;
    }

    if (!hasGroundPixel) {
      typeCode = TYPE_WATER;
      pr = 0xcb; pg = 0xdb; pb = 0xfc;
    }

    // Pack: multiply avoids signed-integer issues with bitwise shift
    pixelsBuf[py * PW + px] = (typeCode * 16777216) + (pr * 65536) + (pg * 256) + pb;
  }
}

// ---- Flip Y axis ----
// The image is authored with row 0 at the top (north).  In Babylon.js the camera
// faces +Z, so Z=0 appears at the near (bottom) edge of the screen — meaning the
// image's top would render at the far (south) end.  Reversing the rows makes the
// image layout match what the player sees from above.
tiles.reverse();
for (let ty = 0; ty < tiles.length; ty++) {
  for (let tx = 0; tx < tiles[ty].length; tx++) {
    tiles[ty][tx].y = ty;
  }
}

// Flip pixels the same way as tiles (reverse row order)
const pixels = new Array(PW * PH);
for (let py = 0; py < PH; py++) {
  const srcRow = PH - 1 - py;
  for (let px = 0; px < PW; px++) {
    pixels[py * PW + px] = pixelsBuf[srcRow * PW + px];
  }
}

// ---- Step 4: Write JSON ----

const output = { width: GRID_W, height: GRID_H, tiles, pixelWidth: PW, pixelHeight: PH, pixels };
writeFileSync(OUT_JSON, JSON.stringify(output), 'utf-8');
console.log(`[export-map] Written: ${OUT_JSON} (${GRID_W}×${GRID_H} tiles, ${PW}×${PH} pixels)`);
