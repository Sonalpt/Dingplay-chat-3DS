'use strict';

// Minimal WAV (PCM16 mono) reader/writer used to shuttle audio through ffmpeg,
// plus the "DPV1" container the console sends and receives voice notes in.
//
// DPV1 layout (little endian):
//   0  "DPV1"
//   4  uint32 sampleRate
//   8  uint32 sampleCount
//  12  IMA-ADPCM stream (see adpcm.js): int16 predictor, u8 index, u8 pad, nibbles

const adpcm = require('./adpcm');

const DPV_MAGIC = 'DPV1';

function pcm16ToWav(pcm, sampleRate) {
  const dataBytes = pcm.length * 2;
  const buf = Buffer.alloc(44 + dataBytes);
  buf.write('RIFF', 0);
  buf.writeUInt32LE(36 + dataBytes, 4);
  buf.write('WAVE', 8);
  buf.write('fmt ', 12);
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20); // PCM
  buf.writeUInt16LE(1, 22); // mono
  buf.writeUInt32LE(sampleRate, 24);
  buf.writeUInt32LE(sampleRate * 2, 28);
  buf.writeUInt16LE(2, 32);
  buf.writeUInt16LE(16, 34);
  buf.write('data', 36);
  buf.writeUInt32LE(dataBytes, 40);
  for (let i = 0; i < pcm.length; i++) buf.writeInt16LE(pcm[i], 44 + i * 2);
  return buf;
}

function wavToPcm16(buf) {
  if (buf.length < 44 || buf.toString('ascii', 0, 4) !== 'RIFF') throw new Error('not a WAV');
  let off = 12;
  let sampleRate = 16000;
  let channels = 1;
  let bits = 16;
  let data = null;
  while (off + 8 <= buf.length) {
    const id = buf.toString('ascii', off, off + 4);
    const size = buf.readUInt32LE(off + 4);
    const body = off + 8;
    if (id === 'fmt ') {
      channels = buf.readUInt16LE(body + 2);
      sampleRate = buf.readUInt32LE(body + 4);
      bits = buf.readUInt16LE(body + 14);
    } else if (id === 'data') {
      data = buf.subarray(body, Math.min(buf.length, body + size));
      break;
    }
    off = body + size + (size & 1);
  }
  if (!data || bits !== 16) throw new Error('unsupported WAV');
  const frames = Math.floor(data.length / (2 * channels));
  const pcm = new Int16Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = data.readInt16LE(i * 2 * channels); // take channel 0
  return { pcm, sampleRate };
}

function encodeDpv(pcm, sampleRate) {
  const stream = adpcm.encode(pcm);
  const head = Buffer.alloc(12);
  head.write(DPV_MAGIC, 0);
  head.writeUInt32LE(sampleRate, 4);
  head.writeUInt32LE(pcm.length, 8);
  return Buffer.concat([head, stream]);
}

function parseDpv(buf) {
  if (buf.length < 16 || buf.toString('ascii', 0, 4) !== DPV_MAGIC) return null;
  const sampleRate = buf.readUInt32LE(4);
  const sampleCount = buf.readUInt32LE(8);
  if (sampleRate < 8000 || sampleRate > 48000) return null;
  if (sampleCount > (buf.length - 16) * 2) return null;
  return { sampleRate, sampleCount, stream: buf.subarray(12) };
}

function dpvToPcm16(buf) {
  const info = parseDpv(buf);
  if (!info) throw new Error('not a DPV1 voice note');
  return { pcm: adpcm.decode(info.stream, info.sampleCount), sampleRate: info.sampleRate };
}

module.exports = { pcm16ToWav, wavToPcm16, encodeDpv, parseDpv, dpvToPcm16, DPV_MAGIC };
