import { ArcRotateCamera, Scene, Vector3 } from '@babylonjs/core';
import {
  CAMERA_MIN_RADIUS, CAMERA_MAX_RADIUS,
  CAMERA_MIN_BETA,   CAMERA_MAX_BETA,
  CAMERA_ROTATE_SPEED,
  CAMERA_ZOOM_SPEED,
  CAMERA_DRAG_SENSITIVITY,
} from '../shared/constants';

const CAMERA_FOLLOW_SPEED = 10; // higher = snappier follow

export class GameCamera {
  readonly camera: ArcRotateCamera;

  private targetAlpha: number;
  private targetBeta: number;
  private targetRadius: number;

  // Smoothly-tracked camera look-at position (chases player each frame)
  private currentTarget: Vector3;

  private isDragging = false;
  private lastPointerX = 0;
  private lastPointerY = 0;

  constructor(scene: Scene, canvas: HTMLCanvasElement, initialTarget: Vector3) {
    this.targetAlpha  = -Math.PI / 4;
    this.targetBeta   = Math.PI / 3.5;
    this.targetRadius = 14;
    this.currentTarget = initialTarget.clone();

    this.camera = new ArcRotateCamera(
      'game-camera',
      this.targetAlpha,
      this.targetBeta,
      this.targetRadius,
      this.currentTarget.clone(),
      scene,
    );

    // attachControl initialises internal camera matrices; clear inputs after
    // so only our custom handlers run.
    this.camera.attachControl(canvas, true);
    this.camera.inputs.clear();

    this.setupPointerEvents(canvas);
    this.setupWheelZoom(canvas);
  }

  private setupPointerEvents(canvas: HTMLCanvasElement): void {
    canvas.addEventListener('pointerdown', (e) => {
      if (e.button === 1) {
        e.preventDefault();
        this.isDragging = true;
        this.lastPointerX = e.clientX;
        this.lastPointerY = e.clientY;
        canvas.setPointerCapture(e.pointerId);
      }
    });

    canvas.addEventListener('pointermove', (e) => {
      if (!this.isDragging) return;
      const dx = e.clientX - this.lastPointerX;
      const dy = e.clientY - this.lastPointerY;
      this.lastPointerX = e.clientX;
      this.lastPointerY = e.clientY;

      this.targetAlpha -= dx * CAMERA_DRAG_SENSITIVITY;
      // Inverted: drag up (negative dy) → higher beta (more horizontal view)
      this.targetBeta = clamp(
        this.targetBeta - dy * CAMERA_DRAG_SENSITIVITY,
        CAMERA_MIN_BETA, CAMERA_MAX_BETA,
      );
    });

    canvas.addEventListener('pointerup', (e) => {
      if (e.button === 1) {
        this.isDragging = false;
        canvas.releasePointerCapture(e.pointerId);
      }
    });
  }

  private setupWheelZoom(canvas: HTMLCanvasElement): void {
    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.targetRadius = clamp(
        this.targetRadius + e.deltaY * CAMERA_ZOOM_SPEED,
        CAMERA_MIN_RADIUS, CAMERA_MAX_RADIUS,
      );
    }, { passive: false });
  }

  /** Instantly move the camera to a position — no lerp. Call on first login. */
  snapTo(position: Vector3): void {
    this.currentTarget.copyFrom(position);
    this.camera.target.copyFrom(position);
  }

  update(dt: number, heldKeys: Set<string>, playerPosition: Vector3): void {
    // Arrow key camera rotation
    const rotStep = CAMERA_ROTATE_SPEED * dt;
    if (heldKeys.has('ArrowLeft'))  this.targetAlpha -= rotStep;
    if (heldKeys.has('ArrowRight')) this.targetAlpha += rotStep;
    if (heldKeys.has('ArrowUp'))    this.targetBeta = clamp(this.targetBeta - rotStep, CAMERA_MIN_BETA, CAMERA_MAX_BETA);
    if (heldKeys.has('ArrowDown'))  this.targetBeta = clamp(this.targetBeta + rotStep, CAMERA_MIN_BETA, CAMERA_MAX_BETA);

    // Smooth alpha / beta / radius
    const snap = 1 - Math.pow(0.001, dt);
    this.camera.alpha  += (this.targetAlpha  - this.camera.alpha)  * snap;
    this.camera.beta   += (this.targetBeta   - this.camera.beta)   * snap;
    this.camera.radius += (this.targetRadius - this.camera.radius) * snap;

    // Smooth camera follow — exponential decay so the camera never lags far
    // behind no matter how fast the player walks.
    const followT = 1 - Math.exp(-CAMERA_FOLLOW_SPEED * dt);
    this.currentTarget.x += (playerPosition.x - this.currentTarget.x) * followT;
    this.currentTarget.y += (playerPosition.y - this.currentTarget.y) * followT;
    this.currentTarget.z += (playerPosition.z - this.currentTarget.z) * followT;
    this.camera.target.copyFrom(this.currentTarget);
  }
}

function clamp(v: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, v));
}
