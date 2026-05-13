import {
  Scene, MeshBuilder, StandardMaterial, Color3,
  Mesh, Vector3, VertexData, VertexBuffer,
  ShaderMaterial, Effect,
} from '@babylonjs/core';

// ---- Unlit terrain shader (OSRS-style: pure vertex color interpolation, no lighting) ----
//
// The GPU bilinearly interpolates between the 4 corner vertex colors of every quad.
// Each corner is shared between up to 4 tiles, so blended colors produce seamless
// gradients across tile boundaries with zero texture overhead.

Effect.ShadersStore['terrainVertexShader'] = /* glsl */`
  attribute vec3 position;
  attribute vec4 color;
  uniform mat4 worldViewProjection;
  varying vec4 vColor;
  void main() {
    gl_Position = worldViewProjection * vec4(position, 1.0);
    vColor = color;
  }
`;

Effect.ShadersStore['terrainFragmentShader'] = /* glsl */`
  precision highp float;
  varying vec4 vColor;
  void main() {
    gl_FragColor = vColor;
  }
`;
import type { TileData, WorldState } from '../shared/types';
import { TILE_SIZE } from '../shared/constants';

export { createWorldFromTiles, findWalkableTileNear } from './WorldState';

// ---- Constants ---------------------------------------------------------------
const WATER_Y              = -0.25;
export const MAX_TERRAIN_H =  4;     // 1.0 height value → 4 world units tall

// ---- Ground vertex colors (smooth color blending + obstacle AO) ---------------
//
// Each vertex sits at the corner of up to 4 tiles.  Its color is the average of
// those neighbours' groundColor values — producing the same smooth gradient that
// the old DynamicTexture + bilinear sampling gave, but now living on the mesh
// itself so it naturally follows the height-deformed surface.
//
// Simple ambient occlusion: vertices adjacent to obstacle tiles (trees, rocks,
// walls) are darkened proportionally — each obstacle neighbour reduces brightness
// by ~15%, capped at 50% dark.

function hexToRgb01(hex: string): [number, number, number] {
  const h = hex.replace('#', '');
  return [
    parseInt(h.slice(0, 2), 16) / 255,
    parseInt(h.slice(2, 4), 16) / 255,
    parseInt(h.slice(4, 6), 16) / 255,
  ];
}

function buildGroundVertexColors(tiles: TileData[][], W: number, H: number): Float32Array {
  const colors = new Float32Array((W + 1) * (H + 1) * 4);

  for (let row = 0; row <= H; row++) {
    for (let col = 0; col <= W; col++) {
      const idx = (row * (W + 1) + col) * 4;

      let r = 0, g = 0, b = 0, count = 0, obstacleNeighbors = 0;

      for (const tx of [col - 1, col]) {
        for (const ty of [H - row - 1, H - row]) {
          if (tx < 0 || tx >= W || ty < 0 || ty >= H) continue;
          const tile = tiles[ty]?.[tx];
          if (!tile) continue;

          // Water has its own mesh — exclude it from terrain color averaging so its
          // blue groundColor doesn't bleed onto adjacent cliff/grass vertices.
          if (tile.type === 'water') continue;

          const [tr, tg, tb] = hexToRgb01(tile.groundColor);
          r += tr; g += tg; b += tb; count++;

          if (tile.obstacle !== 'none' || tile.type === 'wall') obstacleNeighbors++;
        }
      }

      if (count > 0) { r /= count; g /= count; b /= count; }

      // Obstacle AO: darken vertices near obstacles (trees/rocks cast ground shadow)
      const ao = Math.max(0.5, 1.0 - obstacleNeighbors * 0.15);
      colors[idx]     = r * ao;
      colors[idx + 1] = g * ao;
      colors[idx + 2] = b * ao;
      colors[idx + 3] = 1.0;
    }
  }

  return colors;
}

// ---- Terrain height deformation ----------------------------------------------
//
// Height is stored per-vertex in WorldState.vertexHeights (a flat Float32Array,
// length (W+1)*(H+1), indexed as row*(W+1)+col).  The GPU bilinearly interpolates
// between neighbouring corner values — tiles naturally slope into each other.
//
// Water vertices sink below WATER_Y regardless of stored height so cliffs adjacent
// to water don't show jagged edges above the water plane.

export function computeVertexHeight(world: WorldState, col: number, row: number): number {
  const W = world.width;
  const H = world.height;
  // Check whether all neighbouring tiles are water — if so, sink below water plane
  let allWater = true;
  for (const tx of [col - 1, col]) {
    for (const ty of [H - row - 1, H - row]) {
      if (tx < 0 || tx >= W || ty < 0 || ty >= H) continue;
      const tile = world.tiles[ty]?.[tx];
      if (tile && tile.type !== 'water') { allWater = false; break; }
    }
    if (!allWater) break;
  }
  if (allWater) return -0.5;
  return (world.vertexHeights[row * (W + 1) + col] ?? 0) * MAX_TERRAIN_H;
}

function applyHeightDeformation(mesh: Mesh, world: WorldState): void {
  const positions = mesh.getVerticesData(VertexBuffer.PositionKind);
  if (!positions) return;

  const W = world.width;
  const H = world.height;
  const vertsPerRow = W + 1;
  for (let row = 0; row <= H; row++) {
    for (let col = 0; col <= W; col++) {
      positions[(row * vertsPerRow + col) * 3 + 1] = computeVertexHeight(world, col, row);
    }
  }

  mesh.updateVerticesData(VertexBuffer.PositionKind, positions);
  mesh.createNormals(false);
}

// ---- Water plane (tile-by-tile quad mesh) ------------------------------------

function buildWaterPlane(world: WorldState, scene: Scene, root: Mesh): void {
  const positions: number[] = [];
  const indices:   number[] = [];

  for (let ty = 0; ty < world.height; ty++) {
    for (let tx = 0; tx < world.width; tx++) {
      if (world.tiles[ty][tx].type !== 'water') continue;

      const base = positions.length / 3;
      const x0 = tx - 0.5, x1 = tx + 0.5;
      const z0 = ty - 0.5, z1 = ty + 0.5;

      positions.push(x0, WATER_Y, z0);
      positions.push(x1, WATER_Y, z0);
      positions.push(x1, WATER_Y, z1);
      positions.push(x0, WATER_Y, z1);

      indices.push(base, base + 2, base + 1, base, base + 3, base + 2);
    }
  }

  if (indices.length === 0) return;

  const normals = new Float32Array(positions.length);
  VertexData.ComputeNormals(new Float32Array(positions), new Int32Array(indices), normals);

  const vd      = new VertexData();
  vd.positions  = new Float32Array(positions);
  vd.indices    = new Int32Array(indices);
  vd.normals    = normals;

  const mat = new StandardMaterial('water-mat', scene);
  mat.diffuseColor    = new Color3(0.18, 0.48, 0.90);
  mat.emissiveColor   = new Color3(0.03, 0.10, 0.28);
  mat.specularColor   = new Color3(0.30, 0.45, 0.75);
  mat.backFaceCulling = false;

  const waterMesh = new Mesh('water-surface', scene);
  vd.applyToMesh(waterMesh);
  waterMesh.material                 = mat;
  waterMesh.isPickable               = false;
  waterMesh.alwaysSelectAsActiveMesh = true;
  waterMesh.parent                   = root;

  scene.registerBeforeRender(() => {
    const t       = Date.now() / 1000;
    const shimmer = Math.sin(t * 1.7) * 0.038 + Math.sin(t * 2.9 + 1.1) * 0.024;
    mat.emissiveColor = new Color3(
      Math.max(0, 0.03 + shimmer * 0.35),
      Math.max(0, 0.10 + shimmer * 0.80),
      Math.max(0, 0.28 + shimmer * 1.60),
    );
  });
}

// ---- Main -------------------------------------------------------------------

export function buildWorldMeshes(world: WorldState, scene: Scene): Mesh {
  const root = new Mesh('world-root', scene);
  const W    = world.width;
  const H    = world.height;

  // ---- Ground terrain (height-deformed subdivided mesh) ----------------------
  const groundMat = new ShaderMaterial('ground-mat', scene, { vertex: 'terrain', fragment: 'terrain' }, {
    attributes: ['position', 'color'],
    uniforms:   ['worldViewProjection'],
  });

  const terrainMesh = MeshBuilder.CreateGround('ground-terrain', {
    width:        W * TILE_SIZE,
    height:       H * TILE_SIZE,
    subdivisions: W,
    updatable:    true,
  }, scene);
  terrainMesh.position.x   = W / 2 - 0.5;
  terrainMesh.position.z   = H / 2 - 0.5;
  terrainMesh.material     = groundMat;
  terrainMesh.isPickable   = false;
  terrainMesh.parent       = root;
  applyHeightDeformation(terrainMesh, world);
  terrainMesh.setVerticesData(VertexBuffer.ColorKind, buildGroundVertexColors(world.tiles, W, H), true);

  // Invisible pick-target — must match the visual terrain exactly so mouse rays
  // hit the right tile even on elevated terrain.
  const groundPick = MeshBuilder.CreateGround('ground', {
    width:        W * TILE_SIZE,
    height:       H * TILE_SIZE,
    subdivisions: W,
    updatable:    true,
  }, scene);
  groundPick.position.x = W / 2 - 0.5;
  groundPick.position.z = H / 2 - 0.5;
  groundPick.isPickable = true;
  groundPick.visibility = 0;
  groundPick.parent     = root;
  applyHeightDeformation(groundPick, world);

  // ---- Water plane -----------------------------------------------------------
  buildWaterPlane(world, scene, root);

  // ---- Materials -------------------------------------------------------------
  const treeTrunkMat = new StandardMaterial('tree-trunk-mat', scene);
  treeTrunkMat.diffuseColor = new Color3(0.29, 0.18, 0.08);

  const treeCanopyMat = new StandardMaterial('tree-canopy-mat', scene);
  treeCanopyMat.diffuseColor = new Color3(0.1, 0.38, 0.07);

  const rockMat = new StandardMaterial('rock-mat', scene);
  rockMat.diffuseColor = new Color3(0.48, 0.46, 0.44);

  const wallMat = new StandardMaterial('wall-mat', scene);
  wallMat.diffuseColor    = new Color3(0.85, 0.82, 0.76);
  wallMat.specularColor   = new Color3(0.1, 0.1, 0.1);
  wallMat.backFaceCulling = false;

  const chestMat = new StandardMaterial('chest-mat', scene);
  chestMat.diffuseColor = new Color3(0.95, 0.45, 0.05);

  // ---- Source meshes for instancing ------------------------------------------
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

  // ---- Tile objects + wall panels --------------------------------------------

  const WALL_H = 0.9;
  const WALL_T = 0.08;
  const WALL_Y = WALL_H / 2;

  const blocksPanel = (nx: number, ny: number): boolean => {
    if (nx < 0 || ny < 0 || nx >= world.width || ny >= world.height) return true;
    const t = world.tiles[ny][nx].type;
    return t === 'wall' || t === 'door';
  };

  const wallPanels: Mesh[] = [];

  for (let y = 0; y < world.height; y++) {
    for (let x = 0; x < world.width; x++) {
      const tile = world.tiles[y][x];
      // Average the 4 corner vertex heights — this matches what the renderer draws
      // at the tile centre (vertex averaging means tile.height * MAX_TERRAIN_H is wrong
      // at transition edges; the corners blend with neighbours).
      const tileBaseY = tile.type === 'water' ? WATER_Y : (
        computeVertexHeight(world, x,     H - y)     +
        computeVertexHeight(world, x + 1, H - y)     +
        computeVertexHeight(world, x + 1, H - y - 1) +
        computeVertexHeight(world, x,     H - y - 1)
      ) / 4;

      if (tile.obstacle === 'tree') {
        const trunk = sourceTrunk.createInstance(`tree-trunk-${x}-${y}`);
        trunk.position   = new Vector3(x, tileBaseY + 0.3, y);
        trunk.isPickable = true;
        trunk.parent     = root;

        const canopy = sourceCanopy.createInstance(`tree-canopy-${x}-${y}`);
        canopy.position   = new Vector3(x, tileBaseY + 0.9, y);
        canopy.isPickable = true;
        canopy.parent     = root;

      } else if (tile.obstacle === 'rock') {
        const rock = sourceRock.createInstance(`rock-${x}-${y}`);
        rock.position   = new Vector3(x, tileBaseY + 0.16, y);
        rock.rotation.y = ((x * 1234 + y * 5678) % 628) / 100;
        rock.isPickable = true;
        rock.parent     = root;

      } else if (tile.obstacle === 'chest') {
        const chest = MeshBuilder.CreateBox(`chest-${x}-${y}`, {
          width: 0.55, height: 0.55, depth: 0.4,
        }, scene);
        chest.position   = new Vector3(x, tileBaseY + 0.28, y);
        chest.material   = chestMat;
        chest.isPickable = true;
        chest.parent     = root;

      } else if (tile.type === 'wall') {
        if (!blocksPanel(x, y - 1)) {
          const p = MeshBuilder.CreateBox(`wp-n-${x}-${y}`, { width: 1, height: WALL_H, depth: WALL_T }, scene);
          p.position = new Vector3(x, tileBaseY + WALL_Y, y - 0.5);
          p.isPickable = false;
          wallPanels.push(p);
        }
        if (!blocksPanel(x, y + 1)) {
          const p = MeshBuilder.CreateBox(`wp-s-${x}-${y}`, { width: 1, height: WALL_H, depth: WALL_T }, scene);
          p.position = new Vector3(x, tileBaseY + WALL_Y, y + 0.5);
          p.isPickable = false;
          wallPanels.push(p);
        }
        if (!blocksPanel(x - 1, y)) {
          const p = MeshBuilder.CreateBox(`wp-w-${x}-${y}`, { width: WALL_T, height: WALL_H, depth: 1 }, scene);
          p.position = new Vector3(x - 0.5, tileBaseY + WALL_Y, y);
          p.isPickable = false;
          wallPanels.push(p);
        }
        if (!blocksPanel(x + 1, y)) {
          const p = MeshBuilder.CreateBox(`wp-e-${x}-${y}`, { width: WALL_T, height: WALL_H, depth: 1 }, scene);
          p.position = new Vector3(x + 0.5, tileBaseY + WALL_Y, y);
          p.isPickable = false;
          wallPanels.push(p);
        }
      }
    }
  }

  if (wallPanels.length > 0) {
    const wallMesh = Mesh.MergeMeshes(wallPanels, true, true);
    if (wallMesh) {
      wallMesh.name       = 'walls';
      wallMesh.material   = wallMat;
      wallMesh.isPickable = false;
      wallMesh.parent     = root;
    }
  }

  return root;
}
