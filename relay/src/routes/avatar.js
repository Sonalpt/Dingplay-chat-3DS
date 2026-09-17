'use strict';

const auth = require('../auth');
const media = require('../lib/media');
const storage = require('../lib/storage');
const { DEFAULT_PROFILE_PIC } = require('../firebase');

// The console has no image decoder and 64 MB of RAM on the Old 3DS, so avatars
// are served pre-decoded: raw RGBA8, square, already downscaled (48 px on the
// home/settings panels, 24 px in lists). A 48 px avatar is 9 KB.
//
// GET /avatar/:uid?s=48  →  application/octet-stream, header x-size: 48

const SIZES = new Set([16, 24, 32, 48, 64]);

async function rgbaFor(url, size) {
  const key = `${url}#${size}`;
  const hit = media.cacheGet('avatar', key, 'rgba');
  if (hit) return hit;
  const { Jimp } = require('jimp');
  const bytes = await storage.download(url);
  const img = await Jimp.read(bytes);
  img.cover({ w: size, h: size });
  const out = Buffer.from(img.bitmap.data); // RGBA, row-major, top-left origin
  media.cachePut('avatar', key, 'rgba', out);
  return out;
}

module.exports = async function avatarRoutes(app) {
  app.get('/avatar/:uid', { preHandler: auth.requireAuth }, async (req, reply) => {
    const size = Number(req.query.s) || 48;
    if (!SIZES.has(size)) return reply.code(400).send({ error: 'bad_size' });
    const user = await auth.getUser(String(req.params.uid));
    const url = user?.profilePic || DEFAULT_PROFILE_PIC;
    let rgba;
    try {
      rgba = await rgbaFor(url, size);
    } catch (err) {
      req.log.warn({ err: err.message }, 'avatar decode failed, using default');
      try {
        rgba = await rgbaFor(DEFAULT_PROFILE_PIC, size);
      } catch {
        return reply.code(404).send({ error: 'no_avatar' });
      }
    }
    reply.header('content-type', 'application/octet-stream');
    reply.header('x-size', String(size));
    reply.header('cache-control', 'private, max-age=86400');
    return reply.send(rgba);
  });
};
