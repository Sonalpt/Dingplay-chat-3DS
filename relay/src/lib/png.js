'use strict';

// Drawings travel as vector stroke lists (small, and what the console canvas
// captures). The mobile app has no 'draw' message type, so the relay also
// rasterizes each drawing to a PNG and stores the message as type 'image' with a
// mediaUrl — phones see a picture, consoles keep rendering the strokes.
//
// Stroke payload from the console:
//   { w, h, s: [ [colorIndex, penIndex, x0, y0, x1, y1, ...], ... ] }
// colorIndex: 0 ink, 1 red, 2 orange, 3 green, 4 blue, 5 eraser (white)
// penIndex: 0 thin, 1 medium, 2 thick

const zlib = require('node:zlib');

const PALETTE = [
  [0x24, 0x1f, 0x1a], // ink
  [0xef, 0x44, 0x44], // red
  [0xf7, 0x94, 0x1e], // orange
  [0x2f, 0xb8, 0x6e], // green
  [0x37, 0xa8, 0xee], // blue
  [0xff, 0xff, 0xff], // eraser
];
const PEN_WIDTHS = [2, 4, 7];

function validateStrokes(payload, limits) {
  if (!payload || typeof payload !== 'object') return null;
  const w = Number(payload.w);
  const h = Number(payload.h);
  if (!Number.isInteger(w) || !Number.isInteger(h) || w < 16 || h < 16 || w > 400 || h > 240) return null;
  if (!Array.isArray(payload.s) || payload.s.length === 0 || payload.s.length > limits.strokes) return null;
  let points = 0;
  const strokes = [];
  for (const raw of payload.s) {
    if (!Array.isArray(raw) || raw.length < 4 || raw.length % 2 !== 0) return null;
    const color = raw[0] | 0;
    const pen = raw[1] | 0;
    if (color < 0 || color >= PALETTE.length || pen < 0 || pen >= PEN_WIDTHS.length) return null;
    const pts = [];
    for (let i = 2; i < raw.length; i += 2) {
      const x = Number(raw[i]);
      const y = Number(raw[i + 1]);
      if (!Number.isFinite(x) || !Number.isFinite(y)) return null;
      pts.push(Math.max(0, Math.min(w, x)), Math.max(0, Math.min(h, y)));
    }
    points += pts.length / 2;
    if (points > limits.strokePoints) return null;
    strokes.push([color, pen, ...pts]);
  }
  return { w, h, s: strokes };
}

// ---- Rasterizer ------------------------------------------------------------------

function fillDisc(img, W, H, cx, cy, r, rgb) {
  const x0 = Math.max(0, Math.floor(cx - r));
  const x1 = Math.min(W - 1, Math.ceil(cx + r));
  const y0 = Math.max(0, Math.floor(cy - r));
  const y1 = Math.min(H - 1, Math.ceil(cy + r));
  const r2 = r * r;
  for (let y = y0; y <= y1; y++) {
    const dy = y + 0.5 - cy;
    for (let x = x0; x <= x1; x++) {
      const dx = x + 0.5 - cx;
      if (dx * dx + dy * dy <= r2) {
        const o = (y * W + x) * 4;
        img[o] = rgb[0];
        img[o + 1] = rgb[1];
        img[o + 2] = rgb[2];
        img[o + 3] = 255;
      }
    }
  }
}

function rasterize(payload, scale = 2) {
  const W = payload.w * scale;
  const H = payload.h * scale;
  const img = Buffer.alloc(W * H * 4, 255); // white, opaque
  for (const [color, pen, ...pts] of payload.s) {
    const rgb = PALETTE[color];
    const r = (PEN_WIDTHS[pen] * scale) / 2;
    if (pts.length === 2) {
      fillDisc(img, W, H, pts[0] * scale, pts[1] * scale, r, rgb);
      continue;
    }
    for (let i = 2; i < pts.length; i += 2) {
      const ax = pts[i - 2] * scale;
      const ay = pts[i - 1] * scale;
      const bx = pts[i] * scale;
      const by = pts[i + 1] * scale;
      const len = Math.hypot(bx - ax, by - ay);
      const steps = Math.max(1, Math.ceil(len / (r * 0.5)));
      for (let s = 0; s <= steps; s++) {
        const t = s / steps;
        fillDisc(img, W, H, ax + (bx - ax) * t, ay + (by - ay) * t, r, rgb);
      }
    }
  }
  return { rgba: img, width: W, height: H };
}

// ---- PNG encoder (RGBA8, no filtering) ------------------------------------------

const CRC_TABLE = new Int32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  CRC_TABLE[n] = c;
}
function crc32(buf) {
  let c = -1;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const typeBuf = Buffer.from(type, 'ascii');
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([typeBuf, data])));
  return Buffer.concat([len, typeBuf, data, crc]);
}

function encodePng(rgba, width, height) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // RGBA
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  const stride = width * 4;
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; y++) {
    raw[y * (stride + 1)] = 0; // filter: none
    rgba.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

function strokesToPng(payload, scale = 2) {
  const { rgba, width, height } = rasterize(payload, scale);
  return encodePng(rgba, width, height);
}

module.exports = { validateStrokes, rasterize, encodePng, strokesToPng, PALETTE, PEN_WIDTHS };
