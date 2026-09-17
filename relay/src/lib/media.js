'use strict';

// Audio transcoding between the console's ADPCM (DPV1) and the mobile app's
// AAC (.m4a, 16 kHz, 32 kbps — see lib/screens/WorldChat.dart). ffmpeg is
// optional: without it voice notes only travel 3DS ↔ 3DS.

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const config = require('../config');
const wav = require('./wav');

let ffmpegAvailable = null;

function probeFfmpeg() {
  if (ffmpegAvailable !== null) return Promise.resolve(ffmpegAvailable);
  return new Promise((resolve) => {
    const p = spawn(config.ffmpegPath, ['-version']);
    p.on('error', () => resolve((ffmpegAvailable = false)));
    p.on('exit', (code) => resolve((ffmpegAvailable = code === 0)));
  });
}

function runFfmpeg(args, input) {
  return new Promise((resolve, reject) => {
    const p = spawn(config.ffmpegPath, ['-hide_banner', '-loglevel', 'error', ...args]);
    const chunks = [];
    let err = '';
    p.stdout.on('data', (c) => chunks.push(c));
    p.stderr.on('data', (c) => (err += c));
    p.on('error', reject);
    p.on('exit', (code) => (code === 0 ? resolve(Buffer.concat(chunks)) : reject(new Error(`ffmpeg exit ${code}: ${err}`))));
    p.stdin.on('error', () => {}); // ffmpeg may close stdin early
    p.stdin.end(input);
  });
}

/** DPV1 → m4a (AAC LC, 16 kHz mono, 32 kbps). Returns null if ffmpeg is missing. */
async function dpvToM4a(dpv) {
  if (!(await probeFfmpeg())) return null;
  const { pcm, sampleRate } = wav.dpvToPcm16(dpv);
  const input = wav.pcm16ToWav(pcm, sampleRate);
  // ipod muxer needs a seekable output for the moov atom; write to a temp file.
  const tmp = path.join(config.cacheDir, `tx-${crypto.randomBytes(6).toString('hex')}.m4a`);
  fs.mkdirSync(config.cacheDir, { recursive: true });
  try {
    await runFfmpeg(['-f', 'wav', '-i', 'pipe:0', '-ac', '1', '-ar', '16000', '-c:a', 'aac', '-b:a', '32k', '-y', tmp], input);
    return fs.readFileSync(tmp);
  } finally {
    fs.rmSync(tmp, { force: true });
  }
}

/** Any ffmpeg-readable audio (m4a from the phone) → DPV1 at the console's rate. */
async function audioToDpv(bytes, targetRate = 16360) {
  if (!(await probeFfmpeg())) return null;
  const out = await runFfmpeg(['-i', 'pipe:0', '-ac', '1', '-ar', String(targetRate), '-f', 'wav', 'pipe:1'], bytes);
  const { pcm, sampleRate } = wav.wavToPcm16(out);
  const maxSamples = config.limits.voiceSeconds * sampleRate;
  return wav.encodeDpv(pcm.length > maxSamples ? pcm.subarray(0, maxSamples) : pcm, sampleRate);
}

// ---- Disk cache for transcoded / downscaled media ---------------------------------

function cachePath(kind, key, ext) {
  const safe = crypto.createHash('sha1').update(key).digest('hex');
  return path.join(config.cacheDir, kind, `${safe}.${ext}`);
}

function cacheGet(kind, key, ext) {
  const p = cachePath(kind, key, ext);
  return fs.existsSync(p) ? fs.readFileSync(p) : null;
}

function cachePut(kind, key, ext, buf) {
  const p = cachePath(kind, key, ext);
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, buf);
}

module.exports = { probeFfmpeg, dpvToM4a, audioToDpv, cacheGet, cachePut };
