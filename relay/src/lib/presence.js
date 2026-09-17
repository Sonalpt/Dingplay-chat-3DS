'use strict';

const config = require('../config');
const { db, FieldValue, tsToMs } = require('../firebase');

// The mobile app marks presence by writing users/{uid}.lastConnection. Doing the
// same from the relay (throttled per console) makes 3DS users show as online to
// phone friends, and lets the console derive "3 friends online" the same way.

const lastBump = new Map();

async function touch(uid) {
  const now = Date.now();
  const prev = lastBump.get(uid) || 0;
  if (now - prev < config.presenceThrottleMs) return;
  lastBump.set(uid, now);
  try {
    await db.collection('users').doc(uid).set({ lastConnection: FieldValue.serverTimestamp() }, { merge: true });
  } catch (err) {
    console.warn('presence touch failed', uid, err.message);
  }
}

function isOnline(userData, now = Date.now()) {
  if (!userData) return false;
  return now - tsToMs(userData.lastConnection) < config.onlineWindowMs;
}

// Login screen: "1 284 players online". One aggregate query, cached for a minute.
let onlineCache = { count: 0, until: 0 };
async function onlineCount() {
  if (onlineCache.until > Date.now()) return onlineCache.count;
  try {
    const since = new Date(Date.now() - config.onlineWindowMs);
    const agg = await db.collection('users').where('lastConnection', '>', since).count().get();
    onlineCache = { count: agg.data().count, until: Date.now() + 60 * 1000 };
  } catch (err) {
    console.warn('onlineCount failed', err.message);
    onlineCache.until = Date.now() + 15 * 1000;
  }
  return onlineCache.count;
}

module.exports = { touch, isOnline, onlineCount };
