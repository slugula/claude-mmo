import {
  Scene, MeshBuilder, StandardMaterial, Color3,
  Mesh, Vector3, DynamicTexture,
} from '@babylonjs/core';
import type { WorldState } from '../shared/types';
import { TILE_SIZE } from '../shared/constants';
import { seededRandom } from './WorldState';

export { createWorldState, findWalkableTileNear } from './WorldState';

export function buildWorldMeshes(world: WorldState, scene: Scene): Mesh {
  const root = new Mesh('world-root', scene);

  // ---- Ground ----
  const ground = MeshBuilder.CreateGround('ground', {
    width: world.width * TILE_SIZE,
    height: world.height * TILE_SIZE,
    subdivisions: 1,
  }, scene);
  ground.position.x = world.width / 2;
  ground.position.z = world.height / 2;
  ground.isPickable = true;
  ground.parent = root;

  const texSize = 512;
  const gridTex = new DynamicTexture('grid-tex', { width: texSize, height: texSize }, scene, false);
  const ctx = gridTex.getContext();
  ctx.fillStyle = '#3a7220';
  ctx.fillRect(0, 0, texSize, texSize);
  ctx.strokeStyle = '#2a5512';
  ctx.lineWidth = 1.5;
  const step = texSize / 8;
  for (let i = 0; i <= 8; i++) {
    const p = i * step;
    ctx.beginPath(); ctx.moveTo(p, 0); ctx.lineTo(p, texSize); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, p); ctx.lineTo(texSize, p); ctx.stroke();
  }
  gridTex.update();
  gridTex.uScale = world.width / 8;
  gridTex.vScale = world.height / 8;

  const groundMat = new StandardMaterial('ground-mat', scene);
  groundMat.diffuseTexture = gridTex;
  groundMat.specularColor = new Color3(0, 0, 0);
  ground.material = groundMat;

  // ---- Shared materials ----
  const treeTrunkMat = new StandardMaterial('tree-trunk-mat', scene);
  treeTrunkMat.diffuseColor = new Color3(0.29, 0.18, 0.08);

  const treeCanopyMat = new StandardMaterial('tree-canopy-mat', scene);
  treeCanopyMat.diffuseColor = new Color3(0.1, 0.38, 0.07);

  const rockMat = new StandardMaterial('rock-mat', scene);
  rockMat.diffuseColor = new Color3(0.48, 0.46, 0.44);

  // ---- Source meshes for instancing ----
  // Positioned far below the world so they never render visibly.
  // Instances share the source's vertex buffer, giving one GPU draw call per type.
  const sourceTrunk = MeshBuilder.CreateCylinder('tree-trunk-source', {
    height: 0.6, diameter: 0.18, tessellation: 6,
  }, scene);
  sourceTrunk.position.y = -1000;
  sourceTrunk.isPickable = false;
  sourceTrunk.material = treeTrunkMat;
  sourceTrunk.convertToFlatShadedMesh();

  const sourceCanopy = MeshBuilder.CreateSphere('tree-canopy-source', {
    diameter: 0.72, segments: 4,
  }, scene);
  sourceCanopy.position.y = -1000;
  sourceCanopy.isPickable = false;
  sourceCanopy.material = treeCanopyMat;
  sourceCanopy.convertToFlatShadedMesh();

  const sourceRock = MeshBuilder.CreateBox('rock-source', {
    width: 0.55, height: 0.32, depth: 0.48,
  }, scene);
  sourceRock.position.y = -1000;
  sourceRock.isPickable = false;
  sourceRock.material = rockMat;
  sourceRock.convertToFlatShadedMesh();

  // ---- Instances per tile ----
  for (let y = 0; y < world.height; y++) {
    for (let x = 0; x < world.width; x++) {
      const tile = world.tiles[y][x];
      if (tile.obstacle === 'tree') {
        const trunk = sourceTrunk.createInstance(`tree-trunk-${x}-${y}`);
        trunk.position = new Vector3(x, 0.3, y);
        trunk.parent = root;

        const canopy = sourceCanopy.createInstance(`tree-canopy-${x}-${y}`);
        canopy.position = new Vector3(x, 0.9, y);
        canopy.parent = root;
      } else if (tile.obstacle === 'rock') {
        const rock = sourceRock.createInstance(`rock-${x}-${y}`);
        rock.position = new Vector3(x, 0.16, y);
        rock.rotation.y = seededRandom(x * 100 + y)() * Math.PI;
        rock.parent = root;
      }
    }
  }

  return root;
}
