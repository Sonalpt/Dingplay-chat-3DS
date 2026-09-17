'use strict';

const auth = require('../auth');
const presence = require('../lib/presence');
const { db, DEFAULT_PROFILE_PIC, tsToMs } = require('../firebase');

// friends/{uid}.friends[] mirrors lib/models/friend.dart:
//   { uid, username, profilePic, isPending, hasSentInvite, hasReceivedInvite,
//     isAvailable, currentLobbyID, currentLobbyTitle }
// An entry with isPending == true in MY doc is an incoming request waiting on me.

async function readFriendsDoc(uid) {
  const snap = await db.collection('friends').doc(uid).get();
  const raw = snap.exists ? snap.get('friends') : null;
  return Array.isArray(raw) ? raw.filter((f) => f && typeof f === 'object' && f.uid) : [];
}

function unreadByPeer(threadsSnap) {
  const map = new Map();
  threadsSnap.forEach((t) => map.set(t.id, Number(t.get('unreadCount')) || 0));
  return map;
}

// Home screen counters: "3 friends online", requests badge.
async function friendsSummary(uid) {
  const entries = await readFriendsDoc(uid);
  const accepted = entries.filter((f) => f.isPending !== true);
  const users = await auth.getUsers(accepted.map((f) => f.uid));
  const now = Date.now();
  let online = 0;
  for (const f of accepted) if (presence.isOnline(users.get(f.uid), now)) online++;
  return { online, total: accepted.length, requests: entries.length - accepted.length };
}

module.exports = async function friendsRoutes(app) {
  // Screen 04 list. Sorted: online first, then by last seen.
  app.get('/friends', { preHandler: auth.requireAuth }, async (req) => {
    presence.touch(req.uid);
    const [entries, threads] = await Promise.all([
      readFriendsDoc(req.uid),
      db.collection('conversations').doc(req.uid).collection('threads').get(),
    ]);
    const unread = unreadByPeer(threads);
    const users = await auth.getUsers(entries.map((f) => f.uid));
    const now = Date.now();

    const friends = [];
    const requests = [];
    for (const f of entries) {
      const u = users.get(f.uid);
      const name = u?.username || f.username || '?';
      if (f.isPending === true) {
        requests.push({ uid: f.uid, username: name });
        continue;
      }
      const online = presence.isOnline(u, now);
      friends.push({
        uid: f.uid,
        username: name,
        // 'online' | 'in_room' | 'offline' — drives the dot colour and the row subtitle.
        status: f.currentLobbyID ? 'in_room' : online ? 'online' : 'offline',
        lobbyTitle: f.currentLobbyTitle || '',
        lastSeen: tsToMs(u?.lastConnection),
        unread: unread.get(f.uid) || 0,
      });
    }
    const rank = { online: 0, in_room: 1, offline: 2 };
    friends.sort((a, b) => rank[a.status] - rank[b.status] || b.lastSeen - a.lastSeen);
    return { friends, requests };
  });

  // "Add friend" on screen 04: body { username }.
  app.post('/friends/request', { preHandler: auth.requireAuth }, async (req, reply) => {
    const username = String(req.body?.username || '').trim();
    if (!username) return reply.code(400).send({ error: 'missing_username' });
    const snap = await db.collection('users').where('username', '==', username).limit(1).get();
    if (snap.empty) return reply.code(404).send({ error: 'unknown_user' });
    const target = snap.docs[0];
    const targetUid = target.id;
    if (targetUid === req.uid) return reply.code(400).send({ error: 'self' });

    const me = await auth.getUser(req.uid);
    const myRef = db.collection('friends').doc(req.uid);
    const theirRef = db.collection('friends').doc(targetUid);

    let outcome = 'requested';
    await db.runTransaction(async (tx) => {
      const [mySnap, theirSnap] = await Promise.all([tx.get(myRef), tx.get(theirRef)]);
      const mine = (mySnap.exists && Array.isArray(mySnap.get('friends')) ? mySnap.get('friends') : []).filter((f) => f?.uid);
      const theirs = (theirSnap.exists && Array.isArray(theirSnap.get('friends')) ? theirSnap.get('friends') : []).filter((f) => f?.uid);

      const already = mine.find((f) => f.uid === targetUid);
      if (already && already.isPending !== true) {
        outcome = 'already_friends';
        return;
      }
      const base = (uid, username, profilePic, isPending, sent, received) => ({
        uid,
        username,
        profilePic: profilePic || DEFAULT_PROFILE_PIC,
        isPending,
        hasSentInvite: sent,
        hasReceivedInvite: received,
        isAvailable: null,
        currentLobbyID: null,
        currentLobbyTitle: null,
      });
      if (already && already.isPending === true) {
        // They already asked me → mutual, accept directly (same as the mobile app).
        outcome = 'accepted';
        const mineNext = mine.filter((f) => f.uid !== targetUid);
        mineNext.push(base(targetUid, target.get('username'), target.get('profilePic'), false, false, true));
        const theirsNext = theirs.filter((f) => f.uid !== req.uid);
        theirsNext.push(base(req.uid, me?.username, me?.profilePic, false, true, false));
        tx.set(myRef, { friends: mineNext }, { merge: true });
        tx.set(theirRef, { friends: theirsNext }, { merge: true });
        return;
      }
      const theirsNext = theirs.filter((f) => f.uid !== req.uid);
      theirsNext.push(base(req.uid, me?.username, me?.profilePic, true, true, false));
      tx.set(theirRef, { friends: theirsNext }, { merge: true });
    });
    return { ok: true, outcome, uid: targetUid };
  });

  // REQUESTS tab: body { uid, accept: true|false }.
  app.post('/friends/respond', { preHandler: auth.requireAuth }, async (req, reply) => {
    const friendUid = String(req.body?.uid || '');
    const accept = Boolean(req.body?.accept);
    if (!friendUid) return reply.code(400).send({ error: 'missing_uid' });

    const [me, them] = await Promise.all([auth.getUser(req.uid), auth.getUser(friendUid)]);
    if (!them) return reply.code(404).send({ error: 'unknown_user' });
    const myRef = db.collection('friends').doc(req.uid);
    const theirRef = db.collection('friends').doc(friendUid);

    await db.runTransaction(async (tx) => {
      const [mySnap, theirSnap] = await Promise.all([tx.get(myRef), tx.get(theirRef)]);
      const mine = (mySnap.exists && Array.isArray(mySnap.get('friends')) ? mySnap.get('friends') : []).filter((f) => f?.uid && f.uid !== friendUid);
      const theirs = (theirSnap.exists && Array.isArray(theirSnap.get('friends')) ? theirSnap.get('friends') : []).filter((f) => f?.uid && f.uid !== req.uid);
      if (accept) {
        mine.push({ uid: friendUid, username: them.username || '', profilePic: them.profilePic || DEFAULT_PROFILE_PIC, isPending: false, hasSentInvite: true, hasReceivedInvite: false, isAvailable: null, currentLobbyID: null, currentLobbyTitle: null });
        theirs.push({ uid: req.uid, username: me?.username || '', profilePic: me?.profilePic || DEFAULT_PROFILE_PIC, isPending: false, hasSentInvite: false, hasReceivedInvite: true, isAvailable: null, currentLobbyID: null, currentLobbyTitle: null });
      }
      tx.set(myRef, { friends: mine }, { merge: true });
      tx.set(theirRef, { friends: theirs }, { merge: true });
    });
    return { ok: true };
  });
};

module.exports.friendsSummary = friendsSummary;
module.exports.readFriendsDoc = readFriendsDoc;
