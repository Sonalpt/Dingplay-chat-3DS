'use strict';
const test = require('node:test');
const assert = require('node:assert');
const zlib = require('node:zlib');
process.env.RELAY_SECRET = 'test-secret';

const adpcm = require('../src/lib/adpcm');
const wav = require('../src/lib/wav');
const png = require('../src/lib/png');

test('ADPCM round-trips a sine within tolerance', () => {
  const n = 16360;
  const pcm = new Int16Array(n);
  for (let i = 0; i < n; i++) pcm[i] = Math.round(12000 * Math.sin((i / 16360) * 2 * Math.PI * 440));
  const enc = adpcm.encode(pcm);
  assert.equal(enc.length, 4 + n / 2);
  const dec = adpcm.decode(enc, n);
  let err = 0;
  for (let i = 100; i < n; i++) err += Math.abs(dec[i] - pcm[i]);
  assert.ok(err / n < 600, `mean abs error ${err / n}`);
});

test('DPV1 container parses and decodes', () => {
  const pcm = new Int16Array(1000).map((_, i) => (i * 37) % 2000 - 1000);
  const dpv = wav.encodeDpv(pcm, 16360);
  assert.equal(dpv.toString('ascii', 0, 4), 'DPV1');
  const info = wav.parseDpv(dpv);
  assert.equal(info.sampleRate, 16360);
  assert.equal(info.sampleCount, 1000);
  const back = wav.dpvToPcm16(dpv);
  assert.equal(back.pcm.length, 1000);
  assert.equal(wav.parseDpv(Buffer.from('nope')), null);
});

test('WAV writer/reader round-trip', () => {
  const pcm = new Int16Array([0, 100, -100, 32767, -32768]);
  const w = wav.pcm16ToWav(pcm, 8000);
  const r = wav.wavToPcm16(w);
  assert.equal(r.sampleRate, 8000);
  assert.deepEqual(Array.from(r.pcm), Array.from(pcm));
});

test('stroke validation rejects bad payloads and clamps', () => {
  const limits = { strokes: 10, strokePoints: 100 };
  assert.equal(png.validateStrokes(null, limits), null);
  assert.equal(png.validateStrokes({ w: 230, h: 160, s: [] }, limits), null);
  assert.equal(png.validateStrokes({ w: 230, h: 160, s: [[9, 0, 1, 1]] }, limits), null);
  const ok = png.validateStrokes({ w: 230, h: 160, s: [[1, 2, -5, 10, 300, 20]] }, limits);
  assert.deepEqual(ok.s[0], [1, 2, 0, 10, 230, 20]);
});

test('PNG output is a valid PNG with expected size', () => {
  const payload = { w: 64, h: 32, s: [[0, 2, 4, 4, 60, 28], [3, 0, 10, 10]] };
  const buf = png.strokesToPng(payload, 2);
  assert.equal(buf.readUInt32BE(16), 128);
  assert.equal(buf.readUInt32BE(20), 64);
  assert.equal(buf.toString('ascii', 12, 16), 'IHDR');
  // IDAT inflates to (stride+1)*height bytes
  const idatLen = buf.readUInt32BE(33);
  const idat = buf.subarray(41, 41 + idatLen);
  const raw = zlib.inflateSync(idat);
  assert.equal(raw.length, (128 * 4 + 1) * 64);
  // pixel at the green dot should be green
  const o = 10 * 2 * (128 * 4 + 1) + 1 + 10 * 2 * 4;
  assert.deepEqual([raw[o], raw[o + 1], raw[o + 2]], [0x2f, 0xb8, 0x6e]);
});

test('session tokens sign, verify, expire and revoke', () => {
  const auth = require('../src/auth');
  const t = auth.issueToken('uid123');
  assert.equal(auth.verifyToken(t).u, 'uid123');
  assert.equal(auth.verifyToken(t + 'x'), null);
  assert.equal(auth.verifyToken('garbage'), null);
  auth.revokeToken(t);
  assert.equal(auth.verifyToken(t), null);
});
