import {
  Scene, Mesh, MeshBuilder, StandardMaterial,
  Color3, Vector3, SceneLoader, type AbstractMesh,
} from '@babylonjs/core';
import '@babylonjs/loaders/glTF';
import type { PlayerState, Direction, EquipSlot, ItemStack, ShirtColor, SkinColor } from '../shared/types';

// ---- GLTF weapon config -------------------------------------------------
// Adjust position / rotation / scale here to dial in how each weapon sits
// in the player's right hand (all coords in player-root space).
//
//   position: where the model's origin lands in player space
//   rotation: Euler XYZ in radians applied after loading
//   scale:    uniform scale (1.0 = as exported)
//
// Right arm center ≈ (0.27, 0.50, 0.00) for reference.

interface GltfWeaponCfg {
  file:     string;
  position: [number, number, number];
  rotation: [number, number, number];
  scale:    number;
}

const GLTF_WEAPONS: Record<string, GltfWeaponCfg> = {
  iron_axe: {
    file:     'M_Woodcutting_BasicAxe.gltf',
    position: [0.42, 0.30, 0.06],
    rotation: [90, 0, 0],
    scale:    1.0,
  },
  axe: {
    file:     'M_Woodcutting_BasicAxe.gltf',
    position: [0.42, 0.30, 0.06],
    rotation: [90, 0, 0],
    scale:    1.0,
  },
};

// Module-level load cache so multiple PlayerEntity instances share results
const _gltfPromises = new Map<string, Promise<AbstractMesh>>();

function loadGltfWeapon(cfg: GltfWeaponCfg, scene: Scene): Promise<AbstractMesh> {
  const key = cfg.file;
  if (!_gltfPromises.has(key)) {
    const p = SceneLoader.ImportMeshAsync('', '/models/', cfg.file, scene).then(result => {
      const root = result.meshes[0];
      root.isPickable = false;
      // Stash the source out of view; we'll clone per-player
      root.setEnabled(false);
      return root;
    });
    _gltfPromises.set(key, p);
  }
  return _gltfPromises.get(key)!;
}

// ---- Appearance colour tables ----

export const SHIRT_COLORS: Record<ShirtColor, Color3> = {
  blue:   new Color3(0.13, 0.27, 0.80),
  red:    new Color3(0.80, 0.13, 0.13),
  yellow: new Color3(0.80, 0.67, 0.13),
  green:  new Color3(0.13, 0.53, 0.13),
};

export const SKIN_COLORS: Record<SkinColor, Color3> = {
  fair:  new Color3(0.96, 0.83, 0.69),
  tan:   new Color3(0.83, 0.58, 0.42),
  olive: new Color3(0.61, 0.45, 0.27),
  brown: new Color3(0.42, 0.26, 0.15),
};

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
  private pendingHeldItemId: string | null = null;
  private lungeStartMs: number = -1;
  private lastShirtColor: ShirtColor | null = null;
  private lastSkinColor:  SkinColor  | null = null;
  private shirtMat: StandardMaterial | null = null;
  private skinMat:  StandardMaterial | null = null;
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
    this.skinMat = new StandardMaterial(`skin-${id}`, scene);
    this.skinMat.diffuseColor = SKIN_COLORS.fair.clone();
    const skinMat = this.skinMat;

    const noseMat = new StandardMaterial(`nose-${id}`, scene);
    noseMat.diffuseColor = new Color3(0.55, 0.38, 0.28);

    this.shirtMat = new StandardMaterial(`shirt-${id}`, scene);
    this.shirtMat.diffuseColor = SHIRT_COLORS.blue.clone();
    const shirtMat = this.shirtMat;

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

  // ---- Appearance ----

  updateAppearance(shirtColor: ShirtColor, skinColor: SkinColor): void {
    if (shirtColor === this.lastShirtColor && skinColor === this.lastSkinColor) return;
    this.lastShirtColor = shirtColor;
    this.lastSkinColor  = skinColor;
    if (this.shirtMat) this.shirtMat.diffuseColor = SHIRT_COLORS[shirtColor]?.clone() ?? SHIRT_COLORS.blue.clone();
    if (this.skinMat)  this.skinMat.diffuseColor  = SKIN_COLORS[skinColor]?.clone()   ?? SKIN_COLORS.fair.clone();
  }

  // ---- Equipped item mesh ----

  updateEquipped(equipped: Partial<Record<EquipSlot, ItemStack>>): void {
    const rightHandId = equipped.rightHand?.itemId ?? null;
    if (rightHandId === this.lastRightHandId) return;
    this.lastRightHandId = rightHandId;

    // Tear down whatever is currently held
    this.pendingHeldItemId = null;
    if (this.heldItemRoot) {
      this.heldItemRoot.getChildMeshes().forEach(m => { m.material?.dispose(); m.dispose(); });
      this.heldItemRoot.dispose();
      this.heldItemRoot = null;
    }

    if (!rightHandId) return;

    const gltfCfg = GLTF_WEAPONS[rightHandId];
    if (gltfCfg) {
      // Async GLTF path — load (or reuse cached) source and clone into hand
      this.pendingHeldItemId = rightHandId;
      loadGltfWeapon(gltfCfg, this.scene).then(sourceRoot => {
        // Guard: item may have been unequipped while we were loading
        if (this.pendingHeldItemId !== rightHandId) return;

        const clone = sourceRoot.clone(`held-gltf-${rightHandId}-${this.root.name}`, this.root, false)!;
        clone.setEnabled(true);
        clone.isPickable = false;
        clone.getChildMeshes().forEach(m => { m.isPickable = false; });

        const [px, py, pz] = gltfCfg.position;
        const [rx, ry, rz] = gltfCfg.rotation;
        clone.position = new Vector3(px, py, pz);
        clone.rotation = new Vector3(rx, ry, rz);
        clone.scaling  = new Vector3(gltfCfg.scale, gltfCfg.scale, gltfCfg.scale);

        this.heldItemRoot = clone as unknown as Mesh;
        this.pendingHeldItemId = null;
      }).catch(err => {
        console.warn(`Failed to load GLTF weapon ${rightHandId}:`, err);
        this.pendingHeldItemId = null;
      });
    } else {
      // Synchronous procedural path (pickaxe, swords, etc.)
      this.heldItemRoot = this.buildHeldMesh(rightHandId, this.scene);
      if (this.heldItemRoot) this.heldItemRoot.parent = this.root;
    }
  }

  private buildHeldMesh(itemId: string, scene: Scene): Mesh | null {
    switch (itemId) {
      case 'pickaxe':          return this.buildHeldPickaxe(scene);
      case 'bronze_sword':
      case 'iron_sword':       return this.buildHeldSword(itemId, scene, false);
      case 'bronze_longsword': return this.buildHeldSword(itemId, scene, true);
      default:                 return null;
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

  private buildHeldSword(itemId: string, scene: Scene, isLong: boolean): Mesh {
    // Sword held vertically in right hand — blade points up, pommel down
    const root = new Mesh(`held-${itemId}`, scene);
    root.isPickable = false;

    const isBronze = itemId.startsWith('bronze');
    const bladeColor  = isBronze ? new Color3(0.78, 0.48, 0.19) : new Color3(0.60, 0.60, 0.65);
    const guardColor  = isBronze ? new Color3(0.62, 0.36, 0.10) : new Color3(0.42, 0.42, 0.44);
    const handleColor = new Color3(0.36, 0.18, 0.04);

    const bladeMat  = pMat(`hswrd-blade-${itemId}`,  bladeColor,  scene);
    const guardMat  = pMat(`hswrd-guard-${itemId}`,  guardColor,  scene);
    const handleMat = pMat(`hswrd-handle-${itemId}`, handleColor, scene);

    // Blade — tall thin bar extending upward from the grip
    const bladeH = isLong ? 0.58 : 0.44;
    pBox(`hswrd-blade-${itemId}`,  0.04, bladeH, 0.04,  0.36, 0.88, 0.06, bladeMat,  scene, root);

    // Crossguard — horizontal bar at blade base
    pBox(`hswrd-guard-${itemId}`,  0.18, 0.04,  0.05,  0.36, 0.59, 0.06, guardMat,  scene, root);

    // Handle — grip below guard
    pBox(`hswrd-handle-${itemId}`, 0.04, 0.22,  0.04,  0.36, 0.46, 0.06, handleMat, scene, root);

    // Pommel — small cap at base of handle
    pBox(`hswrd-pommel-${itemId}`, 0.07, 0.04,  0.05,  0.36, 0.34, 0.06, guardMat,  scene, root);

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
