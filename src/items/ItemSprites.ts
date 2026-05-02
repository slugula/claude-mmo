type DrawFn = (ctx: CanvasRenderingContext2D, w: number, h: number) => void;

const ITEM_COLORS: Record<string, string> = {
  coins:       '#ffd700',
  copper_ore:  '#c06020',
  tin_ore:     '#909090',
  iron_ore:    '#605050',
  bronze_bar:  '#c07840',
  iron_bar:    '#808080',
  raw_shrimp:  '#f0b060',
  shrimp:      '#f0d080',
  raw_trout:   '#609090',
  trout:       '#90c0c0',
  tinderbox:       '#c04000',
  arrow:           '#c09040',
  kinetic_charges: '#00cfff',
};

const CUSTOM_SPRITES: Record<string, DrawFn> = {

  // ---- Pickaxe ----
  pickaxe: (ctx, w, h) => {
    // Handle — diagonal, center-bottom to upper-left
    ctx.save();
    ctx.translate(w * 0.54, h * 0.60);
    ctx.rotate(-Math.PI / 5);

    const hw = w * 0.13;
    const hh = h * 0.66;
    ctx.fillStyle = '#6a3810';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(-hw / 2, -hh / 2, hw, hh, 2);
    ctx.fill();
    ctx.stroke();
    // Wood grain
    ctx.fillStyle = '#9a5828';
    ctx.fillRect(-hw / 2 + 2, -hh / 2 + 4, 2, hh - 8);
    ctx.restore();

    // Head — arched pick head at upper area
    const hcx = w * 0.38;
    const hcy = h * 0.22;

    // Arch body — three segments suggesting the curve
    ctx.fillStyle = '#506880';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1.2;
    // Centre raised section
    ctx.beginPath();
    ctx.roundRect(hcx - w * 0.10, hcy - h * 0.10, w * 0.20, h * 0.14, 2);
    ctx.fill();
    ctx.stroke();
    // Left arm angling down
    ctx.beginPath();
    ctx.moveTo(hcx - w * 0.10, hcy - h * 0.06);
    ctx.lineTo(w * 0.06, hcy + h * 0.10);
    ctx.lineTo(w * 0.10, hcy + h * 0.10);
    ctx.lineTo(hcx - w * 0.06, hcy - h * 0.02);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    // Right arm angling down-right
    ctx.beginPath();
    ctx.moveTo(hcx + w * 0.10, hcy - h * 0.06);
    ctx.lineTo(w * 0.88, hcy + h * 0.06);
    ctx.lineTo(w * 0.86, hcy + h * 0.10);
    ctx.lineTo(hcx + w * 0.06, hcy - h * 0.02);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    // Sharp pick point — left end
    ctx.fillStyle = '#7a9cb4';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(w * 0.10, hcy + h * 0.10);
    ctx.lineTo(w * 0.01, hcy + h * 0.03);
    ctx.lineTo(w * 0.06, hcy + h * 0.10);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    // Blunt poll — right end
    ctx.fillStyle = '#4a6070';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.rect(w * 0.84, hcy + h * 0.02, w * 0.09, h * 0.12);
    ctx.fill();
    ctx.stroke();

    // Metal highlight
    ctx.fillStyle = 'rgba(180,210,230,0.35)';
    ctx.fillRect(hcx - w * 0.09, hcy - h * 0.09, w * 0.18, 2);
  },

  // ---- Axe ----
  axe: (ctx, w, h) => {
    // Handle — diagonal, from bottom-center to upper-left
    ctx.save();
    ctx.translate(w * 0.58, h * 0.68);
    ctx.rotate(-Math.PI * 0.30);

    const hw = w * 0.14;
    const hh = h * 0.72;
    ctx.fillStyle = '#5c2e0a';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(-hw / 2, -hh * 0.55, hw, hh, 3);
    ctx.fill();
    ctx.stroke();
    // Grain
    ctx.fillStyle = '#8a4c1a';
    ctx.fillRect(-hw / 2 + 2, -hh * 0.50, 2, hh * 0.88);
    ctx.restore();

    // Head — at top of handle, dark iron with blade sweeping right
    const headX = w * 0.25;
    const headY = h * 0.14;

    // Main head body (eye area)
    ctx.fillStyle = '#252528';
    ctx.strokeStyle = '#111111';
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.rect(headX, headY, w * 0.14, h * 0.24);
    ctx.fill();
    ctx.stroke();

    // Blade — wider section curving to the right
    ctx.fillStyle = '#303038';
    ctx.strokeStyle = '#111111';
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.moveTo(headX + w * 0.14, headY);
    ctx.quadraticCurveTo(headX + w * 0.50, headY - h * 0.04, headX + w * 0.50, headY + h * 0.12);
    ctx.quadraticCurveTo(headX + w * 0.50, headY + h * 0.28, headX + w * 0.14, headY + h * 0.24);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    // Cutting edge — lighter sliver on far right of blade
    ctx.fillStyle = '#7a8890';
    ctx.beginPath();
    ctx.moveTo(headX + w * 0.44, headY + h * 0.01);
    ctx.quadraticCurveTo(headX + w * 0.54, headY + h * 0.12, headX + w * 0.44, headY + h * 0.23);
    ctx.lineTo(headX + w * 0.50, headY + h * 0.12);
    ctx.closePath();
    ctx.fill();

    // Poll — small protrusion to the left of head
    ctx.fillStyle = '#252528';
    ctx.strokeStyle = '#111111';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.rect(headX - w * 0.06, headY + h * 0.07, w * 0.06, h * 0.10);
    ctx.fill();
    ctx.stroke();

    // Highlight on top of head
    ctx.fillStyle = 'rgba(180,190,200,0.30)';
    ctx.fillRect(headX + 1, headY + 1, w * 0.13, 2);
  },

  // Same sprite for iron_axe
  iron_axe: (ctx, w, h) => CUSTOM_SPRITES['axe'](ctx, w, h),

  // ---- Bronze Longsword ----
  bronze_longsword: (ctx, w, h) => {
    // Diagonal layout: tip upper-right, pommel lower-left
    const angle = -Math.PI / 4;   // 45° diagonal

    ctx.save();
    ctx.translate(w * 0.50, h * 0.50);
    ctx.rotate(angle);

    // Blade — long, slightly tapered rectangle
    const bladeLen = h * 0.72;
    const bladeW   = w * 0.09;
    ctx.fillStyle = '#c87c30';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    // Base of blade (where guard meets)
    ctx.moveTo(-bladeW / 2, bladeLen * 0.12);
    ctx.lineTo( bladeW / 2, bladeLen * 0.12);
    // Taper to tip
    ctx.lineTo(1.5, -bladeLen * 0.72);
    ctx.lineTo(-1.5, -bladeLen * 0.72);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    // Blade edge highlight
    ctx.fillStyle = '#e8a855';
    ctx.beginPath();
    ctx.moveTo(0, bladeLen * 0.10);
    ctx.lineTo(1, -bladeLen * 0.68);
    ctx.lineTo(-0.5, -bladeLen * 0.68);
    ctx.closePath();
    ctx.globalAlpha = 0.4;
    ctx.fill();
    ctx.globalAlpha = 1;

    // Crossguard — horizontal bar
    const guardW = w * 0.42;
    const guardH = h * 0.06;
    ctx.fillStyle = '#a06020';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(-guardW / 2, bladeLen * 0.10, guardW, guardH, 2);
    ctx.fill();
    ctx.stroke();

    // Guard highlight
    ctx.fillStyle = 'rgba(220,160,80,0.35)';
    ctx.fillRect(-guardW / 2 + 2, bladeLen * 0.10 + 1, guardW - 4, 2);

    // Handle — shorter bar below guard
    const handleLen = h * 0.30;
    const handleW   = w * 0.07;
    ctx.fillStyle = '#5c2e0a';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(-handleW / 2, bladeLen * 0.16, handleW, handleLen, 2);
    ctx.fill();
    ctx.stroke();

    // Handle grip wrap
    ctx.strokeStyle = '#3a1800';
    ctx.lineWidth = 0.8;
    for (let i = 0; i < 3; i++) {
      const y = bladeLen * 0.20 + i * (handleLen * 0.28);
      ctx.beginPath();
      ctx.moveTo(-handleW / 2 + 1, y);
      ctx.lineTo( handleW / 2 - 1, y);
      ctx.stroke();
    }

    // Pommel — round cap at end of handle
    const pommY = bladeLen * 0.16 + handleLen + w * 0.06;
    ctx.fillStyle = '#a06020';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.ellipse(0, pommY, w * 0.075, w * 0.065, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();

    ctx.restore();
  },

  // ---- Log ----
  logs: (ctx, w, h) => drawLog(ctx, w, h, '#4a1e06', '#8b5020', '#6a3210'),
  oak_logs:    (ctx, w, h) => drawLog(ctx, w, h, '#5a2808', '#a06030', '#7a4420'),
  willow_logs: (ctx, w, h) => drawLog(ctx, w, h, '#3a1e10', '#7a4828', '#5a3218'),

  // ---- Egg ----
  egg: (ctx, w, h) => {
    const cx = w / 2;
    const cy = h / 2 + 1;
    const rx = w * 0.32;
    const ry = h * 0.38;
    ctx.beginPath();
    ctx.ellipse(cx, cy, rx, ry, 0, 0, Math.PI * 2);
    ctx.fillStyle = '#f5f0d0';
    ctx.fill();
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth = 1.5;
    ctx.stroke();
    ctx.beginPath();
    ctx.ellipse(cx - rx * 0.25, cy - ry * 0.3, rx * 0.22, ry * 0.15, -0.4, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(255,255,255,0.55)';
    ctx.fill();
  },

  // ---- Basic Chaingun ----
  basic_chaingun: (ctx, w, h) => {
    ctx.save();
    const pad = w * 0.06;

    // Long barrel — horizontal, left-to-right
    const barrelY  = h * 0.38;
    const barrelH  = h * 0.18;
    const barrelX  = pad;
    const barrelW  = w * 0.82;
    ctx.fillStyle   = '#606060';
    ctx.strokeStyle = '#1a1a1a';
    ctx.lineWidth   = 1;
    ctx.beginPath();
    ctx.roundRect(barrelX, barrelY, barrelW, barrelH, 2);
    ctx.fill();
    ctx.stroke();

    // Barrel highlight strip
    ctx.fillStyle = '#888888';
    ctx.beginPath();
    ctx.roundRect(barrelX + 2, barrelY + 2, barrelW - 4, barrelH * 0.35, 1);
    ctx.fill();

    // Muzzle tip — brighter end
    ctx.fillStyle   = '#909090';
    ctx.strokeStyle = '#1a1a1a';
    ctx.beginPath();
    ctx.roundRect(barrelX + barrelW - w * 0.06, barrelY - 2, w * 0.08, barrelH + 4, 2);
    ctx.fill();
    ctx.stroke();

    // Grip / handle — vertical, below center
    const gripX = w * 0.55;
    const gripY = barrelY + barrelH - 1;
    const gripW = w * 0.16;
    const gripH = h * 0.32;
    ctx.fillStyle   = '#484848';
    ctx.strokeStyle = '#1a1a1a';
    ctx.beginPath();
    ctx.roundRect(gripX, gripY, gripW, gripH, 3);
    ctx.fill();
    ctx.stroke();

    // Kinetic energy cell — glowing cyan rectangle on the body
    const cellX = w * 0.28;
    const cellY = barrelY - h * 0.22;
    const cellW = w * 0.22;
    const cellH = h * 0.20;
    ctx.shadowColor = '#00cfff';
    ctx.shadowBlur  = 6;
    ctx.fillStyle   = '#00cfff';
    ctx.strokeStyle = '#007fa8';
    ctx.beginPath();
    ctx.roundRect(cellX, cellY, cellW, cellH, 3);
    ctx.fill();
    ctx.stroke();

    // Cell inner glow highlight
    ctx.shadowBlur  = 0;
    ctx.fillStyle   = 'rgba(255,255,255,0.45)';
    ctx.beginPath();
    ctx.roundRect(cellX + 2, cellY + 2, cellW - 4, cellH * 0.4, 2);
    ctx.fill();

    ctx.restore();
  },
};

function drawLog(
  ctx: CanvasRenderingContext2D, w: number, h: number,
  barkColor: string, grainColor: string, darkColor: string,
): void {
  // Log lying diagonally — body as a parallelogram, end-grain oval on right
  const angle = -0.28; // radians
  const cx = w * 0.48;
  const cy = h * 0.52;
  const len = w * 0.80;
  const rad = h * 0.20;

  const dx = Math.cos(angle) * len / 2;
  const dy = Math.sin(angle) * len / 2;

  ctx.save();
  ctx.translate(cx, cy);
  ctx.rotate(angle);

  // Bark body
  ctx.fillStyle = barkColor;
  ctx.strokeStyle = '#0d0000';
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.rect(-len / 2, -rad, len, rad * 2);
  ctx.fill();
  ctx.stroke();

  // Bark texture lines
  ctx.strokeStyle = darkColor;
  ctx.lineWidth = 0.8;
  for (let i = -1; i <= 1; i++) {
    const ox = i * len * 0.22;
    ctx.beginPath();
    ctx.moveTo(ox, -rad + 2);
    ctx.bezierCurveTo(ox + 3, -rad * 0.3, ox - 3, rad * 0.3, ox, rad - 2);
    ctx.stroke();
  }

  // Top sheen
  ctx.fillStyle = 'rgba(255,255,255,0.12)';
  ctx.fillRect(-len / 2 + 2, -rad + 1, len - 4, rad * 0.5);

  ctx.restore();

  // End-grain ellipse on the right end
  const ex = cx + dx;
  const ey = cy + dy;
  ctx.save();
  ctx.translate(ex, ey);
  ctx.rotate(angle);

  // Outer ring
  ctx.beginPath();
  ctx.ellipse(0, 0, h * 0.10, rad, 0, 0, Math.PI * 2);
  ctx.fillStyle = grainColor;
  ctx.fill();
  ctx.strokeStyle = '#0d0000';
  ctx.lineWidth = 1.2;
  ctx.stroke();

  // Inner ring (growth rings)
  ctx.beginPath();
  ctx.ellipse(0, 0, h * 0.06, rad * 0.58, 0, 0, Math.PI * 2);
  ctx.strokeStyle = darkColor;
  ctx.lineWidth = 0.8;
  ctx.stroke();
  ctx.beginPath();
  ctx.ellipse(0, 0, h * 0.03, rad * 0.28, 0, 0, Math.PI * 2);
  ctx.stroke();

  // Centre dot
  ctx.beginPath();
  ctx.arc(0, 0, 1.5, 0, Math.PI * 2);
  ctx.fillStyle = darkColor;
  ctx.fill();

  ctx.restore();
}

export function drawItemSprite(ctx: CanvasRenderingContext2D, w: number, h: number, itemId: string): void {
  ctx.save();
  ctx.shadowColor = 'rgba(0,0,0,0.90)';
  ctx.shadowBlur = 3;
  ctx.shadowOffsetX = 1;
  ctx.shadowOffsetY = 1;

  const custom = CUSTOM_SPRITES[itemId];
  if (custom) {
    custom(ctx, w, h);
    ctx.restore();
    return;
  }

  const pad = 4;
  const color = ITEM_COLORS[itemId] ?? '#888888';
  ctx.fillStyle = color;
  ctx.fillRect(pad, pad, w - pad * 2, h - pad * 2);

  ctx.restore();

  ctx.strokeStyle = 'rgba(0,0,0,0.85)';
  ctx.lineWidth = 2;
  ctx.strokeRect(pad + 1, pad + 1, w - pad * 2 - 2, h - pad * 2 - 2);

  ctx.strokeStyle = 'rgba(255,255,255,0.18)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(pad, pad + h - pad * 2);
  ctx.lineTo(pad, pad);
  ctx.lineTo(pad + w - pad * 2, pad);
  ctx.stroke();
}
