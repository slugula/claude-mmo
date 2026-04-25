import {
  Scene, Mesh, MeshBuilder, StandardMaterial, Color3, Vector3,
} from '@babylonjs/core';

export class DroppedItemEntity {
  private readonly root: Mesh;

  constructor(id: string, itemId: string, tileX: number, tileY: number, scene: Scene) {
    this.root = this.buildMesh(id, itemId, tileX, tileY, scene);
  }

  private buildMesh(id: string, itemId: string, tileX: number, tileY: number, scene: Scene): Mesh {
    switch (itemId) {
      case 'egg':             return this.buildEgg(id, tileX, tileY, scene);
      case 'pickaxe':         return this.buildPickaxe(id, tileX, tileY, scene);
      case 'axe':
      case 'iron_axe':        return this.buildAxe(id, tileX, tileY, scene);
      case 'logs':
      case 'oak_logs':
      case 'willow_logs':     return this.buildLog(id, itemId, tileX, tileY, scene);
      default:                return this.buildGeneric(id, itemId, tileX, tileY, scene);
    }
  }

  // ---- Egg ----

  private buildEgg(id: string, tileX: number, tileY: number, scene: Scene): Mesh {
    const mesh = MeshBuilder.CreateSphere(`item-${id}`, { diameter: 0.32, segments: 6 }, scene);
    mesh.scaling.y = 0.78;
    mesh.position = new Vector3(tileX, 0.125, tileY);
    mesh.isPickable = true;

    const mat = new StandardMaterial(`item-mat-${id}`, scene);
    mat.diffuseColor = new Color3(0.96, 0.92, 0.78);
    mat.specularColor = new Color3(0.3, 0.3, 0.2);
    mesh.material = mat;
    mesh.convertToFlatShadedMesh();
    return mesh;
  }

  // ---- Log (chopped segment lying on the ground) ----

  private buildLog(id: string, itemId: string, tileX: number, tileY: number, scene: Scene): Mesh {
    const barkColors: Record<string, Color3> = {
      logs:        new Color3(0.33, 0.16, 0.05),
      oak_logs:    new Color3(0.40, 0.20, 0.07),
      willow_logs: new Color3(0.25, 0.18, 0.10),
    };
    const grainColors: Record<string, Color3> = {
      logs:        new Color3(0.62, 0.38, 0.16),
      oak_logs:    new Color3(0.72, 0.48, 0.22),
      willow_logs: new Color3(0.55, 0.36, 0.18),
    };
    const barkColor  = barkColors[itemId]  ?? barkColors['logs'];
    const grainColor = grainColors[itemId] ?? grainColors['logs'];

    // Invisible hitbox
    const root = MeshBuilder.CreateBox(`item-${id}`, { width: 0.52, height: 0.10, depth: 0.22 }, scene);
    root.position = new Vector3(tileX, 0.05, tileY);
    root.isPickable = true;
    root.visibility = 0;

    const barkMat  = flatMat(`log-bark-${id}`,  barkColor,  scene);
    const grainMat = flatMat(`log-grain-${id}`, grainColor, scene);

    // Main log cylinder — rotated to lie along the X axis, angled 30° around Y
    const body = MeshBuilder.CreateCylinder(`log-body-${id}`, {
      diameter: 0.16, height: 0.46, tessellation: 8,
    }, scene);
    body.material = barkMat;
    body.convertToFlatShadedMesh();
    body.rotation.z = Math.PI / 2;   // lie along X
    body.rotation.y = Math.PI / 5;   // angle 36°
    body.position   = new Vector3(0, 0.08, 0);  // sit radius-height above ground
    body.isPickable = false;
    body.parent = root;

    // End-grain disc on one end (cross-section showing the wood interior)
    const endCap = MeshBuilder.CreateCylinder(`log-cap-${id}`, {
      diameter: 0.155, height: 0.018, tessellation: 8,
    }, scene);
    endCap.material = grainMat;
    endCap.convertToFlatShadedMesh();
    endCap.rotation.z = Math.PI / 2;
    endCap.rotation.y = Math.PI / 5;
    // Offset along the log's local axis (approx)
    const ox = Math.cos(Math.PI / 5) * 0.23;
    const oz = -Math.sin(Math.PI / 5) * 0.23;
    endCap.position = new Vector3(ox, 0.08, oz);
    endCap.isPickable = false;
    endCap.parent = root;

    return root;
  }

  // ---- Axe (lying flat on the ground) ----

  private buildAxe(id: string, tileX: number, tileY: number, scene: Scene): Mesh {
    const root = MeshBuilder.CreateBox(`item-${id}`, { width: 0.52, height: 0.08, depth: 0.52 }, scene);
    root.position = new Vector3(tileX, 0.04, tileY);
    root.isPickable = true;
    root.visibility = 0;

    const woodMat  = flatMat(`axe-wood-${id}`,  new Color3(0.40, 0.22, 0.09), scene);
    const ironMat  = flatMat(`axe-iron-${id}`,  new Color3(0.18, 0.18, 0.20), scene);
    const edgeMat  = flatMat(`axe-edge-${id}`,  new Color3(0.55, 0.58, 0.62), scene);

    // Handle — long, lying diagonally (NW–SE axis)
    const handle = box(`axe-handle-${id}`, 0.05, 0.025, 0.34, 0.04, 0, 0.04, woodMat, scene);
    handle.rotation.y = Math.PI / 4;
    handle.parent = root;

    // Head body — at the NW end of handle, perpendicular
    const head = box(`axe-head-${id}`, 0.04, 0.030, 0.16, -0.165, 0, -0.165, ironMat, scene);
    head.rotation.y = Math.PI / 4;
    head.parent = root;

    // Blade edge — tapered wedge showing the sharpened bit extending to one side
    const blade = box(`axe-blade-${id}`, 0.035, 0.020, 0.09, -0.215, 0, -0.215, edgeMat, scene);
    blade.rotation.y = Math.PI / 4;
    blade.parent = root;

    return root;
  }

  // ---- Pickaxe (lying flat on the ground) ----

  private buildPickaxe(id: string, tileX: number, tileY: number, scene: Scene): Mesh {
    const root = MeshBuilder.CreateBox(`item-${id}`, { width: 0.55, height: 0.08, depth: 0.55 }, scene);
    root.position = new Vector3(tileX, 0.04, tileY);
    root.isPickable = true;
    root.visibility = 0;

    const brownMat    = flatMat(`pick-brown-${id}`,  new Color3(0.48, 0.25, 0.12), scene);
    const ironMat     = flatMat(`pick-iron-${id}`,   new Color3(0.40, 0.52, 0.62), scene);
    const ironLightMat = flatMat(`pick-ironL-${id}`, new Color3(0.65, 0.76, 0.85), scene);

    // Handle — thin diagonal box
    const handle = box(`pick-handle-${id}`, 0.05, 0.04, 0.34, 0, 0, 0, brownMat, scene);
    handle.rotation.y = Math.PI / 4;
    handle.parent = root;

    // Crosspiece — T-bar across handle
    const head = box(`pick-head-${id}`, 0.26, 0.04, 0.07, 0.10, 0, -0.10, ironMat, scene);
    head.rotation.y = Math.PI / 4;
    head.parent = root;

    // Pick spike — tapered cone, the sharp pick end
    const spike = MeshBuilder.CreateCylinder(`pick-spike-${id}`,
      { height: 0.15, diameterTop: 0, diameterBottom: 0.05, tessellation: 5 }, scene);
    spike.material = ironLightMat;
    spike.convertToFlatShadedMesh();
    spike.rotation.z = Math.PI / 2;
    spike.rotation.y = Math.PI / 4;
    spike.position = new Vector3(0.21, 0, -0.21);
    spike.isPickable = false;
    spike.parent = root;

    return root;
  }

  // ---- Generic fallback ----

  private buildGeneric(id: string, itemId: string, tileX: number, tileY: number, scene: Scene): Mesh {
    const mesh = MeshBuilder.CreateBox(`item-${id}`, { width: 0.3, height: 0.08, depth: 0.3 }, scene);
    mesh.position = new Vector3(tileX, 0.04, tileY);
    mesh.isPickable = true;

    const mat = new StandardMaterial(`item-mat-${id}`, scene);
    // Use a color from the item id if recognizable
    const COLOR_MAP: Record<string, [number, number, number]> = {
      coins:       [1.0, 0.85, 0.0],
      copper_ore:  [0.76, 0.38, 0.12],
      tin_ore:     [0.56, 0.56, 0.56],
      iron_ore:    [0.38, 0.31, 0.31],
      bronze_bar:  [0.76, 0.47, 0.25],
      iron_bar:    [0.50, 0.50, 0.50],
    };
    const c = COLOR_MAP[itemId];
    mat.diffuseColor = c ? new Color3(c[0], c[1], c[2]) : new Color3(0.8, 0.7, 0.3);
    mesh.material = mat;
    return mesh;
  }

  dispose(): void {
    this.root.getChildMeshes().forEach(m => { m.material?.dispose(); m.dispose(); });
    this.root.material?.dispose();
    this.root.dispose();
  }
}

function box(
  name: string, w: number, h: number, d: number,
  px: number, py: number, pz: number,
  mat: StandardMaterial, scene: Scene,
): Mesh {
  const mesh = MeshBuilder.CreateBox(name, { width: w, height: h, depth: d }, scene);
  mesh.position = new Vector3(px, py, pz);
  mesh.material = mat;
  mesh.isPickable = false;
  mesh.convertToFlatShadedMesh();
  return mesh;
}

function flatMat(name: string, color: Color3, scene: Scene): StandardMaterial {
  const mat = new StandardMaterial(name, scene);
  mat.diffuseColor = color;
  return mat;
}
