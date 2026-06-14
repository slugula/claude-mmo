// pack-assets.mjs — build an obfuscated assets.pak for production client builds.
//
// Usage:
//   node tools/pack-assets.mjs <srcDir> <outPak> <keyPrefix>
// Example (from package-release.ps1):
//   node tools/pack-assets.mjs client-native/assets/models out/assets.pak assets/models
//
// Packs every file under <srcDir> (recursively) into a single LPAK container,
// keyed by "<keyPrefix>/<relative-path>" with forward slashes, lowercased for
// lookup. Each blob is lightly XOR-obfuscated. The runtime reader
// (client-native/src/assets/AssetPack.cpp) MUST keep kKey + the scheme
// byte-identical to this file.
//
// NOTE: This is obfuscation, not encryption — it stops casual folder snooping,
// not a determined reverse-engineer.

import fs from 'node:fs';
import path from 'node:path';

// ---- Obfuscation (mirror of AssetPack.cpp) --------------------------------
const KEY = Uint8Array.from([
  0x7a, 0x1f, 0xc3, 0x9b, 0x46, 0xe2, 0x58, 0x0d, 0xb7, 0x31, 0x6c, 0xa9,
  0xf4, 0x12, 0x8e, 0x5d, 0x24, 0xd0, 0x6f, 0x93, 0xab, 0x07, 0x55, 0xee,
  0x19, 0xc8, 0x3a, 0x71, 0x9d, 0x42, 0xb0, 0x6b, 0xf7, 0x2c, 0x88, 0x51,
  0x0e, 0xa3, 0xd6, 0x64, 0x1a, 0xbf, 0x77, 0x35, 0xe9, 0x40, 0x9c, 0x28,
  0x83, 0x5f, 0xc1, 0x16, 0x7e, 0xaa, 0xd3, 0x68, 0x04, 0xb9, 0x47, 0xf1,
  0x2a, 0x95, 0x60, 0xdc,
]);

function fnv1a(s) {
  let h = 2166136261 >>> 0;
  for (let i = 0; i < s.length; i++) {
    h ^= s.charCodeAt(i);
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h >>> 0;
}

function obfuscate(buf, key) {
  const phase = fnv1a(key) & 63;
  const out = Buffer.from(buf);
  for (let i = 0; i < out.length; i++) {
    out[i] ^= KEY[(i + phase) & 63];
  }
  return out;
}

// ---- Recursive file walk ---------------------------------------------------
function walk(dir, base = dir, acc = []) {
  for (const name of fs.readdirSync(dir)) {
    const full = path.join(dir, name);
    const st = fs.statSync(full);
    if (st.isDirectory()) walk(full, base, acc);
    else acc.push(full);
  }
  return acc;
}

// ---- Main ------------------------------------------------------------------
const [srcDir, outPak, keyPrefix] = process.argv.slice(2);
if (!srcDir || !outPak || !keyPrefix) {
  console.error('Usage: node tools/pack-assets.mjs <srcDir> <outPak> <keyPrefix>');
  process.exit(1);
}

const files = walk(srcDir);
const entries = files.map((full) => {
  const rel = path.relative(srcDir, full).split(path.sep).join('/');
  const key = `${keyPrefix}/${rel}`.toLowerCase();
  const data = obfuscate(fs.readFileSync(full), key);
  return { key, data };
});

// Layout: magic, version, count, [keyLen, key, offset, size]..., then blobs.
const headerSize =
  4 + 4 + 4 +
  entries.reduce((n, e) => n + 4 + Buffer.byteLength(e.key, 'utf8') + 8 + 8, 0);

let offset = headerSize;
const head = [];
head.push(Buffer.from('LPAK', 'ascii'));
const verCount = Buffer.alloc(8);
verCount.writeUInt32LE(1, 0);
verCount.writeUInt32LE(entries.length, 4);
head.push(verCount);

const blobs = [];
for (const e of entries) {
  const keyBuf = Buffer.from(e.key, 'utf8');
  const meta = Buffer.alloc(4 + keyBuf.length + 8 + 8);
  let p = 0;
  meta.writeUInt32LE(keyBuf.length, p); p += 4;
  keyBuf.copy(meta, p); p += keyBuf.length;
  meta.writeBigUInt64LE(BigInt(offset), p); p += 8;
  meta.writeBigUInt64LE(BigInt(e.data.length), p); p += 8;
  head.push(meta);
  blobs.push(e.data);
  offset += e.data.length;
}

fs.mkdirSync(path.dirname(path.resolve(outPak)), { recursive: true });
fs.writeFileSync(outPak, Buffer.concat([...head, ...blobs]));

const totalKB = Math.round(Buffer.concat(blobs).length / 1024);
console.log(`Packed ${entries.length} files -> ${outPak} (${totalKB} KB obfuscated)`);
