import {
  Scene, Mesh, MeshBuilder, StandardMaterial,
  Color3, Vector3,
} from '@babylonjs/core';
import type { PlayerState, Direction, EquipSlot, ItemStack } from '../shared/types';

const FACING_ROTATION: Record<Direction, number> = {
  south: 0,
  east:  Math.PI / 2,
  north: Math.PI,
  west: -Math.PI / 2,
};

export class PlayerEntity {
  readonly root: Mesh;
  private parts: {
    head: Mesh;
    torso: Mesh;
    armL: Mesh;
    armR: Mesh;
    legL: Mesh;
    legR: Mesh;
  };

  private scene: Scene;
  private heldItemRoot: Mesh | null = null;
  private lastRightHandId: string | null = null;
  private lungeStartMs: number = -1;
  private static readonly LUNGE_DURATION_MS = 220;
  private static readonly LUNGE_DIST = 0.20;

  constructor(scene: Scene, id = 'local') {
    this.scene = scene;
    this.root = new Mesh(`player-root-${id}`, scene);
    this.root.isPickable = false;
    this.root.renderingGroupId = 1;
    this.parts = this.buildAvatar(scene, id);

    // Invisible pickable hitbox for remote players so they can be clicked
    if (id !== 'local') {
      const hitbox = MeshBuilder.CreateBox(`player-${id}`, { width: 0.40, height: 1.05, depth: 0.28 }, scene);
      hitbox.position.y = 0.525;
      hitbox.visibility = 0;
      hitbox.isPickable = true;
      hitbox.parent = this.root;
    }
  }

  private buildAvatar(scene: Scene, id = 'local'): typeof this.parts {
    const skinMat = new StandardMaterial(`skin-${id}`, scene);
    skinMat.diffuseColor = new Color3(0.78, 0.61, 0.46);

    const noseMat = new StandardMaterial(`nose-${id}`, scene);
    noseMat.diffuseColor = new Color3(0.55, 0.38, 0.28);

    const shirtMat = new StandardMaterial(`shirt-${id}`, scene);
    shirtMat.diffuseColor = new Color3(0.2, 0.35, 0.65);

    const pantsMat = new StandardMaterial(`pants-${id}`, scene);
    pantsMat.diffuseColor = new Color3(0.25, 0.22, 0.18);

    const make = (
      name: string,
      w: number, h: number, d: number,
      px: number, py: number, pz: number,
      mat: StandardMaterial,
    ): Mesh => {
      const mesh = MeshBuilder.CreateBox(name, { width: w, height: h, depth: d }, scene);
      mesh.position = new Vector3(px, py, pz);
      mesh.material = mat;
      mesh.parent = this.root;
      mesh.isPickable = false;
      mesh.convertToFlatShadedMesh();
      return mesh;
    };

    const head  = make('head',   0.28, 0.28, 0.28,  0,     0.82,  0,     skinMat);
    make('nose', 0.08, 0.06, 0.07, 0, 0.80, 0.16, noseMat);
    const torso = make('torso',  0.38, 0.38, 0.22,  0,     0.50,  0,     shirtMat);
    const armL  = make('armL',   0.12, 0.34, 0.12, -0.27,  0.50,  0,     skinMat);
    const armR  = make('armR',   0.12, 0.34, 0.12,  0.27,  0.50,  0,     skinMat);
    const legL  = make('legL',   0.16, 0.36, 0.16, -0.11,  0.18,  0,     pantsMat);
    const legR  = make('legR',   0.16, 0.36, 0.16,  0.11,  0.18,  0,     pantsMat);

    return { head, torso, armL, armR, legL, legR };
  }

  // ---- Equipped item mesh ----

  updateEquipped(equipped: Partial<Record<EquipSlot, ItemStack>>): void {
    const rightHandId = equipped.rightHand?.itemId ?? null;
    if (rightHandId === this.lastRightHandId) return;
    this.lastRightHandId = rightHandId;

    if (this.heldItemRoot) {
      this.heldItemRoot.getChildMeshes().forEach(m => { m.material?.dispose(); m.dispose(); });
      this.heldItemRoot.dispose();
      this.heldItemRoot = null;
    }

    if (rightHandId) {
      this.heldItemRoot = this.buildHeldMesh(rightHandId, this.scene);
      if (this.heldItemRoot) this.heldItemRoot.parent = this.root;
    }
  }

  private buildHeldMesh(itemId: string, scene: Scene): Mesh | null {
    switch (itemId) {
      case 'pickaxe':  return this.buildHeldPickaxe(scene);
      case 'axe':
      case 'iron_axe': return this.buildHeldAxe(itemId, scene);
      default:         return null;
    }
  }

  private buildHeldPickaxe(scene: Scene): Mesh {
    // Hand position: armR center x=0.27, y=0.50. Handle gripped at y≈0.36.
    const root = new Mesh('held-pickaxe', scene);
    root.isPickable = false;

    const woodMat  = pMat('hpick-wood',  new Color3(0.48, 0.25, 0.12), scene);
    const ironMat  = pMat('hpick-iron',  new Color3(0.40, 0.52, 0.62), scene);
    const edgeMat  = pMat('hpick-edge',  new Color3(0.65, 0.76, 0.85), scene);

    // Handle — vertical wooden grip
    pBox('hpick-handle', 0.05, 0.40, 0.05, 0.36, 0.56, 0.06, woodMat, scene, root);

    // Pick head — arched bar: centre raised, ends curve down
    pBox('hpick-head-c', 0.14, 0.08, 0.06, 0.36, 0.80, 0.06, ironMat, scene, root);  // centre arch
    pBox('hpick-head-l', 0.08, 0.06, 0.06, 0.22, 0.76, 0.06, ironMat, scene, root);  // left arm
    pBox('hpick-head-r', 0.08, 0.06, 0.06, 0.50, 0.76, 0.06, ironMat, scene, root);  // right arm

    // Pick spike — sharp cone on the left
    const spike = MeshBuilder.CreateCylinder('hpick-spike',
      { height: 0.14, diameterTop: 0, diameterBottom: 0.05, tessellation: 5 }, scene);
    spike.material = edgeMat;
    spike.convertToFlatShadedMesh();
    spike.rotation.z = Math.PI / 2;
    spike.position = new Vector3(0.12, 0.76, 0.06);
    spike.isPickable = false;
    spike.parent = root;

    // Poll — blunt end on the right
    pBox('hpick-poll', 0.05, 0.10, 0.06, 0.56, 0.76, 0.06, ironMat, scene, root);

    return root;
  }

  private buildHeldAxe(itemId: string, scene: Scene): Mesh {
    // Axe held vertically — head at top, blade sweeps to the left (local -x)
    const root = new Mesh(`held-${itemId}`, scene);
    root.isPickable = false;

    const woodMat = pMat(`haxe-wood-${itemId}`, new Color3(0.40, 0.22, 0.09), scene);
    const ironMat = pMat(`haxe-iron-${itemId}`, new Color3(0.18, 0.18, 0.20), scene);
    const edgeMat = pMat(`haxe-edge-${itemId}`, new Color3(0.55, 0.58, 0.62), scene);

    // Handle — long vertical wooden grip
    pBox('haxe-handle', 0.05, 0.44, 0.05, 0.36, 0.54, 0.06, woodMat, scene, root);

    // Axe head body — rectangular block at the top of the handle
    pBox('haxe-head', 0.06, 0.18, 0.07, 0.36, 0.80, 0.06, ironMat, scene, root);

    // Blade — wide flat section extending to the left (the bit/cutting edge)
    pBox('haxe-blade-body', 0.13, 0.16, 0.06, 0.22, 0.80, 0.06, ironMat, scene, root);

    // Edge highlight — lighter colour on the cutting edge
    pBox('haxe-blade-edge', 0.03, 0.14, 0.05, 0.13, 0.80, 0.06, edgeMat, scene, root);

    // Poll — small protrusion on the right side (opposite the blade)
    pBox('haxe-poll', 0.06, 0.07, 0.06, 0.44, 0.82, 0.06, ironMat, scene, root);

    return root;
  }

  triggerLunge(): void {
    this.lungeStartMs = performance.now();
  }

  // ---- Render ----

  render(prev: PlayerState, current: PlayerState, alpha: number, tick: number): void {
    const px = lerp(prev.tileX, current.tileX, alpha);
    const py = lerp(prev.tileY, current.tileY, alpha);
    this.root.position.x = px;
    this.root.position.z = py;
    this.root.position.y = 0;

    this.root.rotation.y = FACING_ROTATION[current.facing];

    // Hit flinch
    const ticksSinceHit = (tick - current.lastHitTick) + alpha;
    const FLINCH_DURATION = 1.5;
    if (current.lastHitTick > 0 && ticksSinceHit < FLINCH_DURATION) {
      this.root.rotation.z = Math.sin((ticksSinceHit / FLINCH_DURATION) * Math.PI) * 0.22;
    } else {
      this.root.rotation.z = 0;
    }

    // Lunge — forward burst in facing direction, smoothly back
    const elapsed = performance.now() - this.lungeStartMs;
    if (this.lungeStartMs >= 0 && elapsed < PlayerEntity.LUNGE_DURATION_MS) {
      const t = elapsed / PlayerEntity.LUNGE_DURATION_MS;
      const offset = Math.sin(t * Math.PI) * PlayerEntity.LUNGE_DIST;
      const FACING_OFFSETS: Record<Direction, [number, number]> = {
        south: [0,  1],
        north: [0, -1],
        east:  [1,  0],
        west:  [-1, 0],
      };
      const [dx, dz] = FACING_OFFSETS[current.facing];
      this.root.position.x += dx * offset;
      this.root.position.z += dz * offset;
    }
  }

  get worldPosition(): Vector3 {
    return this.root.position.clone();
  }

  dispose(): void {
    if (this.heldItemRoot) {
      this.heldItemRoot.getChildMeshes().forEach(m => { m.material?.dispose(); m.dispose(); });
      this.heldItemRoot.dispose();
      this.heldItemRoot = null;
    }
    this.root.getChildMeshes().forEach(m => { m.material?.dispose(); m.dispose(); });
    this.root.dispose();
  }
}

function pBox(
  name: string, w: number, h: number, d: number,
  px: number, py: number, pz: number,
  mat: StandardMaterial, scene: Scene, parent: Mesh,
): Mesh {
  const mesh = MeshBuilder.CreateBox(name, { width: w, height: h, depth: d }, scene);
  mesh.position = new Vector3(px, py, pz);
  mesh.material = mat;
  mesh.parent = parent;
  mesh.isPickable = false;
  mesh.convertToFlatShadedMesh();
  return mesh;
}

function pMat(name: string, color: Color3, scene: Scene): StandardMaterial {
  const mat = new StandardMaterial(name, scene);
  mat.diffuseColor = color;
  return mat;
}

function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * Math.min(1, Math.max(0, t));
}
