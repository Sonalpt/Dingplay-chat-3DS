'use strict';

// IMA ADPCM, the codec the console encodes voice notes with (client/source/adpcm.c
// is the C twin of this file — keep the two in sync). 4 bits per sample, mono.
// The stream is raw nibbles preceded by a 4-byte header: int16 predictor,
// uint8 step index, uint8 reserved. No block structure: one header per note.

const STEP_TABLE = [
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
  130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
  1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
  7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
];
const INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

const clamp16 = (v) => (v > 32767 ? 32767 : v < -32768 ? -32768 : v);

function encodeNibble(state, sample) {
  const step = STEP_TABLE[state.index];
  let diff = sample - state.predictor;
  let code = 0;
  if (diff < 0) {
    code = 8;
    diff = -diff;
  }
  let delta = step >> 3;
  if (diff >= step) {
    code |= 4;
    diff -= step;
    delta += step;
  }
  if (diff >= step >> 1) {
    code |= 2;
    diff -= step >> 1;
    delta += step >> 1;
  }
  if (diff >= step >> 2) {
    code |= 1;
    delta += step >> 2;
  }
  state.predictor = clamp16(code & 8 ? state.predictor - delta : state.predictor + delta);
  state.index = Math.min(88, Math.max(0, state.index + INDEX_TABLE[code]));
  return code;
}

function decodeNibble(state, code) {
  const step = STEP_TABLE[state.index];
  let delta = step >> 3;
  if (code & 4) delta += step;
  if (code & 2) delta += step >> 1;
  if (code & 1) delta += step >> 2;
  state.predictor = clamp16(code & 8 ? state.predictor - delta : state.predictor + delta);
  state.index = Math.min(88, Math.max(0, state.index + INDEX_TABLE[code]));
  return state.predictor;
}

/** Int16Array PCM mono → Buffer (4-byte header + packed nibbles, low nibble first). */
function encode(pcm) {
  const state = { predictor: pcm.length ? pcm[0] : 0, index: 0 };
  const out = Buffer.alloc(4 + Math.ceil(pcm.length / 2));
  out.writeInt16LE(state.predictor, 0);
  out.writeUInt8(state.index, 2);
  out.writeUInt8(0, 3);
  for (let i = 0; i < pcm.length; i += 2) {
    const lo = encodeNibble(state, pcm[i]);
    const hi = i + 1 < pcm.length ? encodeNibble(state, pcm[i + 1]) : 0;
    out[4 + (i >> 1)] = lo | (hi << 4);
  }
  return out;
}

/** Buffer (header + nibbles) → Int16Array PCM mono. */
function decode(buf, sampleCount) {
  if (buf.length < 4) return new Int16Array(0);
  const state = { predictor: buf.readInt16LE(0), index: Math.min(88, buf.readUInt8(2)) };
  const n = sampleCount ?? (buf.length - 4) * 2;
  const pcm = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const byte = buf[4 + (i >> 1)];
    const code = i & 1 ? byte >> 4 : byte & 0x0f;
    pcm[i] = decodeNibble(state, code);
  }
  return pcm;
}

module.exports = { encode, decode };
