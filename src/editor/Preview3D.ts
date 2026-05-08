import {
  Engine, Scene, HemisphericLight, DirectionalLight,
  Vector3, Color3, Color4, ArcRotateCamera,
} from '@babylonjs/core';
import type { Mesh } from '@babylonjs/core';
import { buildWorldMeshes } from '../world/World';
import { createWorldFromTiles } from '../world/WorldState';
import type { EditorState } from './EditorState';

export class Preview3D {
  private engine: Engine;
  private scene:  Scene;
  private worldRoot: Mesh | null = null;
  private refreshTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(canvas: HTMLCanvasElement, state: EditorState) {
    this.engine = new Engine(canvas, true, { preserveDrawingBuffer: false, stencil: false });
    this.scene  = new Scene(this.engine);
    this.scene.clearColor = new Color4(0.45, 0.65, 0.85, 1);

    // Lighting
    const hemi = new HemisphericLight('hemi', new Vector3(0, 1, 0), this.scene);
    hemi.intensity = 0.8;
    hemi.groundColor = new Color3(0.2, 0.2, 0.25);

    const sun = new DirectionalLight('sun', new Vector3(-1, -2, -1), this.scene);
    sun.intensity = 0.6;

    // Camera — ArcRotate, orbits around map center
    const cx = state.width  / 2;
    const cz = state.height / 2;
    const camera = new ArcRotateCamera('cam', -Math.PI / 3, Math.PI / 3.5, 40, new Vector3(cx, 0, cz), this.scene);
    camera.attachControl(canvas, true);
    camera.lowerRadiusLimit = 3;
    camera.upperRadiusLimit = 200;
    camera.upperBetaLimit   = Math.PI / 2.1;
    camera.wheelPrecision   = 10;

    this.engine.runRenderLoop(() => this.scene.render());

    window.addEventListener('resize', () => this.engine.resize());

    // Initial build
    this.rebuild(state);
  }

  // Debounced refresh — at most once per 600ms after a map change
  scheduleRefresh(state: EditorState): void {
    if (this.refreshTimer !== null) clearTimeout(this.refreshTimer);
    this.refreshTimer = setTimeout(() => {
      this.refreshTimer = null;
      this.rebuild(state);
    }, 600);
  }

  private rebuild(state: EditorState): void {
    // Dispose previous world
    if (this.worldRoot) {
      this.worldRoot.getChildMeshes(false).forEach(m => m.dispose());
      this.worldRoot.dispose();
      this.worldRoot = null;
    }

    const worldState = createWorldFromTiles(state.tiles);
    this.worldRoot   = buildWorldMeshes(worldState, this.scene);
  }

  dispose(): void {
    if (this.refreshTimer !== null) clearTimeout(this.refreshTimer);
    this.engine.dispose();
  }
}
