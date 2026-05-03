import {
  Scene, MeshBuilder, StandardMaterial, Color3,
  Mesh, Vector3, DynamicTexture,
} from '@babylonjs/core';
import type { WorldState } from '../shared/types';
import { TILE_SIZE } from '../shared/constants';
import { seededRandom } from './WorldState';

export { createWorldState, findWalkableTileNear } from './WorldState';

const PX = 8; // canvas pixels per world tile in the ground texture

// ---- Helpers ----------------------------------------------------------------

/** Euclidean distance to nearest tree tile, capped at maxDist. */
function computeTreeDists(world: WorldState, maxDist: number): number[][] {
  return Array.from({ length: world.height }, (_, ty) =>
    Array.from({ length: world.width }, (_, tx) => {
      let minD = maxDist + 1;
      for (let dy = -maxDist; dy <= maxDist; dy++) {
        for (let dx = -maxDist; dx <= maxDist; dx++) {
          const nx = tx + dx, ny = ty + dy;
          if (nx < 0 || ny < 0 || nx >= world.width || ny >= world.height) continue;
          if (world.tiles[ny][nx].obstacle === 'tree') {
            const d = Math.sqrt(dx * dx + dy * dy);
            if (d < minD) minD = d;
          }
        }
      }
      return minD;
    })
  );
}

function lerpChannel(a: number, b: number, t: number): number {
  return Math.round(a + (b - a) * t);
}

// ---- Decoration source meshes -----------------------------------------------

/**
 * Builds a merged grass-blade cluster: 2 or 3 thin boxes angled outward,
 * baked into one mesh so instances share a single draw call.
 */
function buildGrassSource(name: string, scene: Scene, variant: number): Mesh {
  const rng   = seededRandom(variant * 1777 + 1);
  const count = variant === 0 ? 2 : 3;
  const blades: Mesh[] = [];

  for (let i = 0; i < count; i++) {
    const bw    = 0.028 + rng() * 0.012;
    const bh    = 0.12  + rng() * 0.08;
    const blade = MeshBuilder.CreateBox(`${name}-b${i}`, {
      width: bw, height: bh, depth: 0.022,
    }, scene);
    blade.position.y  = bh / 2;                            // base sits on ground
    blade.position.x  = (rng() - 0.5) * 0.16;
    blade.position.z  = (rng() - 0.5) * 0.16;
    blade.rotation.y  = (i / count) * Math.PI * 2 + rng() * 0.5;
    blade.rotation.z  = (rng() - 0.5) * 0.4;              // slight lean
    blades.push(blade);
  }

  // MergeMeshes bakes each blade's world transform into vertex data.
  // disposeSource=true, allow32BitIndices=true.
  const merged = Mesh.MergeMeshes(blades, true, true)!;
  merged.name        = name;
  merged.position.y  = -1000;  // hide source; instances are independent
  merged.isPickable  = false;
  merged.convertToFlatShadedMesh();
  return merged;
}

/**
 * Builds a merged pebble cluster: 2–3 small squished boxes at slight offsets.
 */
function buildPebbleSource(name: string, scene: Scene, variant: number): Mesh {
  const rng   = seededRandom(variant * 2337 + 99);
  const count = 2 + (variant % 2); // 2 or 3
  const stones: Mesh[] = [];

  for (let i = 0; i < count; i++) {
    const sw    = 0.08 + rng() * 0.05;
    const sh    = 0.04 + rng() * 0.03;
    const sd    = 0.07 + rng() * 0.05;
    const stone = MeshBuilder.CreateBox(`${name}-s${i}`, {
      width: sw, height: sh, depth: sd,
    }, scene);
    stone.position.x = (rng() - 0.5) * 0.22;
    stone.position.z = (rng() - 0.5) * 0.22;
    stone.position.y = sh / 2;
    stone.rotation.y = rng() * Math.PI;
    stones.push(stone);
  }

  const merged = Mesh.MergeMeshes(stones, true, true)!;
  merged.name        = name;
  merged.position.y  = -1000;
  merged.isPickable  = false;
  merged.convertToFlatShadedMesh();
  return merged;
}

// ---- Main -------------------------------------------------------------------

export function buildWorldMeshes(world: WorldState, scene: Scene): Mesh {
  const root = new Mesh('world-root', scene);

  // ---- Ground ---------------------------------------------------------------
  const ground = MeshBuilder.CreateGround('ground', {
    width:  world.width  * TILE_SIZE,
    height: world.height * TILE_SIZE,
    subdivisions: 1,
  }, scene);
  // Offset by -0.5 so each colored tile square is centered on the entity
  // position (integer tile coord) rather than starting at it.
  ground.position.x = world.width  / 2 - 0.5;
  ground.position.z = world.height / 2 - 0.5;
  ground.isPickable = true;
  ground.parent     = root;

  // Non-repeating texture: each world tile → PX×PX pixels, colored individually.
  const texW    = world.width  * PX;
  const texH    = world.height * PX;
  const gridTex = new DynamicTexture('grid-tex', { width: texW, height: texH }, scene, false);
  const ctx     = gridTex.getContext();

  // OSRS-style: tiles near trees transition from dark brownish-green → lighter grass green.
  const FAR_DIST    = 2;
  const treeDists   = computeTreeDists(world, FAR_DIST);

  // Full-green (far from trees) — slightly lighter than old flat colour
  const GRASS_R = 82, GRASS_G = 148, GRASS_B = 54;
  // Warm earth tone directly under trees — lighter so the transition is subtle
  const SHADE_R = 125, SHADE_G = 108, SHADE_B = 62;

  for (let ty = 0; ty < world.height; ty++) {
    for (let tx = 0; tx < world.width; tx++) {
      const dist = treeDists[ty][tx];
      const t    = Math.min(1, dist / FAR_DIST); // 0 = tree-adjacent, 1 = open field

      // Blend between shadow and grass colour
      let r = lerpChannel(SHADE_R, GRASS_R, t);
      let g = lerpChannel(SHADE_G, GRASS_G, t);
      let b = lerpChannel(SHADE_B, GRASS_B, t);

      // Per-tile micro-variation: ±6 brightness so the field isn't a flat wash
      const vrng   = seededRandom(tx * 997 + ty * 31 + 5555);
      const jitter = Math.round((vrng() - 0.5) * 12);
      r = Math.max(0, Math.min(255, r + jitter));
      g = Math.max(0, Math.min(255, g + jitter));
      b = Math.max(0, Math.min(255, b + jitter));

      const px = tx * PX;
      const py = ty * PX;
      ctx.fillStyle = `rgb(${r},${g},${b})`;
      ctx.fillRect(px, py, PX, PX);

    }
  }

  gridTex.update();
  gridTex.uScale = 1; // no tiling — one pixel block per tile
  gridTex.vScale = 1;

  const groundMat = new StandardMaterial('ground-mat', scene);
  groundMat.diffuseTexture = gridTex;
  groundMat.specularColor  = new Color3(0, 0, 0);
  ground.material          = groundMat;

  // ---- Shared materials -----------------------------------------------------
  const treeTrunkMat = new StandardMaterial('tree-trunk-mat', scene);
  treeTrunkMat.diffuseColor = new Color3(0.29, 0.18, 0.08);

  const treeCanopyMat = new StandardMaterial('tree-canopy-mat', scene);
  treeCanopyMat.diffuseColor = new Color3(0.1, 0.38, 0.07);

  const rockMat = new StandardMaterial('rock-mat', scene);
  rockMat.diffuseColor = new Color3(0.48, 0.46, 0.44);

  const grassBladeMat = new StandardMaterial('grass-blade-mat', scene);
  grassBladeMat.diffuseColor = new Color3(0.18, 0.50, 0.09);
  grassBladeMat.specularColor = new Color3(0, 0, 0);

  const pebbleMat = new StandardMaterial('pebble-mat', scene);
  pebbleMat.diffuseColor = new Color3(0.50, 0.48, 0.46);
  pebbleMat.specularColor = new Color3(0, 0, 0);

  // ---- Source meshes for instancing -----------------------------------------
  const sourceTrunk = MeshBuilder.CreateCylinder('tree-trunk-source', {
    height: 0.6, diameter: 0.18, tessellation: 6,
  }, scene);
  sourceTrunk.position.y = -1000;
  sourceTrunk.isPickable = false;
  sourceTrunk.material   = treeTrunkMat;
  sourceTrunk.convertToFlatShadedMesh();

  const sourceCanopy = MeshBuilder.CreateSphere('tree-canopy-source', {
    diameter: 0.72, segments: 4,
  }, scene);
  sourceCanopy.position.y = -1000;
  sourceCanopy.isPickable = false;
  sourceCanopy.material   = treeCanopyMat;
  sourceCanopy.convertToFlatShadedMesh();

  const sourceRock = MeshBuilder.CreateBox('rock-source', {
    width: 0.55, height: 0.32, depth: 0.48,
  }, scene);
  sourceRock.position.y = -1000;
  sourceRock.isPickable = false;
  sourceRock.material   = rockMat;
  sourceRock.convertToFlatShadedMesh();

  // Grass and pebble decoration — disabled for now (too noisy)
  // const grassSources = [0, 1, 2].map(v => { ... });
  // const pebbleSources = [0, 1].map(v => { ... });

  // ---- Instances per tile ---------------------------------------------------
  for (let y = 0; y < world.height; y++) {
    for (let x = 0; x < world.width; x++) {
      const tile = world.tiles[y][x];

      if (tile.obstacle === 'tree') {
        const trunk = sourceTrunk.createInstance(`tree-trunk-${x}-${y}`);
        trunk.position  = new Vector3(x, 0.3, y);
        trunk.isPickable = true;
        trunk.parent    = root;

        const canopy = sourceCanopy.createInstance(`tree-canopy-${x}-${y}`);
        canopy.position  = new Vector3(x, 0.9, y);
        canopy.isPickable = true;
        canopy.parent    = root;

      } else if (tile.obstacle === 'rock') {
        const rock = sourceRock.createInstance(`rock-${x}-${y}`);
        rock.position   = new Vector3(x, 0.16, y);
        rock.rotation.y = seededRandom(x * 100 + y)() * Math.PI;
        rock.isPickable = true;
        rock.parent     = root;

      }
    }
  }

  return root;
}
