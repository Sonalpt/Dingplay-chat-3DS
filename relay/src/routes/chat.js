'use strict';

const crypto = require('node:crypto');
const config = require('../config');
const auth = require('../auth');
const presence = require('../lib/presence');
const media = require('../lib/media');
const storage = require('../lib/storage');
const png = require('../lib/png');
const wav = require('../lib/wav');
const { parseRoomId, roomsCol, roomMessages } = require('../lib/rooms');
const { pickLang } = require('./misc');
const { db, admin, FieldValue, Timestamp, DEFAULT_PROFILE_PIC, tsToMs } = require('../firebase');

const MEDIA_TTL_MS = 60 * 60 * 1000; // mobile app: voice/images expire after 1 h

// ---- Reading ---------------------------------------------------------------------

// World chat is queried on createdAt alone (single-field auto index) and the
// channel is filtered here: the mobile app only has a (channel, createdAt ASC)
// composite, and the "latest page" needs DESC. Over-fetch to compensate.
function collectionFor(room, uid) {
  if (room.kind === 'global') return db.collection('worldChat');
  if (room.kind === 'dm') return db.collection('privateChat').doc(uid).collection(room.peer);
  return roomMessages(room.roomId);
}
const OVERFETCH = 3;

// Flattens a Firestore message into the compact shape the console parses.
function normalize(doc, now) {
  const d = doc.data();
  const ts = tsToMs(d.createdAt);
  if (!ts) return null; // serverTimestamp not resolved yet
  const type = d.type === 'voice' || d.type === 'image' ? d.type : d.strokes ? 'draw' : 'text';
  const out = {
    id: doc.id,
    uid: d.uid || '',
    name: d.username || '',
    type,
    ts,
  };
  if (type === 'text') out.text = String(d.messageContent || '').slice(0, 500);
  if (type === 'draw') {
    try {
      out.draw = JSON.parse(d.strokes);
    } catch {
      out.type = 'image';
    }
  }
  if (type === 'voice' || out.type === 'image') {
    const exp = tsToMs(d.expiresAt);
    out.exp = Boolean(exp && exp < now);
    out.media = Boolean(d.mediaUrl || d.mediaUrl3ds);
    if (d.durationMs) out.dur = Number(d.durationMs) | 0;
  }
  return out;
}

async function blockedSetFor(uid) {
  const me = await auth.getUser(uid);
  return new Set(Array.isArray(me?.blockedUsers) ? me.blockedUsers : []);
}

// ---- Writing helpers -----------------------------------------------------------

function baseMessage(me, uid) {
  return {
    createdAt: FieldValue.serverTimestamp(),
    uid,
    username: me?.username || '',
    profilePic: me?.profilePic || DEFAULT_PROFILE_PIC,
    source: '3ds',
  };
}

// Port of updateConversationThreads() in dingconnect/functions/index.js.
async function updateConversationThreads(senderUid, friendUid, preview, messageType) {
  const [sender, friend] = await Promise.all([auth.getUser(senderUid), auth.getUser(friendUid)]);
  const common = {
    lastMessage: preview,
    lastMessageType: messageType || null,
    lastSenderUid: senderUid,
    updatedAt: FieldValue.serverTimestamp(),
  };
  const senderThread = db.collection('conversations').doc(senderUid).collection('threads').doc(friendUid);
  const friendThread = db.collection('conversations').doc(friendUid).collection('threads').doc(senderUid);
  await Promise.all([
    senderThread.set({ ...common, peerUid: friendUid, peerUsername: friend?.username || '', peerProfilePic: friend?.profilePic || '', unreadCount: 0 }, { merge: true }),
    friendThread.set({ ...common, peerUid: senderUid, peerUsername: sender?.username || '', peerProfilePic: sender?.profilePic || '', unreadCount: FieldValue.increment(1) }, { merge: true }),
  ]);
}

// Port of the push half of sendPrivateChatMessageNotification.
async function notifyPrivate(senderUid, friendUid, preview) {
  try {
    const [friend, availability, sender] = await Promise.all([
      auth.getUser(friendUid),
      db.collection('isAvailable').doc(friendUid).get(),
      auth.getUser(senderUid),
    ]);
    if (!availability.exists || availability.get('isAvailable') !== true) return;
    const token = friend?.fcmToken;
    if (!token) return;
    const senderUsername = sender?.username || 'Someone';
    const fr = friend?.language === 'fr';
    await admin.messaging().send({
      token,
      notification: {
        title: fr ? `Nouveau message de ${senderUsername} !` : `New message from ${senderUsername} !`,
        body: preview,
      },
      data: { type: 'private_chat_message', friendUid: senderUid, friendUsername: senderUsername, friendProfilePic: sender?.profilePic || '' },
    });
  } catch (err) {
    console.warn('notifyPrivate failed', err.message);
  }
}

// Port of sendWorldChatMessageNotification (topic broadcast).
async function notifyWorld(senderUid, preview) {
  try {
    await admin.messaging().send({
      topic: 'world_chat',
      notification: { title: '🌍 World Chat · Chat mondial', body: preview },
      data: { type: 'world_chat_message', senderUid },
    });
  } catch (err) {
    console.warn('notifyWorld failed', err.message);
  }
}

async function writeMessage(room, uid, data, preview, messageType) {
  const id = crypto.randomUUID();
  if (room.kind === 'global') {
    await db.collection('worldChat').doc(id).set({ ...data, channel: room.channel });
    notifyWorld(uid, preview);
  } else if (room.kind === 'dm') {
    const peer = room.peer;
    await Promise.all([
      db.collection('privateChat').doc(uid).collection(peer).doc(id).set(data),
      db.collection('privateChat').doc(peer).collection(uid).doc(id).set(data),
    ]);
    await updateConversationThreads(uid, peer, preview, messageType);
    notifyPrivate(uid, peer, preview);
  } else {
    await roomMessages(room.roomId).doc(id).set(data);
    roomsCol().doc(room.roomId).set({ lastMessageAt: FieldValue.serverTimestamp(), messageCount: FieldValue.increment(1) }, { merge: true });
  }
  return id;
}

async function assertCanPost(room, uid, reply) {
  if (room.kind === 'dm') {
    if (room.peer === uid) return reply.code(400).send({ error: 'self' });
    const peer = await auth.getUser(room.peer);
    if (!peer) return reply.code(404).send({ error: 'unknown_user' });
    if (Array.isArray(peer.blockedUsers) && peer.blockedUsers.includes(uid)) return reply.code(403).send({ error: 'blocked' });
  } else if (room.kind === 'room') {
    const doc = await roomsCol().doc(room.roomId).get();
    if (!doc.exists || doc.get('closed')) return reply.code(404).send({ error: 'room_closed' });
  }
  return null;
}

// One message per ~0.7 s per console keeps a stuck A button from flooding a room.
const lastPost = new Map();
function rateLimited(uid) {
  const now = Date.now();
  const prev = lastPost.get(uid) || 0;
  if (now - prev < 700) return true;
  lastPost.set(uid, now);
  return false;
}

// ---- Routes --------------------------------------------------------------------

module.exports = async function chatRoutes(app) {
  // Screen 05 poll: GET /chat/global?lang=fr&since=<ms>. Returns messages newer
  // than `since` (or the latest page when since is 0), oldest first.
  app.get('/chat/:room', { preHandler: auth.requireAuth }, async (req, reply) => {
    const room = parseRoomId(req.params.room, pickLang(req.query));
    if (!room) return reply.code(400).send({ error: 'bad_room' });
    presence.touch(req.uid);

    const since = Number(req.query.since) || 0;
    const limit = Math.min(config.limits.pageSize, Number(req.query.limit) || config.limits.pageSize);
    const now = Date.now();

    let q = collectionFor(room, req.uid);
    const fetch = room.kind === 'global' ? limit * OVERFETCH : limit;
    let docs;
    if (since > 0) {
      q = q.where('createdAt', '>', Timestamp.fromMillis(since)).orderBy('createdAt', 'asc').limit(fetch);
      docs = (await q.get()).docs;
    } else {
      q = q.orderBy('createdAt', 'desc').limit(fetch);
      docs = (await q.get()).docs.reverse();
    }
    if (room.kind === 'global') docs = docs.filter((d) => d.get('channel') === room.channel);

    const blocked = await blockedSetFor(req.uid);
    const messages = [];
    for (const doc of docs) {
      const m = normalize(doc, now);
      if (m && !blocked.has(m.uid)) messages.push(m);
    }
    if (messages.length > limit) messages.splice(0, messages.length - limit);

    // Opening a DM clears its unread badge (mobile: markConversationRead).
    if (room.kind === 'dm') {
      db.collection('conversations').doc(req.uid).collection('threads').doc(room.peer).set({ unreadCount: 0 }, { merge: true }).catch(() => {});
    }
    // Themed rooms track who is present through the poll itself.
    if (room.kind === 'room') {
      roomsCol().doc(room.roomId).set({ presence: { [req.uid]: now } }, { merge: true }).catch(() => {});
    }

    return { messages, now, more: docs.length >= fetch };
  });

  // Text and drawings: POST /chat/:room  { type: "text", text } | { type: "draw", draw: {w,h,s} }
  app.post('/chat/:room', { preHandler: auth.requireAuth }, async (req, reply) => {
    const room = parseRoomId(req.params.room, pickLang(req.query));
    if (!room) return reply.code(400).send({ error: 'bad_room' });
    if (rateLimited(req.uid)) return reply.code(429).send({ error: 'slow_down' });
    const blockedReply = await assertCanPost(room, req.uid, reply);
    if (blockedReply) return blockedReply;

    const me = await auth.getUser(req.uid);
    const body = req.body || {};

    if (body.type === 'text') {
      const text = String(body.text || '').replace(/[\x00-\x08\x0b-\x1f]/g, '').trim();
      if (!text) return reply.code(400).send({ error: 'empty' });
      if (text.length > config.limits.textChars) return reply.code(413).send({ error: 'too_long' });
      const id = await writeMessage(room, req.uid, { ...baseMessage(me, req.uid), messageContent: text }, text, null);
      return { ok: true, id };
    }

    if (body.type === 'draw') {
      const strokes = png.validateStrokes(body.draw, config.limits);
      if (!strokes) return reply.code(400).send({ error: 'bad_strokes' });
      const id = crypto.randomUUID();
      const image = png.strokesToPng(strokes, 2);
      const mediaUrl = await storage.uploadPublic(`chat_media/${req.uid}/images/${id}.png`, image, 'image/png');
      const data = {
        ...baseMessage(me, req.uid),
        messageContent: '🖍️ Drawing',
        type: 'image',
        mediaUrl,
        strokes: JSON.stringify(strokes),
        expiresAt: Timestamp.fromMillis(Date.now() + MEDIA_TTL_MS),
      };
      const msgId = await writeMessage(room, req.uid, data, '🖍️ Drawing', 'image');
      return { ok: true, id: msgId };
    }

    return reply.code(400).send({ error: 'bad_type' });
  });

  // Voice: POST /chat/:room/voice?dur=<ms> with a raw DPV1 body (application/octet-stream).
  app.post('/chat/:room/voice', { preHandler: auth.requireAuth }, async (req, reply) => {
    const room = parseRoomId(req.params.room, pickLang(req.query));
    if (!room) return reply.code(400).send({ error: 'bad_room' });
    if (rateLimited(req.uid)) return reply.code(429).send({ error: 'slow_down' });
    const blockedReply = await assertCanPost(room, req.uid, reply);
    if (blockedReply) return blockedReply;

    const dpv = Buffer.isBuffer(req.body) ? req.body : null;
    if (!dpv || dpv.length > config.limits.voiceBytes) return reply.code(413).send({ error: 'voice_too_big' });
    const info = wav.parseDpv(dpv);
    if (!info) return reply.code(400).send({ error: 'bad_voice' });
    const durationMs = Math.min(config.limits.voiceSeconds * 1000, Math.round((info.sampleCount / info.sampleRate) * 1000));

    const me = await auth.getUser(req.uid);
    const id = crypto.randomUUID();
    const dpvUrl = await storage.uploadPublic(`chat_media/${req.uid}/voice/${id}.dpv`, dpv, 'application/octet-stream');
    let mediaUrl = dpvUrl;
    try {
      const m4a = await media.dpvToM4a(dpv);
      if (m4a) mediaUrl = await storage.uploadPublic(`chat_media/${req.uid}/voice/${id}.m4a`, m4a, 'audio/mp4');
    } catch (err) {
      console.warn('voice transcode failed', err.message);
    }
    const data = {
      ...baseMessage(me, req.uid),
      messageContent: '🎤 Voice message',
      type: 'voice',
      mediaUrl,
      mediaUrl3ds: dpvUrl,
      durationMs,
      expiresAt: Timestamp.fromMillis(Date.now() + MEDIA_TTL_MS),
    };
    const msgId = await writeMessage(room, req.uid, data, '🎤 Voice message', 'voice');
    return { ok: true, id: msgId, dur: durationMs };
  });

  // Voice playback: GET /media/:room/:id → DPV1 bytes for the console.
  app.get('/media/:room/:id', { preHandler: auth.requireAuth }, async (req, reply) => {
    const room = parseRoomId(req.params.room, pickLang(req.query));
    if (!room) return reply.code(400).send({ error: 'bad_room' });
    const id = String(req.params.id);
    if (!/^[\w-]{1,64}$/.test(id)) return reply.code(400).send({ error: 'bad_id' });

    let ref;
    if (room.kind === 'global') ref = db.collection('worldChat').doc(id);
    else if (room.kind === 'dm') ref = db.collection('privateChat').doc(req.uid).collection(room.peer).doc(id);
    else ref = roomMessages(room.roomId).doc(id);
    const doc = await ref.get();
    if (!doc.exists) return reply.code(404).send({ error: 'not_found' });
    const d = doc.data();
    if (d.type !== 'voice') return reply.code(400).send({ error: 'not_voice' });
    const exp = tsToMs(d.expiresAt);
    if (exp && exp < Date.now()) return reply.code(410).send({ error: 'expired' });

    const srcUrl = d.mediaUrl3ds || d.mediaUrl;
    if (!srcUrl) return reply.code(404).send({ error: 'no_media' });
    let dpv = media.cacheGet('voice', srcUrl, 'dpv');
    if (!dpv) {
      const bytes = await storage.download(srcUrl);
      if (wav.parseDpv(bytes)) dpv = bytes;
      else dpv = await media.audioToDpv(bytes); // phone AAC → console ADPCM
      if (!dpv) return reply.code(415).send({ error: 'no_transcoder' });
      media.cachePut('voice', srcUrl, 'dpv', dpv);
    }
    reply.header('content-type', 'application/octet-stream');
    reply.header('cache-control', 'private, max-age=3600');
    return reply.send(dpv);
  });
};
