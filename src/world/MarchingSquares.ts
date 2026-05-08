/**
 * MarchingSquares.ts
 *
 * Builds smooth filled meshes for any terrain type from the per-pixel grid.
 * UV coords are emitted for every vertex so the ground texture can be applied
 * to land geometry without a separate flat plane.
 *
 * Coordinate conventions
 *   pixelWidth / pixelHeight  — number of pixels (192 × 192 for a 64-tile map)
 *   TILE_PX                   — pixels per tile (3)
 *   1 pixel  = 1/TILE_PX world units
 *   UV: u = hx/(2*pWidth),  v = hy/(2*pHeight)
 *       matches Babylon DynamicTexture invertY=true mapping over the world plane
 */

const TILE_PX = 3; // pixels per tile — matches export-map.mjs

export interface TerrainGeometry {
  positions: Float32Array;
  indices:   Uint32Array;
  normals:   Float32Array;
  uvs:       Float32Array; // u,v pairs — used when applying a texture to the mesh
}

/**
 * Builds a filled triangulated mesh covering all pixels matching typeCode.
 * Pass invertMatch=true to instead cover every pixel that does NOT match
 * (used for the land mesh, which covers all non-water pixels).
 */
export function buildTerrainGeometry(
  pixels:      number[],
  pWidth:      number,
  pHeight:     number,
  typeCode:    number,
  worldY:      number,
  invertMatch  = false,
): TerrainGeometry {
  const s = 1 / TILE_PX;

  const isType = (px: number, py: number): 1 | 0 => {
    if (px < 0 || py < 0 || px >= pWidth || py >= pHeight) return 0;
    const tc = (pixels[py * pWidth + px] >>> 24) & 0xf;
    const match = tc === typeCode;
    return (invertMatch ? !match : match) ? 1 : 0;
  };

  const gW = 2 * pWidth  + 1;
  const gH = 2 * pHeight + 1;
  const vertMap = new Int32Array(gW * gH).fill(-1);

  const posBuf: number[] = [];
  const uvBuf:  number[] = [];
  const idxBuf: number[] = [];

  const getVert = (hx: number, hy: number): number => {
    const key = hy * gW + hx;
    if (vertMap[key] < 0) {
      vertMap[key] = posBuf.length / 3;
      posBuf.push(hx * s * 0.5 - 0.5, worldY, hy * s * 0.5 - 0.5);
      uvBuf.push(hx / (2 * pWidth), hy / (2 * pHeight));
    }
    return vertMap[key];
  };

  const tri = (a: number, b: number, c: number) => idxBuf.push(a, b, c);

  for (let cy = 0; cy <= pHeight; cy++) {
    for (let cx = 0; cx <= pWidth; cx++) {
      const nw = isType(cx,     cy);
      const ne = isType(cx + 1, cy);
      const sw = isType(cx,     cy + 1);
      const se = isType(cx + 1, cy + 1);

      const cs = (nw << 3) | (ne << 2) | (se << 1) | sw;
      if (cs === 0) continue;

      const HX = 2 * cx, HY = 2 * cy;

      const NW = () => getVert(HX,     HY);
      const NE = () => getVert(HX + 2, HY);
      const SW = () => getVert(HX,     HY + 2);
      const SE = () => getVert(HX + 2, HY + 2);
      const MN = () => getVert(HX + 1, HY);
      const MS = () => getVert(HX + 1, HY + 2);
      const MW = () => getVert(HX,     HY + 1);
      const ME = () => getVert(HX + 2, HY + 1);

      switch (cs) {
        // Single corners — natural winding is DOWN; swap b↔c to face up:
        case 1:  tri(SW(), MW(), MS()); break;
        case 2:  tri(SE(), MS(), ME()); break;
        case 4:  tri(NE(), ME(), MN()); break;
        case 8:  tri(NW(), MN(), MW()); break;
        // Two adjacent corners — 3 and 9 are DOWN; 6 and 12 are naturally UP:
        case 3:  tri(SW(), ME(), SE()); tri(SW(), MW(), ME()); break;
        case 6:  tri(NE(), SE(), MS()); tri(NE(), MS(), MN()); break;
        case 9:  tri(NW(), MS(), SW()); tri(NW(), MN(), MS()); break;
        case 12: tri(NW(), NE(), ME()); tri(NW(), ME(), MW()); break;
        // Diagonal ambiguous — both DOWN:
        case 5:  tri(SW(), MW(), MS()); tri(NE(), ME(), MN()); break;
        case 10: tri(NW(), MN(), MW()); tri(SE(), MS(), ME()); break;
        // Three corners — 7 and 11 are DOWN; 13 and 14 are naturally UP:
        case 7:  tri(MW(), SE(), SW()); tri(MW(), ME(), SE()); tri(MW(), MN(), ME()); break;
        case 11: tri(MN(), SW(), NW()); tri(MN(), SE(), SW()); tri(MN(), ME(), SE()); break;
        case 13: tri(MS(), SW(), NW()); tri(MS(), NW(), NE()); tri(MS(), NE(), ME()); break;
        case 14: tri(MW(), NW(), NE()); tri(MW(), NE(), SE()); tri(MW(), SE(), MS()); break;
        // Full square — naturally UP:
        case 15: tri(NW(), NE(), SE()); tri(NW(), SE(), SW()); break;
      }
    }
  }

  const positions = new Float32Array(posBuf);
  const indices   = new Uint32Array(idxBuf);
  const normals   = new Float32Array(positions.length);
  for (let i = 1; i < normals.length; i += 3) normals[i] = 1;
  const uvs = new Float32Array(uvBuf);

  return { positions, indices, normals, uvs };
}

/**
 * Builds vertical bank-wall quads along the water/land boundary,
 * extruded from topY (ground level) down to bottomY (water surface).
 */
export function buildBankGeometry(
  pixels:  number[],
  pWidth:  number,
  pHeight: number,
  typeCode: number,
  topY:    number,
  bottomY: number,
): TerrainGeometry {
  const s = 1 / TILE_PX;

  const isType = (px: number, py: number): 1 | 0 => {
    if (px < 0 || py < 0 || px >= pWidth || py >= pHeight) return 0;
    return ((pixels[py * pWidth + px] >>> 24) & 0xf) === typeCode ? 1 : 0;
  };

  const gW  = 2 * pWidth  + 1;
  const gH  = 2 * pHeight + 1;
  const gSz = gW * gH;
  const vtBot = new Int32Array(gSz).fill(-1);
  const vtTop = new Int32Array(gSz).fill(-1);

  const posBuf: number[] = [];
  const uvBuf:  number[] = [];
  const idxBuf: number[] = [];

  const vBot = (hx: number, hy: number): number => {
    const k = hy * gW + hx;
    if (vtBot[k] < 0) {
      vtBot[k] = posBuf.length / 3;
      posBuf.push(hx * s * 0.5 - 0.5, bottomY, hy * s * 0.5 - 0.5);
      uvBuf.push(0, 0); // solid colour material — UVs unused
    }
    return vtBot[k];
  };
  const vTop = (hx: number, hy: number): number => {
    const k = hy * gW + hx;
    if (vtTop[k] < 0) {
      vtTop[k] = posBuf.length / 3;
      posBuf.push(hx * s * 0.5 - 0.5, topY, hy * s * 0.5 - 0.5);
      uvBuf.push(0, 0);
    }
    return vtTop[k];
  };

  const quad = (hx1: number, hy1: number, hx2: number, hy2: number) => {
    const b1 = vBot(hx1, hy1); const b2 = vBot(hx2, hy2);
    const t1 = vTop(hx1, hy1); const t2 = vTop(hx2, hy2);
    idxBuf.push(b1, t1, t2,  b1, t2, b2);
  };

  for (let cy = 0; cy <= pHeight; cy++) {
    for (let cx = 0; cx <= pWidth; cx++) {
      const nw = isType(cx, cy);     const ne = isType(cx + 1, cy);
      const sw = isType(cx, cy + 1); const se = isType(cx + 1, cy + 1);
      const cs = (nw << 3) | (ne << 2) | (se << 1) | sw;
      if (cs === 0 || cs === 15) continue;

      const HX = 2 * cx, HY = 2 * cy;
      const MNx = HX+1, MNy = HY,   MSx = HX+1, MSy = HY+2;
      const MWx = HX,   MWy = HY+1, MEx = HX+2, MEy = HY+1;

      switch (cs) {
        case 1:  quad(MSx,MSy, MWx,MWy); break;
        case 2:  quad(MEx,MEy, MSx,MSy); break;
        case 4:  quad(MNx,MNy, MEx,MEy); break;
        case 8:  quad(MWx,MWy, MNx,MNy); break;
        case 3:  quad(MWx,MWy, MEx,MEy); break;
        case 6:  quad(MNx,MNy, MSx,MSy); break;
        case 9:  quad(MSx,MSy, MNx,MNy); break;
        case 12: quad(MEx,MEy, MWx,MWy); break;
        case 5:  quad(MSx,MSy, MWx,MWy); quad(MNx,MNy, MEx,MEy); break;
        case 10: quad(MWx,MWy, MNx,MNy); quad(MEx,MEy, MSx,MSy); break;
        case 7:  quad(MWx,MWy, MNx,MNy); break;
        case 11: quad(MNx,MNy, MEx,MEy); break;
        case 13: quad(MEx,MEy, MSx,MSy); break;
        case 14: quad(MSx,MSy, MWx,MWy); break;
      }
    }
  }

  const positions = new Float32Array(posBuf);
  const indices   = new Uint32Array(idxBuf);
  const normals   = new Float32Array(positions.length); // zeroed; caller computes
  const uvs       = new Float32Array(uvBuf);

  return { positions, indices, normals, uvs };
}
