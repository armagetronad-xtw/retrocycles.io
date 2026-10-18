# Hosting the browser client

What a host serving browser players looks like:

```
browser ── https://host/                     ──> Caddy ── the bundle (make dist)
        ── wss://host/signal                 ──> Caddy ── ws://server:4535 (WebRTC signalling)
        ── https://host/resource/<host>/<p>  ──> Caddy ── https://<host>/<p> (allowlisted; CORS added)
        ── UDP host:<ICE range>              ──> the game server (DataChannels)
desktop ── UDP host:4534                     ──> the game server, as always
```

Browsers load the page, open the signalling WebSocket through Caddy (which terminates TLS)
and then talk to the game server directly over a WebRTC DataChannel on its UDP range.
Desktop clients connect as usual.

Files here:
- `docker-compose.yml`, `.env.example`: a game server and Caddy on one host.
- `Caddyfile`: the site, `/signal` to the game server, `/resource/` through the resource proxy.
- `resource_proxy.caddy`: the resource proxy snippet ("Resources" below).
- `resource/`: optional local overrides for the resource proxy, empty by default.
- `build/server.Dockerfile`: builds the dedicated server with WebRTC as a tarball for a
  Debian 13 host (see the comments at its top).

## Building the browser client

The client is built with Emscripten in the `emscripten/emsdk` image. From the repository root:

```
docker run --rm -v "$PWD:/src" -v aa-wasm-deps:/work/deps emscripten/emsdk:latest \
    bash -c 'bash /src/web/wasm/deps.sh && emmake make -C /src/web/wasm -j"$(nproc)" dist'
```

`deps.sh` builds libxml2 once into the `aa-wasm-deps` volume; SDL2, libpng and zlib come from
Emscripten ports. `make dist` writes the bundle to `web/dist/`: `aa_api.js`, `client_opt.*`,
`servers.json` and `client.html`, a small test page for the API (`web/wasm/API.md`).

The play page (`web/page`, Vite and React) is built separately on the host:

```
cd web/page && npm ci && npm run build
```

Run `make dist` after that: if `web/page/dist/` exists it becomes the bundle's `index.html`,
otherwise `client.html` does.

## Running a host with docker-compose

1. A Linux host with Docker, a public IPv4 address and a DNS name pointing at it (Caddy gets
   a Let's Encrypt certificate for it).
2. Open TCP 80 and 443, UDP 4534 for desktop clients, and the ICE range (UDP 50000-50019 by
   default; each browser player takes one port, so widen `AA_ICE_PORT_MIN/MAX` for more
   than about 20).
3. Build an image of a dedicated server configured with `--enable-webrtc` whose entrypoint
   passes the `WEBRTC_*` variables of `docker-compose.yml` on to the server, and set
   `AA_SERVER_IMAGE` to it.
4. Copy `web/dist/` next to this directory (or set `AA_SITE_DIR`). In its `servers.json`,
   keep only the servers that are reachable; `/signal` means this host.
5. `cp .env.example .env` and set `AA_SITE_ADDRESS`, `AA_PUBLIC_IP` and `AA_SERVER_NAME`.
   `AA_TALK_TO_MASTER=1` lists the server for desktop players.
6. `docker compose up -d`. The page is at `https://<your name>/`.

Server settings go into `/data/var/autoexec.cfg` in the `aa-data` volume. If the
DataChannel never opens (`data channel did not open` in the browser console), the UDP range
or `AA_PUBLIC_IP` is wrong.

For a local test without a public IP, leave `AA_SITE_ADDRESS` at `:80` and `AA_PUBLIC_IP`
empty, then open `http://localhost/`.

## Resources

The game server keeps the official `RESOURCE_REPOSITORY_SERVER`; desktop clients download
maps as always. A browser can't fetch from that repository directly because it sends no CORS
header, so the page tells the client (`data-resource-proxy="/resource/"`) to fetch every
resource through the proxy on this host: `http://host/path` becomes
`https://<your name>/resource/host/path`.

The proxy (`resource_proxy.caddy`):
- only forwards `GET` and `HEAD` for `*.xml` and `*.dtd`, and only to the hosts in
  `AA_RESOURCE_UPSTREAMS` (a regex alternation, default `resource.armagetronad.net`). A host
  without a port is fetched over https, `host:port` over plain http. Anything else gets a 403.
- adds `Access-Control-Allow-Origin` (`AA_RESOURCE_CORS_ORIGIN`) and a day of
  `Cache-Control`. It stores nothing; browsers cache maps themselves.
- serves a file from `resource/<host>/<path>` instead, if one is there.

To use your own repository, add its host to `AA_RESOURCE_UPSTREAMS` and restart Caddy.

## Client addresses

The game server sees a browser player as the address their signalling WebSocket came from.
Behind Caddy that would be the proxy's address, so Caddy passes the player's address in the
request target (`uri query X-Forwarded-For {client_ip}`; a query parameter, because the
server's WebSocket library drops request headers). This needs Caddy 2.8 or newer.

The server only believes it from addresses in `WEBRTC_TRUSTED_PROXIES` (default
`127.0.0.0/8 ::1`). In the compose setup Caddy runs in another container, so
`docker-compose.yml` trusts the private ranges; port 4535 isn't published, so only the
stack's own containers can reach it. Any other proxy in front of the server must replace
every query parameter named exactly `X-Forwarded-For` and run on a trusted address.

IPv6 players are counted as a stable pseudo-address in `240.0.0.0/4` derived from their /64,
so IP limits and bans apply to the /64.

## Limits

- One game server per compose stack. Several servers on one host need one `/signal/<name>`
  route and one UDP range each.
- No TURN relay: players on networks that block UDP can't connect.
- The bundle's file names aren't hashed; `Cache-Control: no-cache` with ETags keeps
  redeploys immediate.
