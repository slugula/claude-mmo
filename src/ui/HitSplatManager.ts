import { Scene, Vector3, Matrix } from '@babylonjs/core';

interface Splat {
  el: HTMLElement;
  worldX: number;
  worldZ: number;
  baseWorldY: number;
  spawnTime: number;
  animStartTime: number;
}

const DELAY_MS = 500;
const ANIM_MS  = 1000;

export class HitSplatManager {
  private readonly scene: Scene;
  private splats: Splat[] = [];

  constructor(scene: Scene) {
    this.scene = scene;
  }

  spawn(damage: number, worldX: number, worldZ: number, worldY = 0.9): void {
    const isZero = damage === 0;
    const bg     = isZero ? '#1a44cc' : '#cc0000';
    const border = isZero ? '#0d2888' : '#880000';

    const el = document.createElement('div');
    el.textContent = String(damage);
    el.style.cssText = `
      position: fixed;
      background: ${bg};
      color: #ffffff;
      border: 2px solid ${border};
      border-radius: 50%;
      width: 26px;
      height: 26px;
      display: flex;
      align-items: center;
      justify-content: center;
      font-weight: 700;
      font-size: 12px;
      font-family: 'Segoe UI', system-ui, sans-serif;
      pointer-events: none;
      z-index: 500;
      transform: translate(-50%, -50%);
      text-shadow: 1px 1px 0 #000;
      box-shadow: 0 1px 4px rgba(0,0,0,0.7);
      opacity: 1;
    `;
    document.body.appendChild(el);

    const now = performance.now();
    // Position immediately at entity location (no animation yet)
    const screen = this.worldToScreen(worldX, worldY, worldZ);
    if (screen.z >= 0 && screen.z <= 1) {
      el.style.left = `${screen.x}px`;
      el.style.top  = `${screen.y}px`;
    }

    this.splats.push({
      el,
      worldX,
      worldZ,
      baseWorldY: worldY,
      spawnTime: now,
      animStartTime: now + DELAY_MS,
    });
  }

  update(): void {
    const now = performance.now();
    this.splats = this.splats.filter(s => {
      const age = now - s.spawnTime;
      if (age > DELAY_MS + ANIM_MS) { s.el.remove(); return false; }

      const animAge = Math.max(0, now - s.animStartTime);
      const animT   = animAge / ANIM_MS;

      const animatedY = s.baseWorldY + animT * 0.9;
      const screen = this.worldToScreen(s.worldX, animatedY, s.worldZ);

      if (screen.z < 0 || screen.z > 1) {
        s.el.style.display = 'none';
      } else {
        s.el.style.display = 'flex';
        s.el.style.left    = `${screen.x}px`;
        s.el.style.top     = `${screen.y}px`;
        s.el.style.opacity = String(Math.max(0, 1 - animT * animT * 1.5));
      }
      return true;
    });
  }

  private worldToScreen(worldX: number, worldY: number, worldZ: number): { x: number; y: number; z: number } {
    const engine   = this.scene.getEngine();
    const viewport = this.scene.activeCamera!.viewport.toGlobal(
      engine.getRenderWidth(),
      engine.getRenderHeight(),
    );
    const projected = Vector3.Project(
      new Vector3(worldX, worldY, worldZ),
      Matrix.Identity(),
      this.scene.getTransformMatrix(),
      viewport,
    );
    return { x: projected.x, y: projected.y, z: projected.z };
  }
}
