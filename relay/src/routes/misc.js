'use strict';

const fs = require('node:fs');
const path = require('node:path');
const config = require('../config');
const presence = require('../lib/presence');
const media = require('../lib/media');

function pickLang(q) {
  return q && String(q.lang).toLowerCase() === 'fr' ? 'fr' : 'en';
}

module.exports = async function miscRoutes(app) {
  // Boot screen pings this to know the relay is reachable before showing login.
  app.get('/health', async () => ({
    ok: true,
    version: config.version,
    voiceTranscode: await media.probeFfmpeg(),
    now: Date.now(),
  }));

  // Login screen: "1 284 players online".
  app.get('/stats', async () => ({ online: await presence.onlineCount() }));

  // Home screen banner. Edited by hand in relay/news.json; no restart needed.
  app.get('/news', async (req) => {
    const lang = pickLang(req.query);
    try {
      const news = JSON.parse(fs.readFileSync(path.join(__dirname, '..', '..', 'news.json'), 'utf8'));
      return { tag: news.tag?.[lang] || news.tag?.en || '', text: news.text?.[lang] || news.text?.en || '' };
    } catch {
      return { tag: '', text: '' };
    }
  });
};

module.exports.pickLang = pickLang;
