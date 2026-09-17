# Dingplay Chat for 3DS / 2DS

Chat app for modded 3DS-family consoles, built from the mockups in
`Dingplay Chat 3DS.dc.html` / `Dingplay Chat 3DS - FR.dc.html` and
`dingplay-3ds-implementation-spec.md`.

| folder | what | status |
|---|---|---|
| `relay/` | Node.js relay (Fastify + Firebase Admin). The only thing that talks to Firebase. | runs; `npm test` passes; needs a service account + Web API key |
| `client/` | devkitARM / libctru / citro2d homebrew, all 11 screens, online + Local Wireless | syntax-checked against the real devkitPro headers; needs devkitPro to build and a console to test |
| `fonts/`, `assets/` | Nunito TTFs (converted to BCFNT at build time), launcher icon | — |

Quick start:

```sh
# relay
cd relay && npm install && cp .env.example .env   # fill in secrets (see relay/README.md)
npm start

# console
cd client && make          # → dingplay-chat.3dsx, copy to sdmc:/3ds/
```

Then on the console set `relay=http://<your-machine>:8080` in
`sdmc:/3ds/dingplay-chat/config.ini` (or via the boot screen's "Set relay address").
