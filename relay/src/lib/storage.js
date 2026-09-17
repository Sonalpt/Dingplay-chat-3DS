'use strict';

const crypto = require('node:crypto');
const { bucket } = require('../firebase');
const config = require('../config');

// Uploads a buffer and returns a Firebase-style download URL — the same shape
// the mobile app writes into mediaUrl, so phones can load console media with
// no client change. The token in the URL is what the storage rules honour.
async function uploadPublic(objectPath, buf, contentType) {
  const token = crypto.randomUUID();
  const file = bucket.file(objectPath);
  await file.save(buf, {
    resumable: false,
    contentType,
    metadata: { metadata: { firebaseStorageDownloadTokens: token } },
  });
  return `https://firebasestorage.googleapis.com/v0/b/${config.storageBucket}/o/${encodeURIComponent(objectPath)}?alt=media&token=${token}`;
}

async function download(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`download ${res.status}`);
  return Buffer.from(await res.arrayBuffer());
}

module.exports = { uploadPublic, download };
