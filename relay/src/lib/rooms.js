'use strict';

// Room ids as the console sees them:
//   "global"      → worldChat, channel picked by ?lang (en | fr); "global-en" / "global-fr" pick it explicitly
//   "dm-<uid>"    → privateChat/{me}/{uid} (mirrored under both users)
//   "room-<id>"   → consoleRooms/{id}/messages — console-exclusive themed rooms
//
// Themed rooms live in Firestore under consoleRooms/ so they survive a relay
// restart and a future Switch client can share them. The mobile app never reads
// that collection (see firestore.rules.snippet).

const { db } = require('../firebase');

function parseRoomId(raw, lang) {
  const id = String(raw || '');
  if (id === 'global') return { kind: 'global', channel: lang === 'fr' ? 'fr' : 'en' };
  if (id === 'global-en' || id === 'global-fr') return { kind: 'global', channel: id.slice(7) };
  if (id.startsWith('dm-') && id.length > 3) return { kind: 'dm', peer: id.slice(3) };
  if (id.startsWith('room-') && id.length > 5) return { kind: 'room', roomId: id.slice(5) };
  return null;
}

const roomsCol = () => db.collection('consoleRooms');
const roomMessages = (roomId) => roomsCol().doc(roomId).collection('messages');

module.exports = { parseRoomId, roomsCol, roomMessages };
