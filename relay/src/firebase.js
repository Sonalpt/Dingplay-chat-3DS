'use strict';

const admin = require('firebase-admin');
const config = require('./config');

// Credentials come from GOOGLE_APPLICATION_CREDENTIALS (service-account JSON).
// The relay is the only party that touches Firestore; the console never does.
admin.initializeApp({
  credential: admin.credential.applicationDefault(),
  storageBucket: config.storageBucket,
});

const db = admin.firestore();
db.settings({ ignoreUndefinedProperties: true });

const bucket = admin.storage().bucket();
const { FieldValue, Timestamp } = admin.firestore;

// The mobile app's default profile picture (functions/index.js createUserAccount).
const DEFAULT_PROFILE_PIC =
  'https://firebasestorage.googleapis.com/v0/b/dingplay-e3401.firebasestorage.app/o/profile_pictures%2Fmoi%20mii%202024.png?alt=media&token=d245c538-ad82-4b8b-8906-12b150f70a63';

function tsToMs(ts) {
  if (!ts) return 0;
  if (typeof ts.toMillis === 'function') return ts.toMillis();
  if (ts instanceof Date) return ts.getTime();
  if (typeof ts === 'number') return ts;
  return 0;
}

module.exports = { admin, db, bucket, FieldValue, Timestamp, DEFAULT_PROFILE_PIC, tsToMs };
