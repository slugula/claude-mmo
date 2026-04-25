import { Scene, Vector3, Matrix } from '@babylonjs/core';

const DURATION_TICKS = 50; // 10 seconds at 200ms/tick
const FADE_TICKS = 10;

export class OverheadChat {
  private readonly scene: Scene;
  private readonly el: HTMLElement;

  constructor(scene: Scene) {
    this.scene = scene;

    this.el = document.createElement('div');
    this.el.style.cssText = `
      position: fixed;
      pointer-events: none;
      z-index: 300;
      color: #ffe066;
      font-family: 'Segoe UI', system-ui, sans-serif;
      font-size: 13px;
      font-weight: 700;
      text-shadow:
        1px  1px 2px rgba(0,0,0,0.95),
       -1px -1px 2px rgba(0,0,0,0.95),
        1px -1px 2px rgba(0,0,0,0.95),
       -1px  1px 2px rgba(0,0,0,0.95);
      transform: translate(-50%, -100%);
      white-space: nowrap;
      display: none;
    `;
    document.body.appendChild(this.el);
  }

  update(
    chatMessage: string,
    chatMessageTick: number,
    worldX: number,
    worldY: number,
    worldZ: number,
    currentTick: number,
  ): void {
    const age = currentTick - chatMessageTick;
    if (!chatMessage || age < 0 || age >= DURATION_TICKS) {
      this.el.style.display = 'none';
      return;
    }

    this.el.textContent = chatMessage;

    const screen = this.worldToScreen(worldX, worldY, worldZ);
    if (screen.z < 0 || screen.z > 1) {
      this.el.style.display = 'none';
      return;
    }

    this.el.style.display = 'block';
    this.el.style.left = `${screen.x}px`;
    this.el.style.top  = `${screen.y - 16}px`;

    const fadeStart = DURATION_TICKS - FADE_TICKS;
    const opacity = age >= fadeStart
      ? 1 - (age - fadeStart) / FADE_TICKS
      : 1;
    this.el.style.opacity = String(Math.max(0, opacity));
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

  dispose(): void {
    this.el.remove();
  }
}
