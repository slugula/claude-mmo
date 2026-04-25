import { Scene, Vector3, Matrix } from '@babylonjs/core';
import type { NPCState } from '../shared/types';
import { getNPCDef } from '../npcs/NPCRegistry';

interface Bar {
  wrap: HTMLElement;
  greenEl: HTMLElement;
  lastHitTime: number;
}

const DURATION_MS = 5000;
const BAR_WIDTH = 44;
const BAR_HEIGHT = 6;

export class HealthBarManager {
  private readonly scene: Scene;
  private bars = new Map<string, Bar>();

  constructor(scene: Scene) {
    this.scene = scene;
  }

  recordHit(npcId: string): void {
    const now = performance.now();
    let bar = this.bars.get(npcId);
    if (!bar) {
      bar = this.createBar();
      this.bars.set(npcId, bar);
    }
    bar.lastHitTime = now;
  }

  update(npcs: NPCState[]): void {
    const now = performance.now();
    const byId = new Map(npcs.map(n => [n.id, n] as const));

    for (const [id, bar] of this.bars) {
      const npc = byId.get(id);
      const expired = now - bar.lastHitTime > DURATION_MS;
      if (!npc || expired) {
        bar.wrap.remove();
        this.bars.delete(id);
        continue;
      }

      const def = getNPCDef(npc.kind);
      const ratio = Math.max(0, Math.min(1, npc.hp / def.maxHp));
      bar.greenEl.style.width = `${ratio * 100}%`;

      const screen = this.worldToScreen(npc.tileX, npc.tileY);
      if (screen.z < 0 || screen.z > 1) {
        bar.wrap.style.display = 'none';
      } else {
        bar.wrap.style.display = 'block';
        bar.wrap.style.left = `${screen.x}px`;
        bar.wrap.style.top  = `${screen.y}px`;
      }
    }
  }

  private createBar(): Bar {
    const wrap = document.createElement('div');
    wrap.style.cssText = `
      position: fixed;
      width: ${BAR_WIDTH}px;
      height: ${BAR_HEIGHT}px;
      background: #cc1a1a;
      border: 1px solid #000;
      pointer-events: none;
      z-index: 450;
      transform: translate(-50%, -100%);
      box-shadow: 0 1px 2px rgba(0,0,0,0.6);
    `;
    const green = document.createElement('div');
    green.style.cssText = `
      height: 100%;
      width: 100%;
      background: #2ecc2e;
    `;
    wrap.appendChild(green);
    document.body.appendChild(wrap);
    return { wrap, greenEl: green, lastHitTime: performance.now() };
  }

  private worldToScreen(worldX: number, worldZ: number): { x: number; y: number; z: number } {
    const engine   = this.scene.getEngine();
    const viewport = this.scene.activeCamera!.viewport.toGlobal(
      engine.getRenderWidth(),
      engine.getRenderHeight(),
    );
    const projected = Vector3.Project(
      new Vector3(worldX, 1.5, worldZ),
      Matrix.Identity(),
      this.scene.getTransformMatrix(),
      viewport,
    );
    return { x: projected.x, y: projected.y, z: projected.z };
  }
}
