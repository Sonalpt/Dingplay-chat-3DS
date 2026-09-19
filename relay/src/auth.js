'use strict';

const crypto = require('node:crypto');
const config = require('./config');
const { db, admin } = require('./firebase');

// ---- Session tokens -------------------------------------------------------
// Stateless HMAC tokens: base64url(payload).base64url(sig). The console stores
// this on SD when "Stay signed in" is on and sends it as a Bearer header on
// every request. The raw password is only ever seen by POST /auth/login.

function b64url(buf) {
  return Buffer.from(buf).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
function unb64url(str) {
  str = str.replace(/-/g, '+').replace(/_/g, '/');
  while (str.length % 4) str += '=';
  return Buffer.from(str, 'base64');
}
function sign(payloadB64) {
  return b64url(crypto.createHmac('sha256', config.relaySecret).update(payloadB64).digest());
}

function issueToken(uid) {
  const payload = {
    u: uid,
    i: Date.now(),
    e: Date.now() + config.sessionDays * 24 * 3600 * 1000,
    n: crypto.randomBytes(6).toString('hex'),
  };
  const p = b64url(JSON.stringify(payload));
  return `${p}.${sign(p)}`;
}

// Tokens explicitly signed out of. In-memory for the fast path; sign-out also
// stamps users/{uid}.consoleSessionsBefore so every token issued earlier stays
// dead across relay restarts (checked in requireAuth through the user cache).
const revoked = new Set();

function verifyToken(token) {
  if (typeof token !== 'string') return null;
  const dot = token.indexOf('.');
  if (dot < 0) return null;
  const p = token.slice(0, dot);
  const s = token.slice(dot + 1);
  const expected = sign(p);
  if (s.length !== expected.length || !crypto.timingSafeEqual(Buffer.from(s), Buffer.from(expected))) return null;
  let payload;
  try {
    payload = JSON.parse(unb64url(p).toString('utf8'));
  } catch {
    return null;
  }
  if (!payload.u || !payload.e || payload.e < Date.now()) return null;
  if (revoked.has(payload.n)) return null;
  return payload;
}

async function revokeToken(token) {
  const payload = verifyToken(token);
  if (!payload) return;
  revoked.add(payload.n);
  try {
    await db.collection('users').doc(payload.u).set({ consoleSessionsBefore: Date.now() }, { merge: true });
    invalidateUser(payload.u);
  } catch (err) {
    console.warn('revokeToken: could not stamp user', err.message);
  }
}

// ---- Login ----------------------------------------------------------------
// The mobile app signs in with email + password. The console shows a username
// field (mockup 02), so accept either: a username is resolved to its email via
// users/{uid}.username (usernames/{name} only stores the name, not the uid).

async function resolveEmail(login) {
  if (login.includes('@')) return login;
  const snap = await db.collection('users').where('username', '==', login).limit(1).get();
  if (snap.empty) return null;
  return snap.docs[0].get('email') || null;
}

async function loginWithPassword(login, password) {
  if (!config.webApiKey) {
    const err = new Error('Relay is missing FIREBASE_WEB_API_KEY');
    err.statusCode = 503;
    throw err;
  }
  const email = await resolveEmail(login.trim());
  if (!email) return { ok: false, reason: 'unknown_user' };

  const res = await fetch(
    `https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${encodeURIComponent(config.webApiKey)}`,
    {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ email, password, returnSecureToken: true }),
    },
  );
  const body = await res.json().catch(() => ({}));
  if (!res.ok) {
    const code = body?.error?.message || 'LOGIN_FAILED';
    if (/INVALID_PASSWORD|INVALID_LOGIN_CREDENTIALS|EMAIL_NOT_FOUND/.test(code)) return { ok: false, reason: 'bad_credentials' };
    if (/USER_DISABLED/.test(code)) return { ok: false, reason: 'disabled' };
    if (/TOO_MANY_ATTEMPTS/.test(code)) return { ok: false, reason: 'too_many_attempts' };
    return { ok: false, reason: 'login_failed', detail: code };
  }
  return { ok: true, uid: body.localId, email };
}

// ---- Fastify hook -----------------------------------------------------------

function bearer(req) {
  const h = req.headers.authorization || '';
  if (h.startsWith('Bearer ')) return h.slice(7).trim();
  if (req.headers['x-token']) return String(req.headers['x-token']).trim();
  return null;
}

// preHandler: requires a valid session and sets req.uid / req.token.
async function requireAuth(req, reply) {
  const token = bearer(req);
  const payload = token && verifyToken(token);
  if (!payload) {
    reply.code(401).send({ error: 'unauthorized' });
    return reply;
  }
  // Sessions issued before the user's last explicit sign-out are dead (cached lookup).
  const user = await getUser(payload.u);
  const before = Number(user?.consoleSessionsBefore) || 0;
  if (before && (!payload.i || payload.i < before)) {
    reply.code(401).send({ error: 'unauthorized' });
    return reply;
  }
  req.uid = payload.u;
  req.token = token;
}

// ---- Small user cache ---------------------------------------------------------
// Usernames and avatars rarely change; cache them for a minute to keep polling cheap.

const userCache = new Map();
const USER_TTL = 60 * 1000;

async function getUser(uid) {
  const hit = userCache.get(uid);
  if (hit && hit.until > Date.now()) return hit.data;
  const doc = await db.collection('users').doc(uid).get();
  const data = doc.exists ? doc.data() : null;
  userCache.set(uid, { data, until: Date.now() + USER_TTL });
  return data;
}

async function getUsers(uids) {
  const out = new Map();
  const missing = [];
  for (const uid of uids) {
    const hit = userCache.get(uid);
    if (hit && hit.until > Date.now()) out.set(uid, hit.data);
    else missing.push(uid);
  }
  // getAll takes up to 100 refs per call in practice; chunk to be safe.
  for (let i = 0; i < missing.length; i += 50) {
    const chunk = missing.slice(i, i + 50);
    const docs = await db.getAll(...chunk.map((u) => db.collection('users').doc(u)));
    docs.forEach((doc, idx) => {
      const data = doc.exists ? doc.data() : null;
      userCache.set(chunk[idx], { data, until: Date.now() + USER_TTL });
      out.set(chunk[idx], data);
    });
  }
  return out;
}

function invalidateUser(uid) {
  userCache.delete(uid);
}

module.exports = { issueToken, verifyToken, revokeToken, loginWithPassword, requireAuth, getUser, getUsers, invalidateUser, admin };
