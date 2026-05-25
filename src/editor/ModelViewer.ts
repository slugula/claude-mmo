import {
  Engine, Scene, HemisphericLight, DirectionalLight,
  Vector3, Color3, Color4, ArcRotateCamera, MeshBuilder, StandardMaterial,
} from '@babylonjs/core';

export class ModelViewer {
  private engine: Engine;
  private scene:  Scene;

  constructor(canvas: HTMLCanvasElement) {
    this.engine = new Engine(canvas, true, { preserveDrawingBuffer: false });
    this.scene  = new Scene(this.engine);
    this.scene.clearColor = new Color4(0.06, 0.06, 0.10, 1);

    const hemi = new HemisphericLight('hemi', new Vector3(0, 1, 0), this.scene);
    hemi.intensity = 0.7;

    const sun = new DirectionalLight('sun', new Vector3(-1, -2, 0.5), this.scene);
    sun.intensity = 0.5;

    const camera = new ArcRotateCamera('cam', Math.PI / 4, Math.PI / 3, 2.5, Vector3.Zero(), this.scene);
    camera.attachControl(canvas, true);
    camera.lowerRadiusLimit = 0.5;
    camera.upperRadiusLimit = 8;

    // Slow auto-rotation
    this.scene.registerBeforeRender(() => { camera.alpha += 0.008; });

    this.engine.runRenderLoop(() => this.scene.render());
    window.addEventListener('resize', () => this.engine.resize());
  }

  load(objectType: string): void {
    // Clear existing meshes (keep lights/camera)
    for (const m of this.scene.meshes.slice()) {
      if (!m.name.startsWith('cam') && !m.name.startsWith('hemi') && !m.name.startsWith('sun')) {
        m.dispose();
      }
    }

    switch (objectType) {
      case 'tree':         this.buildTree();        break;
      case 'rock':         this.buildRock();        break;
      case 'chest':        this.buildChest();       break;
      case 'fishing_spot': this.buildFishingSpot(); break;
      case 'water':        this.buildWaterTile();   break;
      case 'wall':         this.buildWall();        break;
      default:             this.buildNPCPlaceholder(objectType); break;
    }
  }

  private buildTree(): void {
    const trunkMat = new StandardMaterial('tm', this.scene);
    trunkMat.diffuseColor = new Color3(0.29, 0.18, 0.08);
    const trunk = MeshBuilder.CreateCylinder('trunk', { height: 0.6, diameter: 0.18, tessellation: 6 }, this.scene);
    trunk.position.y = -0.05;
    trunk.material   = trunkMat;
    trunk.convertToFlatShadedMesh();

    const canopyMat = new StandardMaterial('cm', this.scene);
    canopyMat.diffuseColor = new Color3(0.1, 0.38, 0.07);
    const canopy = MeshBuilder.CreateSphere('canopy', { diameter: 0.72, segments: 4 }, this.scene);
    canopy.position.y = 0.55;
    canopy.material   = canopyMat;
    canopy.convertToFlatShadedMesh();
  }

  private buildRock(): void {
    const mat = new StandardMaterial('rm', this.scene);
    mat.diffuseColor = new Color3(0.48, 0.46, 0.44);
    const rock = MeshBuilder.CreateBox('rock', { width: 0.55, height: 0.32, depth: 0.48 }, this.scene);
    rock.material = mat;
    rock.convertToFlatShadedMesh();
  }

  private buildChest(): void {
    const mat = new StandardMaterial('chm', this.scene);
    mat.diffuseColor = new Color3(0.95, 0.45, 0.05);
    MeshBuilder.CreateBox('chest', { width: 0.55, height: 0.55, depth: 0.4 }, this.scene).material = mat;
  }

  private buildFishingSpot(): void {
    const mat = new StandardMaterial('fm', this.scene);
    mat.diffuseColor  = new Color3(0.18, 0.48, 0.90);
    mat.emissiveColor = new Color3(0.03, 0.1, 0.28);
    const disk = MeshBuilder.CreateCylinder('fish', { diameter: 0.9, height: 0.05, tessellation: 16 }, this.scene);
    disk.material = mat;
  }

  private buildWaterTile(): void {
    const mat = new StandardMaterial('wm', this.scene);
    mat.diffuseColor  = new Color3(0.18, 0.48, 0.90);
    mat.emissiveColor = new Color3(0.03, 0.1, 0.28);
    const tile = MeshBuilder.CreateGround('water', { width: 0.9, height: 0.9 }, this.scene);
    tile.material = mat;
  }

  private buildWall(): void {
    const mat = new StandardMaterial('wlm', this.scene);
    mat.diffuseColor = new Color3(0.85, 0.82, 0.76);
    const wall = MeshBuilder.CreateBox('wall', { width: 0.9, height: 0.9, depth: 0.08 }, this.scene);
    wall.material = mat;
  }

  private buildNPCPlaceholder(kind: string): void {
    // Simple humanoid stand-in: box body + sphere head
    const bodyMat = new StandardMaterial('bm', this.scene);
    bodyMat.diffuseColor = new Color3(0.3, 0.5, 0.8);
    const body = MeshBuilder.CreateBox('body', { width: 0.35, height: 0.5, depth: 0.2 }, this.scene);
    body.position.y = 0.05;
    body.material = bodyMat;

    const headMat = new StandardMaterial('hm', this.scene);
    headMat.diffuseColor = new Color3(0.9, 0.75, 0.6);
    const head = MeshBuilder.CreateSphere('head', { diameter: 0.28, segments: 4 }, this.scene);
    head.position.y = 0.44;
    head.material = headMat;
    void kind; // suppress unused warning
  }

  dispose(): void {
    this.engine.dispose();
  }
}
