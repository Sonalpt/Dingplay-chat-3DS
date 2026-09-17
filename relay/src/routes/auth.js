'use strict';

const auth = require('../auth');
const presence = require('../lib/presence');
const { db, tsToMs } = require('../firebase');
const { friendsSummary } = require('./friends');

function publicProfile(uid, data) {
  return {
    uid,
    username: data?.username || '',
    // The console fetches the avatar through GET /avatar/:uid, never this URL.
    hasAvatar: Boolean(data?.profilePic),
    lastSeen: tsToMs(data?.lastConnection),
  };
}

module.exports = async function authRoutes(app) {
  // Screen 02. Body: { login: "kev_ding" | "kev@x.fr", password }.
  app.post('/auth/login', async (req, reply) => {
    const { login, password } = req.body || {};
    if (typeof login !== 'string' || typeof password !== 'string' || !login.trim() || !password) {
      return reply.code(400).send({ error: 'missing_credentials' });
    }
    const result = await auth.loginWithPassword(login, password);
    if (!result.ok) return reply.code(401).send({ error: result.reason });

    const user = await auth.getUser(result.uid);
    presence.touch(result.uid);
    return { token: auth.issueToken(result.uid), user: publicProfile(result.uid, user) };
  });

  app.post('/auth/logout', { preHandler: auth.requireAuth }, async (req) => {
    auth.revokeToken(req.token);
    return { ok: true };
  });

  // Home screen top panel: identity + "3 friends online" + "2 unread".
  app.get('/me', { preHandler: auth.requireAuth }, async (req) => {
    presence.touch(req.uid);
    const [user, summary, threads] = await Promise.all([
      auth.getUser(req.uid),
      friendsSummary(req.uid),
      db.collection('conversations').doc(req.uid).collection('threads').get(),
    ]);
    let unread = 0;
    threads.forEach((t) => (unread += Number(t.get('unreadCount')) || 0));
    return {
      user: publicProfile(req.uid, user),
      friendsOnline: summary.online,
      friendsTotal: summary.total,
      requests: summary.requests,
      unread,
    };
  });
};

module.exports.publicProfile = publicProfile;
