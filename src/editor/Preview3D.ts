import {
  Engine, Scene, HemisphericLight, DirectionalLight,
  Vector3, Color3, Color4, ArcRotateCamera,
  ArcRotateCameraPointersInput,
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
  private state: EditorState;

  constructor(canvas: HTMLCanvasElement, state: EditorState) {
    this.state  = state;
    this.engine = new Engine(canvas, true, { preserveDrawingBuffer: false, stencil: false });
    this.scene  = new Scene(this.engine);
    this.scene.clearColor = new Color4(0.45, 0.65, 0.85, 1);

    // Lighting
    const hemi = new HemisphericLight('hemi', new Vector3(0, 1, 0), this.scene);
    hemi.intensity = 0.8;
    hemi.groundColor = new Color3(0.2, 0.2, 0.25);

    const sun = new DirectionalLight('sun', new Vector3(-1, -2, -1), this.scene);
    sun.intensity = 0.6;

    // Camera — ArcRotate orbiting map center.
    // Configure so right-drag rotates and left-drag pans, leaving left-click free for painting.
    const cx = state.width  / 2;
    const cz = state.height / 2;
    const camera = new ArcRotateCamera('cam', -Math.PI / 3, Math.PI / 3.5, 40, new Vector3(cx, 0, cz), this.scene);
    camera.lowerRadiusLimit = 3;
    camera.upperRadiusLimit = 200;
    camera.upperBetaLimit   = Math.PI / 2.1;
    camera.wheelPrecision   = 10;

    // Remap buttons: right-button (2) rotates, middle (1) pans. This frees left-button for painting.
    const ptrs = camera.inputs.attached['pointers'] as ArcRotateCameraPointersInput;
    if (ptrs) {
      ptrs.buttons = [2, 1];     // right-click = rotate / orbit, middle = also rotate
    }
    camera.attachControl(canvas, true);
    camera.panningSensibility = 50;

    this.engine.runRenderLoop(() => this.scene.render());
    window.addEventListener('resize', () => this.engine.resize());

    // Left-click-to-paint: detect short (non-drag) clicks on the terrain pick mesh
    this.bindPaintEvents(canvas);

    // Initial build
    this.rebuild(state);
  }

  // Debounced refresh — at most once per 600ms after a map change
  scheduleRefresh(): void {
    if (this.refreshTimer !== null) clearTimeout(this.refreshTimer);
    this.refreshTimer = setTimeout(() => {
      this.refreshTimer = null;
      this.rebuild(this.state);
    }, 600);
  }

  private rebuild(state: EditorState): void {
    // Dispose previous world
    if (this.worldRoot) {
      this.worldRoot.getChildMeshes(false).forEach(m => m.dispose());
      this.worldRoot.dispose();
      this.worldRoot = null;
    }

    const worldState = createWorldFromTiles(state.tiles, Array.from(state.vertexHeights));
    this.worldRoot   = buildWorldMeshes(worldState, this.scene);
  }

  private bindPaintEvents(canvas: HTMLCanvasElement): void {
    let downX = 0, downY = 0;
    const DRAG_THRESHOLD = 4; // px — smaller movement = click, larger = drag (orbit)

    canvas.addEventListener('mousedown', (e) => {
      if (e.button === 0) { downX = e.offsetX; downY = e.offsetY; }
    });

    canvas.addEventListener('mouseup', (e) => {
      if (e.button !== 0) return;
      const dx = e.offsetX - downX;
      const dy = e.offsetY - downY;
      if (Math.sqrt(dx * dx + dy * dy) >= DRAG_THRESHOLD) return;

      // Short click on left button — raycast and paint/erase
      const pickInfo = this.scene.pick(e.offsetX, e.offsetY, m => m.name === 'ground');
      if (!pickInfo.hit || !pickInfo.pickedPoint) return;

      // The ground mesh is offset by (W/2 - 0.5, H/2 - 0.5), so world coords map to tile coords directly
      const tx = Math.round(pickInfo.pickedPoint.x);
      const tz = Math.round(pickInfo.pickedPoint.z);
      if (tx < 0 || tz < 0 || tx >= this.state.width || tz >= this.state.height) return;

      this.state.beginStroke();
      if (e.ctrlKey) {
        this.state.eraseAt(tx, tz);
      } else {
        this.state.paintAt(tx, tz);
      }
      this.state.endStroke();
      this.scheduleRefresh();
    });
  }

  dispose(): void {
    if (this.refreshTimer !== null) clearTimeout(this.refreshTimer);
    this.engine.dispose();
  }
}
