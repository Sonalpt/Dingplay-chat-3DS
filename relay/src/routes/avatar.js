'use strict';

const crypto = require('node:crypto');
const auth = require('../auth');
const media = require('../lib/media');
const mii = require('../lib/mii');
const storage = require('../lib/storage');
const { DEFAULT_PROFILE_PIC } = require('../firebase');

// The console has no image decoder and 64 MB of RAM on the Old 3DS, so avatars
// are served pre-decoded: raw RGBA8, square, already downscaled. A 48 px avatar
// is 9 KB.
//
//   GET  /avatar/:uid?s=48   Dingplay profile picture   → application/octet-stream, x-size: 48
//   POST /mii/render?s=48    body: 3DS Mii (CFLStoreData) → same, rendered by Mii Studio
//
// Both are open (no session): a guest console in a Local Wireless room needs the
// profile pictures of members who do have accounts, and its own Mii before login.
// Profile pictures are already readable by every signed-in Dingplay user; uids are
// not enumerable.

const SIZES = new Set([16, 24, 32, 48, 64]);

async function rgbaFor(kind, key, url, size) {
  const cacheKey = `${key}#${size}`;
  const hit = media.cacheGet(kind, cacheKey, 'rgba');
  if (hit) return hit;
  const { Jimp } = require('jimp');
  const bytes = await storage.download(url);
  const img = await Jimp.read(bytes);
  img.cover({ w: size, h: size });
  const out = Buffer.from(img.bitmap.data); // RGBA, row-major, top-left origin
  media.cachePut(kind, cacheKey, 'rgba', out);
  return out;
}

function sendRgba(reply, rgba, size) {
  reply.header('content-type', 'application/octet-stream');
  reply.header('x-size', String(size));
  reply.header('cache-control', 'private, max-age=86400');
  return reply.send(rgba);
}

module.exports = async function avatarRoutes(app) {
  app.get('/avatar/:uid', async (req, reply) => {
    const size = Number(req.query.s) || 48;
    if (!SIZES.has(size)) return reply.code(400).send({ error: 'bad_size' });
    const uid = String(req.params.uid);
    if (!/^[\w-]{1,64}$/.test(uid)) return reply.code(400).send({ error: 'bad_uid' });
    const user = await auth.getUser(uid);
    if (!user) return reply.code(404).send({ error: 'unknown_user' });
    const url = user.profilePic || DEFAULT_PROFILE_PIC;
    let rgba;
    try {
      rgba = await rgbaFor('avatar', url, url, size);
    } catch (err) {
      req.log.warn({ err: err.message }, 'avatar decode failed, using default');
      try {
        rgba = await rgbaFor('avatar', DEFAULT_PROFILE_PIC, DEFAULT_PROFILE_PIC, size);
      } catch {
        return reply.code(404).send({ error: 'no_avatar' });
      }
    }
    return sendRgba(reply, rgba, size);
  });

  // Mii faces: converted to Mii Studio data and rendered by Nintendo's endpoint,
  // then cached by the Studio payload (same Mii → same bytes → one render).
  app.post('/mii/render', async (req, reply) => {
    const size = Number(req.query.s) || 48;
    if (!SIZES.has(size)) return reply.code(400).send({ error: 'bad_size' });
    const body = Buffer.isBuffer(req.body) ? req.body : null;
    if (!body || body.length < mii.MII_MIN_LEN || body.length > 0x70) return reply.code(400).send({ error: 'bad_mii' });
    const conv = mii.convert3ds(body);
    if (!conv) return reply.code(400).send({ error: 'bad_mii' });
    try {
      const rgba = await rgbaFor('mii', crypto.createHash('sha1').update(conv.studio).digest('hex'), conv.url, size);
      return sendRgba(reply, rgba, size);
    } catch (err) {
      req.log.warn({ err: err.message }, 'mii render failed');
      return reply.code(502).send({ error: 'mii_render_failed' });
    }
  });
};
