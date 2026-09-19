# Dingplay Chat — console relay

Node.js relay between Dingplay Chat console clients (3DS / 2DS family, later
Switch) and Firebase. Consoles speak plain JSON-over-HTTP to this server; the
relay does every Firebase read/write with the Admin SDK against the **same
Firestore schema the mobile app uses**, so a phone and a console see the same
world chat, DMs, friends list and unread badges.

```
3DS  ──HTTP (httpc)──▶  relay (Fastify)  ──Admin SDK──▶  Firestore / Storage / FCM
                                          ──REST──────▶  Identity Toolkit (login only)
```

## Run

```sh
cd relay
npm install
cp .env.example .env        # fill in the three secrets below
npm start                   # http://0.0.0.0:8080
npm test                    # codec / container / png / token unit tests
```

Secrets needed in `.env`:

| var | where to get it |
|---|---|
| `GOOGLE_APPLICATION_CREDENTIALS` | Firebase console → Project settings → Service accounts → *Generate new private key* (save as `relay/serviceAccount.json`, git-ignored) |
| `FIREBASE_WEB_API_KEY` | Project settings → General → *Web API Key*. Needed only for `POST /auth/login` (password check via Identity Toolkit). If the key is restricted to the Android/iOS apps, create an unrestricted "server" key or allow the relay's IP. |
| `RELAY_SECRET` | any long random string; signs console session tokens |

Optional: `FFMPEG_PATH` (voice notes cross to/from the phone only when ffmpeg is
installed), `CACHE_DIR`, `PORT`, `LOG_LEVEL`.

The 3DS `httpc` stack cannot negotiate modern TLS, so the relay must stay
reachable over **plain HTTP** on a port the console can hit. Terminate TLS in
front of it only if you keep an HTTP listener for consoles too.

## API (what the console calls)

All responses are JSON unless noted. Authenticated calls send
`Authorization: Bearer <token>`.

| method | path | notes |
|---|---|---|
| GET | `/health` | `{ok, version, voiceTranscode}` — boot screen reachability check |
| GET | `/stats` | `{online}` — "1 284 players online" |
| GET | `/news?lang=fr` | `{tag, text}` — home banner, edited in `news.json` |
| POST | `/auth/login` | `{login, password}` → `{token, user}`; `login` is a username or email |
| POST | `/auth/logout` | revokes the token |
| GET | `/me` | `{user, friendsOnline, friendsTotal, requests, unread}` |
| GET | `/friends` | `{friends:[{uid,username,status,lobbyTitle,lastSeen,unread}], requests:[{uid,username}]}` |
| POST | `/friends/request` | `{username}` |
| POST | `/friends/respond` | `{uid, accept}` |
| GET | `/chat/:room?since=<ms>&lang=` | `{messages:[…], now, more}` — the poll behind "Syncing · Ns" |
| POST | `/chat/:room` | `{type:"text", text}` or `{type:"draw", draw:{w,h,s}}` |
| POST | `/chat/:room/voice` | raw `DPV1` body (`application/octet-stream`) |
| GET | `/media/:room/:id` | `DPV1` bytes for a voice message (415 if it needs ffmpeg and none is installed) |
| GET | `/rooms` | themed rooms `{rooms:[{id,name,topic,host,count}]}` |
| POST | `/rooms` | `{name, topic}` → `{room:"room-<id>"}` |
| POST | `/rooms/:id/join` · `/leave` · `/close` | |
| GET | `/avatar/:uid?s=48` | raw RGBA8 bytes, `x-size` header — pre-decoded so the console needs no image codec. No session needed (guest consoles in a local room need members' pictures) |
| POST | `/mii/render?s=48` | body: 3DS Mii (`CFLStoreData`, 0x5C–0x60 bytes) → raw RGBA8. Converted to Mii Studio data and rendered by `studio.mii.nintendo.com`, cached by face. No session needed |

Room ids: `global` (world chat, channel from `lang`), `dm-<uid>`, `room-<id>`.

Message shape returned to the console:

```json
{ "id": "…", "uid": "…", "name": "Léa", "type": "text|draw|voice|image",
  "ts": 1758120000000, "text": "…", "draw": {"w":230,"h":160,"s":[[color,pen,x,y,…]]},
  "media": true, "dur": 7000, "exp": false }
```

## How console content shows up on phones

- **Text** — identical to a phone message (`messageContent`, `uid`, `username`, `profilePic`).
- **Drawings** — stored as `type: "image"` with a PNG `mediaUrl` (rasterized here at 2×) **plus** a
  `strokes` field so consoles keep rendering vectors. Phones show the picture. Expires after 1 h like other media.
- **Voice** — stored as `type: "voice"`. `mediaUrl3ds` is always the ADPCM `DPV1` blob; when ffmpeg is
  present `mediaUrl` is an AAC `.m4a` phones can play, otherwise it points to the DPV blob (phones will
  show a player that fails to load). Phone voice notes are transcoded to DPV1 on demand for consoles.
- **Avatars** — account → Dingplay profile picture; no account → the console's Mii, rendered through Nintendo's Mii Studio endpoint (`src/lib/mii.js`, independent implementation from the 3dbrew field layout — no AGPL code).
- **Presence** — the relay bumps `users/{uid}.lastConnection` (throttled) so console users appear online.
- **Push** — DMs from a console trigger the same FCM notification and conversation-index update as the
  mobile app's `sendPrivateChatMessageNotification`; world chat posts fire the `world_chat` topic.

## Firestore

Apply `firestore.rules.snippet` to the mobile project's rules. No new indexes are needed for
world chat (the existing `channel + createdAt` composite index covers the `since` query).
`consoleRooms` needs a composite index on `closed ASC, lastMessageAt DESC` — Firestore prints the
one-click creation link in the relay log the first time `/rooms` is hit.
