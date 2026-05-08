import {
  Scene, MeshBuilder, StandardMaterial, Color3,
  Mesh, Vector3, DynamicTexture, Texture, VertexData,
} from '@babylonjs/core';
import type { WorldState } from '../shared/types';
import { TILE_SIZE } from '../shared/constants';
import { buildTerrainGeometry, buildBankGeometry } from './MarchingSquares';

export { createWorldFromTiles, findWalkableTileNear } from './WorldState';

// ---- Pixel type codes (must match export-map.mjs) ---------------------------
const TYPE_GRASS = 0;
const TYPE_WATER = 1;
const TYPE_CLIFF = 2;

// ---- Terrain layer Y offsets ------------------------------------------------
const CLIFF_Y  = 0.005;
const WATER_Y  = -0.25;  // water surface sits below ground level
const BANK_TOP = 0;  // bank wall top is flush with the land mesh surface

// ---- Ground texture ---------------------------------------------------------
//
// Source is 192×192 pixels (3 px/tile).  We upscale each source pixel to
// TEX_SCALE×TEX_SCALE canvas pixels so nearest-neighbour sampling stays crisp
// at camera distance.  The final canvas is 576×576 (9 px/tile).
//
// Babylon DynamicTexture has invertY=true: canvas Y=0 → UV V=1 (world Z-max).
// Our pixel array has py=0 = game-south, so we flip: canvasY = (pH-1-py)*scale.

const TEX_SCALE = 3;

function buildGroundTexture(
  pixels: number[],
  pWidth: number,
  pHeight: number,
  scene: Scene,
): DynamicTexture {
  const cW = pWidth  * TEX_SCALE;
  const cH = pHeight * TEX_SCALE;
  const tex = new DynamicTexture('grid-tex', { width: cW, height: cH }, scene, false);
  const ctx = tex.getContext();

  for (let py = 0; py < pHeight; py++) {
    const canvasY = (pHeight - 1 - py) * TEX_SCALE;
    for (let px = 0; px < pWidth; px++) {
      const v        = pixels[py * pWidth + px];
      const typeCode = (v >>> 24) & 0xf;

      if (typeCode !== TYPE_GRASS) {
        // Water/cliff/wall/door — paint grass; land mesh won't cover water pixels anyway.
        ctx.fillStyle = '#7ec850';
        ctx.fillRect(px * TEX_SCALE, canvasY, TEX_SCALE, TEX_SCALE);
        continue;
      }

      const r = (v >>> 16) & 0xff;
      const g = (v >>>  8) & 0xff;
      const b =  v         & 0xff;
      ctx.fillStyle = `rgb(${r},${g},${b})`;
      ctx.fillRect(px * TEX_SCALE, canvasY, TEX_SCALE, TEX_SCALE);
    }
  }

  tex.update();
  tex.uScale = 1;
  tex.vScale = 1;
  tex.updateSamplingMode(Texture.NEAREST_SAMPLINGMODE);
  return tex;
}

// ---- Terrain layer helpers --------------------------------------------------

function applyTerrainGeo(
  name: string,
  typeCode: number,
  worldY: number,
  mat: StandardMaterial,
  pixels: number[],
  pWidth: number,
  pHeight: number,
  scene: Scene,
  root: Mesh,
  invertMatch = false,
): void {
  const geo = buildTerrainGeometry(pixels, pWidth, pHeight, typeCode, worldY, invertMatch);
  if (geo.indices.length === 0) return;
  const vd = new VertexData();
  vd.positions = geo.positions;
  vd.indices   = geo.indices;
  vd.normals   = geo.normals;
  vd.uvs       = geo.uvs;
  const mesh = new Mesh(name, scene);
  vd.applyToMesh(mesh);
  mesh.material                 = mat;
  mesh.isPickable               = false;
  mesh.alwaysSelectAsActiveMesh = true;
  mesh.parent                   = root;
}

function buildCliffLayer(
  pixels: number[], pWidth: number, pHeight: number,
  scene: Scene, root: Mesh,
): void {
  const mat = new StandardMaterial('cliff-mat', scene);
  mat.diffuseColor  = new Color3(0.27, 0.157, 0.235); // #45283c — cliff marker colour
  mat.specularColor = new Color3(0.05, 0.05, 0.05);
  mat.backFaceCulling = false;
  applyTerrainGeo('cliff-surface', TYPE_CLIFF, CLIFF_Y, mat, pixels, pWidth, pHeight, scene, root);
}

function buildWaterLayer(
  pixels: number[], pWidth: number, pHeight: number,
  scene: Scene, root: Mesh,
): void {
  const mat = new StandardMaterial('water-mat', scene);
  mat.diffuseColor    = new Color3(0.18, 0.48, 0.90);
  mat.emissiveColor   = new Color3(0.03, 0.10, 0.28);
  mat.specularColor   = new Color3(0.30, 0.45, 0.75);
  mat.backFaceCulling = false;
  applyTerrainGeo('water-surface', TYPE_WATER, WATER_Y, mat, pixels, pWidth, pHeight, scene, root);

  // Shimmer via emissive only — alpha stays at 1 so no grass bleeds through
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

function buildBankLayer(
  pixels: number[], pWidth: number, pHeight: number,
  scene: Scene, root: Mesh,
): void {
  const geo = buildBankGeometry(pixels, pWidth, pHeight, TYPE_WATER, BANK_TOP, WATER_Y);
  if (geo.indices.length === 0) return;

  // Compute horizontal normals from the geometry
  const normals = new Float32Array(geo.positions.length);
  VertexData.ComputeNormals(geo.positions, geo.indices, normals);

  const vd = new VertexData();
  vd.positions = geo.positions;
  vd.indices   = geo.indices;
  vd.normals   = normals;

  const mat = new StandardMaterial('bank-mat', scene);
  mat.diffuseColor    = new Color3(0.42, 0.30, 0.18); // dark earthy brown
  mat.specularColor   = new Color3(0.02, 0.02, 0.02);
  mat.backFaceCulling = false;

  const mesh = new Mesh('water-bank', scene);
  vd.applyToMesh(mesh);
  mesh.material                 = mat;
  mesh.isPickable               = false;
  mesh.alwaysSelectAsActiveMesh = true;
  mesh.parent                   = root;
}

// ---- Main -------------------------------------------------------------------

export function buildWorldMeshes(
  world: WorldState,
  pixels: number[],
  pWidth: number,
  pHeight: number,
  scene: Scene,
): Mesh {
  const root = new Mesh('world-root', scene);

  // ---- Ground plane (marching squares land mesh) ------------------------------
  // Covers every non-water pixel so its boundary aligns exactly with the bank walls.
  const gridTex   = buildGroundTexture(pixels, pWidth, pHeight, scene);
  const groundMat = new StandardMaterial('ground-mat', scene);
  groundMat.diffuseTexture = gridTex;
  groundMat.specularColor  = new Color3(0, 0, 0);
  applyTerrainGeo('ground-land', TYPE_WATER, 0, groundMat, pixels, pWidth, pHeight, scene, root, true);

  // Invisible flat pick-target (named 'ground' — GameEngine checks this name for click-to-walk).
  const groundPick = MeshBuilder.CreateGround('ground', {
    width:  world.width  * TILE_SIZE,
    height: world.height * TILE_SIZE,
    subdivisions: 1,
  }, scene);
  groundPick.position.x   = world.width  / 2 - 0.5;
  groundPick.position.z   = world.height / 2 - 0.5;
  groundPick.isPickable   = true;
  groundPick.visibility   = 0;
  groundPick.parent       = root;

  // ---- Materials --------------------------------------------------------------
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

      if (tile.obstacle === 'tree') {
        const trunk = sourceTrunk.createInstance(`tree-trunk-${x}-${y}`);
        trunk.position   = new Vector3(x, 0.3, y);
        trunk.isPickable = true;
        trunk.parent     = root;

        const canopy = sourceCanopy.createInstance(`tree-canopy-${x}-${y}`);
        canopy.position   = new Vector3(x, 0.9, y);
        canopy.isPickable = true;
        canopy.parent     = root;

      } else if (tile.obstacle === 'rock') {
        const rock = sourceRock.createInstance(`rock-${x}-${y}`);
        rock.position   = new Vector3(x, 0.16, y);
        rock.rotation.y = ((x * 1234 + y * 5678) % 628) / 100;
        rock.isPickable = true;
        rock.parent     = root;

      } else if (tile.obstacle === 'chest') {
        const chest = MeshBuilder.CreateBox(`chest-${x}-${y}`, {
          width: 0.55, height: 0.55, depth: 0.4,
        }, scene);
        chest.position   = new Vector3(x, 0.28, y);
        chest.material   = chestMat;
        chest.isPickable = true;
        chest.parent     = root;

      } else if (tile.type === 'wall') {
        if (!blocksPanel(x, y - 1)) {
          const p = MeshBuilder.CreateBox(`wp-n-${x}-${y}`, { width: 1, height: WALL_H, depth: WALL_T }, scene);
          p.position = new Vector3(x, WALL_Y, y - 0.5);
          p.isPickable = false;
          wallPanels.push(p);
        }
        if (!blocksPanel(x, y + 1)) {
          const p = MeshBuilder.CreateBox(`wp-s-${x}-${y}`, { width: 1, height: WALL_H, depth: WALL_T }, scene);
          p.position = new Vector3(x, WALL_Y, y + 0.5);
          p.isPickable = false;
          wallPanels.push(p);
        }
        if (!blocksPanel(x - 1, y)) {
          const p = MeshBuilder.CreateBox(`wp-w-${x}-${y}`, { width: WALL_T, height: WALL_H, depth: 1 }, scene);
          p.position = new Vector3(x - 0.5, WALL_Y, y);
          p.isPickable = false;
          wallPanels.push(p);
        }
        if (!blocksPanel(x + 1, y)) {
          const p = MeshBuilder.CreateBox(`wp-e-${x}-${y}`, { width: WALL_T, height: WALL_H, depth: 1 }, scene);
          p.position = new Vector3(x + 0.5, WALL_Y, y);
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

  // ---- Terrain layers (marching squares, stacked lowest → highest) -----------
  buildCliffLayer(pixels, pWidth, pHeight, scene, root);
  buildBankLayer(pixels, pWidth, pHeight, scene, root);
  buildWaterLayer(pixels, pWidth, pHeight, scene, root);

  return root;
}
