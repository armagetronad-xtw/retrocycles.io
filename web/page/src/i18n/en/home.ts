// English, the source: the home texts (see ../index.ts). Keys "home.<what>".
// The home screen (the mode rows, the note under them, the documents line, the iPhone hint, the
// cookie question), the embed (X's frame, Discord), and the notes App.tsx composes.
export const home = {
  // the play modes, rows of the home menu (uppercased by CSS) and the stage's head
  "home.mode.quick": "Quick Play",
  "home.mode.ranked": "Ranked",
  "home.mode.browser": "Server Browser",
  "home.mode.leaderboards": "Leaderboards",
  "home.mode.local": "Training",
  "home.mode.custom": "Custom Game",
  "home.mode.private": "Challenge a Friend",
  // beside Quick Play: how many people play now
  "home.livePlaying": "{live} playing",
  "home.aria.modes": "Play modes",

  // the note under the queue (lower case, terse; the client's own reasons stay as the game says them)
  "home.note.noServer": "no server to connect to: servers.json is empty",
  "home.note.notLoaded": "the client is not loaded",
  "home.note.inviteNotHere": "the invited room is not on this page",
  // {why}: why finding a room failed (the platform's words, or the page's)
  "home.note.finding": "finding: {why}",
  // {why}: why following a move to another room failed
  "home.note.moving": "moving: {why}",
  "home.note.connectionLost": "connection lost",
  "home.note.notInGame": "not in a game",
  // {error}: the loader's error, English
  "home.note.loadFailed": "the client failed to load: {error}",
  // the Training stage's sub line
  "home.sub.local": "standalone, no server",

  // the Console's notes (when a typed line could not run)
  "home.console.notLoaded": "not run: the client is not loaded",
  "home.console.trainingNoRoom": "not sent: training has no room to send commands to",
  "home.console.waitRound": "not sent: the room takes commands once your first round starts",
  "home.console.joinFirst": "not sent: join a game first",
  "home.console.refused": "not run: the client refused the line",

  // chat and the player card: answers to a friend request, a mute, a message ({name}: a player)
  "home.social.friendsOff": "friends are not on this platform",
  "home.social.needAccount": "create an account to add friends (Profile, top right)",
  "home.social.noAccount": "{name} doesn't have an account and can't be added",
  "home.social.friendsNow": "you and {name} are friends now",
  "home.social.requestSent": "friend request sent to {name}",
  "home.social.mutedEverywhere": "{name} is muted in every room",
  "home.social.unmuted": "{name} is no longer muted",
  // {why}: the platform's reason, already in the page's words
  "home.social.notMuted": "not muted: {why}",
  "home.social.notSent": "not sent: {why}",
  "home.social.noPlatform": "not sent: no platform",
  // a direct message from someone not in the friends list
  "home.social.aFriend": "a friend",

  // Discord: shown small (picture in picture), no steering
  "home.discord.tapToReturn": "Tap to return",
  // Discord rich presence, what others see on the player's profile
  "home.discord.presencePlaying": "Quick Play",
  "home.discord.presenceChoosing": "Choosing a game",
  // Discord's share link message
  "home.discord.shareMessage": "Ride with me in Retrocycles",

  // the embed (X's frame, Discord): one PLAY in the middle
  "home.embed.play": "Play",
  "home.embed.loading": "Loading",
  "home.embed.say": "Lightcycles in your browser. No download.",
  // {together}: "with Ana", "with Ana and Ben", "with Ana, Ben and 3 more" (the next keys)
  "home.embed.together": "You are here {together}. Everyone who presses PLAY rides in one room.",
  "home.discord.together": "You are here {together}. QUICK PLAY puts you all in one room.",
  "home.embed.withOne": "with {a}",
  "home.embed.withTwo": "with {a} and {b}",
  "home.embed.withMore": { one: "with {a}, {b} and {n} more", other: "with {a}, {b} and {n} more" },
  "home.embed.openFull": "Open full game",
  "home.embed.fullscreen": "Full screen",
  "home.embed.leaveFullscreen": "Leave full screen",

  // the documents line (links to the English documents)
  "home.docs.aria": "About and policies",
  "home.docs.about": "about",
  "home.docs.conduct": "conduct",
  "home.docs.privacy": "privacy",
  "home.docs.terms": "terms",
  "home.docs.news": "news",
  // over the game, the first chat in an official room; {link} is "code of conduct" (the next key)
  "home.docs.conductLine": "chat here follows the {link}",
  "home.docs.codeOfConduct": "code of conduct",

  // the community Discord server, at the bottom right of the home screen (a link, new tab): its
  // name (a proper name, keep it), the line under it before the count is known, the online count
  // ({online}, grouped as the language writes thousands) and the whole for a screen reader
  "home.community.name": "The Grid",
  "home.community.sub": "community Discord",
  "home.community.online": "{online} online",
  "home.community.members": "{members} members",
  "home.community.aria": "The Grid, the community Discord server (opens in a new tab)",

  // iPhone Safari: how to play without the browser's bars; {share} and {add} are Safari's own
  // button names (keep them as Safari shows them in the language)
  "home.hint.homeScreen": "Full screen: tap {share}, then {add}",
  "home.hint.share": "Share",
  "home.hint.addToHome": "Add to Home Screen",
  "home.hint.dismiss": "Dismiss",

  // the cookie question (EEA, UK, Switzerland)
  "home.cookies.aria": "Cookies",
  "home.cookies.ask": "May we collect usage statistics and use Google Analytics cookies, to see how people find and play the game?",
  "home.cookies.privacy": "Privacy",
  "home.cookies.decline": "Decline",
  "home.cookies.accept": "Accept",
  // ---- the bell (NewsBell.tsx): the cell (aria-label, title) and its panel
  "home.news.cell": "News",
  "home.news.cellUnread": { one: "News, {n} new post", other: "News, {n} new posts" },
  "home.news.title": "news",
  "home.news.all": "all news",
  "home.news.new": "new",
  "home.news.empty": "No news yet.",
  "home.news.asking": "Loading the news.",
  "home.news.failed": "The news did not load. It tries again when you next open the bell.",
  "home.news.opens": "Opens in a new tab."
} as const;
