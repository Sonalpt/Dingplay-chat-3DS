'use strict';

// Loads a local .env (if present) without a dependency, then exposes typed config.
const fs = require('node:fs');
const path = require('node:path');

function loadDotEnv() {
  const file = path.join(__dirname, '..', '.env');
  if (!fs.existsSync(file)) return;
  for (const raw of fs.readFileSync(file, 'utf8').split('\n')) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const key = line.slice(0, eq).trim();
    let val = line.slice(eq + 1).trim();
    if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) {
      val = val.slice(1, -1);
    }
    if (process.env[key] === undefined) process.env[key] = val;
  }
}
loadDotEnv();

const config = {
  port: Number(process.env.PORT || 8080),
  host: process.env.HOST || '0.0.0.0',
  webApiKey: process.env.FIREBASE_WEB_API_KEY || '',
  storageBucket: process.env.FIREBASE_STORAGE_BUCKET || 'dingplay-e3401.firebasestorage.app',
  relaySecret: process.env.RELAY_SECRET || '',
  ffmpegPath: process.env.FFMPEG_PATH || 'ffmpeg',
  // TLS: set both to serve HTTPS (the console pins the CA that signed TLS_CERT).
  // Leave unset for plain HTTP — LAN testing only, never for a public relay.
  tlsCert: process.env.TLS_CERT || '',
  tlsKey: process.env.TLS_KEY || '',
  cacheDir: path.resolve(process.env.CACHE_DIR || path.join(__dirname, '..', 'cache')),
  // A session token lives this long before the console has to log in again.
  sessionDays: 30,
  // Presence: a user counts as online when lastConnection is within this window.
  onlineWindowMs: 5 * 60 * 1000,
  // How often one console may bump its own lastConnection.
  presenceThrottleMs: 60 * 1000,
  // Hard caps on what a console may send. Sized for UDS frames / httpc bodies.
  limits: {
    textChars: 200,
    strokePoints: 2000,
    strokes: 120,
    voiceSeconds: 10,
    voiceBytes: 96 * 1024, // ~10 s of IMA-ADPCM @ 16 360 Hz, with headroom
    roomName: 24,
    roomTopic: 32,
    pageSize: 40,
  },
  version: require('../package.json').version,
};

if (!config.relaySecret) {
  // Tokens would be forgeable without a secret. Refuse to start rather than run insecure.
  console.error('RELAY_SECRET is not set. Copy .env.example to .env and fill it in.');
  process.exit(1);
}

module.exports = config;
