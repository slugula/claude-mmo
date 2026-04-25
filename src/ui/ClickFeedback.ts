interface Marker {
  el: HTMLElement;
  startTime: number;
}

export type ClickMarkerColor = 'red' | 'yellow';

const DURATION = 450;
const SIZE = 18;

export class ClickFeedback {
  private markers: Marker[] = [];

  spawn(screenX: number, screenY: number, color: ClickMarkerColor): void {
    const el = document.createElement('div');
    const fill = color === 'red' ? 'rgba(255,60,60,0.55)' : 'rgba(255,220,80,0.55)';
    const border = color === 'red' ? 'rgba(255,30,30,0.9)' : 'rgba(255,200,40,0.9)';
    el.style.cssText = `
      position: fixed;
      left: ${screenX}px;
      top: ${screenY}px;
      width: ${SIZE}px;
      height: ${SIZE}px;
      background: ${fill};
      border: 1.5px solid ${border};
      border-radius: 50%;
      pointer-events: none;
      z-index: 1500;
      transform: translate(-50%, -50%);
    `;
    document.body.appendChild(el);
    this.markers.push({ el, startTime: performance.now() });
  }

  update(): void {
    this.markers = this.markers.filter(m => {
      const t = (performance.now() - m.startTime) / DURATION;
      if (t >= 1) { m.el.remove(); return false; }
      m.el.style.opacity = String(Math.max(0, 1 - t));
      m.el.style.transform = `translate(-50%, -50%) scale(${1 + t * 0.6})`;
      return true;
    });
  }
}
