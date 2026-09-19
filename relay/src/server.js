'use strict';

const fs = require('node:fs');
const config = require('./config');
const fastify = require('fastify');

// HTTPS for the console. The 3DS SSL module speaks TLS 1.0–1.2 with RSA key exchange
// and CBC suites (no ECDHE/GCM on old firmware), which OpenSSL 3 refuses at its default
// security level — hence SECLEVEL=0 and the explicit list. Only this relay's
// self-signed CA is trusted by the app, so the weaker suites still beat plaintext by a
// mile: they stop passive sniffing of passwords and session tokens on shared Wi-Fi.
function tlsOptions() {
  if (!config.tlsCert || !config.tlsKey) return null;
  return {
    cert: fs.readFileSync(config.tlsCert),
    key: fs.readFileSync(config.tlsKey),
    minVersion: 'TLSv1',
    ciphers: [
      'ECDHE-RSA-AES128-GCM-SHA256',
      'ECDHE-RSA-AES128-SHA256',
      'ECDHE-RSA-AES128-SHA',
      'ECDHE-RSA-AES256-SHA',
      'AES128-GCM-SHA256',
      'AES128-SHA256',
      'AES256-SHA256',
      'AES128-SHA',
      'AES256-SHA',
      '@SECLEVEL=0',
    ].join(':'),
    honorCipherOrder: true,
  };
}
const https = tlsOptions();

// Firebase is initialised on first require; keep it here so a missing credential
// fails fast at boot with a readable error instead of on the first request.
require('./firebase');

const app = fastify({
  logger: { level: process.env.LOG_LEVEL || 'info' },
  ...(https ? { https } : {}),
  // Voice notes arrive as raw bodies of up to ~96 KB; JSON bodies are far smaller.
  bodyLimit: 256 * 1024,
});

// The console posts voice notes as application/octet-stream.
app.addContentTypeParser('application/octet-stream', { parseAs: 'buffer' }, (req, body, done) => done(null, body));

// Keep responses small for the 3DS: no pretty-printing, no extra headers.
app.addHook('onSend', async (req, reply) => {
  reply.removeHeader('x-powered-by');
});

// Per-IP rate limits. The relay is reachable from anywhere on plain HTTP and every
// request costs a Firestore read (or a Mii Studio render), so cap what one address
// can do: a console polling every 3 s plus avatars stays far below this.
app.register(require('@fastify/rate-limit'), {
  global: true,
  max: 240,
  timeWindow: '1 minute',
  addHeadersOnExceeding: { 'x-ratelimit-limit': false, 'x-ratelimit-remaining': false, 'x-ratelimit-reset': false },
  addHeaders: { 'x-ratelimit-limit': false, 'x-ratelimit-remaining': false, 'x-ratelimit-reset': false, 'retry-after': true },
});

app.setErrorHandler((err, req, reply) => {
  const status = err.statusCode && err.statusCode >= 400 ? err.statusCode : 500;
  if (status >= 500) req.log.error(err);
  reply.code(status).send({ error: status >= 500 ? 'relay_error' : err.message });
});

app.register(require('./routes/misc'));
app.register(require('./routes/auth'));
app.register(require('./routes/friends'));
app.register(require('./routes/chat'));
app.register(require('./routes/rooms'));
app.register(require('./routes/avatar'));

app.listen({ port: config.port, host: config.host }).then(() => {
  app.log.info(`Dingplay Chat relay v${config.version} listening on ${https ? 'https' : 'http'}://${config.host}:${config.port}`);
  if (!https) app.log.warn('TLS_CERT/TLS_KEY not set: serving plain HTTP (fine on a LAN, not on the internet)');
});
