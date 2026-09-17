// Replaces src/firebase.js so unit tests run without a service account.
const Module = require('module');
const orig = Module._load;
Module._load = function (request, parent, ...rest) {
  if (/(^|\/)firebase$/.test(request) && parent && /relay\/src/.test(parent.filename)) {
    return { db: {}, admin: {}, FieldValue: {}, Timestamp: {}, DEFAULT_PROFILE_PIC: '', tsToMs: () => 0 };
  }
  return orig.call(this, request, parent, ...rest);
};
