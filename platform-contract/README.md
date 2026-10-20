# Retrocycles platform contract, v0

The public interface between the open game (the browser page and a game server built
with `--enable-platform`) and a platform service. Retrocycles.io runs one implementation;
anyone may run another. A server or page with no platform configured behaves like stock
Armagetron Advanced.

v0 covers guests, site accounts (password, external providers, sessions,
old global ids linked with `/link`, game passwords), platform login tokens, an armathentication authority (section 6), TURN relay credentials for the browser (`/v1/net/ice-servers`), Quick
Play (`/v1/quickplay`: the platform picks a room for the page), page analytics events
(`/v1/events`), progression (XP, levels and stats from the servers' match events: `/v1/me`,
`/v1/profiles/{authId}`), one server event stream, the room channel (heartbeats up,
commands down), and moderation (chat checked before delivery, sanctions, reports, names: section 5).
Everything here may change before v1; after v1, changes within a major version are additive.

Conventions: JSON bodies are UTF-8; times are Unix seconds (integers) unless noted;
`base64url` means RFC 4648 §5 without padding; all paths are under the platform base URL
(`PLATFORM_API_URL`, e.g. `http://localhost:5080`) and start with `/v1`.

## 1. Page ↔ platform

Sessions are cookies set by the platform (httpOnly, SameSite=Lax, Secure). The page calls with
`credentials: "include"`; the platform allows the page origin with credentials (CORS). No
credential is ever readable by page JavaScript.

**Requests from the page.** State-changing calls that use the session cookie (marked **CSRF**
below) must carry the header `X-RC-CSRF: 1`, or the platform answers `403` with
`code: "csrf_header_missing"`. A cross-origin page can only send a custom header after a CORS
preflight, which only the configured page origins pass; forms and navigations cannot send it at
all. If the browser sends `Origin`, it must be a configured page origin (`403`,
`code: "origin_not_allowed"`). For v0 compatibility `POST /v1/auth/guest` (without `fresh`) and
`POST /v1/auth/game-token` do not need the header. `POST /v1/events` does not take it either
(beacons cannot send headers) and requires a page `Origin` instead (see "Analytics events").

**Bearer sessions** (a page that cannot keep the cookie: the play page inside a Discord Activity,
where every request goes through Discord's proxy on the Activity's own origin). The session is
`Authorization: Bearer rcs_<key>` instead, from `POST /v1/auth/discord-activity` (below): the same
session as a cookie's (same user, the sessions list, sign out everywhere), only never in a cookie.
Requests that carry it and no session cookie need no `X-RC-CSRF` (there is no ambient credential to
forge); the page keeps the token in memory for one page load. Rate limits that count per address
count such requests per account (everyone behind Discord's proxy shares a few addresses).

**Errors** are `application/problem+json` with a machine-readable `code`, e.g.
`{"status": 401, "title": "Wrong email, handle or password.", "code": "invalid_credentials"}`.
Validation errors are `400` with `errors: {"<field>": ["<code>", ...]}`, where the values are
codes the page maps to text: `email_invalid`, `email_taken`, `handle_required`,
`handle_invalid`, `handle_reserved`, `handle_not_allowed`, `handle_taken`, `name_too_long`,
`name_not_allowed`, `password_required`,
`password_too_short`, `password_too_long`, `password_breached`, `password_contains_name`,
`current_password_invalid`. `429` means rate limited.

### `GET /v1/platform/config`

```json
{
  "apiVersion": "v0",
  "authority": "retrocycles",
  "features": {
    "guests": true, "accounts": true, "platformLogin": true,
    "progression": false, "moderation": false, "ranked": false, "quickplay": true, "analytics": true, "moveOffers": false,
    "linking": true, "linkByPassword": true, "gamePasswords": true, "friends": true, "mutes": true
  },
  "providers": [ { "id": "google", "name": "Google" }, { "id": "discord", "name": "Discord" } ],
  "discord": { "clientId": "1557121168920547440" }
}
```

`discord` (optional): the Discord application whose Activity this platform signs in (the same one
as the `discord` provider); missing when there is none.

The page shows or hides platform features from `features`. With no platform URL
configured, the page behaves as if every feature were `false`. A member the page does not
find counts as `false` (an older platform). `quickplay`: the page's Quick Play asks
`POST /v1/quickplay` for a room instead of taking the first server it knows. `analytics`: the page
sends its analytics events (`POST /v1/events`); without it the page sends none. `progression`: the platform
turns the official servers' match events into XP, levels and stats, and `GET /v1/me` carries them (see
"Progression"); without it the page shows none. `moderation`: the official servers' chat is checked
by the platform, sanctions apply, and the page may offer **Report** on a player (`POST /v1/reports`,
section 5); without it the page offers no reports. `moveOffers`: `GET /v1/me` may carry an offer of a
closer server (`moveOffer`), which the page shows at the end of a round and answers with
`POST /v1/me/offers/{id}` (see "Offers"); without it the page asks for none. `linking`: an account can link
old global ids (`name@forums`) with a one-time code and `/link` in an official room (see "Linked IDs");
without it the page offers none. `linkByPassword` (with `linking`): the account can also link an old id with
its password on the page (see "Linked IDs"); without it only the code. `gamePasswords`: the platform runs an armathentication authority (section 6)
and an account can make game passwords for desktop clients (see "Game passwords"); without it the page offers
none. `friends`: accounts add friends, answer requests and block (see "Friends"); without it the page shows no
friends. `mutes`: a player's mutes are kept by the platform and follow them to every room (see "Mutes");
without it the page mutes by name for one room only. `news`: the platform keeps news posts, serves them as
pages on the site (`/news`, `/news/<slug>`) and sends some to every player's bell (see "News"); without it
the page shows no bell. `providers` lists the external
sign-in providers this platform offers (possibly none); the page offers exactly these.

**The page load** (optional query): `?p=<page session>&v=<page_view props as JSON>[&gpc=1]`.
The page names its load on this first call: the page session of its analytics events (see
"Analytics events") and the props its `page_view` has (checked as on the event; a load whose props
do not pass is not kept), `gpc=1` when the browser sends Global Privacy Control. The platform keeps
its own copy of the page view, so a load whose events never arrive (a blocker, Global Privacy
Control, the cookie question not accepted) still counts; the request's `Origin` must be a page's,
and an automated browser's loads are not kept. The answer is the same with or without it. The
page's sign-in call that follows (`POST /v1/auth/guest`, `GET /v1/me`,
`POST /v1/auth/discord-activity`) carries `?p=<page session>` so the load gets its user.

### `POST /v1/auth/guest`

Creates a guest and signs it in (sets the session cookie). Idempotent: with a valid
session it returns the current user instead. Body: none, or `{"name": "<display name>"}`.
`?p=<page session>` (optional): the page load it is for (see `GET /v1/platform/config`).

`POST /v1/auth/guest?fresh=true` (**CSRF**) is "New player": it ends the current session
(guest or account) and creates a new guest, for the next person at a shared computer.

`200` →
```json
{ "id": "u_01J...", "handle": "g-7f3a9c01", "gid": "g-7f3a9c01@retrocycles",
  "name": "Tester", "isGuest": true }
```

This is the **user** shape, returned by every call below that signs someone in.
Guest handles are `g-` followed by 8 lowercase hex digits.

### `GET /v1/me`

`200` with the user shape, or `401` without a session. With `features.progression` the user shape
also carries `progression` (see "Progression"); with `features.moveOffers`, when the platform offers
the player a closer server, `moveOffer` (see "Offers"). `?p=<page session>` (optional): the page
load it is for (see `GET /v1/platform/config`).

### Accounts

**Handles** match `[a-z0-9_-]{2,24}`, never start with `g-`, and are unique and
case-insensitive (the platform lowercases them). Names that would impersonate staff, the
platform or a known creator are reserved (`admin`, `moderator`, `support`, `retrocycles`, `io`,
creator names, ...: `handle_reserved`); offensive handles and display names are refused with
`handle_not_allowed` / `name_not_allowed` (section 5, "Names"). A handle is the
user part of the global id `<handle>@<authority>`. A gid that was ever used (a claimed guest, a
changed handle) is never given to anyone else. **Passwords** are 10 to 128 characters, with no
composition rules; the platform refuses passwords found in known breaches and the account's own
handle or email.

**Guest claim.** When a guest session signs up (password or external provider), the guest
becomes the account: same `id`, and its `gid` changes from `g-...@<authority>` to
`<handle>@<authority>`. When a guest session signs in to an existing account, the guest is
merged into it and deleted. Either way the old gid stays an alias of the account on the
platform, so events that named it still count for the account.

#### `POST /v1/auth/register` (**CSRF**)

`{"email": "...", "password": "...", "handle": "nova", "name": "Nova",
"sharedComputer": false}` → `200` user, signed in; the platform emails a verification link.
`name` is optional (default: the guest's name, else the handle). `400` validation errors;
`409` `already_signed_in` (signed in to an account: sign out first).

#### `POST /v1/auth/signin` (**CSRF**)

`{"login": "<email or handle>", "password": "...", "sharedComputer": false, "claimGuest": true}`
→ `200` user. `401` `invalid_credentials` for an unknown login, a wrong password, an account
without a password, and a locked account alike (after 5 failures in a row an account is locked
for a minute, doubling per lockout, up to an hour). From a guest session the guest is merged
into the account unless `claimGuest` is `false`.

**Shared computer.** With `sharedComputer: true` (register, sign-in, external start) the session
cookie has no expiry date (it ends when the browser closes) and the session ends after 2 hours
without requests, and after 12 hours at most. Otherwise sessions last 30 days, extended by use.
Every sign-in starts a new session.

#### Email links

The platform's emails link to the page: `<page>/account/verify-email?userId=...&token=...` and
`<page>/account/reset-password?userId=...&token=...` (the paths are configurable). The page
reads both parameters and calls:

- `POST /v1/auth/verify-email` (**CSRF**) `{"userId", "token"}` → `204`, or `400` `invalid_token`.
- `POST /v1/auth/reset-password` (**CSRF**) `{"userId", "token", "password"}` → `204`: the
  password is set and **every session of the account ends**; the page then signs in. `400`
  `invalid_token` or validation errors. Reset links last 1 hour and work once.

#### `POST /v1/auth/forgot-password` (**CSRF**)

`{"email": "..."}` → always `202`, whether or not an account has this address. A reset link is
emailed only to a **verified** address.

#### `POST /v1/auth/resend-verification` (session, **CSRF**)

→ `204`. At most a few emails per account per hour; extra requests are ignored.

#### `GET /v1/me/account` (session)

```json
{ "id": "u_01J...", "handle": "nova", "gid": "nova@retrocycles", "name": "Nova",
  "isGuest": false, "email": "n@example.com", "emailVerified": true, "hasPassword": true,
  "needsHandle": false, "logins": [ { "provider": "discord", "name": "Discord" } ],
  "createdAt": 1790000000 }
```

#### `POST /v1/me/password` (session, **CSRF**)

`{"currentPassword": "...", "newPassword": "..."}` → `204`. `currentPassword` is required when
the account has a password (without one, this adds a password). Every other session ends.

#### `DELETE /v1/me/logins/{provider}` (session, **CSRF**)

→ `204`; `404` `not_linked`; `409` `last_sign_in_method` (add a password or another provider first).

#### `POST /v1/auth/handle` (session, **CSRF**)

`{"handle": "nova"}` → `200` user. Only while `needsHandle` is `true` (an account an external
provider just created, with a suggested handle), once; `409` `handle_already_set`.

### External sign-in (popup)

The page opens a popup (so the running game is not reloaded):

```
GET /v1/auth/external/{provider}/start?mode=signin|link&sharedComputer=false&origin=<page origin>
```

The popup goes to the provider and comes back to
`/v1/auth/external/{provider}/callback` (the redirect URI registered with the provider), then to
`/v1/auth/external/complete`, a small page that reports the result and closes:

```json
{ "type": "rc-auth", "status": "signed_in", "provider": "google", "needsHandle": true }
```

- `status`: `signed_in` (the session cookie is set; `needsHandle: true` means: ask for a handle
  with `POST /v1/auth/handle`), `linked` (`mode=link`: the provider was added to the signed-in
  account), `confirm_link` (see below), or `error` with `error`: `access_denied` (cancelled at the
  provider), `provider_error`, `external_failed`, `not_signed_in` (`mode=link` needs an account
  session), `provider_in_use` (linked to another account), `unknown_provider`, `invalid_mode`.
- The message carries no credentials. It is delivered with `window.opener.postMessage(message,
  origin)`, where `origin` is the `origin` passed at start if it is a configured page origin
  (otherwise each configured page origin is tried). The page must check `event.origin` is the
  platform's origin.
- **Cross-origin isolated pages.** A page served with `Cross-Origin-Opener-Policy: same-origin`
  (the play page, for threads) loses `window.opener` once the popup navigates to another origin.
  Then the result page navigates to `<origin><relay path>#rc-auth=<URL-encoded JSON message>` on
  the page's own origin (the platform is configured with the relay path, e.g.
  `/auth-complete.html`). The relay page posts the message on `new BroadcastChannel("rc-auth")`
  and closes itself; the play page listens on the same channel. As a fallback the page can call
  `GET /v1/me` when it regains focus.
- Use `mode=signin` for guests and signed-out players, `mode=link` on the account page.

**Linking rules.** A provider is attached to an account only while that account is signed in
(`mode=link`), or when the provider reports a **verified** email equal to an account's
**verified** email and the player confirms. Then the result is `confirm_link` and nothing is
signed in yet:

- `GET /v1/auth/external/pending` → `{"provider": "google", "providerName": "Google",
  "handle": "nova", "email": "d@example.com"}` (or `404` `no_pending_link`). The page asks
  "Link Google to nova?".
- `POST /v1/auth/external/confirm-link` (**CSRF**) → `200` user: linked and signed in. If the player
  declines, the page does nothing; the pending sign-in expires after 10 minutes.

Unverified provider emails never link and are not stored: such
a sign-in creates a new account. A guest session signing up through a provider is claimed as
above; one signing in to an already linked account is merged.

### Sessions

- `GET /v1/me/sessions` (session) → `[{"id": "eF3CM8WTpm93fXyx", "createdAt": 1790000000,
  "lastSeenAt": 1790000300, "device": "Chrome on Windows", "current": true,
  "sharedComputer": false}]`, most recently used first.
- `DELETE /v1/me/sessions/{id}` (session, **CSRF**) → `204`; that browser is signed out at its
  next request. `404` `session_not_found`.
- `POST /v1/auth/logout` (**CSRF**) → `204`: this session ends on the platform, not only in the
  browser.
- `POST /v1/auth/logout-all` (session, **CSRF**) → `204`: every session of the user ends, this
  one included.

### Linked IDs

A player who already plays with a global id of another authority (`name@forums`, an
armathentication login) links it to their account, so official servers credit that id's play to
the account (progression, and anything else the platform keys by gid). Two ways:
- **With its password, on the page** (`features.linkByPassword`): the player types the
  id and its password into the page; the platform asks the id's authority as a game server would
  and the page answers the authority's salt, so only a salted, single-use md5 answer leaves the
  browser and the password never reaches the platform (`POST /v1/me/link-checks`, then `POST
  /v1/me/linked-ids`, below).
- **With a code, in game** (the desktop game): the account shows a one-time code, the player logs in
  on an official server with the old id and types `/link <code>` in the chat, and the server reports
  the authenticated id with the code (section 3, "Linking old global ids").

Only with `features.linking`; otherwise these answer `404` `linking_off` (with `features.linking`
but without `features.linkByPassword`, the two password calls answer `404` `link_by_password_off`).
Accounts only: a guest gets `403` `guest`.

- `POST /v1/me/link-codes` (session, **CSRF**) → `201`
  `{"code": "K7Q-4M2", "expiresAt": 1790000600, "minutes": 10}`. Six characters of
  `23456789ABCDEFGHJKMNPQRSTVWXYZ` (no 0, 1, I, L, O, U), shown with a dash; typed back in any case,
  with or without the dash. Works once, for `minutes`; a new code ends the account's earlier unused
  ones. The platform keeps only a hash. `429` `rate_limited` (Retrocycles: 10 codes per account
  per hour).
- `GET /v1/me/linked-ids` (session) → `[{"gid": "Vet@forums", "linkedAt": 1790000300,
  "serverId": "eu-h01-r3"}]`, oldest first: `gid` as the server reported it at the link,
  `serverId` the room it was linked in, or `"page"` for a link made with the password.
- `DELETE /v1/me/linked-ids/{gid}` (session, **CSRF**; `gid` path-encoded, any case) → `204`.
  From then on that id's play no longer counts for the account (what it earned stays), and it can
  be linked again, to any account. `404` `not_linked`.
- `POST /v1/me/link-checks` (session, **CSRF**) `{"gid": "Vet@forums"}` → `200`
  `{"check": "<opaque>", "gid": "Vet@forums", "user": "Vet", "method": "md5", "prefix": "",
  "suffix": "", "salt": "<32 hex>", "expiresAt": 1790000120}`. The platform reads the authority as a
  game server does (a name without a dot is `<name>.authentication.armagetronad.net`; raw IP
  addresses and an explicit port 80 are refused, `400` `not_global_id`), asks it
  `?query=methods` (`md5` preferred, else `bmd5`) and, for `md5`, `?query=params`. `gid` is the id
  as it will be linked (the authority's short form). The page answers with `hash` =
  `md5(md5(prefix + password + suffix) + salt)`, `%u` in prefix and suffix replaced by `user` and the
  password UTF-8; for `bmd5`, `md5(md5(password) + salt)` with the password in Latin-1; the salt as
  16 bytes, the hash as 32 lowercase hex digits. `check` is good for this account until `expiresAt`
  (Retrocycles: 2 minutes). `409` `own_authority` / `already_linked` / `linked_elsewhere` /
  `too_many` (nothing is asked then), `502` `authority_unreachable` / `authority_invalid` /
  `authority_unsupported` (no method a game client knows), `429` `rate_limited` (Retrocycles: 10
  checks per account per hour).
- `POST /v1/me/linked-ids` (session, **CSRF**) `{"check": "...", "hash": "<32 hex>"}` → `201` with
  the new row (`serverId` `"page"`), or `200` with it when it was linked to this account already.
  The platform sends `?query=check&method&user&salt&hash` to the authority and links on
  `PASSWORD_OK` (when the authority names the user on that line, under its own host, that name is
  linked). `422` `wrong_password` / `unknown_user`, `410` `check_expired` (expired, another
  account's, or altered), `400` `hash_invalid`, `409` as above, `502` as above, `429`
  `rate_limited` (the per-gid limit `/link` has too).

A gid is linked to at most one account, matched case-insensitively. Ids of the platform's own
authority (its accounts and guests, also in the long form `<authority>.authentication.armagetronad.net`)
and a server's local accounts (`user@`, no authority) are never linked.

### Game passwords

Clients that cannot use login tokens (the desktop game, any server without `--enable-platform`) log in
the classic way, through the platform's armathentication authority (section 6), as
`<handle>@<authority>`. The site password and external sign-ins never work there: an account makes
**game passwords** instead, generated, single-purpose and revocable, and the game remembers one
(`PASSWORD_STORAGE`). A desktop client cannot paste, so a game password is four short words the
player reads off another screen and types once; the client hashes exactly what was typed, so the
authority accepts the words with spaces, dashes or nothing between them, in lower case, upper case,
with a capital first letter or with every word capitalized. Only with `features.gamePasswords`,
otherwise `404` `game_passwords_off`; accounts only (`403` `guest`).

- `POST /v1/me/game-passwords` (session, **CSRF**) `{"label": "Desktop at home"}` (optional, at most
  40 characters; default `Game password`) → `201`
  `{"id": "gp_01J...", "label": "Desktop at home", "createdAt": 1790000000,
  "password": "tiger plank zebra robin", "login": "nova@retrocycles"}`: four words, lowercase,
  spaced (Retrocycles: of the EFF short word list less the words unfit for a game for all ages, 1201
  words, about 41 bits; guessing is online only, against the failed-check limit of section 6). The
  platform keeps the words encrypted (no weaker than their md5, which logs in as well). `400`
  `label_invalid`; `409` `too_many_game_passwords` (Retrocycles: 10).
- `POST /v1/me/game-passwords/{id}/reveal` (session, **CSRF**) → `200` with the same shape: the words
  again, to type them into the game (the platform keeps a record that they were shown). `404`
  `game_password_not_found`.
- `GET /v1/me/game-passwords` (session) → `[{"id", "label", "createdAt", "lastUsedAt"}]`, oldest
  first; `lastUsedAt` is the last login with it (`null`: never). Never the password.
- `DELETE /v1/me/game-passwords/{id}` (session, **CSRF**) → `204`: the next login with it fails (a game
  already logged in stays). `404` `game_password_not_found`.
- `POST /v1/auth/community-otp` (session, **CSRF**; accounts only) → `{"password": "...",
  "login": "nova@retrocycles", "expiresAt": 1790000120}`: a **one-time password** for a server that
  asks the page's player for a password (`authRequest`, `web/wasm/API.md` on the main branch): the page answers with
  `auth(login, password)`. Works once, for 2 minutes; never shown or kept by the page. `429`
  `rate_limited`.

### Friends

Friendships are between **accounts**. A guest asking gets `403` `guest` ("create an account to
add friends"); a guest cannot be asked either. Only with `features.friends`, otherwise every path below
answers `404` `friends_off`.

A **person** in the answers below:

```json
{ "userId": "u_01J...", "handle": "kim", "name": "Kim", "isGuest": false, "level": 7 }
```

`name` is the display name (what the game shows may differ: names in the game are Latin-1, handles are
not, so a page shows both when they differ). `level` is the progression level, `null` without
`features.progression`. A guest's `handle` is its `g-...` id.

- `GET /v1/me/friends` (session) →
  ```json
  { "friends":  [ { "user": person, "since": 1790000000, "presence": presence } ],
    "incoming": [ { "id": 41, "user": person, "createdAt": 1790000300 } ],
    "outgoing": [ { "id": 42, "user": person, "createdAt": 1790000400 } ] }
  ```
  Friends by name; requests newest first. Requests from players this account blocked or muted are not
  listed (they stay pending and are never shown). A friend's **presence**:
  ```json
  { "state": "playing", "room": { "serverId": "eu-h01-r3", "name": "Quick Play EU 1", "mode": "qp-ffa" },
    "joinable": true, "color": "#ff8800", "lastSeen": 1790000500, "challengeable": true }
  ```
  `state`: `playing` or `spectating` (in an official room, from its heartbeat roster: `room` says which),
  `menu`, `queue`, `local`, `community` (what their page last said, for 3 minutes), else `offline`.
  `joinable`: `POST /v1/quickplay {"friend"}` can seat this account in their room. `color`: their cycle
  colour as their page last said (for a swatch), or `null`. `lastSeen`: their page's last report (Unix
  seconds), `null` if never. `challengeable`: their page hears a 1v1 challenge now (`menu` or
  `community` from their page, or a room's roster with `client: browser`), so `POST /v1/duels {"friendId"}`
  can reach them; whether they take challenges at all is not said. A friend who appears offline is
  `offline` with no room, no `lastSeen` and not `challengeable`.
  Presence is only ever shown to friends.
- `POST /v1/me/presence` (session, **CSRF**) `{"state": "menu", "color": "#ff8800"}` → `204`: what the
  page is doing (`menu`, `queue`: finding or joining a room, `local`, `community`: a game on a server
  without platform login) and the player's cycle colour (optional; kept when left out). The page sends
  it when its state changes and every minute while the tab is visible, and nothing while it is hidden
  or playing in an official room (the room's roster says that). A guest's is answered and not kept.
  `400` for another state or a colour that is not `#rrggbb`.
- `GET /v1/me/privacy` (session) → `{"appearOffline": false, "friendsCanJoin": true, "friendsCanMessage": true}` (the defaults);
  `PUT /v1/me/privacy` (session, **CSRF**) with either or both → the same shape. Appear offline: shown
  offline to every friend, the room hidden. Friends can join off: seen, never `joinable`. Friends can
  message off: messages are refused (`not_accepting`, below).
- **Join and Spectate**: `POST /v1/quickplay` (below) with `{"friend": "<userId>"}` instead of a region or
  room: the platform finds the friend's room itself (they must be friends, `joinable`), then answers as
  for an invite's `room` (`503` `room_full` while full, asked again as for an invite); `404`
  `friend_not_joinable` otherwise. Spectating is the page's: it connects with the spectator switch on.
- `POST /v1/me/friend-requests` (session, **CSRF**) with **one** of `{"handle": "kim"}` (a handle, or a
  global id `name@authority` that resolves to an account, linked ids included) or `{"serverId":
  "eu-h01-r3", "netId": 477}` (a player in that room, through the room's roster: the account its server
  verified, never a name) →
  - `201` `{"state": "outgoing", "request": {"id", "user", "createdAt"}}`: asked;
  - `200` `{"state": "friends", "friend": {"user", "since"}}`: they had asked already, so this answers
    their request and both are friends now (or were already);
  - `200` `{"state": "outgoing", "request": {...}}` when this account had asked already.
  A request to a player who blocked this account is answered the same as any other and never shown to
  them. `400` `self`; `400` `not_an_account` (a guest, a bot or a player the room did not verify);
  `404` `player_not_found`; `409` `blocked` (this account blocked them: unblock first); `409`
  `too_many_requests` (Retrocycles: 100 pending sent) or `too_many_friends` (500); `429` `rate_limited`
  (30 requests per account per hour).
- `POST /v1/me/friend-requests/{id}/accept` (session, **CSRF**; a request to this account) → `200`
  `{"user", "since"}`. `404` `request_not_found`.
- `DELETE /v1/me/friend-requests/{id}` (session, **CSRF**) → `204`: declines a request to this account or
  cancels one it sent; the other side is not told. `404` `request_not_found`.
- `DELETE /v1/me/friends/{userId}` (session, **CSRF**) → `204`, for both sides. `404` `not_friends`.
- `GET /v1/me/blocks` (session) → `[{"user": person, "createdAt"}]`, newest first.
- `PUT /v1/me/blocks/{userId}` (session, **CSRF**) → `204`: ends a friendship and every request between the
  two; their requests are not listed from then on. Idempotent. `400` `self`; `404` `player_not_found`.
- `DELETE /v1/me/blocks/{userId}` (session, **CSRF**) → `204`. Idempotent.

A block is never shown to the blocked player.

### Messages

Direct messages between **friends** (strangers cannot message for now). Same feature switch and account rule as "Friends".

- `POST /v1/me/messages` (session, **CSRF**) `{"to": "<userId>", "text": "gg"}` → `201` a **message**:
  ```json
  { "id": 1201, "from": "u_01J...", "to": "u_01K...", "text": "gg", "createdAt": 1790000700, "read": false }
  ```
  The text passes the rooms' chat check (section 5): a blocked line is refused (`400`
  `message_blocked`), a masked one is delivered masked. `400` `empty`, `too_long` (240 characters),
  `self`; `403` `friends_only` (not friends), `muted` (the sender has a mute or ban in force in every room);
  `404` `player_not_found`; `409` `not_accepting` (the recipient switched messages off, blocked or muted the
  sender: one answer for all three); `429` `rate_limited` (20 a minute).
- `GET /v1/me/conversations` (session) → `{"conversations": [{"user": person, "last": message, "unread": 2}],
  "unread": 3}`, newest first; `unread` over every conversation.
- `GET /v1/me/conversations/{userId}?before=<id>` (session) → up to 50 messages, oldest first: the newest
  ones before `before` (absent: the newest).
- `POST /v1/me/conversations/{userId}/read` (session, **CSRF**) → `204`: every message from them is read.

Kept 90 days, at most the newest 200 per pair; older ones are deleted.

### `POST /v1/auth/discord-activity`

(no session needed.) The play page inside a Discord Activity: `{"code": "<Discord's
authorization code>", "codeVerifier": "<PKCE verifier>"}` (`codeVerifier` when the page sent a
`code_challenge` with Discord's `authorize`). The platform exchanges the code with Discord (no
`redirect_uri`, as for every Activity), reads `/users/@me`, and signs in the account that has this
Discord user as its `discord` sign-in, the same one site sign-in with Discord finds; if there is none,
it makes one (a handle from the username, the name from the display name, no email).
`?p=<page session>` (optional): the page load it is for (see `GET /v1/platform/config`). Answer:

```json
{ "accessToken": "<Discord's, for the SDK's authenticate()>", "sessionToken": "rcs_...",
  "expiresAt": "2026-10-11T05:24:56Z", "me": { "...": "as GET /v1/me" } }
```

`sessionToken` is the bearer session (section 1); it slides like the cookie (a day, renewed when
less than half is left) and shows in the sessions list as a Discord Activity. Errors: `400`
`bad_code` (Discord refused the code), `404` `discord_not_configured`, `503` `discord_unavailable`.

### `GET /v1/me/stream`

(session; accounts, `features.friends`.) Server-sent events for the signed-in account, for an
`EventSource` with `withCredentials` (CORS as for every call):

- `ready` `{"after": <id>}` first; then `message` with a message (either direction; the event's `id` is
  the message's, so a reconnect sends `Last-Event-ID` and resumes after it, or `?after=<id>`);
- `friends` `{}` when requests or friends changed (ask `GET /v1/me/friends` again);
- `read` `{"unread": n}` when the unread count changed (the first one comes at once);
- a comment line every 25 s. The stream ends after about 10 minutes; `EventSource` reconnects by itself
  (`retry: 3000`). Changes made through any platform instance reach every stream.

An `EventSource` cannot send a bearer session: `POST /v1/me/stream-ticket` (bearer or cookie) answers
`{"ticket": "...", "expiresAt": ...}`, good once within 60 s as `GET /v1/me/stream?ticket=<ticket>`. A
reconnect needs a new ticket, so the Retrocycles page polls instead when its session is a bearer one.

### Mutes

Personal, not moderation: a muted player's chat is left out of the page's chat overlay in every
room, their events stay; their friend requests are not listed. Any session may mute (a guest's mutes move
to the account when it is claimed or merged), any player the platform knows (a guest or an account). Only
with `features.mutes`, otherwise `404` `mutes_off`.

- `GET /v1/me/mutes` (session) → `[{"user": person, "createdAt"}]`, newest first (at most 1,000).
- `PUT /v1/me/mutes/{userId}` (session, **CSRF**) → `204`. Idempotent. `400` `self`; `404`
  `player_not_found`; `409` `too_many_mutes` (1,000).
- `DELETE /v1/me/mutes/{userId}` (session, **CSRF**) → `204`. Idempotent.

A player the room did not verify (a desktop client without a login) has no user: a page mutes them by name
for that room only.

### `GET /v1/rooms/{serverId}/players?netIds=477,480`

(session; with `features.friends` or `features.mutes`, else `404` `social_off`.) Who the players of a room
are, by the network ids every client knows them by (the API's `players[].id`): the account or guest their
server verified, from the room's roster (its heartbeats, section 3). At most 64 ids. →

```json
{ "players": [ { "netId": 477, "user": person, "friend": "friends", "muted": false, "blocked": false, "browser": true } ] }
```

One entry per player found; bots, players without a verified id and ids not in the roster are left out (a
player who joined seconds ago is in the roster after the room's next heartbeat: ask again a little later).
`friend`: `self`, `friends`, `outgoing` (this account asked), `incoming` (they asked), `none`; `none` for a
guest asking. `browser`: the roster's client is a browser (only they can be challenged to a duel). A
room whose heartbeat is stale answers an empty list. `404` `room_not_found`.

### `POST /v1/auth/game-token`

Body `{"serverId": "dev-local-1"}`, requires a session.
`200` → `{"token": "<login token>", "expiresAt": 1790000060}`; `401` no session;
`404` unknown server; `403` `banned` when the user has a ban in force for that server (section 5),
with `until` (Unix seconds, `null` for a permanent ban) and `reason` (text for the player):
`{"status": 403, "title": "You are banned.", "code": "banned", "until": 1790086400, "reason": "Abusive chat"}`.
The page shows it and does not connect.

### `GET /v1/auth/keys`

Public keys for login tokens, newest first. Game servers fetch this at startup and
cache it (refresh on an unknown `kid`, at most once per minute).

```json
{ "keys": [ { "kid": "2026-09-a", "alg": "EdDSA", "crv": "Ed25519", "x": "<base64url 32 bytes>" } ] }
```

### `GET /v1/net/ice-servers`

ICE servers (a TURN relay) for the browser's next WebRTC connection to a game server, so
players on networks that block outbound UDP can still play. The page fetches this right
before connecting (next to the game token), passes the list to its `RTCPeerConnection`
and keeps it in memory for that connection only; it never stores it.

A session is optional: the page sends its cookie (`credentials: "include"`) and the
platform names the user in the credential when there is one. Rate limited per client IP
(`429`), generously (a classroom shares one IP). `Cache-Control: no-store`.

Optional query `serverId`: the room about to be joined. A platform with a relay per region
answers with the relay of that room's region; without it (or for a room it does not know)
it answers with its default relay.

`200` →
```json
{
  "iceServers": [
    { "urls": [ "turn:turn.example.org:3478?transport=udp",
                "turn:turn.example.org:3478?transport=tcp",
                "turns:turn.example.org:443?transport=tcp" ],
      "username": "1790003600:u_01J9ZQ5X8Y7W6V5T4S3R2Q1P0N",
      "credential": "waYWl2zfNAshmOV4h7F2eT0wV3w=" }
  ],
  "expiresAt": 1790003600
}
```

- `iceServers` is a list of WebRTC `RTCIceServer` objects (`urls`: a string or a list of
  `stun:`, `turn:` or `turns:` URLs; `username` and `credential` for TURN) that the page may
  hand to `new RTCPeerConnection({ iceServers })` unchanged. It is `[]` when the platform
  runs no relay; the page then connects directly, as without a platform.
- TURN credentials follow the TURN REST API scheme (coturn's `use-auth-secret`):
  `username` = `<expiresAt>:<user id>` (`<expiresAt>:anon-<8 hex>` without a session),
  `credential` = `base64(HMAC-SHA1(<shared secret>, username))` with standard base64
  (with padding) over the UTF-8 bytes of `username`. The shared secret is known only to the
  platform and the TURN server. Test vectors: `test-vectors/ice-credentials-v0.json`.
- `expiresAt` (Unix seconds) is when the TURN server stops accepting the credential for
  new allocations. The page fetches a fresh list for every connection and uses it within
  seconds, so the lifetime can be short (Retrocycles: 1 hour). A connection that is already
  relayed keeps working past `expiresAt`: coturn checks the credential when the allocation
  is made and refreshes it with the session's key (verified with coturn 4.6.3: a session
  with a 60 s credential played on for 12 minutes, across the 10 minute allocation refresh).
  A relay that re-checks the timestamp on refresh would end sessions at `expiresAt`; a
  platform using one must choose a lifetime that covers a play session.

The game servers need nothing for this: they publish host candidates on their public IP
and the relay forwards to those. A page without a platform (community mode) may carry
static ICE servers in its own runtime configuration instead (`web/wasm/API.md` on the main branch, "ICE
servers").

### Quick Play

The page asks the platform for a room, then connects to it exactly as to any other server
(game token for `serverId` when `platformLogin`, ICE servers, `signalUrl`). Only with
`features.quickplay`; a page without a platform keeps its own server list.

#### `GET /v1/regions`

```json
{ "regions": [ { "id": "eu", "name": "Europe", "pingUrl": "https://eu.play.example.org/ping" },
               { "id": "syd", "name": "Sydney", "pingUrl": "https://syd.play.example.org/ping", "candidate": true } ],
  "default": "eu" }
```

The regions Quick Play places players in, and the one it uses when the page names none.
`candidate: true` (optional, absent means false) marks a region without rooms yet: the page
measures it like the others, so the platform learns how many players are near it, but Quick Play
places nobody there until a room names it. A page that ignores the flag and asks for a candidate
region as its `region` gets the nearest region that has rooms. A candidate's `pingUrl` may be a
machine of the hosting provider in that region rather than one of the platform's; it must answer the
same way (CORS or `Cross-Origin-Resource-Policy`).
The page should time a `HEAD` of each `pingUrl` (no body to download; a `pingUrl` answers `GET`
and `HEAD` at once, with `204`, `no-store`, open to any origin by CORS and
`Cross-Origin-Resource-Policy: cross-origin`, no credentials; a cross-origin isolated page times it
as a CORS request) and send every region's time to `POST /v1/quickplay` as
`latencies`, which the platform places the player by, and the fastest region as `region`, for a
platform that does not read `latencies`; it measures, the platform does not. `pingUrl` is ideally a machine in that region (its signalling
gateway); a platform may point it at its own `GET /v1/ping` until it has one. No session.

#### `POST /v1/quickplay` (**CSRF**; session optional)

Body (every member optional): `{"region": "eu", "mode": "qp-ffa", "protocol": 17, "latencies": {"eu": 22, "syd": 231}, "moveId": "mv_...", "room": "eu-h01-r3"}`.
- `region`: an id from `GET /v1/regions` (the page's fastest); missing or unknown: the default
  region. Used only when the platform has no latencies for the player: none in the request and,
  with a session, none stored from an earlier request. Then that region alone is tried.
- `mode`: a room mode (the rooms' `PLATFORM_MODE`); missing: the platform's default.
- `protocol`: the highest network protocol version the client speaks (`sn_MyVersion()`;
  every client since 0.2.8 also speaks every lower one). Missing: the platform assumes
  the version of the client it serves with its page.
- `latencies`: the page's measured round trip to each region's `pingUrl`, region id to
  integer milliseconds (0 to 60000), for the regions it could time. With a session the
  platform keeps each user's latest number per region (to place players and plan hosting by
  latency); an unknown region or a value that is not such a number is ignored, never an
  error. The platform places the player by them, and a request without them by the session
  user's latest stored numbers when it has any. Retrocycles: the nearest region if it has a
  room with players and a free seat; otherwise the closest region that has one, however far
  (a player far from everyone must still find a game); otherwise the nearest region with a
  free seat (an empty room, with bots). With latencies, `region` is ignored. The answer's
  `region` may therefore differ from the request's: the page uses the answer's `region`,
  `serverId` and `signalUrl`, never its own guess.
- `moveId`: the page follows a move (section 3, "Moving players"): the id of the browser
  client's `move` event. If the session's user is one of the move's players, the answer is
  the seat the platform holds for them in the move's target, whatever the region or mode; a
  player of a move for everyone whom the platform could not name yet (the room reports the
  move a moment after the page hears of it) gets one of the move's seats too. An unknown,
  finished or expired move, or one the user is not part of, is ignored: the request is an
  ordinary Quick Play. So the page never needs a second request to fall back.
- `friend`: a friend's user id (session, `features.friends`; not with `room`): the room the friend is in,
  found by the platform from their presence (see "Friends"), then exactly as `room` below; `404`
  `friend_not_joinable` when they are not friends, hide it, or are in no official room.
- `discordInstance`: the Activity launch the page runs in (Discord's `instance_id`; not with `room` or
  `friend`, `400`): everyone in one launch plays in one room. The first request picks a room with
  seats for the launch's players (the platform remembers it per instance), later ones get that room;
  a closed or full one is replaced and the launch follows the new one. Only rooms that take game
  packets over WebSocket (the room's `webSocket` admin flag; Discord allows no WebRTC) are picked.
  With the platform's Discord bot token configured, the instance is checked with Discord: `400`
  `unknown_instance`, `403` `not_in_instance` (the session's Discord user is not in it), `503`
  `discord_unavailable`.
- `webSocket`: `true` when WebRTC does not get through from this page (a connect failed at ICE: a
  network that lets neither UDP nor the TURN relay out): only rooms that take game packets over
  their signalling WebSocket (the room's `webSocket` flag) are picked, an invite's or friend's
  room too (`404 room_closed` when that room does not). The page then connects with
  `transport: "websocket"`. Not with `discordInstance` (which picks only such rooms anyway).
- `avoid`: server ids the page just failed to reach (at most 8; more, or an empty or over-long
  id, is `400`): this pick leaves them out. Ignored with `room` and `friend` (that one room).
  The page sends it when it asks again after a connect failed before the login (see `DELETE`
  below); with every candidate avoided the answer is `503 no_room` as usual.
- `room`: an invite link's room (a `serverId`, at most 64 characters; the page's link is
  `?join=<serverId>`, copied from the in-game menu). The answer is a seat in exactly that
  room or nothing: `region`, `mode` and `latencies` do not place it (the room's own region and
  mode are used; latencies are still kept), and the room may
  also take the slot Quick Play keeps free. `503` with `code: "room_full"` (and
  `retryAfter`) when it is full, the page asking again as for `no_room`; `404` with
  `code: "room_closed"` when it is unknown, not official, stale or not open (no retry).
  A `moveId` the user is part of wins over `room`.

`200` →
```json
{ "roomId": "eu-h01-r3", "serverId": "eu-h01-r3", "name": "Retrocycles EU 3",
  "signalUrl": "wss://eu.play.example.org/signal/eu-h01-r3", "platformLogin": true,
  "region": "eu", "reservationId": "qr_4f0c9a...", "expiresAt": 1790000030 }
```

- `serverId` is the room's server id (for `POST /v1/auth/game-token`); `roomId` is the same
  id in v0. `name` may carry the game's colour codes. `signalUrl` is a `ws:`/`wss:` URL or
  a path the page resolves against its own host (like a `servers.json` `url`).
  `platformLogin`: the room accepts login tokens (its `PLATFORM_LOGIN`).
- The answer holds a **seat** in the room until `expiresAt` (30 s later for Retrocycles), until
  the room reports the player authenticated (`player_authenticated` for the session's user)
  or its player count rises, or until the page releases it. So a burst of presses cannot
  overfill a room before its heartbeats show the joins. With a session, a new Quick Play
  releases the user's earlier open seat.
- `503` (`code: "no_room"`, header `Retry-After`) →
  `{"status": 503, "title": "Every room is full.", "code": "no_room", "retryAfter": 5}`:
  no room the platform would place this player in can take one now (with latencies: no
  region has a free seat; without them: none in that region). The page says so and asks again after
  `retryAfter` seconds (the platform may be starting rooms meanwhile), and gives up after
  about a minute.
- An answer for a move holds its seat until the move's arrival deadline (Retrocycles: 60 s
  after the room moved the players) instead of 30 s; `expiresAt` says when.
- `400` malformed (a member of the wrong type); `429` rate limited. `Cache-Control: no-store`.
- How the region and room are chosen beyond the above is the platform's business
  (Retrocycles: the region by latency, then the region's liveliest room first; see its
  documentation). Each retry after a `503` is placed afresh.

#### `GET /v1/live`

```json
{ "players": 23, "rooms": 3, "minShown": 5 }
```

`?region=<id>` (optional): one region's rooms only.

How many people (`humans` of the heartbeats, desktop players included) are in the open,
healthy, official rooms now, and how many rooms that is. The page shows "N playing" beside
Quick Play only when `players >= minShown` (Retrocycles: 5; a small figure says "empty"
louder than none). `Cache-Control: public, max-age=15`. No session, no CSRF.

#### `DELETE /v1/quickplay/reservations/{reservationId}` (**CSRF**)

The page could not use the seat (signalling, ICE or login failed, or the player backed out
before playing): the seat is free again at once. `204`, also for unknown or already released
ids (they are random, 128 bits; nothing to probe). No session needed.

`?reason=failed`: the page could not reach the room (the connect failed before the login),
not a back-out. The platform may then keep the room out of Quick Play for a while (Retrocycles:
two different players failing within 3 minutes with nobody joining it since; it is tried again
once those age out or someone joins). A room whose game loop still heartbeats while its
signalling does not answer looks healthy otherwise.

### Duels (private rooms)

(session, guests included; with `features.privateRooms`, else `404` `private_rooms_off`.) A 1v1 in a
private pool room. Nobody is put in a room until the other side accepts: the platform claims the pool
room at that moment, nearest to both players. Times are Unix seconds.

```
DuelPerson = { "userId", "name", "isGuest" }
DuelView   = { "code", "state": "pending|accepted|declined|cancelled|expired", "from": DuelPerson,
               "to": DuelPerson | null, "createdAt", "expiresAt",
               "room": { "code", "serverId", "name", "region" } | null }
```

- `POST /v1/duels` `{ "latencies"?, "serverId"?, "netId"?, "friendId"? }` → `DuelView`. Without a target, a **link
  duel** (`?duel=<code>`; `to` is set to whoever accepts it). With `serverId` + `netId`, a
  **challenge** to that player: both must be in that room's roster, the target a browser client. With
  `friendId` (not with a room), a challenge to that friend from the Friends panel while their presence is
  `challengeable`. Either way neither blocking the other (friends' or challenges' blocks), the target not muting the caller and
  not having challenges off (else `404` `player_unavailable`, never which). The caller's earlier
  pending duel is cancelled. `429` `duels_limit`; `429` `duel_cooldown` with `retryAfter` after that
  player declined (or let lapse) the caller's challenge: 10 min after one, a day after three in a day. A challenge expires unanswered after
  60 s; a link lives while its creator keeps polling `wait` (45 s without: expired), 30 min at most.
- `GET /v1/duels/wait?after=<cursor>` → `{ "cursor", "incoming": DuelView[], "outgoing": DuelView | null }`:
  the pending challenges to me (newest first) and my newest duel (pending, or answered in the last
  2 minutes). Held up to ~25 s while the cursor is unchanged; the page polls it while it can be
  challenged (a network game; the home screen for a signed-in player) or waits on a link.
- `POST /v1/duels/{code}/accept` `{ "latencies"? }` → `{ "duel": DuelView, "room": <the private room
  answer with a ticket, as .../join> }`. A link: anyone but its creator; a challenge: its target.
  `409` `own_duel`, `404` `duel_ended`, `503` `private_rooms_busy` with `retryAfter` (still pending).
  An accepted link whose room is live answers the same (late openers watch).
- `POST /v1/duels/{code}/decline` (the target), `.../cancel` (the creator) → `204`.
- `GET /v1/duels/settings` → `{ "acceptChallenges", "blocked": [{ "userId", "name", "since" }] }`; `PUT`
  `{ "acceptChallenges" }` (off declines what waits); `PUT /v1/duels/blocks/{userId}` `{ "name"? }` (their
  waiting challenges declined, mine to them withdrawn; `409` `duel_blocks_full`) and `DELETE` → the
  settings. Guests have them too. The page shows them in Settings (PLAYER, 1V1 CHALLENGES), under a
  challenge in the Esc menu and on a player's card.

The other side then joins with `POST /v1/private-rooms/{room.code}/join` (a fresh ticket per connect).

### News

(session, guests included; with `features.news`, else `404` `news_off`.) Staff write posts in the admin
app; the platform serves the published ones as HTML on the site's domain (`/news`, `/news/<slug>`, an RSS
feed at `/news/feed.xml`), and sends some of them to every player's bell. Times are ISO 8601.

- `GET /v1/me/news` → `{ "items": [{ "id", "title", "summary", "url", "sentAt", "unread" }], "unread",
  "allUrl" }`: the newest posts sent to the bell (8), newest first. `title` is the line it was sent with, else
  the post's title; `url` the post on the site (absolute); `allUrl` the news page. `unread`: sent after the
  player last opened the bell and after the player was created (a newcomer starts with nothing unread).
  Withdrawn sends and unpublished posts are gone.
- `POST /v1/me/news/seen` (**CSRF**) → `204`: the bell was opened; everything sent so far is read.

The page shows the bell in the status bar while `features.news` is on, opens a post in a new tab (from
inside Discord, as an outside link), and asks again every ten minutes and after a game.

### Offers

Only with `features.moveOffers`. A player playing far from the region nearest to them (by the
`latencies` the page sent with Quick Play) may be offered a move to a room there, together with the
other players near it who accept. The page learns of an offer from `GET /v1/me`, which it reads after
every round end anyway (as for XP, "Progression"), shows it at the end of the round, and answers.

#### `GET /v1/me` with an offer

```json
{ "id": "u_01J...", "handle": "g-7f3a9c01", "gid": "g-7f3a9c01@retrocycles", "name": "Tester", "isGuest": true,
  "moveOffer": { "id": "of_Xq3Y0Gm4H1s9aZkQ2bT7wA", "region": "syd", "regionName": "Sydney",
                 "serverName": "Retrocycles SYD 1", "pingHere": 231, "pingThere": 38, "accepted": 3, "needed": 4,
                 "expiresAt": 1790000600, "status": "accepted" } }
```

- `moveOffer` is absent when there is nothing to show. `region` is a region id of `GET /v1/regions`
  and `regionName` its name; `serverName` the room the group would go to now (it may carry colour
  codes). `pingHere`: the player's ping in the room they play in (the room's own measurement);
  `pingThere`: the page's measured round trip to the region (milliseconds, both).
- `status`: `open` (not answered yet) or `accepted` (waiting for the group). An offer the player
  dismissed, one whose group is moving (the move itself takes over, section 3), and one that ended
  are not served. `accepted` of `needed`: how many players near that region accepted, and how many
  the group needs (the platform's minimum, or everyone offered when fewer were): "3 of 4 ready".
- `expiresAt` (Unix seconds): the offer ends then, accepted or not.
- The platform decides who is offered what and how often (Retrocycles: a player whose ping where
  they play is at least 80 ms worse than their latency to their nearest region, where a room has a
  seat; an unanswered offer is served at most two round ends, one offer per player per hour, ten
  minutes long; a group of four, or everyone offered when fewer were). A page shows what it gets.

#### `POST /v1/me/offers/{id}` (session, **CSRF**)

Body `{"action": "shown" | "accept" | "dismiss"}`.
- `shown`: the page showed the offer at a round end (once per round end); the platform counts
  shows, so an unanswered offer stops coming.
- `accept`: the player is one of the group. When it is complete the platform moves the group at
  once (section 3, `move_players` with the players' `gids`, `when: now`, reason `closer`): each
  page then gets the client's `move` event and follows it as any move.
- `dismiss`: the player does not want it; it is never shown again (also after accepting, before
  the group moves).

`200` → the offer as in `GET /v1/me` (with `status` `moving` when this accept completed the group,
`dismissed` after a dismiss). `400` an unknown `action`; `401` no session; `404`
(`code: "offer_not_found"`) not this user's offer, or offers are off; `409`
(`code: "offer_closed"`, `offerStatus`: `dismissed`, `moving`, `expired`, `left_out`) the offer can no
longer change that way.

### Progression

Only with `features.progression`. The numbers come from the game servers' events (section 3), never
from the page: a platform credits XP for what an **authenticated** player (guest or account) did on
an **official** server it holds a key for. Bots and unauthenticated players earn nothing. How much XP
what earns, the caps and the level curve are the platform's business (Retrocycles publishes them in
its documentation); the page shows what it gets.

#### `GET /v1/me` with progression

```json
{ "id": "u_01J...", "handle": "g-7f3a9c01", "gid": "g-7f3a9c01@retrocycles", "name": "Tester",
  "isGuest": true,
  "progression": {
    "level": 2, "xp": 180, "levelXp": 100, "nextLevelXp": 280,
    "stats": { "matches": 1, "rounds": 10, "wins": 3, "kills": 4, "deaths": 9, "seconds": 812 },
    "recent": [ { "id": 1532, "time": 1790000900, "amount": 100, "reason": "first_match_of_day", "match": "m_183" },
                { "id": 1531, "time": 1790000900, "amount": 30, "reason": "match_finished", "match": "m_183" },
                { "id": 1530, "time": 1790000899, "amount": 10, "reason": "round_played", "match": "m_183" } ],
    "lastMatch": { "id": "m_183", "endedAt": 1790000900, "result": "completed", "place": 2, "players": 6,
                   "rounds": 10, "xp": 175, "levelBefore": 1, "levelAfter": 2 } } }
```

- `level` (1, 2, ...), `xp` (total). `levelXp` / `nextLevelXp`: the total XP at which the current
  level began and the next one begins (`nextLevelXp > xp >= levelXp`), so the page can draw how far the
  player is without knowing the curve.
- `stats`: over every match the player was in: `matches` finished (present at a match's end),
  `rounds` played to their end, `wins` (rounds the player won: `place` 1 in `round_ended`), `kills`
  (`death` events with the player as `killer`, `cause` `frag`), `deaths` (as `victim`), `seconds` played
  (the length of the rounds played).
- `recent`: the player's latest XP entries, newest first (at most 20). `id` grows with every entry;
  `amount` may be negative (a correction); `reason` is one of `round_played`, `round_won`, `kill`,
  `match_finished`, `first_match_of_day`, `adjustment` (by the platform's staff), or another a newer
  platform adds (show it as XP without a reason); `match` is the match it was earned in, or `null`.
  A page learns what a round earned by adding the entries whose `id` is above the highest it saw before.
- `lastMatch`: the player's most recent match that has ended, or `null`. `result` `completed` (the
  game declared a winner by its limits) or `aborted` (the server started another match first, or went
  away); `place` the player's place at the end (`null` if they left early); `players` how many were in
  it (bots included); `rounds` the rounds the player played in it; `xp` what it earned the player;
  `levelBefore` / `levelAfter` the player's level when it started and ended.
  A page sees a match end when `lastMatch.id` changes.

A guest's progression moves to the account when the guest signs up (same user) or is merged into an
existing account at sign-in: XP is added, stats follow.

#### `GET /v1/profiles/{authId}`

A player's public progression; no session needed. `authId` is a global id (`handle@authority`, also a
former one: a claimed guest's) or a user id (`u_...`).

`200` →
```json
{ "id": "u_01J...", "handle": "nova", "gid": "nova@retrocycles", "name": "Nova", "isGuest": false,
  "level": 7, "xp": 2380, "levelXp": 2220, "nextLevelXp": 2940,
  "stats": { "matches": 12, "rounds": 118, "wins": 21, "kills": 64, "deaths": 97, "seconds": 9120 } }
```

`404` `profile_not_found`; also `404` when `features.progression` is off.

### Analytics events

First-party usage events of the page, for the platform's funnel numbers (link to first turn,
connect success per stage and route, guest to account, retention). Only with
`features.analytics`; a page without a platform sends none, and a page may send none at all
(it honours Global Privacy Control, and later a player setting). Every event is optional for
the page; the platform may compute nothing from a missing one. The page view also goes to the
platform on the page's first call, `GET /v1/platform/config` (see there): a load whose events never
arrive still counts, as one the events did not report.

#### `POST /v1/events` (session optional; **no CSRF header**, a page `Origin` is required)

The body is JSON, sent as `text/plain` (what `navigator.sendBeacon` sends with a string or a
`text/plain` Blob: a CORS "simple" request, no preflight) or `application/json`, at most 32 KB:

```json
{ "page": "Xq3Y0Gm4H1s9aZkQ2bT7wA",
  "events": [
    { "seq": 1, "name": "page_view", "t": 1790000000123,
      "props": { "campaign": "spring-launch", "referrer": "https://discord.com", "device": "desktop", "browser": "firefox" } },
    { "seq": 2, "name": "play_pressed", "t": 1790000004410, "props": { "mode": "quick", "attempt": 1 } } ] }
```

- `page`: the **page session**, 16 to 43 base64url characters (at least 96 random bits),
  chosen per page load and kept only in the page's memory (never in storage or a cookie).
- `seq`: 1, 2, ... within the page session. `(page, seq)` is the idempotency key: a batch sent
  twice is stored once.
- `t`: the page's clock, Unix milliseconds. The platform only compares it with other events of
  the same page session; its own receive time orders everything else.
- `props`: an object whose members are the ones the event defines (table below): integers,
  booleans, values of an enumeration or strings of a strict pattern. **No free text**: names,
  chat, email addresses, reasons as the client words them, tokens and credentials never go in.
- At most 50 events per batch.
- **Who:** the platform takes the user from the session cookie (`credentials: "include"`); the
  body never names a user. **Origin:** browsers always send `Origin` on this cross-origin
  POST; it must be a configured page origin, else `403` `origin_not_allowed`.

`200` → `{"accepted": 2, "duplicates": 0, "rejected": 0}`. An event with an unknown `name`, or
without a valid required prop, is **rejected** (not stored; counted in `rejected`); a prop the
event does not define, or an optional one with an invalid value, is dropped from the event.
`400` malformed batch (not JSON, no valid `page`, `events` not a list or longer than 50), `403`
origin, `404` analytics are off, `413` body too large, `415` another content type, `429` rate
limited. The page ignores the answer and never retries: analytics never cost the player
anything.

**Events v0** (`?` = optional; `ms` members are milliseconds; `attempt` numbers the connect
attempts of a page session, 1, 2, ...):

| `name` | `props` | when |
|---|---|---|
| `page_view` | `device` (`phone`/`tablet`/`desktop`), `browser`? (`chrome`/`firefox`/`safari`/`edge`/`other`), `campaign`? (`[a-z0-9_-]{1,32}`, the page's `?c=`), `link`? (`[a-z0-9_-]{1,32}`, the page's `?l=` beside a campaign: the link a creator's `/c/<name>` forwarded from), `referrer`? (the referring page's origin, `https?://host[:port]`, only for another site), `utmSource`? `utmMedium`? `utmCampaign`? (the link's `utm_` parameters, lower case with blanks as `-`, `[a-z0-9][a-z0-9._-]{0,63}`; anything else is left out), `embed`? (`true`: the page inside a social post's frame, `/?embed`), `discord`? (`true`: inside a Discord Activity; then no `embed`) | the page loaded |
| `play_pressed` | `mode` (`quick`/`browser`/`local`/`ranked`), `attempt`? (Quick Play and the server browser), `transport`? (`websocket`: this connect goes over the signalling WebSocket because WebRTC failed earlier on this tab; absent: WebRTC, and in Discord) | the player pressed Play in a mode (the server browser: chose a server) |
| `bundle_loaded` | `ms` (since the page's navigation start), `ok`? | the game client is ready (or failed to load) |
| `scene_ready` | `ms` (since navigation start) | the attract scene is first on screen (the backdrop the page shows while the player chooses) |
| `quickplay_result` | `attempt`, `result` (`assigned`/`no_room`/`giveup`/`error`/`cancelled`), `ms` (since the press), `tries`?, `region`? | Quick Play through the platform: a room was assigned, the first `503 no_room` of the attempt, the page gave up (about 60 s), another failure, or the player backed out |
| `connect_stage` | `attempt`, `stage` (`quickplay`/`signalling`/`ice`/`handshake`/`sync`), `ok`, `ms` (the stage's own duration), `route`? (`direct`/`relay`/`websocket`: game packets over the signalling socket, no WebRTC), `relayProtocol`? (`udp`/`tcp`/`tls`), `reason`? (`cancelled`/`timeout`/`signalling`/`ice`/`login_denied`/`login_timeout`/`no_game_state`/`connection_lost`/`full`/`refused`/`error`/`other`) | once per stage an attempt reaches: `ok: true` when it completed, `ok: false` with a `reason` for the stage that failed (then no later stage). `quickplay`: the platform assigned a room; `signalling`: the server answered over the signalling socket; `ice`: the data channel opened; `handshake`: the game server accepted the login; `sync`: in the game. `route` once the ICE route is known (`relay`: through a TURN server) |
| `first_turn` | `ms` (since navigation start), `sincePlay`? (since the first `play_pressed`), `mode`? (`quick`/`browser`/`local`), `input`? (`key`/`touch`) | the first turn while playing, from a turn key or a touch turn button, once per page session |
| `save_prompt` | `trigger` (`level_up`/`first`/`repeat`), `matches`? (the guest's finished matches) | a guest's match summary asked to save their progress: the match raised the level, was the guest's first finished match, or every third after it (the page's rule) |
| `save_pressed` | `trigger` | the guest pressed that "save your progress" (the sign-up opens) |
| `claim_started` | `method` (`password` or a provider id, `[a-z0-9_-]{1,32}`), `kind` (`signup`/`signin`), `from`? (`summary`/`chip`: where the Profile was opened), `trigger`? (with `summary`) | a guest submitted the sign-up or sign-in form, or started a provider sign-in |
| `claim_done` | `method`, `kind`, `from`?, `trigger`? | that sign-up or sign-in succeeded |
| `session_end` | `ms` (since navigation start), `matches`? (games joined), `rounds`? (rounds started while playing), `screen`? (where the page is: `home`, `servers`, `connecting_quick`, `game`, `game_menu`, `profile`, ...; `^[a-z0-9_.:-]{1,64}$`), `screenMs`? (time on it), `last`? (the last control pressed, `<screen>:<control>`: the control's `data-track` or first class name, never its text, or a key's code: `servers:sb-join`, `home:menu-browser`, `servers:back`), `lastMs`? (time since it), `actions`? (controls pressed) | the page was hidden or closed (again each time it is hidden: the last one counts) |
| `first_action` | `control` (the first control pressed on this page load, as `session_end`'s `last`: `home:menu-quick`, `home:menu-browser`, `home:menu-local` for Training), `ms`? (since navigation start) | once per page load, right after the first press (the cookie banner's buttons do not count) |
| `region_latency` | `region` (a region id of `GET /v1/regions`), `ms` (0 to 60000: the best round trip to its `pingUrl`) | once per page load and region the page timed; the region with the lowest `ms` is the page session's **nearest region** |
| `play_stats` | `attempt`, `region`? (the room's region from the Quick Play answer; left out for a server-browser game), `pingMedian`, `pingP90` (ms, 0 to 60000: the median and 90th percentile of the client's ping read every few seconds while playing), `seconds` (0 to 86400: how long the game ran), `fpsMedian`?, `fpsP5`? (1 to 1000: the client's frame rate at the same readings, its median and its slowest 5 %) | a network game ended (the player left, the connection was lost) or the page was hidden while it ran (a game that goes on after that reports again for the rest) |
| `move_result` | `reason` (`merge`/`drain`/`closer`/`join`/`admin`), `result` (`arrived`/`failed`/`cancelled`), `ms` (since the client's `move` event) | the room moved the player (section 3, "Moving players"): the page played in the target, could not get there, or the player backed out |
| `move_offer` | `action` (`shown`/`accepted`/`dismissed`/`expired`), `region` (the region offered, a region id) | an offer of a closer server ("Offers"): the page showed it at a round end, the player accepted or dismissed it, or it went away unanswered (its time on screen passed, or the next round started) |
| `room_joined` | `server` (the room's server id, `[A-Za-z0-9_.:-]{1,64}`), `netId` (0 to 65535: the page's own player there, `players[].id` with `local`) | the page is in a platform room's game (each connect, each move; again when the net id changes). The room's roster names the same net id, so the platform finds where a room's page players play (the page session's surface: a desktop browser, a phone, the X embed) |
| `link_copied` | `what` (`[a-z0-9_-]{1,32}`: `invite`, the in-game INVITE link) | the player copied a share link (the invite link also carries `c=invite`, so the page loads it brings count as that campaign) |
| `full_game_opened` | – | the X embed's way out to the whole page was clicked |
| `client_error` | `kind` (`webgl_lost`/`out_of_memory`/`wasm_abort`/`script`/`promise`), `ms` (since navigation start), optional `where` (`page`/`api`/`client`/`other`/`unknown`: which of the page's own files, for `script` and `promise`) | something broke: a lost WebGL context, a wasm abort, an uncaught error or promise rejection of the page's own scripts (a rejection with a stack of only other origins is not sent; one with no stack is `unknown`); never the message nor the file name; at most 5 per page session |
| `clip_saved`, `clip_shared` | `ms`? / `via`? (`[a-z0-9_-]{1,32}`) | reserved for clips |

Adding an event or a prop is additive: an older platform rejects the unknown event or drops the
unknown prop. The platform stores no IP address with the events.

## 2. Login token (platform → page → game server)

A JWS compact serialization (RFC 7515) signed with Ed25519 (`alg: EdDSA`, RFC 8037):
`base64url(header) "." base64url(payload) "." base64url(signature)`, where the signature
is Ed25519 over the ASCII bytes of `base64url(header) "." base64url(payload)`.

Header: `{"alg":"EdDSA","typ":"rc-login+jws","kid":"<kid>"}`

Payload (a flat object: every member is a string or an integer, no nesting):

| Member | Type | Meaning |
|---|---|---|
| `iss` | string | `"retrocycles"` (the platform's name) |
| `sub` | string | opaque platform user id |
| `gid` | string | global id `<handle>@<authority>`, e.g. `g-7f3a9c01@retrocycles`. It changes when a guest creates an account; the platform keeps the old gid as an alias |
| `name` | string | display name hint (the server does not rename the player) |
| `aud` | string | the server id this token is for |
| `iat` | integer | issued at |
| `exp` | integer | expiry, `iat + 60` |
| `jti` | string | random, 128 bits, base64url |
| `roles` | string | comma-separated platform roles, `""` for none (e.g. `"moderator"`); see below |

A server accepts a token only if **all** hold:
1. total length ≤ 2048 bytes; exactly three base64url parts (strict: no padding, no
   non-zero trailing bits); header and payload decode to JSON objects of the shapes above,
   with `typ == "rc-login+jws"` (unknown members are ignored). The JSON may use any valid
   encoding: issuers write `+` and non-ASCII characters unescaped (raw UTF-8), so
   verifiers must not assume ASCII-only or escaped output;
2. `alg == "EdDSA"`, and `kid` names a known key;
3. the signature verifies;
4. `aud == PLATFORM_SERVER_ID`;
5. `exp >= now - 5` and `iat <= now + 30`;
6. `jti` has not been accepted before (remember accepted `jti`s until `exp + 5`);
7. the authority part of `gid` (after the last `@`) equals `PLATFORM_AUTHORITY`
   (default `retrocycles`), and the handle part is non-empty.

Test vectors: `test-vectors/login-token-v0.json`.

**Roles.** `roles` names the platform roles of the account: `staff` (the platform's support and
review staff), `moderator`, `admin`; a server ignores roles it does not know, and guests never
have any. A server maps them to its access levels when it completes the login (the highest
wins): `admin` → `PLATFORM_ACCESS_ADMIN` (default 1, Admin), `moderator` →
`PLATFORM_ACCESS_MODERATOR` (default 2, Moderator), `staff` → `PLATFORM_ACCESS_STAFF` (default
19, a plain authenticated player), no role → Authenticated as before. A role never grants the
Owner level (0): a mapping to 0 counts as 1. The server's own `USER_LEVEL` and similar settings can
still raise a player's level as for any login. So official moderators have their in-game powers on
every official room without per-server configuration.

### Carrying the token to the server

The browser client sends the token to the game server in the network message with
descriptor id **380** (`platform_login_token`, client → server, accepted only from logged-in
connections). Its payload is one string, the token, written with `nMessage << tString` and
read raw (no color code filtering); see `src/engine/ePlatform.cpp`.
The client sends it once per connection
(`ePlatform::SendLoginToken()`), only to servers the page knows accept platform login (a
`platformLogin` flag from the platform's server list or `servers.json`). Servers without the
platform ignore the message. If the connection has no player yet, the server keeps the
verified login for up to 30 s and applies it when the player appears. On success the server
authenticates the connection's player as `gid`, exactly as a successful armathentication
login would; on failure it logs the reason and the player stays unauthenticated.

### Room tickets (private rooms)

A private room (`PLATFORM_PRIVATE 1`) keeps only connections that bring a **room ticket**: the
platform hands one to whoever opens the room's invite link (`POST /v1/private-rooms` and
`.../{code}/join`), guests included. It is a JWS like the login token, signed with the same keys:

- header `{"alg":"EdDSA","typ":"rc-room+jws","kid":...}` (the `typ` keeps tickets and login
  tokens from standing in for each other);
- payload, in this order: `iss`, `aud` (the server id), `claim` (the platform's claim id),
  `iat`, `exp` (`iat` + 60), `jti`. No player identity.

The page appends it to the room's signalling URL, `?ticket=<jws>` (no client change: the
signalling socket's request target already reaches the server, `src/network/nTransportRtc.cpp`). When
the connection logs in, the server checks rules 1 to 6 of the login token (rule 7 has no `gid`; an
unknown `kid` waits for the keys, or one refresh, as a login token does, up to 30 s);
a missing or refused ticket ends the connection with "This is a private room. Open its invite
link at retrocycles.io to join." A ticket is single use: every connection attempt asks for a new
one. Desktop (UDP) clients cannot bring one, so a private room is browser-only. Signed-in
players still send their login token as usual for their name and stats.

## 3. Game server → platform

Authenticated with `Authorization: Bearer <PLATFORM_SERVER_KEY>`; the path's `serverId`
must match the key's server.

### `POST /v1/servers/{serverId}/events`

```json
{
  "bootId": "<random id chosen at server start>",
  "events": [
    { "seq": 1, "type": "server_started", "time": 1790000000,
      "data": { "version": "0.2.9.3", "platformContract": "v0" } },
    { "seq": 2, "type": "player_authenticated", "time": 1790000012,
      "data": { "gid": "g-7f3a9c01@retrocycles", "name": "Tester", "method": "token", "connection": 3 } },
    { "seq": 3, "type": "round_ended", "time": 1790000140,
      "data": { "match": 1, "round": 4, "seconds": 83, "players": [
        { "name": "Tester", "gid": "g-7f3a9c01@retrocycles", "bot": false, "team": "Tester",
          "score": 12, "alive": true, "place": 1 },
        { "name": "Bot 1", "gid": null, "bot": true, "team": "Bot 1", "score": 3, "alive": false,
          "place": 2 } ] } }
  ]
}
```

- `seq` starts at 1 per `bootId` and increases by one per event. `(serverId, bootId, seq)`
  is the idempotency key: the platform stores each event once and ignores repeats.
- `method` is `"token"` (this contract) or `"krawall"` (armathentication).
- `connection` in `player_authenticated` is the connection the player logged in on, the same number
  `client_connected` sends, so the platform ties a login to its client exactly. A server before it
  leaves it out (the platform then pairs a login with the room's latest connection before it).
- Response `200` → `{"accepted": <new events stored>, "duplicates": <ignored>}`.
  `401`: bad key, or a `serverId` the key doesn't belong to (including unknown ids, so ids
  can't be probed); `400`: malformed, or more than 1000 events in one batch. Gaps in `seq`
  are accepted (the platform doesn't reject them). The server drops a batch on `4xx` (and logs it),
  and retries on `5xx` or network errors with backoff, keeping order.

v0 event types: `server_started`, `player_authenticated`, `match_started`, `round_ended`, `death`,
`match_ended`, `report` (a player's `/report`, section 5), `players_moved` (a `move_players` command
ran, "Moving players" below), `client_connected` ("Clients" below), `player_left` ("Matches" below). Later versions add more; platforms must accept and store unknown types, and must not
require members a server may not send yet (`match`, `seconds`, `bot` and `place` in `round_ended` came
with the match events; a server before them sends rounds only).

**Clients.** `client_connected`: `{"connection": 3, "client": "browser", "protocol": 17, "ip": "203.0.113.7"}`, once per connection, when a
client logs in (before its players join, logged in or not). `client` is `"browser"` for a client on the WebRTC transport
(the page) and `"desktop"` for any other; `protocol` is the newest network protocol the client speaks (16: 0.2.8.3,
17: 0.2.9; the page speaks 17). `connection` is the connection's number (not a player's `netId`), reused after it closes. `ip`: the client's address without the port (left out when unknown); a platform
should not keep it as is (Retrocycles stores a rough location in its place and drops the address). The platform counts
connections by client with it.

**Players in events.** A player is named the same way everywhere: `name` (without colour codes), `gid`
(the authenticated global id, `null` if the player is not authenticated; never set for a bot), `bot`
(an AI player), `team` (the team's name without colour codes, `null` without a team; in a team of one
the team carries the player's name).

**Matches.** The game plays rounds in matches: a match ends when a team reaches the server's limits
(rounds, minutes or score: `LIMIT_ROUNDS`, `LIMIT_TIME`, `LIMIT_SCORE`) and the next starts on its own.
`match` numbers the matches of one `bootId` from 1. The events of a match come in this order:
`match_started`, then per round the round's `death`s and its `round_ended`, and after the last round
exactly one `match_ended`. A server that stops in the middle of a match sends no `match_ended` for it;
its next `server_started` (a new `bootId`) says it went away. Rounds a restart cut short are never
reported.

- `match_started`: `{"match": 3, "limits": {"rounds": 10, "minutes": 10, "score": 100000}}`, the
  server's limits for the match.
- `round_ended`: `{"match", "round", "seconds", "players"}`. `round` counts from 1 within the match;
  `seconds` is the length of the round in game time; `players` are the players in a team at the end
  of the round (spectators did not play it), each with `score` (the player's score in the match so
  far), `alive`, and `place` in the round: 1 for everyone still alive at the end (or, if nobody is,
  the last to die), then by the time of death, later is better; deaths at the same moment share a place.
  A logged-in player also carries `seconds`: the game time they played the round, since they joined it
  (less than the round's own `seconds` for a player who came in late, or came back after a
  `player_left`); left out when the server did not see them join.
- `player_left`: `{"match": 3, "round": 1, "player": <player>, "score": 12, "seconds": 340}`. A logged-in
  human left the running round before its end (disconnected, or out of every team: a spectator now),
  sent within about a game second. `score`: their score in the match when last seen; `seconds`: the
  game time they played the round. In a mode where a round lasts the whole match (respawns), this is the
  only report of a player who leaves before the match ends. A player who comes back in the same round
  is listed again in its `round_ended`, with only the time since.
- `death`: `{"match": 3, "round": 4, "victim": <player>, "killer": <player> | null, "cause": "frag"}`.
  `cause`, by the game's own scoring (the ladderlog's `DEATH_FRAG` / `DEATH_TEAMKILL` /
  `DEATH_SUICIDE`): `frag` (an enemy's wall: the enemy whose wall the victim hit, or which boxed the
  victim in within the last `SUICIDE_TIMEOUT` seconds, is the `killer`), `teamkill` (the same by a
  teammate), `suicide` (the victim's own wall, the rim, or no enemy wall near enough in time; `killer`
  `null`), `other` (killed by something that is no wall: a zone, an admin, a server rule; `killer`
  `null`). The physical wall (rim or trail) is not reported: a rim death with an enemy close behind is
  that enemy's `frag`, as the game scores it.
- `match_ended`: `{"match": 3, "result": "completed", "rounds": 10, "winner": "Tester", "players": [...]}`.
  `result` `completed` (the game declared a winner by its limits; `winner` names the winning team) or
  `aborted` (another match was started before this one was decided: `START_NEW_MATCH`, a vote, the
  game restarted after everybody left; `winner` `null`). `rounds`: the rounds reported for the match.
  `players`: everyone who played at least one of its rounds or is in a team at the end, each with
  `score`, `place` (the team's place by score at the end, 1 best, equal scores share; `null` if the
  player left), `rounds` (how many of the match's rounds listed the player) and `leftEarly` (not in a
  team at the end):
  `{"name": "Tester", "gid": "g-7f3a9c01@retrocycles", "bot": false, "team": "Tester", "score": 42,
  "place": 1, "rounds": 10, "leftEarly": false}`.

### `POST /v1/servers/{serverId}/heartbeat`

The server's current state (a **room**, for Quick Play and the fleet): every 10 s, and
promptly, at most once per second, when its player counts change or it applied a command.

```json
{
  "bootId": "<the bootId of the event stream>",
  "time": 1790000000,
  "state": "open",
  "humans": 3,
  "bots": 2,
  "capacity": 16,
  "clients": 4,
  "mode": "qp-ffa",
  "serverMode": "Free-for-all",
  "version": "0.2.9.3",
  "platformContract": "v0",
  "uptime": 3600,
  "lastCommandId": 17,
  "hostId": "h-eu-01",
  "region": "eu",
  "protocolMin": 0,
  "protocolMax": 17,
  "platformLogin": true,
  "moderation": { "chatFilter": true, "sanctions": true, "reports": true },
  "players": [
    { "netId": 12, "gid": "g-7f3a9c01@retrocycles", "ping": 42, "spectator": false, "client": "browser" },
    { "netId": 15, "gid": null, "ping": 118, "spectator": true, "client": "desktop" }
  ]
}
```

- `state`: `open` (takes new players), `draining` (a `drain` command is in force), `closing`
  (a `closing` command is in force). See "Commands" below. A server always starts `open`.
- `humans`: human players, spectators included; `bots`: AI players; `capacity`: the
  server's connection limit (`MAX_CLIENTS`). A desktop client with several local players
  uses one connection for all of them. `clients`: the connections holding one of those
  slots (`sn_NumUsers()`): players, spectators, and clients still logging in (no player
  yet). Bots hold none. A platform measures how full a room is by `clients` against
  `capacity`.
- `mode`: the `PLATFORM_MODE` setting (e.g. the id of the room template), `null` when unset.
  `serverMode`: the `SERVER_MODE` setting, the kind of game in the words players use
  (`Fortress`, `Team Sumo`, `Sumobar`, `Free-for-all`; one line, at most 32 characters), for
  server browsers; `null` when unset. The same text heads the options the server gives the
  master servers (`Mode: Fortress` as the options' first line), so a browser reading the master
  list sees it for servers without a platform too.
  `version`: the game version. `platformContract`: `"v0"`. `uptime`: seconds since the
  platform link started in this process (about the server's start).
- `lastCommandId`: the id of the last command this server applied, `0` if none since it
  started. It is the **acknowledgement** of the command channel.
- `hostId`, `region`: the `PLATFORM_HOST_ID` and `PLATFORM_REGION` settings, left out when
  empty (a server started by a fleet host names it; a static server may leave both out).
- `protocolMin`, `protocolMax`: the network protocol versions a new client may speak now
  (`sn_CurrentVersion()`, narrowed by `MIN_PROTOCOL_VERSION`, `MAX_PROTOCOL_VERSION`,
  `BACKWARD_COMPATIBILITY` and the clients already connected). A client whose range does not
  overlap it is refused at login ("incompatible"). The program `version` string says nothing
  about this; compatibility is decided on these.
- `platformLogin`: the `PLATFORM_LOGIN` setting (the server accepts login tokens).
- `moderation`: the moderation of section 5 in force on this server, each a boolean:
  `chatFilter` (`PLATFORM_CHAT_FILTER`: chat lines are checked with the platform before
  delivery; it follows the setting while the server runs, but a filter off at start stays
  off), `sanctions` (`PLATFORM_SANCTIONS`: sanctions are fetched and enforced) and `reports`
  (`PLATFORM_REPORTS`: `/report` goes to the platform); the last two are fixed when the
  platform link starts. A platform may require all three for a community server it lists as
  moderated (the trusted tier). Added later in v0: a heartbeat without it comes from an
  older server, whose moderation is unknown.
- `players`: the **roster**, every connected human (bots are never listed; at most 256 entries), in
  the server's player order. `netId`: the player's network object id (what every client's API reports
  as `players[].id`; stable while the player is connected, reused after it leaves). `gid`: the
  authenticated global id, `null` for a player who is not authenticated. `ping`: the server's current
  estimate of the round trip to the player's connection in milliseconds (an integer; players of one
  desktop client share it). `spectator`: the player is watching (spectator mode, or in no team).
  `client`: `"browser"` when the player's connection came over the WebRTC transport (the page),
  `"desktop"` otherwise (added later in v0; a platform must not require it).
  The roster carries no names: a platform that needs one takes it from the events. A ping change
  alone does not make a heartbeat prompt; the roster is as of the heartbeat's `time`.
- `clients`, `protocolMin`, `protocolMax` and `platformLogin` were added to v0 for Quick Play;
  a platform treats a heartbeat without them as coming from an older server (`clients`
  unknown: count `humans`; protocol unknown: not known to be compatible). `players` came later
  for regions and moves; without it the platform knows no roster for the room.
- Response `204`. `401` as for events; `400` malformed (a member of the wrong type, a
  negative count, an unknown `state`). Unknown members are ignored.
- A heartbeat is a snapshot, not an event: the platform keeps only the latest one per
  server and does not store them as history. The server never retries an old heartbeat:
  on a failure it sends a fresh one 2 s later, then keeps its 10 s rhythm. Heartbeats do
  not wait behind undelivered events.
- The platform decides when a server counts as gone; Retrocycles marks it **stale** when
  its last heartbeat is more than 15 s old.

### `GET /v1/servers/{serverId}/commands?after=<id>&wait=<seconds>`

Commands from the platform to the server, as a long poll. The server keeps one request
open at a time, on a thread of its own, and applies what it gets on the game thread.

- It answers at once when the server has commands with an id above `after`; otherwise it
  holds the request for up to `wait` seconds (default and maximum 25; `0` answers at once)
  and answers as soon as a command arrives, or with an empty list.
- Without `after` (the server just started and has no cursor), it answers with the commands
  the server has not acknowledged yet (ids above the last `lastCommandId` the platform saw
  from it), leaving out expired ones.

`200` →
```json
{
  "commands": [
    { "id": 18, "type": "drain", "time": 1790000100,
      "data": { "message": "This server is closing soon. Please join another one." } }
  ],
  "cursor": 18
}
```

- Ids are positive integers that grow in the order the platform created the commands (not
  necessarily by one). Commands are in id order. `cursor` is the `after` of the next
  request.
- The server applies each command once, in order, and ignores ids at or below the last one
  it applied. It reports the last applied id as `lastCommandId` in its next heartbeat, which
  it sends promptly. **Unknown types are ignored and acknowledged** like any other, so a
  platform can add types (plan: sanctions, room configuration) without breaking servers.
- `401` as above; on `5xx` or a network error the server polls again after a backoff (1 s,
  doubling up to 60 s) and keeps its current state meanwhile.
- A platform may hand a command out more than once (a retried poll, a restart); applying
  one is idempotent for the server. A restarted server is `open` again: a platform that
  still wants it drained sends `drain` again.

v0 command types (`data` members are optional; texts are at most 200 characters, longer
ones are cut):

| `type` | `data` | The server |
|---|---|---|
| `drain` | `message`: why new players are turned away | stops announcing itself to the master server (`TALK_TO_MASTER` off at runtime; the previous value comes back with `undrain`), refuses new connections with `message` (the player sees it as the reason the connection ended), and keeps the players it has. State `draining`. |
| `undrain` | | undoes `drain` or `closing`: announcing as before, new connections welcome, a running countdown stops. State `open`. |
| `closing` | `message`, `seconds` (0 to 600, default 30) | refuses new connections like `drain`, shows `message` to everyone (console and centre of the screen), then counts down in centre messages (at 60, 30, 20, 10, 5, 4, 3, 2 and 1 s left). It does not stop itself: the host stops the process after the countdown. State `closing`. |
| `message` | `text`, `center` (`false`) | shows `text` to everyone in the console, and also in the centre of the screen with `center: true`. |
| `sanction` | a **sanction** (section 5) | keeps it until it ends (mutes and bans) and enforces it at once on the players it names: a ban or a kick ends their connection with the reason, a mute stops their chat. |
| `unsanction` | `id`: the sanction's id | forgets that sanction (a lifted mute: the player can chat again). |
| `moderation_config` | | fetches `GET /v1/servers/{serverId}/moderation` again (the word lists or the sanctions changed). |
| `rename` | `gid`, `name`, `reason` | renames the player logged in as `gid` to `name` (the server's `RENAME`: the client cannot change it back until it reconnects) and tells them `reason`. |
| `move_players` | `moveId`, `target`, `gids`, `when`, `reason`, `expiresAt` | sends players to another room ("Moving players" below) and reports it with a `players_moved` event. |
| `player_message` | `gid`, `kind` (`message`/`notice`), `text` | prints `text` (UTF-8, at most 400 characters) on the console of the desktop players logged in as `gid`, a `message` in the whisper's pink; never to a browser client ("Friends in the rooms" below). Only with `PLATFORM_SOCIAL 1`. |

The moderation commands (`sanction`, `unsanction`, `moderation_config`, `rename`) are only
applied by a server whose sanctions are on (`PLATFORM_SANCTIONS 1`); otherwise they are
acknowledged and ignored like unknown types.

How commands are created (an admin tool, a fleet controller) is up to the platform and not
part of this contract.

### `PUT /v1/servers/{serverId}/recordings/{segmentId}?complete=&startedAt=&endedAt=`

A gameplay recording segment of the room. It is sent by a
host-side recorder with the room's key, not by the game server itself. Body `application/gzip`: the room's own
ladderlog lines, filtered to positions, spawns, deaths, rosters, scores and match boundaries, normally one match from
`NEW_MATCH` to `MATCH_ENDED`.

- `segmentId` is `[A-Za-z0-9][A-Za-z0-9._-]{0,79}`, chosen by the sender; `(serverId, segmentId)` is the idempotency
  key.
- `complete` (`true`/`false`): the segment is a whole match.
- `startedAt` / `endedAt`: Unix seconds by the sender's clock (optional).
- Answers:
  - `201 {"stored": true, "id": <n>}`; a repeat gets `200 {"stored": false, "id": <n>}`;
  - `400` for a bad id or a body that is not gzip;
  - `413` above the platform's size limit;
  - `404` when the platform takes no recordings (the sender drops what it holds and asks again later);
  - `401` as for every server call.
- A platform may ignore the endpoint entirely (`404`). Recordings never carry chat or player addresses.

### Moving players

A platform moves a group of players from one room to another (merging small rooms, emptying a
host, a closer server: the players who accepted an offer, section 1 "Offers", by `gids`, `when: now`,
reason `closer`) with the `move_players` command. The room tells each browser client where
to go, redirects desktop clients the classic way, and reports what it did; the page then asks the
platform for its seat in the target (`POST /v1/quickplay` with `moveId`, section 1) and connects.

`move_players` `data`:

```json
{ "moveId": "mv_Jx3...", "when": "match_end", "reason": "merge", "expiresAt": 1790000900,
  "target": { "serverId": "eu-h01-r2", "name": "Retrocycles EU 2",
              "signalUrl": "wss://eu.play.example.org/signal/eu-h01-r2", "udp": "203.0.113.7:4534" },
  "gids": null }
```

- `moveId`: the platform's id of the move (at most 64 printable ASCII characters); the room passes
  it on unchanged.
- `target`: the room to go to. `serverId` (for the login token), `name` (shown to the players; may
  carry colour codes), `signalUrl` (as in a Quick Play answer), `udp` (optional, `host:port`: where
  a desktop client can reach the target; without it desktop players stay).
- `gids`: the players to move, by the global id they authenticated with (the `gid` of the
  `player_authenticated` event, compared without regard to ASCII case), or `null` for **everyone**
  in the room: players and spectators, authenticated or not. Bots never move.
- `when`: `now` (at once, also in the middle of a round) or `match_end` (default): in the pause
  after the current match's `match_ended` and before the next match starts. A room with no match
  running (nobody playing in a team) runs a `match_end` move at once.
- `reason`: `merge`, `drain`, `closer`, `join` (a player's `/join` to a friend's room, "Friends in
  the rooms" below) or `admin`; shown to the players as the reason and passed to the browser
  client. A server takes an unknown reason as `admin`.
- `expiresAt` (optional, Unix seconds): a move still waiting for its match end then is dropped and
  reported with `outcome` `expired`.

The room keeps waiting moves in order (several may wait; a player that already left with one is
not moved again). When a move runs, for each connection with a human player it moves (a desktop
client with several local players moves with all of them):

- a **browser client that announced the `move` capability** (network message 381 below) gets the
  move as network message 382; the page leaves by itself. A client still connected 10 s later is
  disconnected with the reason "moved to <name>";
- a **desktop client** (any other connection) is disconnected with the classic redirection to
  `target.udp` (`login_deny` with a redirect, as `MOVE_TO`); the client connects there on its own.
  Without `udp` the player stays (`no_udp`);
- a browser client without the capability (an older page) stays (`no_support`).

The room then sends `players_moved`:

```json
{ "seq": 41, "type": "players_moved", "time": 1790000400,
  "data": { "moveId": "mv_Jx3...", "outcome": "done", "target": "eu-h01-r2",
            "sent": ["g-7f3a9c01@retrocycles", null], "redirected": ["g-0c1d2e3f@retrocycles"],
            "skipped": [ { "gid": "g-99aa00bb@retrocycles", "why": "not_here" },
                         { "gid": null, "why": "no_udp" } ] } }
```

- `sent`: the players whose browser client got the move; `redirected`: the desktop players sent to
  `udp`; `skipped`: who stays, with `why`: `not_here` (a named gid is not in the room), `no_udp`,
  `no_support`, `expired`. A player who is not authenticated is a `null`.
- `outcome`: `done`, or `expired` (the move never ran; every named gid is `skipped` with `expired`,
  a move for everyone lists nobody).
- `target`: the target's `serverId`.

**Network messages** (descriptor ids of this fork):
- **381** `platform_client_caps`, client to server, one string: the capabilities of the client,
  words separated by blanks. v0 knows `move`. The browser client sends it once per connection,
  right after it is logged in. A server keeps it for the connection's lifetime.
- **382** `platform_move`, server to client, five strings: `moveId`, `serverId`, `name`,
  `signalUrl`, `reason`. Sent only to connections that announced `move`.

Clients and servers that do not know a descriptor ignore the message: every version since 0.2.8
looks it up in its descriptor table and drops unknown ids (`nDescriptor::HandleMessage`; a debug
build logs it once). The room nevertheless sends 382 only to clients that asked for it, so a
desktop client or an older browser client never sees it.

### Linking old global ids

`/link <code>` (with `PLATFORM_LINK 1`) on a server: the line is never delivered, held for
moderation or kept for reports. The server answers on the player's console by itself when the
player is not logged in, is logged in with a local account (`user@`) or with an id of
`PLATFORM_AUTHORITY`, still has a `/link` waiting, or sent 3 in the last minute. Otherwise it asks,
off the game thread (5 s timeout):

`POST /v1/servers/{serverId}/links`

```json
{ "bootId": "<the boot>", "gid": "Vet@forums", "name": "Vet", "code": "k7q-4m2" }
```

- `gid`: the player's authenticated global id (as in events); `name`: without colour codes;
  `code`: as typed (the platform normalizes it).
- `200` → `{"result": "linked", "message": "Linked Vet@forums to nova. ...", "gid": "Vet@forums",
  "handle": "nova", "name": "Nova"}`. `result`:

  | `result` | |
  |---|---|
  | `linked` | linked now; `handle` and `name` are the account's |
  | `already_linked` | this gid is linked to the code's account already (the code is not used up) |
  | `invalid_code` | no such code (or not a code at all) |
  | `expired` | the code's time is up, or a newer code replaced it |
  | `used` | the code linked something already |
  | `linked_elsewhere` | the gid is linked to another account (the code is not used up) |
  | `own_authority` | a gid of the platform's own authority |
  | `not_global_id` | no authority (`user@`) |
  | `too_many` | the account has as many linked ids as the platform allows (Retrocycles: 10) |
  | `rate_limited` | too many tries for this gid (Retrocycles: 10 per hour, any outcome) |
  | `not_official` | the platform links only in the rooms it counts as official |

  `message` is the platform's sentence for the player. A server words the results it knows itself
  and shows `message` for the others, so a platform can add results.
- `400` malformed (`gid` missing or over 128 characters, `code` missing or over 64), `401` bad key,
  `404` `linking_off`: the platform links nothing (the server says so to the player).
- From the link on, every event that names the gid counts for the account on the platform
  (progression, moderation: an account sanction lists the linked gids among its `gids` from the
  next time it is sent). Nothing the gid earned before the link moves: the platform keeps no progress
  for gids it does not know.

### Friends in the rooms

Desktop players in a room (with `PLATFORM_SOCIAL 1`) reach "Friends" and "Messages" (section 1)
from the chat line. These lines are never delivered, held for moderation or
kept for reports:

| Line | |
|---|---|
| `/f` (or `/friend`) | friends online and waiting requests |
| `/f name` | asks a player of this room (the name matched like `/msg` does) or an account by handle to be friends; accepts their request if they asked first |
| `/msg name text` | only when `name` matches **nobody in the room** (then it stays the room's own whisper, untouched): a direct message to the friend of that name |
| `/r text` | answers the last direct message |
| `/join name` | moves the player to a friend's room (a `move_players` with reason `join`, `when: now`, for that player alone), as the page's Join does |

The server answers by itself when the player is not logged in (only `/f`, `/r` and `/join`; a
`/msg` of a guest stays the room's), when a command of theirs is still on its way, when they sent 12
in the last minute, or when the text is over 240 characters (Unicode code points of its UTF-8, not
bytes). Otherwise it asks, off the game
thread (5 s timeout):

`POST /v1/servers/{serverId}/social`

```json
{ "bootId": "<the boot>", "gid": "Kim@retrocycles", "command": "friend", "target": "Lee", "netId": 477 }
```

- `command`: `friends`, `friend`, `message`, `reply` or `join`; `target`: the name as typed
  (`friend`, `message`, `join`); `netId`: for `friend`, the room's player the name matched (absent:
  nobody here, the platform looks for a handle); `text`: `message`, `reply`.
- `200` → `{"result": "requested", "lines": ["Friend request sent to Lee."]}`: `lines` (up to 8, UTF-8)
  are the platform's words for the player; the server prints them on the player's console, in the
  whisper's pink for `result` `sent`. `result`: `friends`, `requested`, `sent`, `moving`, or why not
  (`not_an_account`, `player_not_found`, `no_message`, `not_joinable`, `same_room`, `target_full`, the
  error codes of "Friends" and "Messages"). A server words nothing itself, so a platform can add results.
- `400` malformed (`gid` missing, `target` or `text` too long), `401` bad key, `404` `friends_off`
  (the server says the room has no friends or messages).
- A direct message to a player in an official room on a desktop client also reaches them there: a
  `player_message` command (`kind` `message`, `[from Kim @ Quick Play EU 1] gg`; the room is named
  when the sender plays in another one). When a room verifies an account with unread messages
  (`player_authenticated`), a `notice` says so once: `You have 2 unread messages; /r answers the
  last (Kim).` A browser client gets neither: its page has `GET /v1/me/stream`.

## 4. Game server settings

| Setting | Default | Meaning |
|---|---|---|
| `PLATFORM_API_URL` | `""` | empty: no platform, stock behaviour |
| `PLATFORM_SERVER_ID` | `""` | this server's id at the platform |
| `PLATFORM_SERVER_KEY` | `""` | bearer secret; never logged or recorded |
| `PLATFORM_LOGIN` | `0` | accept platform login tokens |
| `PLATFORM_PRIVATE` | `0` | private room: end every connection that brought no valid room ticket (section 2, "Room tickets"). With `PLATFORM_API_URL` or `PLATFORM_SERVER_ID` empty nothing can check a ticket, so every connection is ended (the server says so at start) |
| `PLATFORM_PRIVATE_REMATCH_SECONDS` | `30` | private room: seconds after a won match for every player to say `/rematch` before everyone is disconnected; `0`: matches roll on |
| `PLATFORM_AUTHORITY` | `retrocycles` | authority part required in token `gid`s |
| `PLATFORM_MODE` | `""` | mode id reported in heartbeats (`mode`) |
| `PLATFORM_HOST_ID` | `""` | host id reported in heartbeats (`hostId`) |
| `PLATFORM_REGION` | `""` | region reported in heartbeats (`region`) |
| `PLATFORM_CHAT_FILTER` | `1` | check chat lines with the platform before delivering them (section 5); `0`: stock chat (the local word filter is not applied either) |
| `PLATFORM_CHAT_TIMEOUT` | `150` | milliseconds a line waits for the platform's verdict before the local word filter decides (50 to 2000) |
| `PLATFORM_SANCTIONS` | `1` | fetch and enforce sanctions, apply the moderation commands; `0`: none of it |
| `PLATFORM_REPORTS` | `1` | handle the `/report` chat command (section 5) |
| `PLATFORM_LINK` | `1` | handle the `/link` chat command (section 3, "Linking old global ids") |
| `PLATFORM_SOCIAL` | `1` | desktop players' `/f`, `/msg` to a friend elsewhere, `/r`, `/join` and the `player_message` command (section 3, "Friends in the rooms"); they send the player's global id and the message text to the platform. Independent of the moderation settings |
| `PLATFORM_IP_BAN_GRACE` | `15` | seconds a connection from an address under an address ban has to log in before it is ended |
| `PLATFORM_ACCESS_ADMIN` / `PLATFORM_ACCESS_MODERATOR` / `PLATFORM_ACCESS_STAFF` | `1` / `2` / `19` | access level a login token's role gives (section 2, "Roles") |

## 5. Moderation

One path for every client: the **game server** checks chat, enforces sanctions and takes in-game
reports, so desktop and browser players are treated alike. Community servers keep full control:
they build without `--enable-platform`, set `PLATFORM_CHAT_FILTER 0` / `PLATFORM_SANCTIONS 0`, or
point `PLATFORM_API_URL` at their own platform. The page side is reports only
(`features.moderation`).

Switching moderation off does not switch the friends commands off: with `PLATFORM_SOCIAL 1` (the
default) a desktop player's `/f`, `/msg` to a friend elsewhere, `/r` and `/join` still go to the
platform with the player's global id and, for messages, the text (section 3, "Friends in the
rooms"). A server that should send no chat text to the platform also sets `PLATFORM_SOCIAL 0` (and
`PLATFORM_REPORTS 0`: a report carries recent chat).

**Never by address alone.** Schools and families share one IP address. Sanctions name an
**account** (a user id, guests included, and its gids) or a **connection** (one player on one server
run). An **address** is only ever the target of a short ban by a person, for players who are not
logged in (desktop players play without logging in), and it never touches a logged-in player from
that address.

### Sanctions

```json
{ "id": "s_01J9ZQ5X8Y7W6V5T4S3R2Q1P0N", "kind": "mute", "scope": "all", "serverId": null,
  "target": { "type": "account", "userId": "u_01J...", "gids": ["nova@retrocycles", "g-7f3a9c01@retrocycles"] },
  "createdAt": 1790000000, "until": 1790000600, "reason": "Abusive chat" }
```

- `kind`: `mute` (the player's chat lines are not delivered; the player is told until when),
  `kick` (the player's connection ends now; nothing is kept), `ban` (the connection ends now and
  logins are refused until `until`).
- `scope`: `all` (every official server of the platform: the platform pushes it to the servers it
  counts as official, and lists it in `GET .../moderation` for any server) or `room` (only
  `serverId`). A server ignores sanctions of scope `room` for another server.
- `target`, one of:
  - `{"type": "account", "userId", "gids"}`: every player logged in as one of `gids` (the account's
    current gid and its former ones), or with a login token whose `sub` is `userId`.
  - `{"type": "connection", "serverId", "bootId", "conn", "name"}`: the player on network
    connection `conn` of that server run (from a chat line or a report: section "Chat lines"). It ends
    when that connection does. For players who are not logged in. `mute` and `kick` only.
  - `{"type": "address", "ip"}`: players **not logged in** from this IP address. `ban` only, and only
    short (Retrocycles: at most 24 hours; longer only after review). A new connection from the
    address gets `PLATFORM_IP_BAN_GRACE` seconds to log in (a browser's token login, armathentication);
    if it has not, it ends with the reason (the ban arriving again, or in a refreshed list, does not
    cut that time short). Logged-in players from the address are never affected.
- `until`: Unix seconds when it ends, `null` for never (`ban` only). A server drops sanctions whose
  time is over (by its own clock) and never enforces them. A `kick` has no duration (its `until` is
  its `createdAt`): a server applies it when it arrives and keeps nothing.
- `reason`: shown to the player (at most 200 characters).

The texts the player sees are the server's: a refused or ended connection says
`You are banned from this server until <time UTC>: <reason>` (or `... permanently: ...`), a
dropped chat line of a muted player gets `You are muted until <time UTC>: <reason>` (the other
players see nothing).

### `GET /v1/servers/{serverId}/moderation`

What a server needs to moderate: the word lists for its local filter and the sanctions in force for
it. Bearer server key as in section 3. A server fetches it when it starts, every 5 minutes, and at
once on a `moderation_config` command.

```json
{ "commandId": 1234,
  "chatFilter": { "version": "w-3f2a9c", "mask": ["..."], "block": ["..."], "allow": ["kill", "core dump"],
                  "notice": "That message was not sent: it breaks the chat rules." },
  "sanctions": [ { "id": "s_...", "kind": "ban", ... } ] }
```

- `commandId`: the highest command id that existed for this server when the answer was made
  (`0` if none). Commands with a higher id were created later: a server that already applied
  `sanction` / `unsanction` commands above `commandId` applies them again on top of this list (in
  id order), so a list that crossed a command on the way never undoes it.
- `sanctions`: every `mute` and `ban` in force for this server (scope `all`, or `room` with this
  `serverId`). The list **replaces** what the server knew.
- `chatFilter`: see "The local word filter". `version` changes when the lists do. `notice`
  (optional, at most 200 characters): what the sender of a line the local filter drops is told, the
  same text as the platform's `drop` verdicts (a server without it uses its own default).
- `200`; `401` as in section 3; `404` when the platform does no moderation (the server then stops
  holding chat for 10 minutes and asks again).

### Chat lines: `POST /v1/servers/{serverId}/moderation/chat`

With `PLATFORM_CHAT_FILTER 1`, every chat line a player sends is held before delivery: public chat,
`/me`, `/team`, `/shout`, `/msg` (and the server's other talking commands, e.g. `/enemy`, `/chat`
on sty+ct servers). Other chat commands (`/vote`, `/players`, admin commands) are not checked but wait
behind a held line of the same player, so a player's lines keep their order. The server batches the
held lines of a moment into one request:

```json
{ "bootId": "<the event stream's bootId>",
  "lines": [
    { "id": 41, "time": 1790000000, "channel": "public", "text": "hello all",
      "player": { "name": "Tester", "gid": "g-7f3a9c01@retrocycles", "userId": "u_01J...", "conn": 3 } },
    { "id": 42, "time": 1790000001, "channel": "private", "text": "hi", "to": "Bob",
      "player": { "name": "desk", "gid": null, "userId": null, "conn": 5, "ip": "203.0.113.7" } }
  ] }
```

- `id`: grows per line within a `bootId`. `channel`: `public`, `team`, `private` (`to`: the
  recipient's name), `me`, or another the server names (a platform treats unknown ones as `public`).
  `text`: what the player wrote, without the command and recipient (`/msg Bob hi` → `hi`); it may
  contain the game's colour codes `0xRRGGBB`.
- `player`: `name` without colour codes; `gid` the authenticated global id or `null`; `userId` the
  login token's `sub` (token logins only) or `null`; `conn` the network connection (1-based); `ip` only
  for players who are not logged in (the only thing a sanction against them can name).
- At most 100 lines per request.

`200` →
```json
{ "verdicts": [
    { "id": 41, "action": "allow" },
    { "id": 42, "action": "replace", "text": "you **** idiot" },
    { "id": 43, "action": "drop", "notice": "That message was not sent: it breaks the chat rules." } ],
  "sanctions": [ { "id": "s_...", "kind": "mute", ... } ] }
```

- `allow`: delivered as written. `replace`: delivered with `text` instead (same command and
  recipient). `drop`: not delivered; the sender is told `notice` if there is one (at most 200
  characters). A line without a verdict is decided by the local word filter, as on a timeout.
- `sanctions`: sanctions the platform just made because of these lines (a strike that mutes the
  sender). The server applies them at once, exactly like a `sanction` command (which also comes,
  to every server in scope).
- **Timeout.** A line waits at most `PLATFORM_CHAT_TIMEOUT` ms (default 150) for its verdict. Then the
  local word filter decides: a line with a `block` match is **dropped** (the sender is told the
  `notice`, as for a `drop` verdict), any other line is **delivered** (masked where a `mask` term
  matches): a slow or absent platform never silences a server, and never lets a `block` line through.
  A verdict that arrives later changes nothing players saw; its `sanctions` are still applied, and the
  platform counts the line's violation whether or not its verdict was in time (a late `drop` of a line
  the local filter delivered is logged by the server and kept with the line for reports, `verdict`
  below). After 3 failed requests in a row (no answer, `5xx`, an unusable `200`, or a `4xx` other
than `404`: a platform that refuses the requests gives no verdicts either) the server stops
  holding: lines are decided by the local filter at once and still sent, without waiting; the first
  successful answer turns holding back on. `404`: no moderation on this platform; the server stops
  holding and sending for 10 minutes.
- A muted player's lines are answered by the server from the sanctions it holds, without a request.
- The platform decides what is allowed (Retrocycles: the word lists below plus a classifier slot,
  and an allowlist for game talk such as "kill", "frag", "cut him off", "core dump"), keeps the lines
  as evidence for reports for a short time (Retrocycles: 14 days), and runs the strike ladder.

### The local word filter

`chatFilter` from `GET .../moderation` (a server without it uses a small built-in list):

- `mask`: terms whose matches are replaced by `*` (one per character) in the delivered line.
  `block`: terms a line must not be delivered with at all: when the server decides a line itself, a
  line with a `block` match (not covered by `allow`) is dropped and its sender told the `notice`,
  never delivered masked; the platform drops such lines too and counts a strike. `allow`: terms that
  never match, also inside a `mask`/`block` match (e.g. `assassin` keeps `*ass*` from masking it).
- A **term** is one or more words of letters `a`-`z`. A word may end with `*` (any ending: `idiot*`
  matches `idiots`) or be `*word*` (anywhere inside a word; use sparingly).
- **Normalization** before matching: colour codes `0xRRGGBB` are left out; letters are lowercased and
  Latin-1 accents removed (`é` → `e`); every character that is neither a letter nor one of the
  digits and signs `0 1 3 4 5 7 8 @ $ ! | +` separates words. Those stand for `o i e a s t b a s i i t`:
  between two letters of a word always (`d4rn`), and at the start or end of a word as a second reading
  (`a$$` and `$hit` match as `ass` and `shit`, `darn!` still matches `darn` and masks only `darn`); a
  run of them without any letter is no word (`1337`). Three or more one-letter words in a row
  (`f u c k`, `f.u.c.k!`: a sign at a word's start or end does not count as a letter here) count as
  one word. A run of three or more equal letters matches as one or as
  two of them (`fuuuck` matches `fuck`, `asss` matches `ass`).
- **Matching:** a term matches consecutive words, each word of the term matching one word of the
  line. A match is left alone when every word in it is part of an `allow` match (`assassin`,
  `core dump`), so `allow: ["kill"]` does not protect `kill yourself`. Masking replaces the letters
  (and the signs read as letters) of the matched words, nothing else.
- Test vectors: `test-vectors/chat-filter-v0.json` (lists, lines, the masked text and whether a
  `block` term matched, i.e. whether a server drops the line).

### Reports

**From the page:** `POST /v1/reports` (session, **CSRF**; only with `features.moderation`)

```json
{ "target": "nova@retrocycles", "reason": "abusive_chat", "note": "kept insulting my team",
  "serverId": "eu-h01-r3" }
```

- `target`: a gid (also a former one) or a user id. `reason`: `abusive_chat`, `hate`,
  `offensive_name`, `cheating`, `griefing`, `impersonation`, `other`. `note`: optional, at most 500
  characters. `serverId`: optional, the server the reporter saw it on; the platform attaches the
  recent chat lines of that server it received (section "Chat lines") as evidence.
- `201` → `{"id": "r_01J...", "status": "open"}`. The same reporter reporting the same target again
  within 10 minutes gets `200` with the open report (one report, not a pile).
- `400` validation (`errors`: `target_required`, `reason_invalid`, `note_too_long`), `404`
  `target_not_found`, `409` `cannot_report_self`, `429` rate limited (per account, and per address
  sized for a classroom).

**In the game:** `/report <name> <reason>` (with `PLATFORM_REPORTS 1`; desktop players too, logged in or
not). The server finds the player by name like `/msg` does, thanks the reporter (or says who it could
not find), and sends a `report` event (section 3):

```json
{ "seq": 12, "type": "report", "time": 1790000300, "data": {
    "reporter": { "name": "Tester", "gid": "g-7f3a9c01@retrocycles", "userId": "u_01J...", "conn": 3 },
    "target": { "name": "desk", "gid": null, "userId": null, "conn": 5, "ip": "203.0.113.7" },
    "reason": "spamming insults",
    "chat": [ { "id": 40, "time": 1790000290, "channel": "public", "name": "desk", "gid": null, "conn": 5,
                "text": "...", "delivered": "masked", "verdict": "drop" } ] } }
```

- Players as in "Chat lines" (`ip` only for players not logged in). `reason`: the reporter's words
  (at most 200 characters). `chat`: the server's recent chat window (at most the last 50 lines of the
  last 10 minutes) as the players saw it plus what was held back: `text` is what the sender wrote,
  `delivered` is `as_written`, `masked` (by the local filter or a `replace`), `dropped` or
  `muted`; `to` for private lines; `verdict` only when the platform's verdict came after the server
  had decided the line itself and disagreed (`drop` of a line delivered as written or masked).
  Private and team lines are only included when the reporter or the target wrote them or, for
  private lines, received them.
- A reporter may send at most 3 reports a minute; more are refused with a message.

### Names

The platform checks display names and handles against the same lists (plus reserved and creator
names) when an account is created or renamed (`handle_not_allowed`, `name_not_allowed`,
`handle_reserved`) and when a server reports `player_authenticated`; an offensive name in a game is
answered with a `rename` command to that server.

## 6. The armathentication authority

The platform answers the classic authentication of Armagetron Advanced (`nKrawall`, `src/network`),
so any server and client, desktop and older ones included, can log in `<handle>@<authority>`. A server
fetches `http://<authority>/armaauth/0.1/?query=...` (the authority's host and an optional `:port`;
a short name `<name>` stands for `<name>.authentication.armagetronad.net`, which points at the
platform, and the server then names the player `<handle>@<name>`). Only with `features.gamePasswords`
(otherwise `404` to everything). Answers are `text/plain`:

| Request | Answer |
|---|---|
| `?query=methods` | `200` `METHODS md5` |
| `?query=params&method=md5` | `200`, empty: prefix and suffix are empty (another method: `404` `UNKNOWN_METHOD`) |
| `?query=check&method=md5&user=<handle>&salt=<32 hex>&hash=<32 hex>` | `200` with `PASSWORD_OK`, `PASSWORD_FAIL` or `UNKNOWN_USER` (no such account, or a guest); `400` malformed |

- `salt` is the salt the server sent its client, already scrambled with the server's address; `hash`
  is the client's answer, `md5(md5(prefix + password + suffix) + salt)` with the bytes of both md5s,
  so here `md5(md5(password) + salt)` (`test-vectors/armaauth-v0.json`). The platform accepts a game
  password of the account in any of its typed forms (section 1, "Game passwords") or one of its
  pending one-time passwords, which a match uses up.
- Every check outcome is a `200` and the word says it, which `nKrawall` reads either way: a server
  whose fetch fails on any HTTP error (libcurl's `CURLOPT_FAILONERROR`, the `browser` line) would
  otherwise tell the player the authority's URL was not found.
- `user` is the handle in any case (the platform's handles are lowercase; the server keeps the case the
  player typed in the gid it reports, and the platform resolves gids case-insensitively). The answer
  names no user line: a server only accepts a claimed authority it contacted itself.
- Guests have no passwords here: their games log in with tokens only.
- Retrocycles: a handle with 30 failed checks in an hour fails every check for the rest of that hour;
  per client address the authority answers at most 600 requests a minute.

## 7. Direct Connect

A browser can join a server that has only an IP address and a port, no domain and no TLS
certificate. The page cannot open plain `ws://` from an `https` page, so
signalling goes through the platform's **signalling gateway**; the game packets do not.

```
page  --wss-->  wss://<api>/v1/direct/signal?to=<host>:<signalPort>  --ws-->  ws://<host>:<signalPort>/
page  <========= WebRTC data channel, UDP, straight to the host's ICE port =========>  game server
```

- `to`: the host (an IPv4 address, a bracketed IPv6 address or a name) and its **signalling
  port**, which for a stock server is the game port + 1 (`WEBRTC_SIGNAL_PORT` 0). The gateway
  connects to `ws://<host>:<signalPort>/` (path `/`; a game server accepts a WebSocket on any
  path and reads only the query).
- **Text frames only**, both ways, passed through unchanged: the signalling messages of the
  WebRTC transport (`{"type":"offer"|"candidate", ...}` up, the answer down; see
  `web/wasm/API.md` and `src/network/nTransportRtc.cpp`). There is **no WebSocket game transport**
  through the gateway: the page never sends `{"type":"websocket"}` on it, and the gateway
  drops binary frames (it would otherwise relay every game packet). A browser that cannot use
  WebRTC can only join servers with their own `wss://` signalling URL and
  `WEBRTC_WEBSOCKET_ENABLE 1`, directly.
- The gateway adds **no `X-Forwarded-For`** and the game server does not trust it. Game
  servers find each player's real address themselves, in this order:
  1. the client a **trusted proxy** names (`WEBRTC_TRUSTED_PROXIES`, `?X-Forwarded-For=`; the
     official rooms behind their own Caddy or region gateway);
  2. the selected ICE pair's remote address when the pair is **direct** (`host`, `srflx`,
     `prflx`): where the packets really come from. Through the gateway, this is the rule
     that applies;
  3. the **signalling socket's peer** (for a player whose pair is relayed, through the
     gateway this is the gateway: such players share an address);
  4. a **relayed** ICE address (the TURN server's);
  5. a trusted proxy's own address when it named nobody.

  IPv6 addresses count as a stable address in 240.0.0.0/4 per /64 (as in section 3's events),
  so a ban on one browser player never bans every browser player of the server.
- What the game server needs: WebRTC compiled in (`configure` does that whenever
  libdatachannel is found), `WEBRTC_ENABLE 1` and `WEBRTC_SIGNAL_BIND` on a reachable
  interface (both the defaults), the TCP signalling port and the UDP ICE port open (the
  server prints both at start: `WebRTC: firewall: open TCP ... and UDP ...`; the default ICE
  port is the game port + 1000). Players whose network forces a TURN relay cannot reach such
  servers: the platform's TURN relays only to its own rooms.
- Platform login tokens are not issued for servers the platform does not know (section 2),
  so players appear unverified there.
