'use strict';

// 3DS Mii (CFLStoreData, 0x5C bytes + 4 padding/CRC) → Mii Studio data, so the
// console's own Mii can be rendered by Nintendo's Mii Studio image endpoint.
//
// Bit layout of the 3DS format is documented on 3dbrew ("Mii"); the Studio
// format is 46 unsigned bytes in a fixed order, obfuscated as
//   out[0] = 0x00; prev = 256; for each v: e = (7 + (v ^ prev)) & 0xFF; prev = e
// Field order below is the Studio order.

const MII_MIN_LEN = 0x5c;

function bits(v, lo, len) {
  return (v >>> lo) & ((1 << len) - 1);
}

/** Parses the 3DS bit-packed layout into named fields. */
function parse3ds(buf) {
  if (!Buffer.isBuffer(buf) || buf.length < MII_MIN_LEN) return null;
  if (buf.readUInt8(0) !== 3) return null; // version
  const flags18 = buf.readUInt16LE(0x18);
  const b30 = buf.readUInt8(0x30);
  const b31 = buf.readUInt8(0x31);
  const b33 = buf.readUInt8(0x33);
  const eye = buf.readUInt32LE(0x34);
  const brow = buf.readUInt32LE(0x38);
  const nose = buf.readUInt16LE(0x3c);
  const mouth = buf.readUInt16LE(0x3e);
  const mouth2 = buf.readUInt16LE(0x40);
  const beard = buf.readUInt16LE(0x42);
  const glasses = buf.readUInt16LE(0x44);
  const mole = buf.readUInt16LE(0x46);
  let name = '';
  for (let i = 0; i < 10; i++) {
    const c = buf.readUInt16LE(0x1a + i * 2);
    if (!c) break;
    name += String.fromCharCode(c);
  }
  return {
    name,
    gender: bits(flags18, 0, 1),
    favoriteColor: bits(flags18, 10, 4),
    height: buf.readUInt8(0x2e),
    weight: buf.readUInt8(0x2f),
    faceType: bits(b30, 1, 4),
    faceColor: bits(b30, 5, 3),
    wrinkles: bits(b31, 0, 4),
    makeup: bits(b31, 4, 4),
    hairType: buf.readUInt8(0x32),
    hairColor: bits(b33, 0, 3),
    hairFlip: bits(b33, 3, 1),
    eyeType: bits(eye, 0, 6),
    eyeColor: bits(eye, 6, 3),
    eyeSize: bits(eye, 9, 4),
    eyeStretch: bits(eye, 13, 3),
    eyeRotation: bits(eye, 16, 5),
    eyeHorizontal: bits(eye, 21, 4),
    eyeVertical: bits(eye, 25, 5),
    browType: bits(brow, 0, 5),
    browColor: bits(brow, 5, 3),
    browSize: bits(brow, 8, 4),
    browStretch: bits(brow, 12, 3),
    browRotation: bits(brow, 16, 4),
    browHorizontal: bits(brow, 21, 4),
    browVertical: bits(brow, 25, 5),
    noseType: bits(nose, 0, 5),
    noseSize: bits(nose, 5, 4),
    noseVertical: bits(nose, 9, 5),
    mouthType: bits(mouth, 0, 6),
    mouthColor: bits(mouth, 6, 3),
    mouthSize: bits(mouth, 9, 4),
    mouthStretch: bits(mouth, 13, 3),
    mouthVertical: bits(mouth2, 0, 5),
    mustacheType: bits(mouth2, 5, 3),
    beardType: bits(beard, 0, 3),
    facialHairColor: bits(beard, 3, 3),
    mustacheSize: bits(beard, 6, 4),
    mustacheVertical: bits(beard, 10, 5),
    glassesType: bits(glasses, 0, 4),
    glassesColor: bits(glasses, 4, 3),
    glassesSize: bits(glasses, 7, 4),
    glassesVertical: bits(glasses, 11, 5),
    moleEnable: bits(mole, 0, 1),
    moleSize: bits(mole, 1, 4),
    moleHorizontal: bits(mole, 5, 5),
    moleVertical: bits(mole, 10, 5),
  };
}

// Studio uses one shared colour table; console palettes map onto it.
const studioHairColor = (c) => (c === 0 ? 8 : c);
const studioEyeColor = (c) => c + 8;
const studioGlassesColor = (c) => (c === 0 ? 8 : c < 6 ? c + 13 : 0);
const studioMouthColor = (c) => (c < 4 ? c + 19 : 0);

/** 46 Studio bytes, in Studio field order. */
function toStudio(m) {
  return [
    studioHairColor(m.facialHairColor), m.beardType, m.weight,
    m.eyeStretch, studioEyeColor(m.eyeColor), m.eyeRotation, m.eyeSize, m.eyeType, m.eyeHorizontal, m.eyeVertical,
    m.browStretch, studioHairColor(m.browColor), m.browRotation, m.browSize, m.browType, m.browHorizontal, m.browVertical,
    m.faceColor, m.makeup, m.faceType, m.wrinkles,
    m.favoriteColor, m.gender,
    studioGlassesColor(m.glassesColor), m.glassesSize, m.glassesType, m.glassesVertical,
    studioHairColor(m.hairColor), m.hairFlip, m.hairType,
    m.height,
    m.moleSize, m.moleEnable, m.moleHorizontal, m.moleVertical,
    m.mouthStretch, studioMouthColor(m.mouthColor), m.mouthSize, m.mouthType, m.mouthVertical,
    m.mustacheSize, m.mustacheType, m.mustacheVertical,
    m.noseSize, m.noseType, m.noseVertical,
  ];
}

function encodeStudio(values) {
  let prev = 256;
  let out = '00';
  for (const v of values) {
    const e = (7 + ((v & 0xff) ^ prev)) & 0xff;
    prev = e;
    out += e.toString(16).padStart(2, '0');
  }
  return out;
}

function studioUrl(data, width = 96) {
  return `https://studio.mii.nintendo.com/miis/image.png?data=${data}&type=face&width=${width}&instanceCount=1`;
}

/** Buffer (3DS CFLStoreData) → { name, studio: hex string, url } or null when not a Mii. */
function convert3ds(buf) {
  const m = parse3ds(buf);
  if (!m) return null;
  const studio = encodeStudio(toStudio(m));
  return { name: m.name, studio, url: studioUrl(studio) };
}

module.exports = { parse3ds, toStudio, encodeStudio, studioUrl, convert3ds, MII_MIN_LEN };
