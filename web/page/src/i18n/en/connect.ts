// English, the source: the connect texts (see ../index.ts). Keys "connect.<what>": the Server
// Browser (ServerBrowser.tsx, servers.ts), the stage (Stage.tsx), the connecting screen
// (Connecting.tsx), Quick Play's finding (platform/quickplay.ts) and moves (platform/moves.ts).
export const connect = {
  // ---- the stage ----
  "connect.stage.label": "Selected mode",
  "connect.ranked.notYet": "Ranked play isn't available yet.",
  // a ban: the reversed cell is the platform's; then this sentence and two links
  "connect.ban.say": "Bans apply to your account or guest ID in all official rooms. If you think this is a mistake, you can appeal.",
  "connect.ban.conduct": "code of conduct",
  // the appeals section of the code of conduct
  "connect.ban.appeals": "appeals",

  // ---- the connecting screen: a step's name (shown in capitals) and its sentence ----
  "connect.step.connecting": "connecting",
  "connect.step.connecting.say": "signalling socket, then the data channel, then the login",
  "connect.step.syncing": "syncing",
  "connect.step.syncing.say": "logged in, waiting for the game object",
  // downloading the round's map (the map's file name replaces the sentence when known)
  "connect.step.fetching": "fetching",
  "connect.step.fetching.say": "the round's map",
  "connect.step.playing": "playing",
  "connect.step.playing.say": "the room is starting the round",
  // Training starting
  "connect.step.starting": "starting",
  "connect.step.starting.say": "training against the AI",
  // Quick Play: the platform looks for a room
  "connect.step.finding": "finding",
  "connect.step.signing": "signing in",
  "connect.step.signing.say": "with your Discord account",
  "connect.step.duel": "waiting",
  "connect.finding.found": "found a room",
  "connect.finding.friend": "looking for {name}'s room",
  "connect.finding.invite": "looking for the invited room",
  "connect.finding.any": "looking for a room",
  "connect.finding.reroute": "that room did not answer, trying another",
  // "<why>, retrying in 3 s"
  "connect.finding.retry": "{why}, retrying in {seconds} s",
  "connect.finding.noResponse": "no response from the server",
  "connect.finding.friendFull": "{name}'s room is full",
  "connect.finding.inviteFull": "the invited room is full",
  "connect.finding.allFull": "every room is full",
  "connect.finding.duelCreate": "making your duel link",
  "connect.finding.duelAccept": "getting the duel room",
  "connect.finding.duelRoom": "joining the duel room",
  "connect.finding.duelFull": "every private room is taken",
  // the key cell "esc" then this word, at the foot of the connecting screen
  "connect.cancel": "cancel",
  "connect.duel.rule": "First to 12. Stay in the zone!",
  "connect.duel.waitingFor": "Waiting for your opponent…",
  "connect.duel.sendLeft": "Send this link to a friend. It expires in {time}.",
  "connect.duel.playWhile": "Play while you wait",
  "connect.step.arrived": "ready",
  "connect.duel.arrived": "Your opponent has arrived!",
  "connect.duel.joining": "Letting {name} know you're here…",
  "connect.duel.joiningLeft": "They have {time} to answer.",
  "connect.duel.titleReady": "OPPONENT READY!",
  "connect.fail.duelNotNow": "{name} can't play right now. Their invite stays open: try the link again in a bit.",
  "connect.fail.duelNoAnswer": "{name} didn't answer. Their invite stays open: try the link again in a bit.",
  "connect.fail.duelExpiredFor": "{name}'s invite has expired.",
  "connect.fail.duelTaken": "Someone else is joining that duel right now.",
  "connect.duel.copy": "Copy invite link",
  "connect.duel.done": "Copied",
  "connect.duel.shareLink": "Share link",
  "connect.duel.shareText": "A 1v1 on Retrocycles: open the link to play me",
  "connect.duel.room": "{room}, {region}",
  "connect.duel.sub": "1v1 in {room}",
  "connect.duel.subWith": "1v1 with {name} in {room}",

  // ---- why finding a room failed (shown after "finding: ") ----
  "connect.fail.platformGone": "the platform is gone",
  "connect.fail.malformed": "a malformed answer",
  "connect.fail.friendClosed": "{name} cannot be joined now",
  "connect.fail.inviteClosed": "the invited room is closed",
  "connect.fail.privateEnded": "that private room has ended",
  "connect.fail.privateBusy": "every private room is taken right now, try again in a few minutes",
  "connect.fail.duelEnded": "that duel has ended",
  "connect.fail.duelExpired": "nobody opened your duel link in time",
  "connect.fail.ownDuel": "that's your own duel link: send it to someone else",
  "connect.fail.friendStayedFull": "{name}'s room stayed full",
  "connect.fail.inviteStayedFull": "the invited room stayed full",
  "connect.fail.allStayedFull": "every room stayed full",
  "connect.fail.noRoom": "no room",
  "connect.fail.unreachable": "the room did not answer",

  // ---- moves (the room sends its players to another room) ----
  "connect.move.own": "moving to {name}…",
  "connect.move.together": "moving to {name} with everyone…",
  "connect.move.noWay": "no way to the new room",

  // ---- the Server Browser ----
  "connect.browser.home": "Back to the menu",
  "connect.browser.back": "Back",

  // ---- the Leaderboards page: the Server Browser's frame; its tabs, sorts and columns are game.score.*
  "connect.boards.title": "leaderboards",
  "connect.boards.you": "you",
  "connect.boards.top": "top",
  "connect.boards.windows": "Time window",
  "connect.boards.retry": "retry",
  "connect.boards.stale": "Not updated: the leaderboards can't be read right now.",
  "connect.boards.list": "Leaderboard",
  "connect.boards.detail": "Player details",
  "connect.boards.unavailable": "The leaderboards need the Retrocycles platform.",
  "connect.boards.play": "play",

  // the page's title
  "connect.browser.title": "Servers",
  // the search field's key (a verb: search the list)
  "connect.browser.find": "Find",
  "connect.browser.refresh": "Refresh",
  "connect.browser.refreshLabel": "Refresh the list",
  "connect.browser.filters": "Filters",
  "connect.browser.scopes": "Servers to show",
  "connect.browser.scope.all": "All",
  "connect.browser.scope.official": "Official",
  "connect.browser.scope.community": "Community",
  "connect.browser.failCell": "The server list can't be reached.",
  "connect.browser.failSay": "These are the servers the page knows without it.",
  "connect.browser.list": "Servers",
  // column heads (each sorts the list)
  "connect.browser.col.server": "Server",
  "connect.browser.col.players": "Players",
  "connect.browser.col.ping": "Ping",
  "connect.browser.col.region": "Region",
  // the default order: the best servers first
  "connect.browser.best": "Best",
  "connect.browser.sortedBest": "Sorted by best",
  "connect.browser.none": "No servers match.",
  "connect.browser.noneQuery": "Nothing is called “{query}”.",
  "connect.browser.noneQueryFiltered": "Nothing is called “{query}” under these filters.",
  "connect.browser.noneFilters": "The filters hide every server.",
  "connect.browser.clear": "Clear filters",
  // the board's groups
  "connect.tier.friends": "With friends",
  "connect.tier.bookmarked": "Bookmarked",
  "connect.tier.playing": "Playing now",
  "connect.tier.unknown": "No player count",
  "connect.tier.full": "Full",
  "connect.tier.empty": "Empty",
  // the EMPTY group's fold
  "connect.browser.show": "Show",
  "connect.browser.hide": "Hide",
  // REGION: the filter's key; "Any" when no region is chosen
  "connect.region.key": "Region",
  "connect.region.any": "Any",
  "connect.region.label": "Region: {region}",
  // a full room's player count
  "connect.browser.full": "Full",
  "connect.browser.bots": { one: "{n} bot", other: "{n} bots" },
  "connect.browser.friends": { one: "{n} friend", other: "{n} friends" },
  "connect.badge.official": "Official",
  "connect.badge.moderated": "Moderated",
  // the details pane
  "connect.detail.label": "Server details",
  "connect.detail.showVersion": "Show the server's version",
  "connect.detail.hideVersion": "Hide the server's version",
  "connect.detail.versionTitle": "Version",
  "connect.detail.hideVersionTitle": "Hide the version",
  "connect.detail.version": "Version",
  "connect.detail.notReported": "Not reported",
  "connect.detail.players": "Players",
  // after the player count: " · 2 bots"
  "connect.detail.bots": { one: "{n} bot", other: "{n} bots" },
  "connect.detail.noRoster": "This server doesn't publish its players yet.",
  "connect.detail.nobody": "Nobody is on. Join and others will find it.",
  // a roster row's marks
  "connect.detail.friend": "Friend",
  "connect.detail.bot": "Bot",
  "connect.detail.fullWhy": "The server is full. You can watch until a seat frees up.",
  "connect.detail.join": "Join",
  "connect.detail.full": "Full",
  "connect.detail.spectate": "Spectate",
  "connect.detail.bookmark": "Bookmark",
  "connect.detail.bookmarked": "Bookmarked",
  "connect.detail.bookmarkTitle": "Bookmark: keep it at the top of the list",
  "connect.detail.bookmarkedTitle": "Bookmarked: at the top of the list. Press to remove it.",

  // ---- Direct Connect: a server by its address, not from the board ----
  // the plate's cell (shown in capitals) and its accessible name
  "connect.direct.open": "Direct connect",
  "connect.direct.openLabel": "Direct connect: join a server by its address",
  "connect.direct.title": "Direct connect",
  // the field's key; the hint under it (the forms people type, not translated words)
  "connect.direct.address": "Address",
  "connect.direct.hint": "host:port, [IPv6]:port, or a wss:// address.",
  "connect.direct.error.invalid": "That isn't a server address. Try host:port, like 203.0.113.7:4534.",
  "connect.direct.error.port": "The port is a number from 1 to 65535.",
  // MORE: a browser port other than the game port + 1
  "connect.direct.more": "More",
  "connect.direct.signal": "Browser port",
  "connect.direct.signalHint": "Only if the server's owner gave you one. Usually the game port + 1, here {port}.",
  // the preview, from the platform asking the server
  "connect.direct.probing": "Asking the server…",
  "connect.direct.noProbe": "No preview: the server can't be asked from here. You can still join.",
  "connect.direct.ownAddress": "The server's own address: no preview, you join it directly.",
  "connect.direct.unreachable": "This server didn't answer on port {port}. Check the address, or try again later.",
  "connect.direct.browserClosed": "Its browser port, TCP {port}, is closed: browsers can't join until its owner opens it.",
  "connect.direct.needsPlatform": "Joining by address needs the Retrocycles platform, which can't be reached now. A wss:// address still works.",
  "connect.direct.blocked": "Retrocycles blocks this server after reports about it. It can't be joined from here.",
  // the preview's facts: whether its browser port answers
  "connect.direct.browsers": "Browsers",
  "connect.direct.browserOk": "Open",
  "connect.direct.browserNo": "Closed",
  "connect.direct.browserUnknown": "Not known",
  "connect.direct.browserPort": "Port {port}",
  // the warning before the first join to an address
  "connect.direct.warnHead": "Before you join",
  "connect.direct.warn": "This server isn't run or checked by Retrocycles. Its owner can see your IP address, and Retrocycles doesn't moderate its chat.",
  "connect.direct.understood": "I understand",
  "connect.direct.continue": "Continue",
  "connect.direct.cancel": "Cancel",
  // JOIN when the blocklist refuses
  "connect.direct.refused": "Blocked",
  "connect.direct.save": "Save",
  "connect.direct.saveLabel": "Save to My servers",
  "connect.direct.remove": "Remove",
  // a My servers row's state after its address
  "connect.direct.rowDown": "Not answering",
  "connect.direct.rowBlocked": "Blocked",
  "connect.tier.mine": "My servers",
  "connect.badge.direct": "Direct",
  // a join that failed, under the menu (the client's own reason otherwise)
  "connect.fail.directBlocked": "This server only takes direct connections, and your network blocks them.",
  "connect.fail.directClosed": "This server's browser port is closed, so browsers can't join it. Its owner has to open it.",
  "connect.fail.moveBlocked": "Retrocycles blocks the server this one tried to send you to: you stayed here."
} as const;
