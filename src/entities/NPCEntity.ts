import {
  Scene, Mesh, MeshBuilder, StandardMaterial,
  Color3, Vector3,
} from '@babylonjs/core';
import type { NPCState, NPCKind, Direction } from '../shared/types';
import { getNPCDef } from '../npcs/NPCRegistry';

const DEATH_TICKS = 5;

const FACING_ROTATION: Record<Direction, number> = {
  south: 0,
  east:  Math.PI / 2,
  north: Math.PI,
  west: -Math.PI / 2,
};

export class NPCEntity {
  private readonly root: Mesh;
  private readonly size: number;

  constructor(id: string, kind: NPCKind, scene: Scene) {
    const def = getNPCDef(kind);
    this.size = def.size;

    this.root = new Mesh(`npc-root-${id}`, scene);
    this.root.isPickable = false;

    this.buildHitbox(id, scene);

    switch (kind) {
      case 'chicken':    this.buildChicken(id, scene);    break;
      case 'shopkeeper': this.buildShopkeeper(id, scene); break;
    }
  }

  private buildHitbox(id: string, scene: Scene): void {
    const size = this.size;
    const hitbox = MeshBuilder.CreateBox(`npc-${id}`, {
      width:  size * 0.7,
      height: 1.2,
      depth:  size * 0.7,
    }, scene);
    hitbox.position = new Vector3(0, 0.6, 0);
    hitbox.parent = this.root;
    hitbox.isPickable = true;
    hitbox.visibility = 0;
  }

  private buildChicken(id: string, scene: Scene): void {
    const whiteMat  = flatMat(`npc-white-${id}`,  new Color3(0.95, 0.95, 0.92), scene);
    const yellowMat = flatMat(`npc-yellow-${id}`, new Color3(0.95, 0.78, 0.10), scene);
    const redMat    = flatMat(`npc-red-${id}`,    new Color3(0.85, 0.15, 0.10), scene);

    this.box(`npc-body-${id}`,  0.28, 0.22, 0.22,  0,    0.22, 0,    whiteMat, scene);
    this.box(`npc-head-${id}`,  0.18, 0.18, 0.18,  0,    0.46, 0.04, whiteMat, scene);

    const beak = MeshBuilder.CreateCylinder(`npc-beak-${id}`,
      { height: 0.1, diameterTop: 0, diameterBottom: 0.07, tessellation: 4 }, scene);
    beak.position = new Vector3(0, 0.46, 0.14);
    beak.rotation.x = Math.PI / 2;
    beak.material = yellowMat;
    beak.parent = this.root;
    beak.isPickable = false;
    beak.convertToFlatShadedMesh();

    this.box(`npc-comb-${id}`,  0.05, 0.07, 0.05,  0,    0.57, 0.01, redMat,    scene);
    this.box(`npc-wingL-${id}`, 0.06, 0.15, 0.18, -0.17, 0.22, 0,    whiteMat,  scene);
    this.box(`npc-wingR-${id}`, 0.06, 0.15, 0.18,  0.17, 0.22, 0,    whiteMat,  scene);
    this.box(`npc-legL-${id}`,  0.05, 0.12, 0.05, -0.07, 0.06, 0,    yellowMat, scene);
    this.box(`npc-legR-${id}`,  0.05, 0.12, 0.05,  0.07, 0.06, 0,    yellowMat, scene);
  }

  private buildShopkeeper(id: string, scene: Scene): void {
    const skinMat   = flatMat(`sk-skin-${id}`,   new Color3(0.82, 0.64, 0.48), scene);
    const hairMat   = flatMat(`sk-hair-${id}`,   new Color3(0.35, 0.22, 0.12), scene);
    const apronMat  = flatMat(`sk-apron-${id}`,  new Color3(0.42, 0.26, 0.14), scene);
    const pantsMat  = flatMat(`sk-pants-${id}`,  new Color3(0.20, 0.42, 0.22), scene);
    const bootMat   = flatMat(`sk-boot-${id}`,   new Color3(0.15, 0.11, 0.08), scene);

    // Head (balding — smaller hair cap on top)
    this.box(`sk-head-${id}`,  0.28, 0.28, 0.28, 0,    0.82, 0, skinMat, scene);
    // Hair — thin ring on sides/back only (a flat shell on back of head)
    this.box(`sk-hair-back-${id}`, 0.30, 0.12, 0.06, 0, 0.80, -0.13, hairMat, scene);
    // Goatee
    this.box(`sk-goatee-${id}`,   0.10, 0.08, 0.04, 0, 0.70,  0.14, hairMat, scene);

    // Torso (apron over shirt — use apron color)
    this.box(`sk-torso-${id}`, 0.38, 0.38, 0.22, 0,    0.50, 0, apronMat, scene);
    // Arms
    this.box(`sk-armL-${id}`,  0.12, 0.34, 0.12, -0.27, 0.50, 0, skinMat, scene);
    this.box(`sk-armR-${id}`,  0.12, 0.34, 0.12,  0.27, 0.50, 0, skinMat, scene);
    // Pants (green)
    this.box(`sk-legL-${id}`,  0.16, 0.32, 0.16, -0.11, 0.19, 0, pantsMat, scene);
    this.box(`sk-legR-${id}`,  0.16, 0.32, 0.16,  0.11, 0.19, 0, pantsMat, scene);
    // Boots
    this.box(`sk-bootL-${id}`, 0.17, 0.06, 0.17, -0.11, 0.03, 0, bootMat, scene);
    this.box(`sk-bootR-${id}`, 0.17, 0.06, 0.17,  0.11, 0.03, 0, bootMat, scene);
  }

  private box(
    name: string, w: number, h: number, d: number,
    px: number, py: number, pz: number,
    mat: StandardMaterial, scene: Scene,
  ): Mesh {
    const mesh = MeshBuilder.CreateBox(name, { width: w, height: h, depth: d }, scene);
    mesh.position = new Vector3(px, py, pz);
    mesh.material = mat;
    mesh.parent = this.root;
    mesh.isPickable = false;
    mesh.convertToFlatShadedMesh();
    return mesh;
  }

  render(prev: NPCState, current: NPCState, alpha: number, tick: number, groundY = 0): void {
    this.root.position.x = lerp(prev.tileX, current.tileX, alpha);
    this.root.position.z = lerp(prev.tileY, current.tileY, alpha);
    this.root.position.y = groundY;
    this.root.rotation.y = FACING_ROTATION[current.facing];

    if (current.dying) {
      const elapsed = (tick - current.dyingTick) + alpha;
      const progress = Math.min(elapsed / DEATH_TICKS, 1);
      this.root.rotation.z = progress * (Math.PI / 2);
      this.root.position.y = groundY - progress * 0.1;
    } else {
      this.root.rotation.z = 0;

      // Attack lunge animation — jump slightly forward when attacking
      const ticksSinceAttack = (tick - current.lastAttackTick) + alpha;
      const LUNGE_DURATION = 1.5;
      if (current.lastAttackTick > 0 && ticksSinceAttack < LUNGE_DURATION) {
        const t = ticksSinceAttack / LUNGE_DURATION;
        const lunge = Math.sin(t * Math.PI) * 0.28;
        const [fx, fz] = facingOffset(current.facing);
        this.root.position.x += fx * lunge;
        this.root.position.z += fz * lunge;
      }
    }
  }

  dispose(): void {
    this.root.getChildMeshes().forEach(m => { m.material?.dispose(); m.dispose(); });
    this.root.dispose();
  }
}

function facingOffset(facing: Direction): [number, number] {
  switch (facing) {
    case 'north': return [0, -1];
    case 'south': return [0,  1];
    case 'east':  return [1,  0];
    case 'west':  return [-1, 0];
  }
}

function flatMat(name: string, color: Color3, scene: Scene): StandardMaterial {
  const mat = new StandardMaterial(name, scene);
  mat.diffuseColor = color;
  return mat;
}

function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * Math.min(1, Math.max(0, t));
}
