'use strict';
// One-time (idempotent) backfill: add `usernameLower` to every users doc so the relay
// can resolve username logins case-insensitively. Safe to re-run; only writes docs that
// are missing the field or have it stale. Uses the relay's service account.
//   node tools/backfill-username-lower.js
const { db } = require('../src/firebase');

(async () => {
  const snap = await db.collection('users').get();
  let scanned = 0, written = 0;
  let batch = db.batch();
  let pending = 0;
  for (const doc of snap.docs) {
    scanned++;
    const username = doc.get('username');
    if (!username) continue;
    const lower = String(username).toLowerCase();
    if (doc.get('usernameLower') === lower) continue;
    batch.set(doc.ref, { usernameLower: lower }, { merge: true });
    written++;
    if (++pending >= 400) {
      await batch.commit();
      batch = db.batch();
      pending = 0;
    }
  }
  if (pending) await batch.commit();
  console.log(`scanned ${scanned} users, wrote usernameLower on ${written}`);
  process.exit(0);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
