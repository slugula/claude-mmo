import {
  Scene, MeshBuilder, StandardMaterial, Color3,
  Mesh, Vector3, DynamicTexture, Texture, VertexData, VertexBuffer,
} from '@babylonjs/core';
import type { TileData, WorldState } from '../shared/types';
import { TILE_SIZE } from '../shared/constants';

export { createWorldFromTiles, findWalkableTileNear } from './WorldState';

// ---- Constants ---------------------------------------------------------------
const WATER_Y         = -0.25;
const MAX_TERRAIN_H   =  4;     // 1.0 height value → 4 world units tall

// ---- Ground texture (bilinear smooth color blending) -------------------------
//
// Each tile fills a 3×3 pixel block with its groundColor.  Babylon bilinear
// sampling blends between adjacent tile colors → soft gradients, no jagged edges.
//
// DynamicTexture has invertY=true: canvas y=0 → UV v=0 → world Z=max.
// Tile y=0 is world Z=0 (near camera), so we flip: canvasY = (H-1-ty)*3.

function buildGroundTexture(tiles: TileData[][], width: number, height: number, scene: Scene): DynamicTexture {
  const cW = width  * 3;
  const cH = height * 3;
  const tex = new DynamicTexture('grid-tex', { width: cW, height: cH }, scene, false);
  const ctx = tex.getContext();

  for (let ty = 0; ty < height; ty++) {
    const cy = (height - 1 - ty) * 3;
    for (let tx = 0; tx < width; tx++) {
      const cx   = tx * 3;
      const tile = tiles[ty]?.[tx];
      ctx.fillStyle = tile?.groundColor ?? '#7ec850';
      ctx.fillRect(cx, cy, 3, 3);
    }
  }

  tex.update();
  tex.uScale = 1;
  tex.vScale = 1;
  tex.updateSamplingMode(Texture.BILINEAR_SAMPLINGMODE);
  return tex;
}

// ---- Terrain height deformation ----------------------------------------------
//
// CreateGround(W, H, subdivisions=W) produces (W+1)×(H+1) vertices.
// Vertex layout: row 0 → z_local=+H/2 (world Z max), row H → z_local=-H/2 (world Z min).
// With mesh center at (W/2-0.5, 0, H/2-0.5):
//   world_X = col - 0.5  →  tile_x ≈ col
//   world_Z = 255.5 - row →  tile_y ≈ H - row  (clamped to [0,H-1])

function applyHeightDeformation(mesh: Mesh, tiles: TileData[][], W: number, H: number): void {
  const positions = mesh.getVerticesData(VertexBuffer.PositionKind);
  if (!positions) return;

  const vertsPerRow = W + 1;

  for (let row = 0; row <= H; row++) {
    for (let col = 0; col <= W; col++) {
      const idx  = (row * vertsPerRow + col) * 3;
      const tx   = Math.max(0, Math.min(W - 1, col));
      const ty   = Math.max(0, Math.min(H - 1, H - row));
      const tile = tiles[ty]?.[tx];

      let y: number;
      if (tile?.type === 'water') {
        y = -0.5; // sunken below water plane so it stays hidden
      } else {
        y = (tile?.height ?? 0) * MAX_TERRAIN_H;
      }
      positions[idx + 1] = y;
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
  const gridTex    = buildGroundTexture(world.tiles, W, H, scene);
  const groundMat  = new StandardMaterial('ground-mat', scene);
  groundMat.diffuseTexture = gridTex;
  groundMat.specularColor  = new Color3(0, 0, 0);

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
  applyHeightDeformation(terrainMesh, world.tiles, W, H);

  // Invisible flat pick-target (name 'ground' — GameEngine uses this for click-to-walk)
  const groundPick = MeshBuilder.CreateGround('ground', {
    width:  W * TILE_SIZE,
    height: H * TILE_SIZE,
    subdivisions: 1,
  }, scene);
  groundPick.position.x = W / 2 - 0.5;
  groundPick.position.z = H / 2 - 0.5;
  groundPick.isPickable = true;
  groundPick.visibility = 0;
  groundPick.parent     = root;

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
      const tileBaseY = (tile.type !== 'water' ? (tile.height ?? 0) * MAX_TERRAIN_H : WATER_Y);

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
