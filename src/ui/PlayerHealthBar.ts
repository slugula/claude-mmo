import { Scene, Vector3, Matrix } from '@babylonjs/core';

const DURATION_MS = 5000;
const BAR_WIDTH   = 44;
const BAR_HEIGHT  = 6;

/**
 * A single DOM health bar that follows the local player.
 * Appears after the player is hit (matching NPC health bar rules) and fades after DURATION_MS.
 */
export class PlayerHealthBar {
  private readonly scene: Scene;
  private readonly wrap: HTMLElement;
  private readonly greenEl: HTMLElement;
  private lastHitTime = -Infinity;
  private visible = false;

  constructor(scene: Scene) {
    this.scene = scene;

    this.wrap = document.createElement('div');
    this.wrap.style.cssText = `
      position: fixed;
      width: ${BAR_WIDTH}px;
      height: ${BAR_HEIGHT}px;
      background: #cc1a1a;
      border: 1px solid #000;
      pointer-events: none;
      z-index: 450;
      transform: translate(-50%, -100%);
      box-shadow: 0 1px 2px rgba(0,0,0,0.6);
      display: none;
    `;
    this.greenEl = document.createElement('div');
    this.greenEl.style.cssText = `height: 100%; width: 100%; background: #2ecc2e;`;
    this.wrap.appendChild(this.greenEl);
    document.body.appendChild(this.wrap);
  }

  /** Call whenever the player takes a hit — starts/resets the visibility timer. */
  recordHit(): void {
    this.lastHitTime = performance.now();
  }

  /** Call every frame with the player's world position (including terrain Y) and current hp/maxHp. */
  update(worldX: number, worldY: number, worldZ: number, hp: number, maxHp: number): void {
    const now = performance.now();
    const expired = now - this.lastHitTime > DURATION_MS;

    if (expired) {
      if (this.visible) {
        this.wrap.style.display = 'none';
        this.visible = false;
      }
      return;
    }

    const ratio = maxHp > 0 ? Math.max(0, Math.min(1, hp / maxHp)) : 0;
    this.greenEl.style.width = `${ratio * 100}%`;

    const screen = this.worldToScreen(worldX, worldY, worldZ);
    if (screen.z < 0 || screen.z > 1) {
      this.wrap.style.display = 'none';
      this.visible = false;
    } else {
      this.wrap.style.display = 'block';
      this.visible = true;
      this.wrap.style.left = `${screen.x}px`;
      this.wrap.style.top  = `${screen.y}px`;
    }
  }

  destroy(): void {
    this.wrap.remove();
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
