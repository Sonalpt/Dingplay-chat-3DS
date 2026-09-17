'use strict';

const config = require('./config');
const fastify = require('fastify');

// Firebase is initialised on first require; keep it here so a missing credential
// fails fast at boot with a readable error instead of on the first request.
require('./firebase');

const app = fastify({
  logger: { level: process.env.LOG_LEVEL || 'info' },
  // Voice notes arrive as raw bodies of up to ~96 KB; JSON bodies are far smaller.
  bodyLimit: 256 * 1024,
});

// The console posts voice notes as application/octet-stream.
app.addContentTypeParser('application/octet-stream', { parseAs: 'buffer' }, (req, body, done) => done(null, body));

// Keep responses small for the 3DS: no pretty-printing, no extra headers.
app.addHook('onSend', async (req, reply) => {
  reply.removeHeader('x-powered-by');
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
  app.log.info(`Dingplay Chat relay v${config.version} listening on ${config.host}:${config.port}`);
});
