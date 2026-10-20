// The play page. One fixed viewport: the client's canvas at the bottom (index.html),
// the queue (.frame) over it, the chosen mode (.stage) or the
// Profile stage (from the chip) over that, the game frame on top while a game runs.
// The page's own state is small: which tile has the focus, which mode is grown, the
// note under a tile, the server list's cursor, whether the Profile is up; the session
// state itself is the client's (useClient), never copied, and who is signed in is the
// platform store's (platform/session.ts, usePlatform).
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useClient } from "./aa/useClient";
import { t, useLang } from "./i18n";
import type { AaConsoleEvent, AaHandshakeEvent, AaMoveEvent, AaPlayer, SessionState, ServerEntry } from "./aa/types";
import { Chip } from "./components/Chip";
import { ConductLine, DocsLine, conductLineSeen, markConductLineSeen } from "./components/Docs";
import { KEYS_LINE_MS, KeysLine, countKeysLine, keysLineWanted } from "./components/KeysLine";
import { Training } from "./components/Training";
import { Profile, type ProfileHandle, type ProfileIntent, type ProfileView } from "./components/Profile";
import type { PlateColour } from "./components/profile/colour";
import { MatchSummary, XpLine } from "./components/Rewards";
import { OfferPanel } from "./components/Offer";
import { stripColors } from "./components/Colored";
import { AuthPrompt } from "./components/AuthPrompt";
import { ChatOverlay, type ChatOverlayHandle, type ChatTab, type PanelView, type Settings } from "./components/ChatOverlay";
import { audible, useChat } from "./chat/useChat";
import { beginSession, logInput, logNote, useLog } from "./chat/log";
import { GameFrame } from "./components/GameFrame";
import { Settings as SettingsSheet, type SettingsHandle, type SettingsPreview } from "./components/Settings";
import { SettingsCell } from "./components/SettingsCell";
import type { TabId } from "./settings/schema";
import { useSettingsStore } from "./settings/store";
import { swatch, type CardSocial, type GameMenuHandle } from "./components/GameMenu";
import { Stage, type StageBox } from "./components/Stage";
import type { ServerBrowserHandle } from "./components/ServerBrowser";
import { Strip, type Note } from "./components/Strip";
import { Friends, type FriendDuel, type FriendsHandle } from "./components/Friends";
import { FriendsCell, MessagesCell } from "./components/FriendsCell";
import { NewsCell, NewsPanel } from "./components/NewsBell";
import { markNewsSeen, refreshNews, settleNews, useNews } from "./platform/news";
import { ChatDock, type ChatDockHandle } from "./components/ChatDock";
import { friendsAccount, friendsOn, mutesOn, refreshFriends, refreshMutes, setMute, socialText, socialText as messageText, useFriends, useMutes, useRoomPlayers } from "./platform/friends";
import { askFriend, reportPresence, sendMessage } from "./platform/client";
import { onMessage, useConversations, useEventStream, useUnread } from "./platform/messages";
import { directEntry } from "./chat/sort";
import type { FriendInfo, PageState } from "./platform/types";
import { configText, playerColor, signedInName } from "./player";
import { isMe } from "./chat/sort";
import type { ChatSocial } from "./chat/useChat";
import { focusableIn, isTextField, moveFocus, wouldWrap } from "./keyboard";
import { HINT_ACTIONS, dropMouseCamera, embedKeys, hintItems, moveOffOldDefaults, type HintItem } from "./keys";
import { embedSoundOff } from "./embedSound";
import { MODES, modeIndex, setPrivateRooms, type Mode } from "./modes";
import { discordJoinable, invitedServer, loadServers, serverCode, signalUrl } from "./servers";
import { Banned, banText, iceOptionsFor, loginTokenFor, platformState, refreshPlatformUser } from "./platform/session";
import { noteWebRtcBlocked, noteWebRtcWorked, webRtcBlocked } from "./platform/webrtcFallback";
import { NOTE_MAX, REASONS, reportsOffered, sendReport, type ReasonId, type ReportResult, type ReportTarget, type RoomLine } from "./platform/reports";
import type { ChatEntry } from "./chat/sort";
import { DUEL_ARRIVE_MS, FindFailure, copyText, findRoom, giveBackSeat, inviteLink, keepLinkOpen, livePlayers, platformServers, preferredRegion, privateRoomLink, privateRoomTicket, privateRoomsEnabled, quickPlayEnabled, readInvite, readPrivateInvite, regionLatencies, type FindingPhase, type JoinTarget } from "./platform/quickplay";
import { blockChallenges, challengesBlocked, duelFailText, msOf, nudgeDuels, setChallenges, setDuelName, unblockChallenges, useDuelSettings, useDuels } from "./platform/duels";
import { PlatformError, acceptDuel, cancelDuel, createDuel, declineDuel, getAccount, goDuel, laterDuel } from "./platform/client";
import { alertArrival, primeAlerts, stopFlash, setUnreadBadge } from "./alerts";
import { DuelPanel, type DuelLine } from "./components/Duel";
import { DuelRoom, REMATCH_CLOSED } from "./components/DuelRoom";
import { ConnectAttempt, claimOpened, countMatch, countRound, endGame, flush, noteAction, reasonOf, sampleGame, setScreen, sinceNavigation, startGame, track, type ConnectMode } from "./platform/analytics";
import { usePlatform } from "./platform/usePlatform";
import { MoveFailure, findMoveSeat, foreignMove, moveLine, moveReason } from "./platform/moves";
import { DIRECT_OFFERED, directReportsOffered, isBlocked, readConnectLink, sendDirectReport, throughGateway, type DirectLine } from "./platform/direct";
import { addressText, signalHost } from "./directAddress";
import { MoveAsk, type MoveAskLine } from "./components/MoveAsk";
import { useRewards } from "./platform/useRewards";
import { progressionOn, progressionOwner, refreshProgression, useProgression, xpText } from "./platform/progression";
import { usePlayer } from "./player";
import { DISCORD, EMBED, WEBSOCKET_TRANSPORT, fullPageLink, readPlayLink } from "./links";
import { DISCORD_PLATFORM, DISCORD_SHARED_ROOM, discordInvite, discordPresence, openOutside, useDiscord } from "./discord";
import { DiscordTogether, EmbedPlay, FullGameCell, FullscreenCell, fullscreenOffered } from "./components/Embed";
import { pageMetersAvailable, touchPrimary, useCompact, useGameHud } from "./compact";
import { useHudStyle } from "./settings/hudStyle";
import { CycleHud } from "./components/CycleHud";
import { RespawnCountdown } from "./components/RespawnCountdown";
import { DeathNote } from "./components/DeathNote";
import type { LeaderboardsHandle } from "./components/Leaderboards";
import { SCORE_TABS, Scoreboard, type ScoreTab } from "./components/Scoreboard";
import { leaderboardsOn, readMyPlaces, useBoardList, useLeaderboardBaseline, useMatchTally } from "./platform/leaderboards";
import type { BoardSort, MyPlace } from "./platform/types";
import { onScoreKey } from "./scoreKeys";
import { TouchControls } from "./components/TouchControls";
import { MenuCell } from "./components/MenuCell";
import { rowLayout, useTouchLayout } from "./touchLayout";
import { HomeScreenHint, homeScreenHintSeen } from "./components/HomeScreenHint";
import { CookieBanner, useCookieBanner } from "./components/CookieBanner";
import { escapeInFullscreen, toggleFullscreen, fullscreenOnPlay, iphoneSafari, nudgeScroll, standalone } from "./fullscreen";

const EMPTY_BOX: StageBox = { x: 0, y: 0 };

// an emailed account link (public/account/*/index.html forwards
// /account/verify-email?userId=&token= to /?account=verify-email&userId=&token=):
// read once and removed from the address bar and the history at once, so the token
// is not left behind; the Profile stage opens with it once the page is ready
function readAccountLink(): ProfileIntent | null {
  const query = new URLSearchParams(location.search);
  const kind = query.get("account");
  if (kind !== "verify-email" && kind !== "reset-password") return null;
  const userId = query.get("userId") || "";
  const token = query.get("token") || "";
  query.delete("account");
  query.delete("userId");
  query.delete("token");
  const rest = query.toString();
  history.replaceState(history.state, "", location.pathname + (rest ? "?" + rest : "") + location.hash);
  return userId && token ? { kind, userId, token } : null;
}

// how often a network game's ping is read for play_stats
const PING_SAMPLE_MS = 3000;
// how long `playing` may wait for its first round and the sync before the game is shown anyway
const ROUND_WAIT_MS = 15000;
// Quick Play's way around a room that does not answer (a wedged signalling server):
// the signalling must answer the offer within SIGNAL_STALL_MS (a live room does in well under a
// second), and a connect that fails before the login asks for another room, MAX_REROUTES times
const SIGNAL_STALL_MS = 8000;
const MAX_REROUTES = 2;
// a Direct Connect that has not got through its signalling or its ICE by then is given up, and
// the page says why in plain words
const DIRECT_STALL_MS = 20000;
// how long a server's question to move the player elsewhere stays up unanswered (then: stay)
const MOVE_ASK_MS = 30000;
// a bare flag: this browser's game name was checked once for an account name a sign-in had copied
const ACCOUNT_NAME_DROPPED_KEY = "rc.accountNameDropped";

// the states in which scene(k) is allowed (API.md: from idle, disconnected, scene)
function canScene(s: SessionState): boolean {
  return s === "idle" || s === "disconnected" || s === "scene";
}

// keys the home screen, the stages and the sheets use themselves
const PAGE_KEYS = new Set(["Enter", " ", "Escape", "Tab", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "/"]);

export function App() {
  // the page re-renders in a newly chosen language (i18n/index.ts); everything below follows App
  useLang();
  const canvas = useMemo(() => document.getElementById("canvas") as HTMLCanvasElement | null, []);
  const client = useClient(canvas);
  const { aa, phase, state, scene, error, menuPresses, chatPresses, chatPrefill, consolePresses, rounds, players, auth, scores } = client;
  const platform = usePlatform();
  // the platform's friends: the friends cell, the panel, the card's ADD FRIEND
  const friendsLive = friendsOn(platform);
  const player = usePlayer(aa, platform.mode === "platform" ? platform.apiUrl : null);
  const discord = useDiscord();
  // the Settings sheet edits the game config itself (settings/store.ts)
  const settingsStore = useSettingsStore(aa);
  // the account's event stream (messages, friend changes) and the unread count
  useEventStream();
  const unreadMessages = useUnread();
  const friendsList = useFriends();
  // the bell (features.news): the posts staff sent to every player, and its panel
  const news = useNews();
  const [newsUp, setNewsUp] = useState(false);

  const [servers, setServers] = useState<ServerEntry[]>([]);
  const [serversLoaded, setServersLoaded] = useState(false);
  // the Server Browser: whether servers.json could be read, and when it last was
  const [serversOk, setServersOk] = useState(true);
  const [serversAt, setServersAt] = useState<number | null>(null);
  // the board's own list: the platform's (GET /v1/servers: what each server answered last);
  // null: servers.json, as Quick Play reads it
  const [boardList, setBoardList] = useState<ServerEntry[] | null>(null);
  const browserRef = useRef<ServerBrowserHandle>(null);
  const boardsRef = useRef<LeaderboardsHandle>(null);
  // the Leaderboards page's PLAY: Quick Play once its stage has closed
  const [quickNext, setQuickNext] = useState(false);
  const [focused, setFocused] = useState(0);
  // the focused line's index, for a change of the list (modes.ts setPrivateRooms) outside a render
  const focusedRef = useRef(0);
  focusedRef.current = focused;
  const [mode, setMode] = useState<Mode | null>(null); // the grown tile
  const [shown, setShown] = useState<Mode | null>(null); // what the stage renders; outlives mode by the shrink
  const [stageOpen, setStageOpen] = useState(false);
  const [box, setBox] = useState<StageBox>(EMPTY_BOX);
  const [sub, setSub] = useState("");
  const [note, setNote] = useState<Note | null>(null);
  const [menuOpen, setMenuOpen] = useState(false);
  const dockUnread = unreadMessages;
  // the messages cell stands while the account has any conversation (the way back to them), its count while unread
  const storedConversations = useConversations();
  // unread messages in front of the page's title ("(2) Retrocycles"): a background tab says so
  useEffect(() => setUnreadBadge(unreadMessages), [unreadMessages]);
  const hasConversations = storedConversations.length > 0 || unreadMessages > 0;
  const dockFriends = friendsList;
  const [sessionName, setSessionName] = useState("");
  // the Profile stage: `profile` is it up (like `mode`), `profileOpen` the travel done
  // (like `stageOpen`), `profileShown` rendered, outliving `profile` by the shrink
  const [profile, setProfile] = useState(false);
  const [profileOpen, setProfileOpen] = useState(false);
  const [profileShown, setProfileShown] = useState(false);
  // the Friends panel: the same three phases, from its own status cell
  const [friendsUp, setFriendsUp] = useState(false);
  const [friendsOpen, setFriendsOpen] = useState(false);
  const [friendsShown, setFriendsShown] = useState(false);
  // the Settings sheet: the same three phases, from its status cell (or the Esc
  // menu's SETTINGS lane in a game); its open tab, for the scene behind it
  const [settingsUp, setSettingsUp] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [settingsShown, setSettingsShown] = useState(false);
  const [settingsTab, setSettingsTab] = useState<TabId | null>(null);
  // the tab Settings opens on when something sent the player to a row there (the Profile's CUSTOM colour)
  const [settingsStart, setSettingsStart] = useState<TabId | null>(null);
  const [settingsPreview, setSettingsPreview] = useState<SettingsPreview>({ camera: false, glance: false, course: 0 });
  const [accountLink, setAccountLink] = useState<ProfileIntent | null>(() => readAccountLink());
  // Quick Play through the platform: the finding step (null in community mode) and the room it found
  const [finding, setFinding] = useState<FindingPhase | null>(null);
  // my invite link left open to play meanwhile (Challenge a Friend's PLAY WHILE YOU WAIT): its
  // arrival comes over the game or the home screen (GO TO 1V1 / NOT NOW)
  const [openInvite, setOpenInvite] = useState<{ code: string; link: string; until: number } | null>(null);
  const [inviteCopied, setInviteCopied] = useState(0);
  const [quickRoom, setQuickRoom] = useState("");
  // the game runs on a server that logged the player in through the platform: its rounds
  // earn XP (platform/useRewards.ts)
  const [rewardGame, setRewardGame] = useState(false);
  // the Profile opened on a view other than home ("save your progress" in the match summary)
  const [profileStart, setProfileStart] = useState<ProfileView | null>(null);
  // the code of conduct in one line over the game: the first chat in an official room
  const [conductLine, setConductLine] = useState(false);
  // the room the game runs in when it is an official one (platform login): its server id,
  // for reports (the platform attaches that room's chat)
  const [roomServerId, setRoomServerId] = useState<string | null>(null);
  // a ban the platform answered a connect with (shown in the open stage), "" for none
  const [ban, setBan] = useState<string | null>(null);
  // the server id of the room the game runs in, official or not: the invite link's room
  const [gameServerId, setGameServerId] = useState<string | null>(null);
  // the invite code of the server the game runs on (servers.ts serverCode): any listed server
  const [gameCode, setGameCode] = useState<string | null>(null);
  // an invite link's room (?join=), joined once the page is ready
  // a ?join= link, or inside Discord a share link's room (discord.ts DISCORD_SHARED_ROOM)
  const [joinRoom, setJoinRoom] = useState<string | null>(() => readInvite() ?? DISCORD_SHARED_ROOM);
  // a private room's invite link (?duel=<code>), joined once the page is ready
  const [joinPrivate, setJoinPrivate] = useState<string | null>(() => readPrivateInvite());
  // the invite code of the private room the game runs in (its link replaces the room's ?join= one)
  const [privateCode, setPrivateCode] = useState<string | null>(null);
  // Quick Play once the page is ready: the /play link (?play), the embed's PLAY pressed while loading,
  // or a Discord launch (straight into a game; leaving it lands on the menu) unless a share link names a room
  const [playNow, setPlayNow] = useState(() => readPlayLink() || (DISCORD && !DISCORD_SHARED_ROOM));
  // iPhone Safari: the way to full screen, Add to Home Screen (HomeScreenHint), until put away
  const [homeHint, setHomeHint] = useState(() => !EMBED && iphoneSafari() && !standalone() && !homeScreenHintSeen());
  // Google Analytics' cookie question (googleTag.ts), in the hint's place while it is up
  const cookieBanner = useCookieBanner();
  // how many play now (GET /v1/live), null when the page does not say
  const [live, setLive] = useState<number | null>(null);
  // the keys line at the start of the first games (KeysLine.tsx)
  const [keysLine, setKeysLine] = useState<HintItem[] | null>(null);
  // the Training screen over the paused local game (components/Training.tsx)
  const [trainingUp, setTrainingUp] = useState(false);
  // the chat overlay's tab (kept from game to game), whether its panel is open and on which
  // view (a chat tab, or the Console)
  const [chatTab, setChatTab] = useState<ChatTab>("all");
  const [chatOpen, setChatOpen] = useState(false);
  const [chatView, setChatView] = useState<PanelView>("all");
  // the room carries UTF-8 chat (an official room): Korean, Chinese, Japanese
  const [chatUtf8, setChatUtf8] = useState(false);

  const stripRef = useRef<HTMLElement>(null);
  // the grown mode for callbacks made before it rendered (a connect started by select())
  const modeRef = useRef<Mode | null>(null);
  modeRef.current = mode;
  const chipRef = useRef<HTMLButtonElement>(null);
  const profileRef = useRef<ProfileHandle>(null);
  const friendsRef = useRef<FriendsHandle>(null);
  const dockRef = useRef<ChatDockHandle>(null);
  const friendsCellRef = useRef<HTMLButtonElement>(null);
  const settingsRef = useRef<SettingsHandle>(null);
  const settingsCellRef = useRef<HTMLButtonElement>(null);
  const newsCellRef = useRef<HTMLButtonElement>(null);
  const menuRef = useRef<GameMenuHandle>(null);
  const chatRef = useRef<ChatOverlayHandle>(null);
  // a friend's room to join once the page is back on the home screen (JOIN from inside a game
  // leaves first), and the spectator switch for the next game and what it was before
  // where to go once this game is left: a room, a friend, a duel, or plain Quick Play (a duel room's QUICK PLAY)
  const [pendingJoin, setPendingJoin] = useState<JoinTarget | { quick: true } | null>(null);
  // the duel room's opponent by the name they play under (its VS card), while in a duel's room
  const [duelWith, setDuelWith] = useState<string | null>(null);
  const spectateNext = useRef(false);
  const spectatorBefore = useRef<string | null>(null);
  // the server the game runs on (set by connect): its signalling URL, whether it is one of our
  // rooms, its entry (a Direct Connect's info). A server that is not ours gets the page's chat
  // filter and its reports, and its moves are checked
  const [roomFrom, setRoomFrom] = useState<{ url: string; official: boolean; entry: ServerEntry } | null>(null);
  const roomFromRef = useRef(roomFrom);
  roomFromRef.current = roomFrom;
  // a move such a server asked for to another host: the player answers it (MoveAsk), never the page
  const [moveAsk, setMoveAsk] = useState<{ kind: "ask"; move: AaMoveEvent; until: number } | { kind: "note"; text: string; until: number } | null>(null);
  // a Direct Connect given up by the page: the stage it stood at
  const directStalled = useRef<string | null>(null);
  // ?connect=host:port: the Server Browser with the address filled and the warning up
  const [connectLink, setConnectLink] = useState<{ address: string; warn: boolean } | null>(() => {
    const address = readConnectLink();
    return address ? { address, warn: true } : null;
  });
  const clearConnectLink = useCallback(() => setConnectLink(null), []);
  // the panel open now (for handlers that must not take the focus from it), and where the
  // focus was when the Console opened over the home screen (it goes back there)
  const chatOpenRef = useRef(false);
  // the chat seen up to (the newest line's id when the panel last opened or closed), and the newest
  // now: the touch controls' chat cell shows a dot for chat that came in since
  const [chatSeen, setChatSeen] = useState(0);
  const lastChatId = useRef(0);
  const consoleReturn = useRef<HTMLElement | null>(null);
  const frameRef = useRef<HTMLElement>(null);
  const leaving = useRef(false);
  // bumped by every connect and every back-out: a connect still waiting for its
  // login token / ICE servers is dropped when the player left meanwhile
  const connectSeq = useRef(0);
  const sceneStarted = useRef(false);
  // the seat Quick Play holds for us until the login went through (given back when the
  // connection fails or the player backs out before), and the running search
  const seat = useRef<string | null>(null);
  const findAbort = useRef<AbortController | null>(null);
  // a Quick Play pick (not an invite, a friend's room or a server of the browser): the rooms
  // that did not answer so far, which the next ask avoids; the seat's room; and whether the
  // page itself ended a connect whose signalling never answered
  const reroute = useRef<{ avoid: string[]; tries: number } | null>(null);
  const seatRoom = useRef<string | null>(null);
  const stalled = useRef(false);
  // usage events (platform/analytics.ts; nothing is sent without a platform): the connect
  // attempt in progress, the mode of the game being played, the first Play press, the
  // player's turn keys and whether the first turn was reported, the rounds counted so far
  const attempt = useRef<ConnectAttempt | null>(null);
  const playMode = useRef<"quick" | "browser" | "local" | null>(null);
  const firstPlayAt = useRef<number | null>(null);
  const turnCodes = useRef<Set<string>>(new Set());
  const firstTurnSent = useRef(false);
  const inMatch = useRef(false);
  const roundsCounted = useRef(0);
  // the region of the room Quick Play placed us in (play_stats); none for the server browser
  const playRegion = useRef<string | null>(null);
  // Esc stepping back through the Profile over a running game also reaches the game as its
  // menu key (menuOpen): that press must not open the menu
  const profileEscAt = useRef(0);
  const loadReported = useRef(false);
  const sceneReported = useRef(false);
  // a move to another room (the room's `move` event, API.md "Moves"): `leaving` until the old
  // session ended, then `joining` (the seat, the connect) until the game runs there. While it
  // is set, the end of the old session is no failure: no note, the stage stays
  // foreign: the move of a server that is not ours, to the entry the page itself made for it (no platform seat)
  const moving = useRef<{ move: AaMoveEvent; phase: "leaving" | "joining"; at: number; foreign: ServerEntry | null } | null>(null);

  // ---- what the page shows ------------------------------------------------------

  const real: SessionState = state ? state.state : "idle";
  const session: SessionState = real;
  // a network game shows once there is something to see: `playing` comes while the canvas is
  // still black (a room that was empty wakes up and builds the round, then the client syncs
  // to it, "Syncing with server..."), so the connecting screen stays up until the session's
  // first `round` "started" and the game timer synced (getState().synced, polled: no event),
  // or ROUND_WAIT_MS at most. Once shown it stays shown for the session
  const sessionLive = real === "connecting" || real === "syncing" || real === "fetching" || real === "playing";
  const roundsBefore = useRef(rounds.started);
  const roundsNow = useRef(rounds.started);
  roundsNow.current = rounds.started;
  useEffect(() => {
    if (!sessionLive) roundsBefore.current = rounds.started;
  }, [sessionLive, rounds.started]);
  const [gameShown, setGameShown] = useState(false);
  // a room's message of the day (the API's `message` event) is not held on the connecting screen;
  // the client prints it to the console too, so it reaches the chat overlay's lines like any server line
  useEffect(() => {
    setGameShown(false);
    if (real !== "playing" || !aa) return;
    const since = performance.now();
    const t = window.setInterval(() => {
      const ready = roundsNow.current > roundsBefore.current && aa.getState().synced;
      if (!ready && performance.now() - since < ROUND_WAIT_MS) return;
      window.clearInterval(t);
      setGameShown(true);
    }, 50);
    return () => window.clearInterval(t);
  }, [real, aa]);
  const inGame = (session === "playing" && gameShown) || session === "local";
  // the Server Browser's page is up (not a connect from it, not a ban): the status bar works over it
  // (the Leaderboards page stands in the same frame: the status bar works over it too)
  const serversPage =
    (mode === "browser" || mode === "boards") && !ban && !inGame && !(session === "connecting" || session === "syncing" || session === "fetching" || session === "playing");

  // the bell: put away when a game starts; asked again when the player comes back from one
  useEffect(() => {
    if (inGame) setNewsUp(false);
    else void refreshNews();
  }, [inGame]);
  // the server's password request (AuthPrompt.tsx) until it is answered here or the session
  // ends; it takes the menu's place, once the game shows (it lives in the game frame)
  const [answered, setAnswered] = useState<typeof auth>(null);
  // when the player last typed /login (chat or Console): a request soon after is theirs; one
  // the server sends unasked is confirmed first (AuthPrompt.tsx)
  const loginTypedAt = useRef(0);
  const noteLogin = (text: string) => {
    if (/^\s*\/login(\s|$)/i.test(text)) loginTypedAt.current = Date.now();
  };
  const authAsked = useMemo(() => !!auth && Date.now() - loginTypedAt.current < 60000, [auth]);
  const authUp = !!auth && auth !== answered && session === "playing" && inGame;
  const answerAuth = (run: () => void) => {
    run();
    setAnswered(auth);
    canvas?.focus({ preventScroll: true });
  };
  useEffect(() => {
    if (authUp) setMenuOpen(false);
  }, [authUp]);
  const loading = phase === "loading";
  const loadFraction = phase === "ready" ? 1 : client.loadFraction ?? 0.05;
  const resource = session === "fetching" ? state?.resource ?? null : null;
  // the keys the connecting screen names, read from the key map when a stage opens
  const stageKeys = useMemo(() => (aa && shown ? hintItems(aa) : []), [aa, shown]);

  // ---- helpers ------------------------------------------------------------------

  const showNote = useCallback((text: string, quiet: boolean, tile: number) => {
    setNote({ text, quiet, tile });
  }, []);

  const playScene = useCallback(
    (k: number) => {
      if (!aa || !canScene(real)) return;
      aa.scene(k);
    },
    [aa, real]
  );

  const focusTile = useCallback(
    (i: number, moveDom: boolean) => {
      if (mode) return;
      i = Math.max(0, Math.min(MODES.length - 1, i));
      setFocused(i);
      if (moveDom) {
        const tile = stripRef.current?.children[i] as HTMLElement | undefined;
        tile?.focus({ preventScroll: true });
      }
      playScene(MODES[i].scene);
    },
    [mode, playScene]
  );

  const closeStage = useCallback(() => {
    setStageOpen(false);
    setMenuOpen(false);
    setMode(null);
    setFinding(null);
    setBan(null);
  }, []);

  // a Play press: the first one starts the "since Play" clock of first_turn
  const pressed = useCallback(() => {
    if (firstPlayAt.current === null) firstPlayAt.current = performance.now();
  }, []);

  // a connect attempt (Quick Play, or a server of the browser): play_pressed, then its stages
  // viaSocket: it goes over the signalling WebSocket, WebRTC having failed on this tab (Quick
  // Play asks for such a room then; a server of the browser says so in its lobby)
  const startAttempt = useCallback(
    (mode: ConnectMode, viaSocket = mode === "quick" && webRtcBlocked() && !WEBSOCKET_TRANSPORT) => {
      attempt.current?.fail("cancelled"); // an earlier one that never ended
      if (mode !== "quick") reroute.current = null; // a server of the browser is that one server
      pressed();
      playRegion.current = null;
      const a = new ConnectAttempt(mode, viaSocket ? "websocket" : undefined);
      attempt.current = a;
      return a;
    },
    [pressed]
  );

  // the seat was not used: free it now (the platform would after 30 s); `failed`: the room
  // could not be reached, which the platform counts toward skipping it
  const dropSeat = useCallback((failed = false) => {
    findAbort.current?.abort();
    findAbort.current = null;
    const id = seat.current;
    seat.current = null;
    seatRoom.current = null;
    if (id) void giveBackSeat(id, failed);
  }, []);

  // the move is over: its usage event, and the page is itself again
  const endMove = useCallback((result: "arrived" | "failed" | "cancelled") => {
    const m = moving.current;
    if (!m) return;
    moving.current = null;
    document.documentElement.dataset.move = "";
    document.documentElement.dataset.moveResult = result;
    track("move_result", { reason: moveReason(m.move.reason), result, ms: performance.now() - m.at });
  }, []);

  const connect = useCallback(
    (server: ServerEntry | undefined) => {
      if (!server) {
        showNote(t("home.note.noServer"), false, modeIndex("quick"));
        return;
      }
      setSub(stripColors(server.name));
      setSessionName(stripColors(server.name));
      beginSession(stripColors(server.name));
      setRewardGame(!!server.platformLogin && !!server.serverId);
      setRoomServerId(server.platformLogin && server.serverId ? server.serverId : null);
      setGameServerId(server.serverId || null);
      setGameCode(serverCode(server.url));
      setPrivateCode(server.privateCode ?? null);
      if (!server.privateCode) setDuelWith(null);
      const a = attempt.current && !attempt.current.done ? attempt.current : null;
      if (!aa) {
        a?.fail("error");
        showNote(t("home.note.notLoaded"), false, modeIndex("quick"));
        return;
      }
      // an official room (platform login) runs our server, which carries UTF-8 chat; any other
      // server gets the game's Latin-1 (what it cannot hold goes as '?')
      const utf8 = !!server.platformLogin && !!server.serverId;
      setChatUtf8(utf8);
      setRoomFrom({ url: server.url, official: utf8 && !server.direct, entry: server });
      setMoveAsk(null);
      directStalled.current = null;
      // SPECTATE (a friend's room): watch this game; the switch persists as SPECTATOR_MODE_1, so
      // what it was goes back when the game ends
      if (spectateNext.current) {
        spectateNext.current = false;
        if (spectatorBefore.current === null) spectatorBefore.current = aa.getConfig("SPECTATOR_MODE_1")?.value ?? "0";
        aa.setConfig("SPECTATOR_MODE_1", "1");
      }
      aa.setChatUtf8?.(utf8);
      leaving.current = false;
      // the signalling stage counts from here: the login token and ICE servers are fetched first
      a?.begin("signalling");
      if (a) playMode.current = a.mode;
      const seq = ++connectSeq.current;
      // right before connecting, in parallel: a fresh login token for a server with
      // platform login, and the ICE servers (the platform's TURN relay with short-lived
      // credentials, else platform.json's static list). Either may be missing (community
      // mode, a failed fetch): the connect then goes ahead without it. Nothing is kept.
      // WebRTC failed on this tab before: a room that takes the game over its WebSocket goes that way
      // (never through the platform's signalling gateway: that would relay every packet)
      const viaSocket = webRtcBlocked() && server.lobby?.webSocket === true && !server.direct?.gateway;
      // a private room also needs its single-use ticket, which rides on the signalling URL
      Promise.all([loginTokenFor(server), iceOptionsFor(server.serverId, viaSocket), privateRoomTicket(server)])
        .then(([loginToken, ice, ticket]) => {
          if (seq !== connectSeq.current) return;
          let url = signalUrl(server);
          if (ticket) url += (url.includes("?") ? "&" : "?") + "ticket=" + encodeURIComponent(ticket);
          // false + an `error` event when refused
          if (!aa.connect(url, { ...(loginToken ? { loginToken } : {}), ...ice })) {
            a?.fail("refused");
            return;
          }
          // a Quick Play room whose signalling never answers: end the connect here rather than
          // at the client's handshake timeout; the `disconnected` state asks for another room
          stalled.current = false;
          if (reroute.current && a)
            window.setTimeout(() => {
              if (seq !== connectSeq.current || a.done || a.current !== "signalling") return;
              if (aa.getState().state !== "connecting") return;
              console.info("quickplay: " + (server.serverId ?? "the room") + " did not answer in " + SIGNAL_STALL_MS / 1000 + " s");
              stalled.current = true;
              aa.disconnect();
            }, SIGNAL_STALL_MS);
          // a Direct Connect stuck at its signalling (the browser port closed) or its ICE (no path
          // between this network and the server): ended here, the stage named in the note
          if (server.direct && a)
            window.setTimeout(() => {
              if (seq !== connectSeq.current || a.done || aa.getState().state !== "connecting") return;
              directStalled.current = a.current ?? "signalling";
              console.info("direct: " + server.direct?.key + " stuck at " + directStalled.current + " after " + DIRECT_STALL_MS / 1000 + " s");
              aa.disconnect();
            }, DIRECT_STALL_MS);
        })
        .catch((e) => {
          if (seq !== connectSeq.current) return;
          // the platform refused the login token: a ban in force for this room. No connect;
          // the open stage shows the reversed cell (until when and why) with the code of
          // conduct and the way to appeal
          a?.fail(e instanceof Banned ? "login_denied" : "error");
          endMove("failed");
          const grown = modeRef.current;
          const i = grown ? modeIndex(grown) : modeIndex("quick");
          dropSeat();
          if (e instanceof Banned && grown) {
            // the stage stays up and says so (Stage.tsx BanPane); Esc closes it as always
            setFinding(null);
            setBan(banText(e));
            return;
          }
          closeStage();
          showNote(String(e instanceof Error ? e.message : e), false, i);
          setFocused(i);
          window.setTimeout(() => {
            const tile = stripRef.current?.children[i] as HTMLElement | undefined;
            tile?.focus({ preventScroll: true });
            if (aa && canScene(aa.getState().state)) aa.scene(MODES[i].scene);
          }, 60);
        });
    },
    [aa, closeStage, dropSeat, showNote]
  );

  // Quick Play: with the platform's `features.quickplay`, the finding step asks it for a
  // room (retrying while every room is full, about a minute), then the usual connect;
  // otherwise (community mode) the first servers.json entry, as before. An invite link's
  // `room`: the servers.json entry with that server id, else a seat in exactly that room
  // through the platform; a room neither knows is a note under the queue
  const quickPlay = useCallback((target: JoinTarget | null = null, again: { avoid: string[]; tries: number } | null = null) => {
    const room = target && "room" in target ? target.room : null;
    // only a pick may go to another room: an invite or a friend's room is that one room
    reroute.current = target ? null : again ?? { avoid: [], tries: 0 };
    // a friend's room watched rather than played: the spectator switch for that game (connect)
    spectateNext.current = !!target && "friend" in target && target.spectate;
    const seq = ++connectSeq.current;
    const i = modeIndex("quick");
    const a = startAttempt("quick");
    // a Discord launch starts before its sign-in is through: that is the first step shown
    if (DISCORD && platformState().mode === "loading") setFinding({ phase: "signing" });
    quickPlayEnabled().then((enabled) => {
      if (seq !== connectSeq.current) return;
      const listed = room ? invitedServer(servers, room) : undefined;
      if (listed) {
        connect(listed);
        return;
      }
      if (!enabled && target) {
        a.fail("error");
        closeStage();
        showNote(t("home.note.inviteNotHere"), false, i);
        return;
      }
      if (!enabled) {
        connect(servers[0]);
        return;
      }
      setFinding({ phase: "asking", invite: !!room });
      setQuickRoom("");
      const abort = new AbortController();
      findAbort.current = abort;
      a.begin("quickplay");
      findRoom(abort.signal, (phase) => {
        if (phase.phase === "asking") a.quickTry();
        if (phase.phase === "waiting" && phase.full) a.quickNoRoom();
        if (seq === connectSeq.current) setFinding(phase);
      }, target, reroute.current?.avoid ?? [])
        .then((found) => {
          if (!found) return; // backed out
          if (seq !== connectSeq.current) {
            void giveBackSeat(found.reservationId);
            return;
          }
          findAbort.current = null;
          seat.current = found.reservationId;
          seatRoom.current = found.server.serverId ?? null;
          setQuickRoom(found.roomId);
          a.quickResult("assigned", found.region);
          playRegion.current = found.region;
          a.ok("quickplay");
          connect(found.server);
          if (found.sub) setSub(stripColors(found.sub));
          if (found.server.privateCode) setDuelWith(found.opponent ? stripColors(found.opponent) : null);
        })
        .catch((e) => {
          if (seq !== connectSeq.current) return;
          findAbort.current = null;
          const giveup = e instanceof FindFailure && e.giveup;
          a.quickResult(giveup ? "giveup" : "error");
          a.fail(giveup && e.full ? "full" : "error");
          // the reversed cell under the queue names the step that failed
          closeStage();
          showNote(e instanceof FindFailure && e.plain ? e.message : t("home.note.finding", { why: e instanceof Error ? e.message : String(e) }), false, i);
          setFocused(i);
          window.setTimeout(() => {
            const tile = stripRef.current?.children[i] as HTMLElement | undefined;
            tile?.focus({ preventScroll: true });
            if (aa && canScene(aa.getState().state)) aa.scene(MODES[i].scene);
          }, 60);
        });
    });
  }, [aa, closeStage, connect, servers, showNote, startAttempt]);

  // from the chip (home), or over a running game on one view only: "save your progress" in
  // the match summary opens the sign-up (a click the player chose; the game goes on beneath)
  const openProfile = useCallback(
    (view?: ProfileView) => {
      if (profile || loading) return;
      if (inGame ? !view : !!mode && !serversPage) return;
      setNote(null);
      setMenuOpen(false);
      setFriendsOpen(false);
      setFriendsUp(false);
      setSettingsOpen(false);
      setSettingsUp(false);
      setProfileStart(view ?? null);
      setProfile(true);
      requestAnimationFrame(() => requestAnimationFrame(() => setProfileOpen(true)));
    },
    [inGame, loading, mode, profile, serversPage]
  );

  const closeProfile = useCallback(() => {
    setProfileOpen(false);
    setProfile(false);
    setProfileStart(null);
    // back to the game when it runs (the keys are its again), else to the chip
    window.setTimeout(() => {
      if (document.body.classList.contains("is-game")) canvas?.focus({ preventScroll: true });
      else chipRef.current?.focus({ preventScroll: true });
    }, 0);
  }, [canvas]);

  // the Friends panel, from its cell on the home screen (not over a game)
  const openFriends = useCallback(() => {
    if (friendsUp || loading || inGame || (mode && !serversPage)) return;
    // the two sheets share the right edge: one at a time
    if (profile) {
      setProfileOpen(false);
      setProfile(false);
      setProfileStart(null);
    }
    setNote(null);
    setSettingsOpen(false);
    setSettingsUp(false);
    setFriendsUp(true);
    requestAnimationFrame(() => requestAnimationFrame(() => setFriendsOpen(true)));
  }, [friendsUp, inGame, loading, mode, profile, serversPage]);

  // JOIN or SPECTATE a friend (the Friends panel, the in-game menu's Friends view): their room
  // through Quick Play (the platform finds it); from inside a game this one is left first
  const joinFriend = useCallback(
    (friend: FriendInfo, spectate: boolean) => {
      const target: JoinTarget = { friend: friend.user.userId, name: friend.user.name, spectate };
      setFriendsOpen(false);
      setFriendsUp(false);
      setMenuOpen(false);
      if (aa && (real === "playing" || real === "local" || real === "connecting" || real === "syncing" || real === "fetching")) {
        leaving.current = true;
        if (real === "local") aa.stopLocal();
        else aa.disconnect();
      }
      setPendingJoin(target);
    },
    [aa, real]
  );

  const closeFriends = useCallback(() => {
    setFriendsOpen(false);
    setFriendsUp(false);
    window.setTimeout(() => friendsCellRef.current?.focus({ preventScroll: true }), 0);
  }, []);

  // MESSAGE in the Friends panel: the conversation in the chat dock, at the panel's left; on a
  // phone the panel fills the screen, so it closes first
  const openChat = useCallback((f: FriendInfo) => {
    if (window.matchMedia("(max-width: 760px)").matches) {
      setFriendsOpen(false);
      setFriendsUp(false);
    }
    dockRef.current?.open(f.user.userId);
  }, []);

  // the Settings sheet: from its cell on the home screen (the right edge, one sheet at a
  // time), or from the Esc menu in a game (the menu's place at the left)
  const openSettings = useCallback(() => {
    if (settingsUp || loading || !aa || (!inGame && mode && !serversPage)) return;
    if (profile) {
      setProfileOpen(false);
      setProfile(false);
      setProfileStart(null);
    }
    setFriendsOpen(false);
    setFriendsUp(false);
    setMenuOpen(false);
    setNote(null);
    setSettingsUp(true);
    requestAnimationFrame(() => requestAnimationFrame(() => setSettingsOpen(true)));
  }, [aa, inGame, loading, mode, profile, serversPage, settingsUp]);

  // the cycle colour, which the Profile's swatch sets (the same COLOR_*_1 as Settings' PLAYER tab)
  const colourKnown = settingsStore.get("COLOR_R_1");
  const plateColour: PlateColour | null =
    colourKnown.known && colourKnown.writable
      ? {
          rgb: [Number(colourKnown.value) || 0, Number(settingsStore.get("COLOR_G_1").value) || 0, Number(settingsStore.get("COLOR_B_1").value) || 0],
          onPick: (rgb) => {
            settingsStore.setMany([
              ["COLOR_R_1", String(rgb[0])],
              ["COLOR_G_1", String(rgb[1])],
              ["COLOR_B_1", String(rgb[2])]
            ]);
            if (aa) document.documentElement.style.setProperty("--player", playerColor(aa));
          },
          onCustom: () => {
            setSettingsStart("player");
            openSettings();
          }
        }
      : null;

  // the bell's panel: opening it asks again and reads everything sent so far; closing settles the
  // rows' NEW marks and gives the focus back to the cell when the keys closed it
  const openNews = useCallback(() => {
    setNewsUp(true);
    void refreshNews().then(markNewsSeen);
    markNewsSeen();
  }, []);
  const closeNews = useCallback((focusCell: boolean) => {
    setNewsUp(false);
    settleNews();
    if (focusCell) newsCellRef.current?.focus({ preventScroll: true });
  }, []);

  // closing saves at once; in a game Esc goes back to the menu it came from
  const closeSettings = useCallback(
    (toMenu = false) => {
      setSettingsOpen(false);
      setSettingsUp(false);
      setSettingsStart(null);
      settingsStore.saveNow();
      window.setTimeout(() => {
        if (document.body.classList.contains("is-game")) {
          if (toMenu) setMenuOpen(true);
          else canvas?.focus({ preventScroll: true });
        } else settingsCellRef.current?.focus({ preventScroll: true });
      }, 0);
    },
    [canvas, settingsStore]
  );

  const select = useCallback(
    (m: Mode, room: JoinTarget | null = null) => {
      if (mode || profile || friendsUp || loading) return;
      const i = modeIndex(m);
      if (i < 0) return; // a mode out of the queue (Ranked)
      noteAction("menu-" + m);
      // a phone or tablet goes full screen where its browser allows (fullscreen.ts; from the press)
      if ((m === "quick" || m === "browser" || m === "local" || m === "custom")) fullscreenOnPlay();
      const tile = stripRef.current?.children[i] as HTMLElement | undefined;
      if (tile) {
        // where the line sits now: the stage title travels from here (Stage.tsx)
        const r = tile.getBoundingClientRect();
        setBox({ x: r.left, y: r.top });
      }
      setNote(null);
      setSub("");
      setMode(m);
      setFocused(i);
      playScene(MODES[i].scene);
      requestAnimationFrame(() => requestAnimationFrame(() => setStageOpen(true)));

      if (m === "quick") quickPlay(room);
      // the private 1v1 sumo room: a new one, or the invite link's
      if (m === "custom") {
        track("play_pressed", { mode: "quick" });
        // the chirp of an arrival may play later without a press (alerts.ts); a new link
        // replaces one left open (the platform withdraws it)
        primeAlerts();
        if (!room) setOpenInvite(null);
        quickPlay(room && ("duel" in room || "duelRoom" in room) ? room : { duel: null });
      }
      if (m === "ranked") track("play_pressed", { mode: "ranked" });
      if (m === "local") {
        setSub(t("home.sub.local"));
        if (!aa) {
          showNote(t("home.note.notLoaded"), false, i);
          closeStage();
          return;
        }
        leaving.current = false;
        pressed();
        beginSession("training");
        track("play_pressed", { mode: "local" });
        playMode.current = "local";
        if (!aa.startLocal()) closeStage(); // the refusal arrives as an `error` event
      }
    },
    [aa, closeStage, friendsUp, loading, mode, playScene, pressed, profile, quickPlay, showNote]
  );

  useEffect(() => {
    if (!quickNext || mode) return;
    setQuickNext(false);
    select("quick");
  }, [quickNext, mode, select]);

  const collapse = useCallback(() => {
    if (!mode) return;
    noteAction("back");
    const i = modeIndex(mode);
    connectSeq.current++;
    dropSeat();
    endMove("cancelled");
    // backed out before the client had a session to end (finding a room, fetching the token):
    // the attempt ends here; with a session, its `disconnected` ends it
    const a = attempt.current;
    if (a && !a.done && !(real === "connecting" || real === "syncing" || real === "fetching" || real === "playing")) {
      if (a.current === "quickplay") a.quickResult("cancelled");
      a.fail("cancelled");
    }
    if (aa && (real === "connecting" || real === "syncing" || real === "fetching" || real === "playing")) {
      leaving.current = true;
      aa.disconnect(); // the `disconnected` state closes the stage
      return;
    }
    if (aa && real === "local") {
      leaving.current = true;
      aa.stopLocal();
      return;
    }
    closeStage();
    window.setTimeout(() => {
      const tile = stripRef.current?.children[i] as HTMLElement | undefined;
      tile?.focus({ preventScroll: true });
    }, 0);
    setFocused(i);
    playScene(MODES[i].scene);
  }, [aa, closeStage, dropSeat, endMove, mode, playScene, real]);

  // the old session ended: the seat in the target, then the usual connect. The stage shows
  // the Quick Play steps with the move's line; a failure is the usual note under Quick Play
  const followMove = useCallback(() => {
    const m = moving.current;
    if (!m) return;
    m.phase = "joining";
    document.documentElement.dataset.move = "joining";
    const seq = ++connectSeq.current;
    const abort = new AbortController();
    findAbort.current = abort;
    const line = moveLine(m.move, stripColors);
    setFinding({ phase: "asking" });
    findMoveSeat(m.move, abort.signal, m.foreign)
      .then((found) => {
        if (!found) return; // backed out
        if (seq !== connectSeq.current || moving.current !== m) {
          if (found.reservationId) void giveBackSeat(found.reservationId);
          return;
        }
        findAbort.current = null;
        seat.current = found.reservationId || null;
        setQuickRoom(found.roomId);
        playRegion.current = found.region || null;
        setFinding({ phase: "found", room: found.server.name });
        connect(found.server);
        setSub(line);
      })
      .catch((e) => {
        if (seq !== connectSeq.current) return;
        findAbort.current = null;
        endMove("failed");
        const i = modeIndex("quick");
        closeStage();
        showNote(t("home.note.moving", { why: e instanceof MoveFailure || e instanceof Error ? e.message : String(e) }), false, i);
        // Quick Play is one key away: its line has the focus
        setFocused(i);
        window.setTimeout(() => {
          const tile = stripRef.current?.children[i] as HTMLElement | undefined;
          tile?.focus({ preventScroll: true });
          if (aa && canScene(aa.getState().state)) aa.scene(MODES[i].scene);
        }, 60);
      });
  }, [aa, closeStage, connect, endMove, showNote]);

  const leaveGame = useCallback(() => {
    if (!aa) return;
    leaving.current = true;
    if (real === "local") aa.stopLocal();
    else aa.disconnect();
    setMenuOpen(false);
  }, [aa, real]);

  // the embed's way to the whole page opened its tab (the link itself does that): this room is
  // left, so the player does not hold a seat here while playing there
  const leaveForFullGame = useCallback(
    (e?: React.MouseEvent<HTMLAnchorElement>) => {
      // Discord's frame opens no tab itself: its own confirmation, then the browser
      if (DISCORD && e) {
        e.preventDefault();
        openOutside(e.currentTarget.href);
      }
      window.setTimeout(leaveGame, 0);
    },
    [leaveGame]
  );

  // the Server Browser's JOIN or SPECTATE
  const joinServer = useCallback(
    (server: ServerEntry, spectate: boolean) => {
      if (real !== "idle" && real !== "disconnected" && real !== "scene") return;
      spectateNext.current = spectate;
      noteAction(spectate ? "spectate" : "join");
      startAttempt("browser", webRtcBlocked() && !WEBSOCKET_TRANSPORT && server.lobby?.webSocket === true);
      connect(server);
    },
    [connect, real, startAttempt]
  );

  // the board's list read again (the Server Browser asks every 20 s while it is up, and on
  // REFRESH): the platform's when it keeps one, else servers.json
  const refreshServers = useCallback(() => {
    void platformServers().then((listed) => {
      if (listed) {
        setBoardList(listed);
        setServersAt(Date.now());
        return;
      }
      setBoardList(null);
      return loadServers().then(({ list, ok }) => {
        setServers(list);
        setServersOk(ok);
        setServersLoaded(true);
        setServersAt(Date.now());
      });
    });
  }, []);

  // ---- effects ------------------------------------------------------------------

  useEffect(() => {
    loadServers().then(({ list, ok }) => {
      setServers(list);
      setServersOk(ok);
      setServersLoaded(true);
      setServersAt(Date.now());
    });
    void platformServers().then((listed) => listed && setBoardList(listed));
  }, []);

  // the client is ready: start the scene at the focused tile
  useEffect(() => {
    if (phase !== "ready" || !aa || !serversLoaded || sceneStarted.current) return;
    sceneStarted.current = true;
    aa.scene(MODES[focused].scene);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [phase, aa, serversLoaded]);

  // an invite link: once the page is ready, Quick Play into that room (the player clicked
  // the link to play there; the sound starts at the first key, as always)
  useEffect(() => {
    if (!joinRoom || phase !== "ready" || !aa || !serversLoaded) return;
    setJoinRoom(null);
    window.setTimeout(() => select("quick", { room: joinRoom }), 0);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [joinRoom, phase, aa, serversLoaded]);

  // the Challenge a Friend line follows the platform's answer once it came (modes.ts; shown from the
  // first paint as last time); the lines after it move by one, the focus stays on its line
  const [, setModesVersion] = useState(0);
  const privateRoomsLine = useCallback((on: boolean) => {
    const was = MODES[focusedRef.current]?.mode;
    if (setPrivateRooms(on) < 0) return;
    setModesVersion((v) => v + 1);
    if (was) setFocused(Math.max(0, modeIndex(was)));
  }, []);
  useEffect(() => {
    if (platform.mode === "loading") return;
    privateRoomsLine(privateRoomsEnabled());
  }, [platform, privateRoomsLine]);

  // a private room's invite link: into that room once the page and the platform are ready
  useEffect(() => {
    if (!joinPrivate || phase !== "ready" || !aa || platform.mode === "loading") return;
    const code = joinPrivate;
    setJoinPrivate(null);
    if (!privateRoomsEnabled()) {
      showNote(t("connect.fail.privateEnded"), false, modeIndex("quick"));
      return;
    }
    privateRoomsLine(true);
    window.setTimeout(() => select("custom", { duel: code }), 0);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [joinPrivate, phase, aa, platform]);

  // a ?connect= link: the Server Browser, its Direct Connect pane with the address
  // and the warning; the player joins from there or not at all
  useEffect(() => {
    if (!connectLink || !DIRECT_OFFERED || phase !== "ready" || !aa || !serversLoaded || mode) return;
    window.setTimeout(() => select("browser"), 0);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connectLink, phase, aa, serversLoaded]);

  // the /play link (or the embed's PLAY pressed early, or a Discord launch): Quick Play once the
  // page is ready (a Discord launch's sign-in may still be going: Quick Play shows it as its first step)
  useEffect(() => {
    if (!playNow || phase !== "ready" || !aa || !serversLoaded) return;
    setPlayNow(false);
    window.setTimeout(() => select("quick"), 0);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [playNow, phase, aa, serversLoaded]);

  // a Discord launch goes straight into a game: only the wordmark and its load bar until Quick
  // Play opens (body.is-launch), not a flash of the menu. Over once a mode is up, the load failed,
  // or nothing was started after all (the menu is then the way)
  const [launchHold, setLaunchHold] = useState(DISCORD);
  useEffect(() => {
    if (!launchHold) return;
    if (mode || phase === "failed") return setLaunchHold(false);
    if (playNow || joinRoom) return;
    const timer = window.setTimeout(() => setLaunchHold(false), 500);
    return () => window.clearTimeout(timer);
  }, [launchHold, mode, phase, playNow, joinRoom]);

  // the old default keys (keys_cursor.cfg) become the browser's template once (keys.ts)
  useEffect(() => {
    if (phase === "ready" && aa) {
      moveOffOldDefaults(aa);
      dropMouseCamera(aa); // mouse motion no longer swings the camera (keys.ts)
      embedKeys(aa); // the embed: the arrows turn and brake, as its hints say
      embedSoundOff(aa); // the embed starts silent unless sound was turned on in it
    }
  }, [phase, aa]);

  // no password the player typed is kept, not even scrambled for this run: on a shared
  // computer the next person's /login with the same id would be answered with it, and any
  // server that asks for that id would get an answer it can try to crack
  useEffect(() => {
    if (phase === "ready" && aa) aa.setConfig("PASSWORD_STORAGE", "-1");
  }, [phase, aa]);

  useEffect(() => {
    if (phase === "failed") showNote(t("home.note.loadFailed", { error: client.loadError ?? "" }), false, 0);
  }, [phase, client.loadError, showNote]);

  // the client's transitions (API.md state machine)
  useEffect(() => {
    if (!aa || !state) return;
    // logged in: the room counts us now, the seat has done its job
    if (state.state === "syncing" || state.state === "playing") seat.current = null;
    // the connect attempt's stages: the login was accepted (syncing), in the game (playing)
    const a = attempt.current;
    if (a && !a.done && (state.state === "syncing" || state.state === "playing")) {
      a.setRoute(state.ice);
      // in over WebRTC (direct or relayed): a block remembered from an earlier visit is over
      if (state.ice?.local && state.ice.local !== "websocket") noteWebRtcWorked();
      if (state.state === "syncing" || a.current === "handshake") {
        a.ok("handshake");
        a.begin("sync");
      }
      if (state.state === "playing") a.ok("sync");
    }
    if ((state.state === "playing" || state.state === "local") && !inMatch.current) {
      inMatch.current = true;
      countMatch();
      // a network game: its ping, sampled below, goes out as play_stats when it ends
      if (state.state === "playing" && attempt.current) startGame(attempt.current.attempt, playRegion.current);
      // Training: its screen names the keys over the paused game (Training.tsx), every time
      if (state.state === "local") setTrainingUp(true);
      // the keys, the first few games in this browser (not in Training: its screen names them)
      else if (keysLineWanted()) {
        const items = hintItems(aa);
        if (items.length) {
          countKeysLine();
          setKeysLine(items);
        }
      }
      if (state.state === "local") playMode.current = "local";
      // the turn keys of player 1, for first_turn
      try {
        turnCodes.current = new Set(
          aa
            .getBindings()
            .filter((b) => b.player === 1 && (b.action === "CYCLE_TURN_LEFT" || b.action === "CYCLE_TURN_RIGHT") && b.code)
            .map((b) => b.code as string)
        );
      } catch {
        turnCodes.current = new Set();
      }
    }
    if (state.state === "playing" && moving.current?.phase === "joining") endMove("arrived");
    if (state.state === "disconnected" && moving.current?.phase === "leaving") {
      // the room sent us on: no note, the stage stays and shows the way to the new room
      inMatch.current = false;
      endGame(); // the old room's play_stats; the new room's game starts at its `playing`
      setRewardGame(false);
      setRoomServerId(null);
      leaving.current = false;
      followMove();
      return;
    }
    // the connect to the new room failed: the usual note under Quick Play, prefixed
    const failedMove = state.state === "disconnected" && moving.current?.phase === "joining";
    if (failedMove) endMove("failed");
    // a Quick Play room that could not be reached before the login (its seat still held): the
    // seat goes back as failed and, a few times, the page asks for another room, the stage up
    const wasStalled = stalled.current;
    if (state.state === "disconnected") stalled.current = false;
    // a Direct Connect that never got in: where it stopped, in plain words. Through the
    // gateway a signalling that never answered is the server's browser port; ICE that never found
    // a path is this network (our relay takes no traffic to servers that are not ours)
    const direct = state.state === "disconnected" ? roomFromRef.current?.entry.direct ?? null : null;
    const directStage = !direct || !a || a.done
      ? null
      : directStalled.current ?? (state.reason && state.reason !== "requested" ? reasonOf(state.reason, a.current ?? "signalling") : null);
    if (state.state === "disconnected") directStalled.current = null;
    const directWhy = directStage === "ice" ? t("connect.fail.directBlocked") : directStage === "signalling" && direct?.gateway ? t("connect.fail.directClosed") : null;
    if (state.state === "disconnected") {
      setRoomFrom(null);
      setMoveAsk(null);
    }
    // the signalling answered but no WebRTC path opened: this network lets neither UDP nor the
    // relay through, so the next connects (the reroute below too) go over the WebSocket. Not for a
    // Direct Connect: that one server's ports say nothing about this network's other ways out
    const iceBlocked = state.state === "disconnected" && !wasStalled && !direct && !!a && !a.done && !WEBSOCKET_TRANSPORT && !!state.reason
      && reasonOf(state.reason, a.current ?? "signalling") === "ice";
    if (iceBlocked) noteWebRtcBlocked();
    const unreached = state.state === "disconnected" && !!seat.current && !failedMove && !moving.current
      && (wasStalled || (!!state.reason && state.reason !== "requested"));
    const again = reroute.current;
    if (unreached && again && again.tries < MAX_REROUTES && seatRoom.current) {
      inMatch.current = false;
      endGame();
      setRewardGame(false);
      setGameServerId(null);
      setGameCode(null);
      if (a && !a.done) a.fail(wasStalled ? "signalling" : reasonOf(state.reason, a.current ?? "signalling"));
      // ICE that found no path is this network, not the room: the WebSocket retry may go back to it
      const avoid = iceBlocked ? again.avoid : [...again.avoid, seatRoom.current];
      console.info("quickplay: " + seatRoom.current + " could not be reached (" + (wasStalled ? "no answer" : state.reason) + "), asking for another room");
      dropSeat(true);
      leaving.current = false;
      quickPlay(null, { avoid, tries: again.tries + 1 });
      return;
    }
    if (state.state === "disconnected") {
      inMatch.current = false;
      endGame();
      if (spectatorBefore.current !== null) {
        aa.setConfig("SPECTATOR_MODE_1", spectatorBefore.current);
        spectatorBefore.current = null;
      }
      setRewardGame(false);
      setTrainingUp(false);
      setGameServerId(null);
      setGameCode(null);
      if (a && !a.done) a.fail(wasStalled ? "signalling" : directStage === "ice" ? "ice" : directStage === "signalling" ? "signalling" : state.reason === "requested" && leaving.current ? "cancelled" : reasonOf(state.reason, a.current ?? "signalling"));
      const from = mode ? modeIndex(mode) : focused;
      dropSeat(unreached); // the connection failed before the login: the seat goes back
      reroute.current = null;
      closeStage();
      leaving.current = false;
      // "requested" is the player's own leave (the menu's, or Esc out of a connect): no note
      if (wasStalled) showNote(t("connect.fail.unreachable"), false, from);
      else if (directWhy) showNote(directWhy, false, from);
      // a duel's room closing without a rematch (gPlatformRematch) says so in the page's words
      else if (state.reason && REMATCH_CLOSED.test(state.reason)) showNote(t("game.duelRoom.closed"), false, from);
      else if (state.reason && state.reason !== "requested") showNote(failedMove ? t("home.note.moving", { why: state.reason }) : state.reason, false, from);
      else setNote(null);
      setFocused(from);
      // deferred: the selector must be displayed again before it can take the focus,
      // and the scene is never restarted from inside the state handler itself
      window.setTimeout(() => {
        const tile = stripRef.current?.children[from] as HTMLElement | undefined;
        // the Console open (it stays over the home screen) keeps the focus; Esc returns it here
        if (chatOpenRef.current) consoleReturn.current = tile ?? null;
        else tile?.focus({ preventScroll: true });
        if (aa.getState().state === "disconnected") aa.scene(MODES[from].scene);
      }, 60);
    }
    if (state.state === "playing" || state.state === "local") {
      setMenuOpen(false);
      canvas?.focus({ preventScroll: true });
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [state]);

  // the handshake's steps (API.md `handshake`): the signalling answered, the data channel opened
  useEffect(() => {
    if (!aa) return;
    return aa.on("handshake", (e: AaHandshakeEvent) => {
      const a = attempt.current;
      if (!a || a.done) return;
      if (e.step === "signalling") {
        a.ok("signalling");
        a.begin("ice");
      } else if (e.step === "ice") {
        a.setRoute(aa.getState().ice);
        a.ok("ice");
        a.begin("handshake");
      }
    });
  }, [aa]);

  // the room moves us (API.md "Moves"): one line on the Quick Play stage, leave, and once
  // the session has ended (the state effect above) follow. Never from inside the event: the
  // disconnect waits for the next turn of the event loop
  // a server that is not ours moves the player to another port of its own host
  // only: that move goes at once, to the URL the page makes for it (through the gateway when the
  // game came that way); a move anywhere else is asked first (MoveAsk), and never followed alone
  const foreignTarget = useCallback((e: AaMoveEvent, from: { url: string; entry: ServerEntry }): ServerEntry => {
    const d = from.entry.direct;
    const host = signalHost(e.signalUrl, location.href);
    const url = d?.gateway && host ? (throughGateway(e.signalUrl, host) ?? e.signalUrl) : e.signalUrl;
    return {
      name: e.name || e.serverId || from.entry.name,
      url,
      description: "",
      direct: d ? { ...d, key: host ?? d.key, host: host ?? d.host, browser: null } : undefined
    };
  }, []);

  const startMove = useCallback(
    (e: AaMoveEvent, foreign: ServerEntry | null) => {
      if (!aa) return;
      moving.current = { move: e, phase: "leaving", at: performance.now(), foreign };
      document.documentElement.dataset.move = "leaving";
      document.documentElement.dataset.moveResult = "";
      seat.current = null;
      setMenuOpen(false);
      setMoveAsk(null);
      setNote(null);
      setBan(null);
      setMode("quick");
      setFocused(modeIndex("quick"));
      setStageOpen(true);
      setFinding({ phase: "asking" });
      setSub(moveLine(e, stripColors));
      window.setTimeout(() => {
        const s = aa.getState().state;
        if (moving.current?.move === e && s !== "disconnected" && s !== "idle") aa.disconnect();
      }, 0);
    },
    [aa]
  );

  // the player's answer to a server's move elsewhere: MOVE (unless the blocklist refuses the
  // target), or STAY (the game goes on where it is)
  const answerMove = useCallback(
    async (go: boolean) => {
      const ask = moveAsk?.kind === "ask" ? moveAsk : null;
      setMoveAsk(null);
      const from = roomFromRef.current;
      if (!go || !ask || !from || !aa) return;
      const now = aa.getState().state;
      if (moving.current || !(now === "playing" || now === "syncing" || now === "fetching")) return;
      if (await isBlocked(signalHost(ask.move.signalUrl, location.href))) {
        setMoveAsk({ kind: "note", text: t("connect.fail.moveBlocked"), until: Date.now() + 6000 });
        return;
      }
      startMove(ask.move, foreignTarget(ask.move, from));
    },
    [aa, foreignTarget, moveAsk, startMove]
  );

  const moveAskLine: MoveAskLine | null = !moveAsk
    ? null
    : moveAsk.kind === "note"
      ? { kind: "note", text: moveAsk.text }
      : { kind: "ask", name: stripColors(roomFrom?.entry.name || sessionName), address: addressText(moveAsk.move.signalUrl, location.href), until: moveAsk.until };

  // unanswered, the question goes: the player stays
  useEffect(() => {
    if (!moveAsk) return;
    const timer = window.setTimeout(() => setMoveAsk(null), Math.max(0, moveAsk.until - Date.now()));
    return () => window.clearTimeout(timer);
  }, [moveAsk]);

  useEffect(() => {
    if (!aa) return;
    return aa.on("move", (e: AaMoveEvent) => {
      const now = aa.getState().state;
      if (moving.current || !e || typeof e.moveId !== "string" || !(now === "playing" || now === "syncing" || now === "fetching")) return;
      const from = roomFromRef.current;
      if (from && !from.official) {
        const how = foreignMove(e, from.url);
        console.info("move: " + (from.entry.direct?.key ?? from.url) + " -> " + e.signalUrl + ": " + how);
        if (how === "refuse") return;
        if (how === "ask") {
          setMoveAsk({ kind: "ask", move: e, until: Date.now() + MOVE_ASK_MS });
          return;
        }
        startMove(e, foreignTarget(e, from));
        return;
      }
      moving.current = { move: e, phase: "leaving", at: performance.now(), foreign: null };
      document.documentElement.dataset.move = "leaving";
      document.documentElement.dataset.moveResult = "";
      seat.current = null;
      setMenuOpen(false);
      setNote(null);
      setBan(null);
      setMode("quick");
      setFocused(modeIndex("quick"));
      setStageOpen(true);
      setFinding({ phase: "asking" });
      setSub(moveLine(e, stripColors));
      window.setTimeout(() => {
        const s = aa.getState().state;
        if (moving.current?.move === e && s !== "disconnected" && s !== "idle") aa.disconnect();
      }, 0);
    });
  }, [aa]);

  // loading: the client is ready (or failed), the attract scene is first on screen
  useEffect(() => {
    if (loadReported.current || (phase !== "ready" && phase !== "failed")) return;
    loadReported.current = true;
    track("bundle_loaded", { ms: sinceNavigation(), ok: phase === "ready" });
  }, [phase]);

  useEffect(() => {
    if (sceneReported.current || !scene) return;
    sceneReported.current = true;
    track("scene_ready", { ms: sinceNavigation() });
  }, [scene]);

  // the poster (index.html): the scene's opening frame, shown while the client loads; it
  // fades into the live scene once the scene draws (or goes when the client failed), and
  // leaves the page so it can never cover the game
  useEffect(() => {
    if (!scene && phase !== "failed") return;
    const poster = document.getElementById("poster");
    if (!poster) return;
    poster.classList.add("is-gone");
    const t = window.setTimeout(() => poster.remove(), 900);
    return () => window.clearTimeout(t);
  }, [scene, phase]);

  // rounds: the API's `round` event is the page's one definition (API.md "Events"); a
  // round started while playing counts for session_end (the joined round included), its
  // end triggers the XP poll (platform/useRewards.ts)
  const playingNow = real === "playing" || real === "local";

  // ---- inside a Discord Activity (discord.ts) ----
  // the game name is the one played under, else the handle, as a sign-in in the Profile makes
  // it (Discord signs the player in before anything is shown); never the Discord name
  const discordUser = DISCORD && platform.mode === "platform" && platform.user && !platform.user.isGuest ? platform.user : null;
  const discordNamed = useRef<string | null>(null);
  useEffect(() => {
    if (!aa || !discordUser || discordNamed.current === discordUser.id) return;
    discordNamed.current = discordUser.id;
    const item = aa.getConfig("PLAYER_1");
    player.setName(signedInName(item ? configText(item.value) : "", discordUser.handle));
  }, [aa, discordUser, player]);

  // an older sign-in made the game name the account's: for a Google account the full
  // name. Once per browser a name still equal to an account name of more than one word
  // becomes the handle (one typed since then stays)
  const signedIn = platform.mode === "platform" && platform.user && !platform.user.isGuest ? platform.user : null;
  useEffect(() => {
    if (!aa || !signedIn) return;
    try {
      if (localStorage.getItem(ACCOUNT_NAME_DROPPED_KEY)) return;
      localStorage.setItem(ACCOUNT_NAME_DROPPED_KEY, "1");
    } catch {
      return;
    }
    const item = aa.getConfig("PLAYER_1");
    const current = item ? configText(item.value).trim() : "";
    const account = signedIn.name.trim();
    if (current === account && /\s/.test(account) && account !== signedIn.handle) player.setName(signedIn.handle);
  }, [aa, signedIn, player]);

  // the account Discord made has a suggested handle (as a popup sign-up's): the first time the
  // home screen is up, the Profile asks for one, once per launch. The launch's game comes first
  const [handleWanted, setHandleWanted] = useState(false);
  const handleRead = useRef(false);
  useEffect(() => {
    if (!discordUser || handleRead.current || platform.mode !== "platform") return;
    handleRead.current = true;
    getAccount(platform.apiUrl)
      .then((a) => setHandleWanted(a.needsHandle))
      .catch((e) => console.info("platform: no account (" + String(e instanceof Error ? e.message : e) + ")"));
  }, [discordUser, platform]);
  const homeFree = phase === "ready" && !launchHold && !mode && !inGame && !profile && !friendsUp && !settingsUp;
  useEffect(() => {
    if (!handleWanted || !homeFree) return;
    setHandleWanted(false);
    openProfile("handle");
  }, [handleWanted, homeFree, openProfile]);

  // what the player's Discord profile says: in a room (its name, since when) or choosing
  const playingSince = useRef<number | null>(null);
  useEffect(() => {
    if (!DISCORD || !discordUser) return;
    if (real === "playing") {
      playingSince.current ??= Date.now();
      discordPresence(t("home.discord.presencePlaying"), sessionName ? stripColors(sessionName) : null, playingSince.current);
    } else if (real !== "connecting" && real !== "syncing") {
      playingSince.current = null;
      discordPresence(t("home.discord.presenceChoosing"), null, null);
    }
  }, [real, sessionName, discordUser]);

  // a phone running hot (Discord's thermal state, serious or worse): the lowest graphics tier
  // until it cools; the tier the player had comes back then
  const tierBeforeHeat = useRef<string | null>(null);
  useEffect(() => {
    if (!aa || !DISCORD) return;
    const hot = discord.thermal >= 2;
    if (hot && tierBeforeHeat.current === null) {
      tierBeforeHeat.current = aa.getConfig("BROWSER_QUALITY")?.value ?? "0";
      aa.setConfig("BROWSER_QUALITY", "1");
      console.info("discord: the phone is hot, graphics at Low");
    } else if (!hot && tierBeforeHeat.current !== null) {
      aa.setConfig("BROWSER_QUALITY", tierBeforeHeat.current);
      tierBeforeHeat.current = null;
    }
  }, [aa, discord.thermal]);
  useEffect(() => {
    const fresh = rounds.started - roundsCounted.current;
    roundsCounted.current = rounds.started;
    for (let i = 0; i < fresh; i++) countRound();
    document.documentElement.dataset.rounds = String(rounds.started);
    document.documentElement.dataset.roundsEnded = String(rounds.ended);
  }, [rounds]);

  // the network game's ping for play_stats: a reading soon after the start, then every few
  // seconds (getState() is cheap; API.md says to poll it)
  const networkGame = real === "playing";
  useEffect(() => {
    if (!aa || !networkGame) return;
    const read = () => {
      const s = aa.getState();
      sampleGame(s.ping, s.fps);
    };
    const first = window.setTimeout(read, 1000);
    const every = window.setInterval(read, PING_SAMPLE_MS);
    return () => {
      window.clearTimeout(first);
      window.clearInterval(every);
    };
  }, [aa, networkGame]);

  // an API call was refused: the line under the tile you are on
  useEffect(() => {
    if (!error || inGame) return;
    showNote(error.text, false, mode ? modeIndex(mode) : focused);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [error]);

  // the game's own menu key (menuOpen) toggles the page's menu while a game runs (not
  // while the Profile is open over it: there Esc steps back through the Profile). Inside
  // the open menu the keys are the page's, so a press reaches here only from the game
  // (the focus on the canvas, e.g. after a click on it). A match summary on screen takes
  // the first press (it closes); the next opens the menu
  const menuKey = useRef<() => void>(() => {});
  menuKey.current = () => {
    if (!inGame || profile || authUp || performance.now() - profileEscAt.current < 500) return;
    if (settingsUp) return closeSettings(true);
    if (summary && !menuOpen) return rewards.closeSummary();
    setMenuOpen((open) => !open);
  };
  useEffect(() => {
    if (menuPresses > 0) menuKey.current();
  }, [menuPresses]);

  // Esc in full screen is the menu key too (fullscreen.ts escapeInFullscreen): locked to the page
  // where the browser allows it, where the client still leaves it to the browser (sdl_shim.cpp
  // BrowserKey), so the page presses the menu key for it; where it left full screen anyway, the
  // menu opens as it went
  const escLeft = useRef<() => void>(() => {});
  escLeft.current = () => {
    if (menuOpen || profile || authUp || settingsUp || summary || trainingUp) return;
    setMenuOpen(true);
  };
  const escLocked = useRef<() => void>(() => {});
  escLocked.current = () => {
    if (!chatOpen && !trainingUp) menuKey.current();
  };
  useEffect(() => {
    if (!inGame || EMBED || DISCORD) return;
    return escapeInFullscreen(
      () => escLeft.current(),
      () => escLocked.current()
    );
  }, [inGame]);

  // the menu opens on its home view with the focus inside (the keys become the page's);
  // closed, the focus goes back to the canvas and the keys to the game
  const menuWasOpen = useRef(false);
  useEffect(() => {
    if (menuOpen) menuRef.current?.reset();
    else if (menuWasOpen.current && document.body.classList.contains("is-game") && !profile) canvas?.focus({ preventScroll: true });
    menuWasOpen.current = menuOpen;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [menuOpen]);

  // reports from the player card go to the room's /report while this game runs (or to
  // POST /v1/reports when the card knows the player's gid; platform/reports.ts)
  // On a server that is not ours a report goes to POST /v1/direct/reports with the
  // server's address and the chat lines this page saw there (chatLines: set once `chat` is made)
  const chatLines = useRef<ChatEntry[]>([]);
  const foreignReport = !!roomFrom && !roomFrom.official && session === "playing" && directReportsOffered();
  const canReport = foreignReport || (!!roomServerId && session === "playing" && platform.mode === "platform" && reportsOffered());
  const report = useCallback(
    async (target: ReportTarget, reason: ReasonId, note: string): Promise<ReportResult> => {
      const from = roomFromRef.current;
      if (from && !from.official) {
        const code = REASONS.find((r) => r.id === reason)?.code ?? "other";
        const address = from.entry.direct?.key ?? from.url;
        // the lines' clock: performance.now() when each came, as Unix seconds
        const offset = Date.now() / 1000 - performance.now() / 1000;
        const lines: DirectLine[] = chatLines.current
          .filter((e) => (e.kind === "say" || e.kind === "team" || e.kind === "whisper") && !e.dm)
          // what was said, not the page's stars (filterEntry keeps it as raw)
          .map((e) => ({ name: e.from?.name ?? "", text: e.raw ?? e.text, at: Math.round(offset + e.at / 1000) }));
        const sent = await sendDirectReport(address, target.name, code, note.replace(/\s+/g, " ").trim().slice(0, NOTE_MAX), lines);
        if (sent === "sent") return { ok: true, text: t("game.report.sentDirect") };
        if (sent === "limited") return { ok: false, text: t("game.report.tooMany"), limited: true };
        return { ok: false, text: sent === "unavailable" ? t("game.report.directNotTaken") : t("game.report.failed"), limited: false };
      }
      const room: RoomLine = {
        say: (line) => !!aa && aa.chat(line),
        listen: (fn) => (aa ? aa.on("console", (e: AaConsoleEvent) => fn(e.text)) : () => {})
      };
      return sendReport(target, reason, note, roomServerId, room);
    },
    [aa, roomServerId]
  );

  // the game's CHAT key (chatOpen) opens the chat overlay's line, with the prefill of a
  // template instant chat; not while the menu or the Profile has the left of the screen.
  // The first time this browser opens it in an official room (a server with platform login,
  // like rewardGame), the code of conduct in one line for 10 s too; remembered as a bare
  // "seen" flag (components/Docs.tsx), so the line says what applies before anyone types.
  useEffect(() => {
    if (chatPresses === 0 || !inGame || menuOpen || profile) return;
    chatRef.current?.open(chatPrefill);
    if (!rewardGame || conductLineSeen()) return;
    markConductLineSeen();
    setConductLine(true);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [chatPresses]);

  // the Console: the chat panel on its raw log, from the game's CONSOLE_INPUT key
  // (consoleOpen), or ` on the home screen (not in the Esc menu). In a game not while the
  // menu or the Profile has the left of the screen; out of one only over the queue itself
  // (no stage, no Profile), where the focus goes back when it closes
  const openConsole = useCallback(() => {
    if (loading || !aa || profile) return;
    if (inGame ? menuOpen : !!mode) return;
    if (!inGame && !chatOpenRef.current) {
      const el = document.activeElement as HTMLElement | null;
      consoleReturn.current = el && el !== document.body ? el : null;
    }
    chatRef.current?.openConsole();
  }, [aa, inGame, loading, menuOpen, mode, profile]);

  useEffect(() => {
    if (consolePresses > 0) openConsole();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [consolePresses]);

  // the log (chat/log.ts): every line the game printed, this session and the one before
  const log = useLog();

  // what the Console line runs: plain text is a setting or a console command, run here at
  // the owner's level exactly as typed (aa.console); /... goes to the room as typed
  // (aa.chat: the room's own commands and spam limits, as on the desktop). The answers are
  // the game's own lines; the page only says when it could not run the line at all
  const runConsole = useCallback(
    (text: string) => {
      logInput(text);
      noteLogin(text);
      if (!aa) {
        logNote(t("home.console.notLoaded"));
        return;
      }
      if (text.startsWith("/")) {
        if (aa.chat(text)) return;
        const now = aa.getState().state;
        logNote(
          now === "local"
            ? t("home.console.trainingNoRoom")
            : now === "playing"
              ? t("home.console.waitRound")
              : t("home.console.joinFirst")
        );
        return;
      }
      if (!aa.console(text)) logNote(t("home.console.refused"));
    },
    [aa]
  );

  // the setting names the Console line completes, and a setting's current value (its ghost)
  const settings = useMemo<Settings | null>(
    () =>
      aa
        ? {
            names: (prefix) => {
              try {
                return aa.listConfig(prefix).map((c) => c.name).sort();
              } catch {
                return [];
              }
            },
            value: (name) => {
              try {
                return aa.getConfig(name)?.value ?? null;
              } catch {
                return null;
              }
            }
          }
        : null,
    [aa]
  );

  // the room's lines (chat/useChat.ts) while a game runs: chat, deaths, rounds, who came and went
  const playersNow = players.length ? players : state?.players ?? [];

  // in a platform room's game: the room and this page's own player there (room_joined), which
  // the room's roster names too, so the platform knows where its page players play (a desktop
  // browser, a phone, the X embed); once per connection (net ids are per session), sent now
  const localId = real === "playing" && gameServerId ? playersNow.find((p) => p.local)?.id : undefined;
  const joinedSent = useRef("");
  useEffect(() => {
    if (localId == null || !gameServerId) {
      if (real !== "playing") joinedSent.current = "";
      return;
    }
    const key = gameServerId + " " + localId;
    if (joinedSent.current === key) return;
    joinedSent.current = key;
    track("room_joined", { server: gameServerId, netId: localId });
    flush(false);
  }, [real, gameServerId, localId]);

  // who the official room's players are: the accounts or guests its
  // server verified, by network id (platform/friends.ts), for the card's ADD FRIEND and MUTE,
  // /f and /mute, and to leave a muted player's chat out in every room
  const socialRoom = roomServerId && real === "playing" && (friendsLive || mutesOn(platform)) ? roomServerId : null;
  const others = useMemo(() => playersNow.filter((p) => !p.bot && !isMe(p)).map((p) => p.id), [playersNow]);
  const roomPlayers = useRoomPlayers(socialRoom, others);
  const mutedUsers = useMutes();
  const mutedIds = useMemo(() => {
    const ids = new Set<number>();
    for (const [id, p] of roomPlayers.byNetId) if (mutedUsers.has(p.user.userId)) ids.add(id);
    return ids;
  }, [roomPlayers.byNetId, mutedUsers]);

  // a friend request for a player of this room (by network id: the account its server
  // verified) or a typed handle; the line the chat or the card says
  const askFor = useCallback(
    async (typed: string, p: AaPlayer | null): Promise<string> => {
      if (platform.mode !== "platform" || !friendsOn(platform)) return t("home.social.friendsOff");
      if (!friendsAccount(platform)) return t("home.social.needAccount");
      const known = p ? roomPlayers.byNetId.get(p.id) : undefined;
      const name = p?.name ?? typed;
      if (p && !known) return t("home.social.noAccount", { name });
      try {
        const answer = await askFriend(platform.apiUrl, known && socialRoom ? { serverId: socialRoom, netId: p!.id } : { handle: typed.replace(/^@/, "") });
        roomPlayers.refresh();
        void refreshFriends().catch(() => {});
        return answer.state === "friends" ? t("home.social.friendsNow", { name }) : t("home.social.requestSent", { name });
      } catch (e) {
        return socialText(e, name);
      }
    },
    [platform, roomPlayers, socialRoom]
  );

  // a mute through the platform for a verified player; null when it cannot (the chat then
  // mutes by name for this room)
  const muteFor = useCallback(
    async (p: AaPlayer, on: boolean): Promise<string | null> => {
      const known = roomPlayers.byNetId.get(p.id);
      if (!mutesOn(platform) || !known) return null;
      try {
        await setMute(known.user.userId, on);
        roomPlayers.refresh();
        return on ? t("home.social.mutedEverywhere", { name: p.name }) : t("home.social.unmuted", { name: p.name });
      } catch (e) {
        return t("home.social.notMuted", { why: socialText(e, p.name) });
      }
    },
    [platform, roomPlayers]
  );

  // a direct message to the friend of a typed name or handle: "" when sent (the
  // stream brings it back as a line), the reason when not, null when no friend is called that
  const messageFriend = useCallback(
    async (typed: string, text: string): Promise<string | null> => {
      if (platform.mode !== "platform" || !friendsAccount(platform)) return null;
      const want = typed.trim().replace(/^@/, "").replace(/_/g, " ").toLowerCase();
      const friend = friendsList?.friends.find((f) => f.user.name.toLowerCase() === want || f.user.handle.toLowerCase() === want);
      if (!friend) return null;
      try {
        await sendMessage(platform.apiUrl, friend.user.userId, text);
        return "";
      } catch (e) {
        return t("home.social.notSent", { why: messageText(e, friend.user.name) });
      }
    },
    [platform, friendsList]
  );

  const messageUser = useCallback(
    async (userId: string, text: string): Promise<string> => {
      if (platform.mode !== "platform") return t("home.social.noPlatform");
      try {
        await sendMessage(platform.apiUrl, userId, text);
        return "";
      } catch (e) {
        return t("home.social.notSent", { why: messageText(e) });
      }
    },
    [platform]
  );

  const social = useMemo<ChatSocial | null>(
    () => (socialRoom || friendsAccount(platform) ? { mute: muteFor, friend: askFor, message: messageFriend, messageTo: messageUser } : null),
    [socialRoom, platform, muteFor, askFor, messageFriend, messageUser]
  );
  // a server that is not ours: its lines through the page's own chat filter
  const foreignRoom = !!roomFrom && !roomFrom.official;
  const chat = useChat(aa, playingNow, playersNow, chatUtf8 && real === "playing", social, foreignRoom);
  chatLines.current = chat.entries;
  const heard = audible(chat.entries, chat.mutes, mutedIds);
  lastChatId.current = heard.length ? heard[heard.length - 1].id : 0;
  const chatUnread = heard.some((e) => e.id > chatSeen && (e.kind === "say" || e.kind === "team" || e.kind === "whisper") && !e.from?.local);

  // a direct message, either way, while a game runs: a whisper line in the chat overlay, with the
  // friend's room when they play elsewhere (their presence)
  const dmContext = useRef({ friends: friendsList, me: "", myName: "", room: gameServerId, add: chat.addDirect, on: playingNow });
  dmContext.current = {
    friends: friendsList,
    me: platform.mode === "platform" && platform.user ? platform.user.id : "",
    myName: player.name,
    room: gameServerId,
    add: chat.addDirect,
    on: playingNow
  };
  useEffect(
    () =>
      onMessage((m) => {
        const c = dmContext.current;
        if (!c.on || !c.me) return;
        const mine = m.from === c.me;
        const other = mine ? m.to : m.from;
        const friend = c.friends?.friends.find((f) => f.user.userId === other);
        const room = friend?.presence?.room;
        c.add(directEntry(m.text, mine, c.myName, friend?.user.name ?? t("home.social.aFriend"), other, room && room.serverId !== c.room ? room.name : null, performance.now()));
      }),
    []
  );

  // the player card's view of the same (GameMenu.tsx): MUTE goes through the chat's mute
  // (the platform when it can, else by name for this room)
  // ---- duels: a challenge from a player's card, answered over the game ----
  // the platform is asked about duels while a network game runs (a private room's too: a
  // challenge there is answered with the same panel, though none is made from one), and on the
  // home screen for a signed-in player (their presence says "menu"), where a friend's challenge
  // from the Friends panel comes up at the lower left and theirs is made
  const onMenu = friendsAccount(platform) && !finding && (real === "idle" || real === "disconnected" || real === "scene");
  const duelsLive = (real === "playing" || onMenu || !!openInvite) && privateRoomsEnabled();
  const duels = useDuels(duelsLive);
  const myUserId = platform.mode === "platform" ? platform.user?.id ?? null : null;
  // duels this page already acted on (accepted, cancelled, told): their later states are not news
  const duelsDone = useRef(new Set<string>());
  const [duelNote, setDuelNote] = useState<string | null>(null);
  const [duelBusy, setDuelBusy] = useState(false);
  const [duelMoving, setDuelMoving] = useState<string | null>(null);
  useEffect(() => {
    if (!duelNote) return;
    const timer = window.setTimeout(() => setDuelNote(null), 6000);
    return () => window.clearTimeout(timer);
  }, [duelNote]);
  useEffect(() => {
    if (real !== "playing") setDuelMoving(null);
  }, [real]);
  const incomingDuel = duelsLive ? duels.incoming.find((d) => d.state === "pending" && !duelsDone.current.has(d.code)) ?? null : null;
  // the challenges this page made from a card, with the challenged player's name as the game
  // shows it: my newest duel is one of them, not a link (an accepted link names its opener too,
  // and its creator is already in its room)
  const myChallenges = useRef(new Map<string, string>());
  // the game's own PLAYER_1 at the moment it is sent (the page's copy may not be read yet)
  useEffect(() => setDuelName(() => {
    const item = aa?.getConfig("PLAYER_1");
    return stripColors(item ? configText(item.value) : player.name);
  }), [aa, player.name]);
  // the name the room shows for me (what the other player sees), else the page's
  const myGameName = stripColors(playersNow.find((p) => isMe(p))?.name ?? player.name);
  const myChallenge = duelsLive && duels.outgoing && duels.outgoing.from.userId === myUserId && myChallenges.current.has(duels.outgoing.code) ? duels.outgoing : null;

  // into the duel's room: this game is left first (as a friend's JOIN), then the 1v1 line's
  // connecting screen names the room and the opponent
  const goToDuel = useCallback(
    (roomCode: string, opponent: string | null) => {
      setMenuOpen(false);
      setDuelMoving(opponent ?? "");
      if (aa && (real === "playing" || real === "connecting" || real === "syncing" || real === "fetching")) {
        leaving.current = true;
        aa.disconnect();
      }
      // from the home screen: what is up over it goes first (the join waits for the bare menu)
      if (!inGame) {
        if (profile) closeProfile();
        if (friendsUp) closeFriends();
        if (settingsUp) closeSettings();
        if (mode) collapse();
      }
      setPendingJoin({ duelRoom: roomCode, opponent });
    },
    [aa, real, inGame, profile, friendsUp, settingsUp, mode, closeProfile, closeFriends, closeSettings, collapse]
  );

  // a duel room's QUICK PLAY: this room is left, then Quick Play as from the home screen
  const leaveToQuickPlay = useCallback(() => {
    setMenuOpen(false);
    if (aa && (real === "playing" || real === "connecting" || real === "syncing" || real === "fetching")) {
      leaving.current = true;
      aa.disconnect();
    }
    setPendingJoin({ quick: true });
  }, [aa, real]);

  // my challenge answered: accepted, both go; declined or unanswered, a line says so
  useEffect(() => {
    const d = myChallenge;
    if (!d || d.state === "pending" || duelsDone.current.has(d.code)) return;
    duelsDone.current.add(d.code);
    const name = myChallenges.current.get(d.code) ?? d.to?.name ?? "";
    if (d.state === "accepted" && d.room) goToDuel(d.room.code, name);
    else if (d.state === "declined") setDuelNote(t("game.duel.declined", { name }));
    else if (d.state === "expired") setDuelNote(t("game.duel.expired", { name }));
  }, [myChallenge, goToDuel]);

  // my challenge still waiting when this page stops watching (Quick Play from the menu, a game
  // left): withdrawn, so it neither hangs on the other side until it lapses nor counts as unanswered
  // (declared before the ref's update below: it reads the last pending one)
  const pendingMine = useRef<string | null>(null);
  useEffect(() => {
    const code = pendingMine.current;
    if (duelsLive || !code || platform.mode !== "platform") return;
    pendingMine.current = null;
    duelsDone.current.add(code);
    void cancelDuel(platform.apiUrl, code).catch(() => {});
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [duelsLive]);
  useEffect(() => {
    pendingMine.current = myChallenge && myChallenge.state === "pending" && !duelsDone.current.has(myChallenge.code) ? myChallenge.code : null;
  }, [myChallenge]);

  // ---- my invite link left open while I play: someone opens it, I say GO or NOT NOW ----
  const inviteDuel = openInvite && duels.outgoing && duels.outgoing.code === openInvite.code ? duels.outgoing : null;
  // the arrivals already answered here (their arrivedAt): NOT NOW, GO, or announced as ended
  const arrivalsDone = useRef(new Set<number>());
  const arrival = inviteDuel && inviteDuel.state === "arrived" && !arrivalsDone.current.has(inviteDuel.arrivedAt ?? 0) ? inviteDuel : null;
  // announced once per arrival: the chirp, and the title while the tab is in the background
  const announced = useRef(0);
  useEffect(() => {
    if (!arrival) return;
    const at = arrival.arrivedAt ?? 0;
    if (announced.current === at) return;
    announced.current = at;
    alertArrival();
  }, [arrival]);
  // an arrival over without my answer (they backed out, or a minute passed), the link over, or another one made
  const lastArrival = useRef<{ at: number; name: string } | null>(null);
  useEffect(() => {
    if (!openInvite) return;
    if (arrival) {
      lastArrival.current = { at: arrival.arrivedAt ?? 0, name: arrival.to?.name ?? "" };
      return;
    }
    stopFlash();
    const was = lastArrival.current;
    if (was && inviteDuel?.state === "pending" && !arrivalsDone.current.has(was.at)) {
      arrivalsDone.current.add(was.at);
      lastArrival.current = null;
      if (inviteDuel.arrivalEnd === "left") setDuelNote(t("game.duel.opponentLeft", { name: was.name }));
      else if (inviteDuel.arrivalEnd === "no_answer") setDuelNote(t("game.duel.arrivalLapsed", { name: was.name }));
    }
    const other = duels.outgoing && duels.outgoing.code !== openInvite.code;
    if (other || (inviteDuel && (inviteDuel.state === "expired" || inviteDuel.state === "cancelled" || inviteDuel.state === "declined"))) {
      if (inviteDuel?.state === "expired") setDuelNote(t("game.duel.inviteExpired"));
      setOpenInvite(null);
    }
    // its time is up with no word from the platform (the page asleep, the network gone)
    if (!inviteDuel && Date.now() > openInvite.until + 5000) setOpenInvite(null);
  }, [openInvite, arrival, inviteDuel, duels.outgoing]);
  useEffect(() => {
    if (!inviteCopied) return;
    const timer = window.setTimeout(() => setInviteCopied(0), 2000);
    return () => window.clearTimeout(timer);
  }, [inviteCopied]);

  const goArrival = useCallback(async () => {
    const d = arrival;
    if (!d || duelBusy || platform.mode !== "platform") return;
    const name = d.to?.name ?? "";
    setDuelBusy(true);
    try {
      const view = await goDuel(platform.apiUrl, d.code);
      if (!view.room) throw new Error("no room");
      arrivalsDone.current.add(d.arrivedAt ?? 0);
      duelsDone.current.add(d.code);
      stopFlash();
      setOpenInvite(null);
      goToDuel(view.room.code, name);
    } catch (e) {
      if (e instanceof PlatformError && e.code === "opponent_left") {
        arrivalsDone.current.add(d.arrivedAt ?? 0);
        setDuelNote(t("game.duel.opponentLeft", { name }));
      } else setDuelNote(duelFailText(e));
      nudgeDuels();
    } finally {
      setDuelBusy(false);
    }
  }, [arrival, duelBusy, platform, goToDuel]);

  const notNowArrival = useCallback(() => {
    const d = arrival;
    if (!d || platform.mode !== "platform") return;
    arrivalsDone.current.add(d.arrivedAt ?? 0);
    lastArrival.current = null;
    stopFlash();
    setDuelNote(t("game.duel.toldNotNow", { name: d.to?.name ?? "" }));
    nudgeDuels();
    void laterDuel(platform.apiUrl, d.code).catch(() => {});
  }, [arrival, platform]);

  const copyInvite = useCallback(() => {
    if (!openInvite) return;
    void copyText(openInvite.link).then((ok) => ok && setInviteCopied(Date.now()));
  }, [openInvite]);

  const cancelInvite = useCallback(() => {
    if (!openInvite || platform.mode !== "platform") return;
    const code = openInvite.code;
    duelsDone.current.add(code);
    setOpenInvite(null);
    stopFlash();
    nudgeDuels();
    void cancelDuel(platform.apiUrl, code).catch(() => {});
  }, [openInvite, platform]);

  // a challenge to me withdrawn or timed out while its panel was up: said once
  const shownIncoming = useRef<{ code: string; name: string } | null>(null);
  useEffect(() => {
    const was = shownIncoming.current;
    shownIncoming.current = incomingDuel ? { code: incomingDuel.code, name: incomingDuel.from.name } : null;
    if (was && !incomingDuel && !duelsDone.current.has(was.code)) {
      duelsDone.current.add(was.code);
      setDuelNote(was.name ? t("game.duel.endedFrom", { name: was.name }) : t("game.duel.ended"));
    }
  }, [incomingDuel]);

  const acceptIncoming = useCallback(async () => {
    const d = incomingDuel;
    if (!d || duelBusy || platform.mode !== "platform") return;
    setDuelBusy(true);
    try {
      const answer = await acceptDuel(platform.apiUrl, d.code, await regionLatencies(1500), myGameName);
      duelsDone.current.add(d.code);
      // a challenge's accept always claims its room (only a link's opener arrives without one)
      if (!answer.room) throw new Error("no room");
      goToDuel(answer.room.code, d.from.name);
    } catch (e) {
      setDuelNote(duelFailText(e));
    } finally {
      setDuelBusy(false);
    }
  }, [incomingDuel, duelBusy, platform, goToDuel, myGameName]);

  const declineIncoming = useCallback(() => {
    const d = incomingDuel;
    if (!d || platform.mode !== "platform") return;
    duelsDone.current.add(d.code);
    nudgeDuels();
    void declineDuel(platform.apiUrl, d.code).catch(() => {});
  }, [incomingDuel, platform]);

  const cancelChallenge = useCallback(async (): Promise<string> => {
    const d = myChallenge;
    if (!d || platform.mode !== "platform") return "";
    duelsDone.current.add(d.code);
    try {
      await cancelDuel(platform.apiUrl, d.code);
    } catch {
      /* it ended anyway */
    }
    nudgeDuels();
    return t("game.duel.withdrawn", { name: myChallenges.current.get(d.code) ?? d.to?.name ?? "" });
  }, [myChallenge, platform]);

  // my say over challenges (the switch, who I blocked), read once the menu is up in a game
  const duelSettings = useDuelSettings(duelsLive && menuOpen);

  // the Esc menu's two quiet acts under a challenge: no more from them, or from anyone (this one
  // declined either way, by the platform)
  const blockIncoming = useCallback(async (): Promise<string> => {
    const d = incomingDuel;
    if (!d) return "";
    duelsDone.current.add(d.code);
    try {
      await blockChallenges(d.from.userId, d.from.name);
      return t("game.duel.blocked", { name: d.from.name });
    } catch {
      return t("game.duel.failedSetting");
    }
  }, [incomingDuel]);

  const challengesOff = useCallback(async (): Promise<string> => {
    const d = incomingDuel;
    if (d) duelsDone.current.add(d.code);
    try {
      await setChallenges(false);
      return t("game.duel.off");
    } catch {
      return t("game.duel.failedSetting");
    }
  }, [incomingDuel]);

  // the card's CHALLENGE TO 1V1: another browser player the room verified, from an official room
  // (not a private one: that is a duel already); BLOCK CHALLENGES beside it
  const cardDuel = useMemo<CardSocial["duel"]>(() => {
    if (!duelsLive || privateCode || !socialRoom || platform.mode !== "platform") return null;
    const apiUrl = platform.apiUrl;
    const server = socialRoom;
    return {
      can: (p) => roomPlayers.byNetId.get(p.id)?.browser === true,
      sent: (p) => {
        const known = roomPlayers.byNetId.get(p.id);
        return !!known && !!myChallenge && myChallenge.state === "pending" && myChallenge.to?.userId === known.user.userId;
      },
      onChallenge: async (p) => {
        try {
          const made = await createDuel(apiUrl, await regionLatencies(1500), { serverId: server, netId: p.id }, myGameName);
          myChallenges.current.set(made.code, stripColors(p.name));
          nudgeDuels();
          return t("game.duel.sent", { name: p.name });
        } catch (e) {
          return duelFailText(e);
        }
      },
      onCancel: cancelChallenge,
      blocked: (p) => {
        const known = roomPlayers.byNetId.get(p.id);
        return !!known && challengesBlocked(duelSettings, known.user.userId);
      },
      onBlock: async (p, on) => {
        const known = roomPlayers.byNetId.get(p.id);
        if (!known) return t("game.duel.failedSetting");
        try {
          if (on) await blockChallenges(known.user.userId, stripColors(p.name));
          else await unblockChallenges(known.user.userId);
          return t(on ? "game.duel.blocked" : "game.duel.unblocked", { name: stripColors(p.name) });
        } catch {
          return t("game.duel.failedSetting");
        }
      }
    };
  }, [duelsLive, privateCode, socialRoom, platform, roomPlayers.byNetId, myChallenge, cancelChallenge, myGameName, duelSettings]);

  // the Friends card's CHALLENGE TO 1V1 (Friends.tsx): a friend whose page hears it now (their
  // presence's challengeable), no room shared; the answer comes as for a card's
  const friendDuel = useMemo<FriendDuel | null>(() => {
    if (!duelsLive || !onMenu || platform.mode !== "platform") return null;
    const apiUrl = platform.apiUrl;
    return {
      can: (f) => !!f.presence?.challengeable,
      sent: (f) => !!myChallenge && myChallenge.state === "pending" && myChallenge.to?.userId === f.user.userId,
      onChallenge: async (f) => {
        try {
          const made = await createDuel(apiUrl, await regionLatencies(1500), { friendId: f.user.userId }, myGameName);
          myChallenges.current.set(made.code, f.user.name);
          nudgeDuels();
          return t("game.duel.sent", { name: f.user.name });
        } catch (e) {
          return duelFailText(e);
        }
      },
      onCancel: cancelChallenge
    };
  }, [duelsLive, onMenu, platform, myChallenge, myGameName, cancelChallenge]);

  // the challenger's swatch: their cycle in this room, by the name the game shows (none when
  // the room does not list them)
  const challenger = incomingDuel ? playersNow.find((p) => !p.local && !p.bot && stripColors(p.name).trim() === incomingDuel.from.name.trim()) : undefined;
  const realDuelLine: DuelLine | null = duelMoving !== null
    ? { kind: "moving", opponent: duelMoving }
    : incomingDuel
      ? { kind: "incoming", duel: incomingDuel, busy: duelBusy, color: challenger ? swatch(challenger) : undefined }
      : arrival
        ? { kind: "arrived", duel: arrival, name: arrival.to?.name ?? "", busy: duelBusy }
        : myChallenge && myChallenge.state === "pending" && !duelsDone.current.has(myChallenge.code)
          ? { kind: "waiting", duel: myChallenge, name: myChallenges.current.get(myChallenge.code) ?? myChallenge.to?.name ?? "" }
          : duelNote
            ? { kind: "note", text: duelNote }
            : openInvite
              ? { kind: "invite", until: openInvite.until, copied: !!inviteCopied }
              : null;
  const duelLine = realDuelLine;
  // the home screen's panel (no game holds the keys: its acts are in the Tab order)
  const homeDuel =
    !loading && !inGame && duelLine ? (
      <DuelPanel
        home
        line={duelLine}
        onAccept={() => (duelLine.kind === "arrived" ? void goArrival() : void acceptIncoming())}
        onDecline={() => (duelLine.kind === "arrived" ? notNowArrival() : declineIncoming())}
        onCancel={() => (duelLine.kind === "invite" ? cancelInvite() : void cancelChallenge())}
        onCopy={() => copyInvite()}
      />
    ) : null;
  // the connecting screen covers the home screen until the room starts a round (a Quick Play into a
  // round already running waits for its end): an arrival, a challenge or my open invite stands over
  // it at the same corner (on the way to the duel itself the screen says it already)
  const duelOverStage = !!homeDuel && stageOpen && (mode === "quick" || mode === "custom") && duelLine?.kind !== "moving";
  const menuDuel = incomingDuel
    ? {
        from: incomingDuel.from.name,
        color: challenger ? swatch(challenger) : undefined,
        until: msOf(incomingDuel.expiresAt),
        busy: duelBusy,
        onAccept: () => void acceptIncoming(),
        onDecline: declineIncoming,
        onBlock: blockIncoming,
        onOff: challengesOff
      }
    : arrival
      ? {
          kind: "arrived" as const,
          from: arrival.to?.name ?? "",
          until: msOf(arrival.arrivedAt ?? arrival.createdAt) + DUEL_ARRIVE_MS,
          busy: duelBusy,
          onAccept: () => {
            setMenuOpen(false);
            void goArrival();
          },
          onDecline: notNowArrival,
          onBlock: async () => "",
          onOff: async () => ""
        }
      : null;

  const cardSocial = useMemo<CardSocial>(
    () => ({
      friends: friendsLive && !!socialRoom,
      who: (id) => roomPlayers.byNetId.get(id),
      muted: (p) => {
        const known = roomPlayers.byNetId.get(p.id);
        return (!!known && mutedUsers.has(known.user.userId)) || chat.mutes.has(p.name.trim().toLowerCase());
      },
      onFriend: (p) => askFor(p.name, p),
      onMute: (p, on) => chat.mute(p, on),
      duel: cardDuel
    }),
    [friendsLive, socialRoom, roomPlayers.byNetId, mutedUsers, askFor, chat, cardDuel]
  );

  // the overlay takes the game console's place: the game's own scrolling lines (TEXT_OUT)
  // are off while a game runs and back to what they were after it (the scene keeps them
  // off itself; client.html, which shares the settings, shows them)
  // the compact view (the embed, a phone, a small window: compact.ts): the chat at rest says
  // less, and the game's HUD keeps the speed, rubber and brake meters, larger and spread out
  const compact = useCompact();
  // the page's own meters (the rail and the danger edge) where the client can tell it its cycle's
  // (the HUD style MODERN, settings/hudStyle.ts: on every device; CLASSIC keeps the game's HUD)
  const hudStyle = useHudStyle();
  const pageMeters = hudStyle === "modern" && pageMetersAvailable(aa);
  // a touch device's own controls (TouchControls): the turns and the menu, with a client that turns
  const touch = touchPrimary() && !!aa && typeof aa.turn === "function";
  // the touch buttons' layout (Settings, CONTROLS), always the one chosen: a client without
  // aa.brake shows its brake key dimmed (TouchControls) rather than another layout
  const touchLayout = useTouchLayout();
  const touchKeys = touch && rowLayout(touchLayout);
  useEffect(() => {
    document.body.classList.toggle("is-touch-keys", touchKeys);
  }, [touchKeys]);
  // the rail's brake bar only where there is a brake to press: a touch layout without a brake key,
  // or Discord on a phone (whatever its pointer says), shows the speed alone
  const hudBrakes = touchKeys || !(touchPrimary() || DISCORD_PLATFORM === "mobile");
  // a sheet over the game (Settings, Friends): the HUD, the touch buttons and the resting chat
  // lines step aside, so nothing of the game is drawn over the sheet (the menu cell on its X)
  const sheetUp = settingsUp || friendsUp;

  // the scoreboard: the game's SCORE key shows and hides it (the `scores` event), each
  // show on MATCH; with the platform's leaderboards, TODAY, THIS WEEK and ALL TIME after it
  const boardsLive = leaderboardsOn(platform);
  const [scoreTab, setScoreTab] = useState<ScoreTab>("match");
  const [scoreSort, setScoreSort] = useState<BoardSort>("kills");
  const [scoreBoard, setScoreBoard] = useState<string | null>(null);
  const scoresUp = inGame && scores.shown && !menuOpen && !profile && !authUp && !sheetUp;
  useEffect(() => {
    if (scores.shown) setScoreTab("match");
  }, [scores.n, scores.shown]);

  const leaderboardList = useBoardList(boardsLive && inGame);
  const scoreBoards = boardsLive && platform.mode === "platform" && !!platform.user ? leaderboardList : [];
  const tally = useMatchTally(roomServerId, scoresUp && scoreTab === "match");
  // the places the first death note compares with: read when an official game starts
  useLeaderboardBaseline(boardsLive && real === "playing" && !!roomServerId);
  // Shift with the SCORE key: the next tab (scoreKeys.ts keeps that press from the game)
  useEffect(() => {
    if (!scoresUp || !scoreBoards.length) return onScoreKey(null);
    onScoreKey((e) => {
      if (!e.shiftKey || e.ctrlKey || e.altKey || e.metaKey || !boundKey("SCORE", e.code, ["Tab"])) return false;
      if (!e.repeat) setScoreTab((tab) => SCORE_TABS[(SCORE_TABS.indexOf(tab) + 1) % SCORE_TABS.length]);
      return true;
    });
    return () => onScoreKey(null);
  }, [scoresUp, scoreBoards.length, aa]);
  // the SCORE key's name, for the window's SHIFT TAB NEXT
  const scoreKeyLabel = useMemo(() => {
    try {
      return aa?.getBindings().find((b) => b.action === "SCORE" && b.code)?.keyName ?? null;
    } catch {
      return null;
    }
  }, [aa, scoresUp]);
  useEffect(() => {
    document.body.classList.toggle("is-sheet-up", sheetUp);
  }, [sheetUp]);
  useGameHud(aa, playingNow, compact, hudStyle, pageMetersAvailable(aa), touch);
  // iPhone Safari: a game starting tries the scroll that folds the browser's bars away (fullscreen.ts)
  useEffect(() => {
    if (playingNow) nudgeScroll();
  }, [playingNow]);

  useEffect(() => {
    if (!aa || !playingNow) return;
    const before = aa.getConfig("TEXT_OUT")?.value ?? "1";
    aa.setConfig("TEXT_OUT", "0");
    return () => {
      aa.setConfig("TEXT_OUT", before);
    };
  }, [aa, playingNow]);

  // the line closed: the keys go back to the game, or over the home screen the focus to where
  // it was (unless a click put it somewhere already). Chatting is the effect below
  const onChatOpen = useCallback(
    (open: boolean, view: PanelView) => {
      const was = chatOpenRef.current;
      chatOpenRef.current = open;
      setChatOpen(open);
      setChatSeen(lastChatId.current);
      setChatView(view);
      if (open || !was) return;
      if (document.body.classList.contains("is-game")) {
        if (!document.body.classList.contains("is-menu-game")) canvas?.focus({ preventScroll: true });
        return;
      }
      const back = consoleReturn.current;
      consoleReturn.current = null;
      window.setTimeout(() => {
        const at = document.activeElement;
        if (at && at !== document.body) return;
        if (back && back.isConnected) back.focus({ preventScroll: true });
        else (stripRef.current?.children[focused] as HTMLElement | undefined)?.focus({ preventScroll: true });
      }, 0);
    },
    [aa, canvas, focused]
  );

  useEffect(() => {
    if (!conductLine) return;
    const t = window.setTimeout(() => setConductLine(false), 10000);
    return () => window.clearTimeout(t);
  }, [conductLine]);

  useEffect(() => {
    if (!inGame) {
      setConductLine(false);
      setKeysLine(null);
    }
  }, [inGame]);

  useEffect(() => {
    if (!keysLine) return;
    const t = window.setTimeout(() => setKeysLine(null), KEYS_LINE_MS);
    return () => window.clearTimeout(t);
  }, [keysLine]);

  // how many play now, for the Quick Play line: asked while the queue is on screen, every
  // 30 s (the platform caches it 15 s); null in community mode or below its minShown
  useEffect(() => {
    if (platform.mode !== "platform" || inGame) return;
    let gone = false;
    const ask = () => void livePlayers().then((n) => !gone && setLive(n));
    ask();
    const t = window.setInterval(ask, 30000);
    return () => {
      gone = true;
      window.clearInterval(t);
    };
  }, [platform.mode, inGame]);

  // the stage keeps rendering its body while it shrinks back into the tile (380 ms)
  useEffect(() => {
    if (mode) {
      setShown(mode);
      return;
    }
    const t = window.setTimeout(() => setShown(null), 380);
    return () => window.clearTimeout(t);
  }, [mode]);

  // a friend's room to join, once the home screen is back and nothing else is up (taken when
  // the timer fires: clearing it here would re-run this effect, whose cleanup ends the timer)
  useEffect(() => {
    if (!pendingJoin || mode || inGame || profile || friendsUp || loading || !canScene(real)) return;
    const target = pendingJoin;
    const t = window.setTimeout(() => {
      setPendingJoin(null);
      if ("quick" in target) select("quick");
      else select("duelRoom" in target ? "custom" : "quick", target);
    }, 80);
    return () => window.clearTimeout(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [pendingJoin, mode, inGame, profile, friendsUp, loading, real]);

  // presence: what this page is doing, for its friends' lists, when it changes
  // and every minute while the tab is visible; nothing while it plays in an official room (the
  // room's roster says that), for a guest, or without the platform's friends
  const pageState: PageState | null = !friendsAccount(platform)
    ? null
    : real === "local"
      ? "local"
      : real === "playing"
        ? roomServerId
          ? null
          : "community"
        : finding || real === "connecting" || real === "syncing" || real === "fetching"
          ? "queue"
          : "menu";
  useEffect(() => {
    if (!pageState || !aa || platform.mode !== "platform") return;
    const url = platform.apiUrl;
    const send = () => {
      if (document.visibilityState !== "visible") return;
      void reportPresence(url, pageState, playerColor(aa)).catch((e) => console.info("presence not sent: " + String(e)));
    };
    send();
    const t = window.setInterval(send, 60000);
    document.addEventListener("visibilitychange", send);
    return () => {
      window.clearInterval(t);
      document.removeEventListener("visibilitychange", send);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [pageState, platform.mode === "platform" ? platform.user?.id : null, aa]);

  // the Friends panel keeps rendering while it shrinks back into its cell (380 ms); a game
  // that starts closes it
  useEffect(() => {
    if (friendsUp) {
      setFriendsShown(true);
      return;
    }
    const t = window.setTimeout(() => setFriendsShown(false), 380);
    return () => window.clearTimeout(t);
  }, [friendsUp]);

  useEffect(() => {
    if (settingsUp) {
      setSettingsShown(true);
      return;
    }
    const t = window.setTimeout(() => setSettingsShown(false), 380);
    return () => window.clearTimeout(t);
  }, [settingsUp]);

  /* The Settings sheet's live preview: with CAMERA open over the home screen the
     attract scene follows a cycle with the player's own custom camera (SCENE_PLAYER_CAMERA, not
     saved) and a focused glance row holds a glance (SCENE_PLAYER_GLANCE); whenever the camera
     shows the player's cycle in the middle of the screen (that preview, or any tab over a game)
     the 3D view's centre moves to the middle of the part the sheet leaves free
     (BROWSER_LENS_SHIFT, in half widths). The scene then drives its camera course
     (SCENE_PLAYER_COURSE: a cycle in the player's colour on straights, S-bends, reversals and a
     brake, the part chosen by the focused row) instead of the recorded rounds. All four go back
     to 0 when the sheet closes. A
     bundle without them is left alone (setConfig of an unknown name raises an error). */
  useEffect(() => {
    if (!aa) return;
    const set = (name: string, value: string) => {
      const item = aa.getConfig(name);
      if (item && Number(item.value) !== Number(value)) aa.setConfig(name, value);
    };
    const look = settingsUp && settingsPreview.camera && !inGame;
    set("SCENE_PLAYER_CAMERA", look ? "1" : "0");
    set("SCENE_PLAYER_GLANCE", look && settingsPreview.glance ? "1" : "0");
    // the camera course instead of the recorded rounds, the part the focused row needs
    set("SCENE_PLAYER_COURSE", look ? String(settingsPreview.course) : "0");
    const shift = () => {
      let s = 0;
      const el = document.getElementById("settings");
      if (settingsUp && el && (look || inGame)) {
        const r = el.getBoundingClientRect();
        const w = window.innerWidth;
        // the sheet at the right: the middle of what is left of it; at the left, of what is right
        if (w > 0 && r.width > 0 && r.width < w * 0.8) s = r.left > w - r.right ? r.left / w - 1 : r.right / w;
      }
      set("BROWSER_LENS_SHIFT", String(Math.round(s * 1000) / 1000));
    };
    // after the layout settles (the sheet changes sides with CAMERA in a game)
    const id = window.requestAnimationFrame(() => window.requestAnimationFrame(shift));
    window.addEventListener("resize", shift);
    return () => {
      window.cancelAnimationFrame(id);
      window.removeEventListener("resize", shift);
    };
  }, [aa, settingsUp, settingsPreview, inGame, settingsTab]);

  // a game that starts or ends under an open sheet closes it (its place changes sides)
  const settingsInGame = useRef(inGame);
  useEffect(() => {
    if (settingsInGame.current !== inGame && settingsUp) {
      setSettingsOpen(false);
      setSettingsUp(false);
    }
    settingsInGame.current = inGame;
  }, [inGame, settingsUp]);

  useEffect(() => {
    if (!inGame || !friendsUp) return;
    setFriendsOpen(false);
    setFriendsUp(false);
  }, [inGame, friendsUp]);

  // the friends cell's count of requests: read for whoever is signed in, and every minute
  // while the queue is up (the panel reads it again whenever it opens)
  const friendsUserId = platform.mode === "platform" && platform.user ? platform.user.id : null;
  useEffect(() => {
    if (!friendsAccount(platform) || inGame) return;
    const read = () => void refreshFriends().catch((e) => console.info("friends not read: " + String(e)));
    read();
    const t = window.setInterval(read, 60000);
    return () => window.clearInterval(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [friendsUserId, friendsLive, inGame]);

  // this player's mutes: read for whoever plays now, and when a game starts
  useEffect(() => {
    void refreshMutes();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [friendsUserId, platform.mode === "platform" && platform.config.features.mutes, inGame]);

  // the Profile stage keeps rendering while it shrinks back into the chip (380 ms)
  useEffect(() => {
    if (profile) {
      setProfileShown(true);
      return;
    }
    const t = window.setTimeout(() => setProfileShown(false), 380);
    return () => window.clearTimeout(t);
  }, [profile]);

  // an emailed account link opens the Profile stage once the page is ready (the scene
  // runs behind it as always); without a platform it is dropped
  useEffect(() => {
    if (!accountLink || phase === "loading" || platform.mode === "loading") return;
    if (platform.mode !== "platform") {
      setAccountLink(null);
      return;
    }
    openProfile();
  }, [accountLink, phase, platform.mode, openProfile]);

  // who is signed in may change elsewhere (another tab, a revoked session, a password
  // reset): ask again when this tab comes back, at most every 10 s. It only updates
  // the chip and the next connect; a running game keeps its login (the server
  // authenticated that connection already).
  useEffect(() => {
    if (platform.mode !== "platform") return;
    let last = Date.now();
    const again = () => {
      if (document.visibilityState !== "visible" || Date.now() - last < 10000) return;
      last = Date.now();
      void refreshPlatformUser();
    };
    window.addEventListener("focus", again);
    document.addEventListener("visibilitychange", again);
    return () => {
      window.removeEventListener("focus", again);
      document.removeEventListener("visibilitychange", again);
    };
  }, [platform.mode]);

  // Quick Play through the platform: time the regions once, before the first press
  useEffect(() => {
    if (platform.mode === "platform" && platform.config.features.quickplay === true) void preferredRegion();
  }, [platform]);


  /* Chatting, as the desktop client has it: while its chat line, its console or its in-game menu
     is open the player is marked chatting (the others see it) and the game's chatbot steers the
     cycle meanwhile (gCycle.cpp, the owner's client). Here every page layer over a game counts:
     the chat panel on any view, the Esc menu, a password prompt, Settings, Friends, the Profile.
     Set again on every round start and state change: a flag set before the round's player
     existed would be lost (the local game too: its state is "local", not "playing"). */
  const chatting = (session === "playing" || session === "local") && (chatOpen || menuOpen || authUp || settingsUp || friendsUp || profile);
  useEffect(() => {
    if (!aa || phase !== "ready") return;
    const s = aa.getState().state;
    if (s === "playing" || s === "local" || !chatting) aa.setChatting(chatting);
  }, [aa, phase, chatting, session, rounds.started]);

  // body classes carry the page's phases to the CSS; data-* on <html> expose the scene
  useEffect(() => {
    const body = document.body;
    body.classList.toggle("is-loading", loading);
    body.classList.toggle("is-launch", launchHold);
    body.classList.toggle("is-stage", !!mode && !inGame);
    // the Server Browser's own page: the wordmark and the status bar stay over it
    body.classList.toggle("is-servers", serversPage);
    // the Profile sheet over the home screen: the queue stays, dimmed, the chip lit
    body.classList.toggle("is-profile", profile && !inGame);
    body.classList.toggle("is-game", inGame);
    body.classList.toggle("is-profile-game", profile && inGame);
    body.classList.toggle("is-menu-game", (menuOpen || authUp) && inGame);
    // the scoreboard in the left sheet's place: the chat's lines at rest make way
    body.classList.toggle("is-scores-game", scoresUp);
    // the Friends panel over the home screen: dimmed as behind the Profile, its cell lit
    body.classList.toggle("is-friends", friendsUp && !inGame);
    // the bell's panel: the menu steps back as behind the Friends panel, so one thing has the focus
    body.classList.toggle("is-news", newsUp && !inGame);
    // the Console over the home screen: the queue dims and goes out of reach
    body.classList.toggle("is-console", chatOpen && !inGame);
    // the Settings sheet over the home screen; on CAMERA and GRAPHICS the scene comes up to full
    body.classList.toggle("is-settings", settingsUp && !inGame);
    body.classList.toggle("is-settings-look", settingsUp && !inGame && (settingsTab === "camera" || settingsTab === "graphics"));
  }, [loading, launchHold, mode, profile, inGame, menuOpen, authUp, chatOpen, friendsUp, newsUp, settingsUp, settingsTab, serversPage, scoresUp]);

  // the page's view for session_end (analytics.ts): where a visitor was when they left
  const screen = loading
    ? "loading"
    : inGame
      ? profile
        ? "game_profile"
        : settingsUp
          ? "game_settings"
          : friendsUp
            ? "game_friends"
            : chatOpen
              ? "game_chat"
              : menuOpen || authUp
                ? "game_menu"
                : session === "local"
                  ? "local_game"
                  : "game"
      : profile
        ? "profile"
        : settingsUp
          ? "settings"
          : friendsUp
            ? "friends"
            : chatOpen
              ? "console"
              : ban
                ? "ban"
                : serversPage
                  ? "servers"
                  : mode
                    ? "connecting_" + mode
                    : "home";
  useEffect(() => setScreen(screen), [screen]);

  useEffect(() => {
    const html = document.documentElement;
    html.dataset.session = session;
    html.dataset.sceneState = scene ? String(scene.state) : "0";
    html.dataset.sceneTransitioning = scene && scene.transitioning ? "1" : "0";
  }, [session, scene]);

  // Quick Play for checks: the finding step's phase and the room the platform chose
  useEffect(() => {
    const html = document.documentElement;
    html.dataset.quickplay = finding ? finding.phase : "";
    html.dataset.quickplayRoom = quickRoom;
  }, [finding, quickRoom]);

  // the platform for checks: data-platform loading/community/platform, the signed-in gid
  useEffect(() => {
    const html = document.documentElement;
    html.dataset.platform = platform.mode;
    html.dataset.platformUser = platform.mode === "platform" && platform.user ? platform.user.gid : "";
    html.dataset.platformAccount = platform.mode === "platform" && platform.user && !platform.user.isGuest ? "1" : "0";
  }, [platform]);

  // what a round and a match earned: the platform's numbers after each round
  const rewards = useRewards({ platform, active: real === "playing" && rewardGame, roundsEnded: rounds.ended, roundsStarted: rounds.started });
  const xpLine = rewards.line;
  const offer = rewards.offer;
  // an offer of a closer server takes the rewards' place (the XP line stays)
  const summary = offer ? null : rewards.summary;
  // the match summary's LEADERBOARD row: today's place, read when a summary comes up
  const [summaryPlace, setSummaryPlace] = useState<MyPlace | null>(null);
  useEffect(() => {
    setSummaryPlace(null);
    if (!summary || !boardsLive) return;
    let gone = false;
    void readMyPlaces().then((p) => !gone && setSummaryPlace(p?.today ?? null));
    return () => {
      gone = true;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [summary?.key]);

  // for checks: the level the page last saw (GET /v1/me progression), "" without one
  const progression = useProgression();
  // a guest with progress to lose, where an account could keep it: the chip's "not saved"
  const unsaved =
    platform.mode === "platform" &&
    platform.config.features.accounts &&
    !!platform.user?.isGuest &&
    !!progression &&
    progressionOwner() === platform.user.id &&
    (progression.xp > 0 || progression.stats.matches > 0);
  useEffect(() => {
    document.documentElement.dataset.progressionLevel = progression ? String(progression.level) : "";
  }, [progression]);
  // the chip's level: read once for whoever plays now (a guest on arrival, a sign-in, New
  // player), not only when a game or the Profile asks
  const platformUserId = platform.mode === "platform" && platform.user ? platform.user.id : null;
  useEffect(() => {
    if (platformUserId && progressionOn(platform)) void refreshProgression();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [platformUserId]);
  // the level the platform has for whoever plays now (the chip, the player card)
  const myLevel = progression && platform.mode === "platform" && platform.user && progressionOwner() === platform.user.id ? progression.level : null;

  // for checks: the line and the summary on screen now, and how many of each came so far
  const seen = useRef({ lineKey: 0, lines: 0, total: 0, summaryKey: 0, summaries: 0 });
  useEffect(() => {
    const html = document.documentElement;
    const c = seen.current;
    if (xpLine && xpLine.key !== c.lineKey) {
      c.lineKey = xpLine.key;
      c.lines++;
      c.total += xpLine.amount;
    }
    if (summary && summary.key !== c.summaryKey) {
      c.summaryKey = summary.key;
      c.summaries++;
    }
    html.dataset.xpLine = xpLine ? [xpLine.amount !== 0 ? xpText(xpLine.amount) : "", xpLine.levelUp ? "level " + xpLine.level : ""].filter(Boolean).join(" ") : "";
    html.dataset.xpLines = String(c.lines);
    html.dataset.xpTotal = String(c.total);
    html.dataset.matchSummary = summary ? summary.match.id : "";
    html.dataset.matchSummaries = String(c.summaries);
    html.dataset.savePrompt = summary?.save ?? "";
  }, [xpLine, summary]);

  // for checks: the offer of a closer server on screen now (its phase: open / accepted / moving) and its id
  useEffect(() => {
    document.documentElement.dataset.moveOffer = offer ? offer.phase : "";
    document.documentElement.dataset.moveOfferId = offer ? offer.offer.id : "";
  }, [offer]);

  // for checks: the chat overlay's line open now, and on which view
  useEffect(() => {
    document.documentElement.dataset.chatOpen = chatOpen ? "1" : "";
    document.documentElement.dataset.chatView = chatOpen ? chatView : "";
  }, [chatOpen, chatView]);

  // for checks: the conduct line on screen now
  useEffect(() => {
    document.documentElement.dataset.conductLine = conductLine ? "1" : "";
  }, [conductLine]);

  // a press outside the open sheet (Profile, Friends, Settings) closes it, as Esc does; the
  // dimmed home screen behind it takes no pointer, so this is the press players reach for. The
  // press does nothing else (it neither starts a lane nor reaches the game, nor reopens the
  // sheet from its own cell). The Esc menu in a game is one too: a press on
  // the game behind it resumes, and the game never gets that press. Two holds: a Settings key capture only cancels, and words typed
  // into a Profile or Friends form stay up (its close cell flashes instead), since the sheet
  // opens at home again and they would be gone. Other dialogs and the chat keep their presses.
  const typedIn = useRef<string | null>(null);
  useEffect(() => {
    const sheet = profile ? "profile" : friendsUp ? "friends" : settingsUp ? "settings" : menuOpen && inGame ? "game-menu" : null;
    if (!sheet || authUp) {
      typedIn.current = null;
      return;
    }
    const swallow = (e: Event) => {
      e.preventDefault();
      e.stopImmediatePropagation();
    };
    const typed = (e: Event) => {
      const t = e.target as HTMLElement;
      const el = t.closest<HTMLElement>("#profile, #friends, #game-menu");
      if (el && t.id !== "profile-name") typedIn.current = el.id + ":" + (el.dataset.view ?? "");
    };
    const kept = (el: HTMLElement) =>
      typedIn.current === el.id + ":" + (el.dataset.view ?? "") &&
      [...el.querySelectorAll<HTMLInputElement | HTMLTextAreaElement>("input:not([type=checkbox]):not([type=radio]):not([type=hidden]), textarea")].some(
        (f) => f.id !== "profile-name" && f.value.trim() !== ""
      );
    let nudged = 0;
    const down = (e: PointerEvent) => {
      const el = document.getElementById(sheet);
      const t = e.target as Element | null;
      if (!el || el.hidden || !t || el.contains(t) || t.closest('[role="dialog"], [role="alertdialog"], .chat, #dm-dock')) return;
      swallow(e);
      // the click and mousedown this press makes go nowhere either
      const once = (ev: Event) => swallow(ev);
      window.addEventListener("mousedown", once, { capture: true, once: true });
      window.addEventListener("click", once, { capture: true, once: true });
      window.setTimeout(() => {
        window.removeEventListener("mousedown", once, { capture: true });
        window.removeEventListener("click", once, { capture: true });
      }, 600);
      if (sheet === "settings") {
        if (settingsRef.current?.capturing()) settingsRef.current.back();
        else closeSettings();
        return;
      }
      if (kept(el)) {
        el.dataset.nudge = "1";
        window.clearTimeout(nudged);
        nudged = window.setTimeout(() => delete el.dataset.nudge, 700);
        return;
      }
      // the name commits on blur, which the swallowed press no longer gives it
      (document.activeElement as HTMLElement | null)?.blur();
      if (sheet === "profile") closeProfile();
      else if (sheet === "game-menu") setMenuOpen(false);
      else closeFriends();
    };
    window.addEventListener("pointerdown", down, { capture: true });
    document.addEventListener("input", typed, { capture: true });
    return () => {
      window.removeEventListener("pointerdown", down, { capture: true });
      document.removeEventListener("input", typed, { capture: true });
      window.clearTimeout(nudged);
    };
  }, [authUp, closeFriends, closeProfile, closeSettings, friendsUp, inGame, menuOpen, profile, settingsUp]);

  // first_turn, once per page load, from a turn key (keys below) or a touch turn button (TouchControls)
  const noteFirstTurn = useRef<(input: "key" | "touch") => void>(() => {});
  noteFirstTurn.current = (input) => {
    if (firstTurnSent.current || !playingNow) return;
    firstTurnSent.current = true;
    track("first_turn", {
      ms: sinceNavigation(),
      sincePlay: firstPlayAt.current === null ? undefined : performance.now() - firstPlayAt.current,
      mode: playMode.current ?? undefined,
      input
    });
  };

  // keyboard: arrows/Enter/Esc for the menu, Tab by hand (keyboard.ts)
  const keys = useRef<(e: KeyboardEvent) => void>(() => {});
  keys.current = (e: KeyboardEvent) => {
    // the first turn of this page load (the game reads the key itself; this only notes it)
    if (!firstTurnSent.current && playingNow && !e.repeat && turnCodes.current.has(e.code) && !isTextField(e.target)) {
      noteFirstTurn.current("key");
    }

    // the chat dock (ChatDock.tsx): Tab runs through it and, past either end, on into the sheet
    // that is up (else the home screen's menu); Esc puts the window into its tab; Enter or Space
    // on one of its buttons is clicked here (the client takes the keys); the well keeps its own
    // (Enter sends there)
    const dock = document.getElementById("dm-dock");
    const sheetEl = profile ? document.getElementById("profile") : friendsUp ? document.getElementById("friends") : settingsUp ? document.getElementById("settings") : null;
    if (dock && e.target instanceof Node && dock.contains(e.target)) {
      if (e.key === "Tab") {
        e.preventDefault();
        const next = sheetEl ?? (serversPage ? null : frameRef.current);
        if (wouldWrap(e.shiftKey, dock) && (next === null || focusableIn(next))) moveFocus(e.shiftKey, next);
        else moveFocus(e.shiftKey, dock);
      } else if (e.key === "Escape") {
        e.preventDefault();
        if (!dockRef.current?.back()) chipRef.current?.focus({ preventScroll: true });
      } else if ((e.key === "Enter" || e.key === " ") && !isTextField(e.target)) {
        const el = e.target as HTMLElement;
        if (el.tagName === "BUTTON") {
          e.preventDefault();
          if (!(el as HTMLButtonElement).disabled) el.click();
        }
      }
      return;
    }

    // the Settings sheet handles its own keys (data-aa-keys="page", Settings.tsx); Esc with the
    // focus outside it (a click on the scene) closes it
    if (settingsUp) {
      const el = document.getElementById("settings");
      if (!inGame && e.key === "Escape" && el && !el.contains(e.target as Node)) {
        e.preventDefault();
        closeSettings();
      }
      return;
    }

    // the Profile sheet: its fields keep their keys (typing, Space on the checkbox,
    // Enter submits), but Tab stays inside it and Esc steps back even from a field; the
    // arrows move between its lanes and buttons as in the in-game menu (not from a field,
    // whose caret they move); Enter or Space on one of its buttons is clicked here (the
    // client takes the keys)
    // (the Friends panel the same way)
    const sheet = profile
      ? { el: document.getElementById("profile"), back: () => profileRef.current?.back() ?? false, close: closeProfile }
      : friendsUp
        ? { el: document.getElementById("friends"), back: () => friendsRef.current?.back() ?? false, close: closeFriends }
        : null;
    if (sheet) {
      if (e.key === "Tab") {
        e.preventDefault();
        // past either end into the chat dock when it shows (its own Tab hands the focus back)
        moveFocus(e.shiftKey, wouldWrap(e.shiftKey, sheet.el) && focusableIn(dock) ? dock : sheet.el);
      } else if ((e.key === "ArrowDown" || e.key === "ArrowUp") && !isTextField(e.target)) {
        e.preventDefault();
        moveFocus(e.key === "ArrowUp", sheet.el);
      } else if (e.key === "Escape") {
        e.preventDefault();
        profileEscAt.current = performance.now();
        if (!sheet.back()) sheet.close();
      } else if ((e.key === "Enter" || e.key === " ") && !isTextField(e.target)) {
        const el = e.target as HTMLElement | null;
        if (el && el.tagName === "BUTTON") {
          e.preventDefault();
          if (!(el as HTMLButtonElement).disabled) el.click();
        } else if (el && el.tagName === "A" && e.key === "Enter") {
          e.preventDefault();
          el.click(); // a document link (the consent line): a new tab
        }
      }
      return;
    }

    if (isTextField(e.target)) return;

    // the Console over the home screen: the game's CONSOLE_INPUT key (` by default) when no
    // text field has the focus (in a game the client raises consoleOpen itself)
    if (!inGame && !mode && !e.repeat && !e.ctrlKey && !e.altKey && !e.metaKey && consoleKey(e.code)) {
      e.preventDefault();
      openConsole();
      return;
    }

    // the game's TOGGLE_FULLSCREEN key (p) outside a game: the client raises its fullscreen
    // event only in one, so the page toggles it here (the press is the user gesture it needs);
    // never a key the page itself uses
    if (!inGame && !e.repeat && !e.ctrlKey && !e.altKey && !e.metaKey && !PAGE_KEYS.has(e.key) && boundKey("TOGGLE_FULLSCREEN", e.code, [])) {
      e.preventDefault();
      toggleFullscreen();
      return;
    }

    // a link of the documents line: Enter follows it (the client takes Enter too)
    if (!inGame && e.key === "Enter" && (e.target as HTMLElement | null)?.tagName === "A") {
      e.preventDefault();
      (e.target as HTMLElement).click();
      return;
    }

    // the chip: Enter or Space opens the Profile stage; the friends cell its panel
    if (!inGame && (!mode || serversPage) && e.target === chipRef.current && (e.key === "Enter" || e.key === " ")) {
      e.preventDefault();
      openProfile();
      return;
    }
    if (!inGame && (!mode || serversPage) && e.target === friendsCellRef.current && (e.key === "Enter" || e.key === " ")) {
      e.preventDefault();
      openFriends();
      return;
    }
    // the bell: Enter or Space opens its panel (and closes it again)
    if (!inGame && e.target === newsCellRef.current && (e.key === "Enter" || e.key === " ")) {
      e.preventDefault();
      if (newsUp) closeNews(false);
      else openNews();
      return;
    }
    // the home screen's duel panel: Enter or Space on ACCEPT, DECLINE or CANCEL
    const duelAct = (e.target as HTMLElement | null)?.closest?.("#duel-home-slot button") as HTMLButtonElement | null;
    if (!inGame && duelAct && (e.key === "Enter" || e.key === " ")) {
      e.preventDefault();
      if (!duelAct.disabled) duelAct.click();
      return;
    }

    if (e.key === "Tab") {
      if (inGame) return; // the game's SCORE key
      e.preventDefault();
      // the Server Browser's Tab runs through the page and the status bar over it (the chat dock
      // too); on the home screen past either end of the menu into the dock when it shows
      const region = serversPage ? null : mode ? document.getElementById("stage") : frameRef.current;
      moveFocus(e.shiftKey, !serversPage && !mode && wouldWrap(e.shiftKey, region) && focusableIn(dock) ? dock : region);
      return;
    }

    if (inGame) {
      // Escape is the game's INGAME_MENU key: the menuOpen event opens the menu; inside
      // it the keys are the page's (data-aa-keys) and GameMenu handles them
      return;
    }

    if (mode) {
      // the Server Browser's page takes its keys first (ServerBrowser.tsx); Esc it leaves goes home
      if (serversPage && (mode === "boards" ? boardsRef : browserRef).current?.key(e)) return;
      const onStage = e.target as HTMLElement | null;
      if ((e.key === "Enter" || e.key === " ") && onStage?.tagName === "BUTTON" && onStage.closest("#stage")) {
        e.preventDefault();
        onStage.click();
        return;
      }
      if (e.key === "Escape") {
        e.preventDefault();
        collapse();
      }
      return;
    }

    if (e.key === "ArrowDown" || e.key === "ArrowRight") {
      e.preventDefault();
      focusTile(focused + 1, true);
    } else if (e.key === "ArrowUp" || e.key === "ArrowLeft") {
      e.preventDefault();
      focusTile(focused - 1, true);
    } else if (e.key === "Enter") {
      e.preventDefault();
      select(MODES[focused].mode);
    } else if (e.key === "Escape") {
      e.preventDefault();
      setNote(null);
    }
  };

  // the keys bound to an action (the key map's codes; the fallback when it says none)
  const boundKey = (action: string, code: string, fallback: string[]): boolean => {
    if (!code) return false;
    let codes = fallback;
    try {
      const bound = aa?.getBindings().filter((b) => b.action === action && b.code).map((b) => b.code as string) ?? [];
      if (bound.length) codes = bound;
    } catch {
      // the key map unreadable: the fallback
    }
    return codes.includes(code);
  };
  // the keys bound to CONSOLE_INPUT (` when it says none)
  const consoleKey = (code: string): boolean => boundKey("CONSOLE_INPUT", code, ["Backquote"]);

  useEffect(() => {
    const handler = (e: KeyboardEvent) => keys.current(e);
    document.addEventListener("keydown", handler);
    return () => document.removeEventListener("keydown", handler);
  }, []);

  // ---- the page --------------------------------------------------------------------

  return (
    <>
      <main className="frame" id="frame" ref={frameRef}>
        <h1 className="mark">
          Retrocycles
          <i className="mark-load" id="mark-load" aria-hidden="true" style={{ "--load": loadFraction } as React.CSSProperties} />
        </h1>

        <div className="status" id="status">
        <Chip
          ref={chipRef}
          name={player.name}
          raw={player.raw}
          handle={platform.mode === "platform" && platform.user && !platform.user.isGuest ? platform.user.handle : null}
          unsaved={unsaved}
          level={myLevel}
          disabled={!aa}
          onOpen={() => {
            // the sheet hangs from the chip: a second press puts it away
            if (profile) return closeProfile();
            claimOpened("chip");
            openProfile();
          }}
        />
        {friendsLive && (
          <FriendsCell
            ref={friendsCellRef}
            requests={friendsList?.incoming.length ?? 0}
            disabled={!aa}
            onOpen={() => (friendsUp ? closeFriends() : openFriends())}
          />
        )}
        {friendsLive && hasConversations && (
          <MessagesCell unread={dockUnread} disabled={!aa || inGame} onOpen={() => dockRef.current?.toggle()} />
        )}
        {news.status !== "off" && (
          <NewsCell
            ref={newsCellRef}
            unread={news.status === "ready" ? news.unread : null}
            open={newsUp}
            disabled={!aa}
            onToggle={() => (newsUp ? closeNews(false) : openNews())}
          />
        )}
        <SettingsCell ref={settingsCellRef} pressed={settingsUp} disabled={!aa} onOpen={() => (settingsUp ? closeSettings() : openSettings())} />
        {newsUp && news.status !== "off" && !inGame && <NewsPanel news={news} cell={newsCellRef.current} onClose={closeNews} />}
        </div>

        <Strip ref={stripRef} focused={focused} note={note} live={live} onFocusTile={focusTile} onSelect={(m) => select(m)} />

        <DocsLine />

        {cookieBanner && !loading && !mode && !inGame && <CookieBanner />}
        {homeDuel && !duelOverStage && (
          // a friend's challenge, or mine to one, on the home screen: the lower left, clear of the
          // sheets at the right edge; its acts take the focus here (no game holds the keys)
          <div className="rewards-duel-home" id="duel-home-slot">
            {homeDuel}
          </div>
        )}
        {homeHint && !cookieBanner && !loading && !mode && !inGame && <HomeScreenHint onClose={() => setHomeHint(false)} />}

        {DISCORD && !loading && !mode && !inGame && discord.participants.length > 1 && (
          <DiscordTogether names={discord.participants.filter((p) => p.id !== discord.self).map((p) => p.name)} />
        )}
        {EMBED && !DISCORD && (
          <EmbedPlay
            ready={!loading}
            waiting={playNow}
            note={note}
            href={fullPageLink()}
            onPlay={() => {
              fullscreenOnPlay();
              if (loading || !serversLoaded) setPlayNow(true);
              else select("quick");
            }}
          />
        )}
        {EMBED && fullscreenOffered() && !inGame && !mode && !profile && !authUp && !sheetUp && <FullscreenCell />}
      </main>

      <Stage
        mode={shown}
        open={stageOpen}
        box={box}
        sub={sub}
        session={session}
        resource={resource}
        finding={finding}
        ban={ban}
        keys={stageKeys}
        servers={discordJoinable(boardList ?? servers)}
        serversLoaded={serversLoaded}
        serversOk={boardList ? true : serversOk}
        serversAt={serversAt}
        friends={friendsList}
        browserRef={browserRef}
        connectLink={mode === "browser" ? connectLink : null}
        onConnectLink={clearConnectLink}
        directGateway={platform.mode === "platform" && platform.config.features.directConnect === true}
        boardsRef={boardsRef}
        youColor={aa ? playerColor(aa) : null}

        onSaveProgress={() => {
          claimOpened("leaderboard");
          openProfile("signup");
        }}
        onQuickPlay={() => {
          collapse();
          setQuickNext(true);
        }}
        onRefreshServers={refreshServers}
        onJoinServer={joinServer}
        onBack={collapse}
        onCopyDuel={async () => {
          if (finding?.phase !== "duel") return false;
          const link = finding.link;
          const ok = await copyText(link);
          if (ok) setFinding((f) => (f?.phase === "duel" && f.link === link ? { ...f, copied: true } : f));
          return ok;
        }}
        onShareDuel={() => {
          // the system's share sheet, as the in-game INVITE
          if (finding?.phase !== "duel" || typeof navigator.share !== "function") return;
          navigator.share({ title: "Retrocycles", text: t("connect.duel.shareText"), url: finding.link }).catch(() => {});
        }}
        onPlayWhileWaiting={() => {
          // the link stays open (its arrival comes over the game); a Quick Play from the bare menu
          if (finding?.phase !== "duel") return;
          keepLinkOpen(finding.code);
          setOpenInvite({ code: finding.code, link: finding.link, until: finding.until });
          collapse();
          setPendingJoin({ quick: true });
        }}
      />
      {duelOverStage && (
        <div className="rewards-duel-home is-over-stage" id="duel-stage-slot">
          {homeDuel}
        </div>
      )}

      <Profile
        ref={profileRef}
        shown={profileShown}
        open={profileOpen}
        platform={platform}
        inGame={inGame && !profile}
        name={player.name}
        rawName={player.raw}
        onName={player.setName}
        onTypedName={player.askName}
        nameRefused={player.refused}
        intent={accountLink}
        onIntentTaken={() => setAccountLink(null)}
        startView={profileStart}
        onWantOpen={() => openProfile()}
        onClose={closeProfile}
        colour={plateColour}
      />

      <Friends
        ref={friendsRef}
        shown={friendsShown}
        open={friendsOpen}
        platform={platform}
        onChat={openChat}
        me={player.name}
        onJoin={joinFriend}
        duel={friendDuel}
        onSignUp={() => {
          setFriendsOpen(false);
          setFriendsUp(false);
          claimOpened("chip");
          window.setTimeout(() => openProfile("signup"), 0);
        }}
        onClose={closeFriends}
      />

      {friendsLive && (
        <ChatDock
          ref={dockRef}
          platform={platform}
          friends={dockFriends}
          hidden={inGame || loading || (!!mode && !serversPage)}
          beside={friendsUp || profile || settingsUp}
          onJoin={(f) => joinFriend(f, false)}
        />
      )}

      <SettingsSheet
        ref={settingsRef}
        shown={settingsShown}
        open={settingsOpen}
        inGame={inGame}
        store={settingsStore}
        name={player.raw}
        onName={player.askName}
        nameRefused={player.refused}
        onColour={() => aa && document.documentElement.style.setProperty("--player", playerColor(aa))}
        audio={client.audio}
        onAudio={() => aa?.unlockAudio()}
        startTab={settingsStart}
        onTab={setSettingsTab}
        onPreview={setSettingsPreview}
        onAccount={() => {
          closeSettings();
          if (!inGame) window.setTimeout(() => openProfile(platform.mode === "platform" && platform.user && !platform.user.isGuest ? "account" : undefined), 0);
        }}
        onPrivacy={() => {
          closeSettings();
          if (!inGame) window.setTimeout(() => (friendsLive ? openFriends() : openProfile()), 0);
        }}
        onClose={() => closeSettings(inGame)}
      />

      {inGame && (
        <GameFrame
          ref={menuRef}
          local={session === "local"}
          serverName={sessionName}
          menuOpen={menuOpen}
          players={players.length ? players : state?.players ?? []}
          canReport={canReport}
          level={myLevel}
          invite={privateCode && session === "playing" ? privateRoomLink(privateCode) : gameCode && session === "playing" ? inviteLink(gameCode) : null}
          onInvite={DISCORD && session === "playing" && !privateCode ? () => void discordInvite(gameCode) : null}
          onLeave={leaveGame}
          fullGame={EMBED ? fullPageLink("embed-menu") : null}
          onFullGame={leaveForFullGame}
          onCloseMenu={() => setMenuOpen(false)}
          onSettings={openSettings}
          onReport={report}
          social={cardSocial}
          friends={
            friendsAccount(platform)
              ? { list: friendsList, refresh: () => void refreshFriends().catch(() => {}), onJoin: joinFriend }
              : null
          }
          duel={menuDuel}
          moveAsk={moveAskLine?.kind === "ask" ? { name: moveAskLine.name, address: moveAskLine.address, onMove: () => void answerMove(true), onStay: () => void answerMove(false) } : null}
        >
          {pageMeters && aa && playingNow && !menuOpen && !profile && !authUp && !sheetUp && <CycleHud aa={aa} brakes={hudBrakes} />}
          {aa && playingNow && !menuOpen && !profile && !authUp && !sheetUp && <RespawnCountdown aa={aa} />}
          {aa && boardsLive && real === "playing" && !!roomServerId && !menuOpen && !profile && !authUp && !sheetUp && <DeathNote aa={aa} />}

          {scoresUp && (
            <Scoreboard
              tab={scoreTab}
              onTab={setScoreTab}
              players={players.length ? players : state?.players ?? []}
              tally={tally}
              roomName={stripColors(sessionName)}
              unverified={foreignRoom}
              boards={scoreBoards}
              board={scoreBoard ?? scoreBoards[0]?.id ?? null}
              youColor={aa ? playerColor(aa) : null}
              onBoard={setScoreBoard}
              sort={scoreSort}
              onSort={setScoreSort}
              keyLabel={scoreKeyLabel}
              onSave={() => {
                claimOpened("leaderboard");
                openProfile("signup");
              }}
            />
          )}
          {DISCORD && playingNow && discord.layout !== "focused" && (
            // Discord shows the Activity small (picture in picture, the call grid): no steering there
            <p className="discord-small" id="discord-small" role="status">
              {t("home.discord.tapToReturn")}
            </p>
          )}
          {EMBED && aa && playingNow && !menuOpen && !profile && !authUp && !chatOpen && !sheetUp && (
            <FullGameCell aa={aa} href={fullPageLink("embed-dead")} onOpen={leaveForFullGame} />
          )}
          {EMBED && fullscreenOffered() && !touch && playingNow && !menuOpen && !profile && !authUp && !chatOpen && !sheetUp && <FullscreenCell />}
          {/* a mouse's way to the Esc menu, where the touch controls have their menu cell */}
          {!touch && playingNow && !menuOpen && !profile && !authUp && !chatOpen && !sheetUp && <MenuCell onOpen={() => setMenuOpen(true)} />}
          {touch && aa && playingNow && !menuOpen && !profile && !authUp && !chatOpen && !sheetUp && (
            <TouchControls aa={aa} layout={touchLayout} unread={chatUnread} onMenu={() => setMenuOpen(true)} onChat={() => chatRef.current?.open(null)}
              onTurn={() => noteFirstTurn.current("touch")} />
          )}
          {authUp && auth && (
            <AuthPrompt
              request={auth}
              asked={authAsked}
              serverName={sessionName}
              onAnswer={(user, password) => answerAuth(() => aa?.auth(user, password))}
              onCancel={() => answerAuth(() => aa?.authCancel())}
            />
          )}
          {!profile && !menuOpen && !authUp && !summary && !offer && conductLine && <ConductLine />}
          {!profile && !menuOpen && !authUp && !summary && !offer && !conductLine && keysLine && (
            <KeysLine items={keysLine ?? (aa ? hintItems(aa) : HINT_ACTIONS.map((h) => ({ keys: ["?"], words: h.words })))} />
          )}
          {!profile && !menuOpen && !duelLine && moveAskLine && (
            <div className="rewards rewards-duel" id="move-ask-slot">
              <MoveAsk line={moveAskLine} onMove={() => void answerMove(true)} onStay={() => void answerMove(false)} />
            </div>
          )}
          {!profile && !menuOpen && duelLine && (
            <div className="rewards rewards-duel" id="duel-slot">
              <DuelPanel
                line={duelLine}
                onAccept={() => (duelLine.kind === "arrived" ? void goArrival() : void acceptIncoming())}
                onDecline={() => (duelLine.kind === "arrived" ? notNowArrival() : declineIncoming())}
                onCancel={() => (duelLine.kind === "invite" ? cancelInvite() : void cancelChallenge())}
                onCopy={() => copyInvite()}
              />
            </div>
          )}
          {privateCode && real === "playing" && (
            // kept mounted while the menu or the Profile is up (hidden): its VS card and result are not replayed
            <div className="rewards rewards-duel-room" id="duel-room-slot" hidden={!!profile || menuOpen}>
              <DuelRoom
                key={privateCode}
                aa={aa}
                opponent={duelWith}
                players={playersNow}
                onQuickPlay={leaveToQuickPlay}
              />
            </div>
          )}
          {!profile && !menuOpen && !duelLine && (xpLine || summary || offer) && (
            <div className="rewards" id="rewards">
              {xpLine && !summary && <XpLine line={xpLine} />}
              {offer && <OfferPanel
                  line={offer}
                  onAccept={() => rewards.acceptOffer()}
                  onDismiss={() => rewards.dismissOffer()}
                />}
              {summary && <MatchSummary
                  summary={summary}
                  board={summaryPlace}
                  onSave={(_from, trigger) => {
                    claimOpened("summary", trigger);
                    openProfile("signup");
                  }}
                  onClose={() => rewards.closeSummary()}
                  onHold={rewards.holdSummary}
                />}
            </div>
          )}
          {trainingUp && aa && session === "local" && <Training aa={aa} layout={touchLayout} onClose={() => setTrainingUp(false)} />}
        </GameFrame>
      )}

      {/* the chat overlay's own layer: over the game, and its Console over the home screen */}
      <div className="chatframe">
        <ChatOverlay
          ref={chatRef}
          entries={!inGame ? [] : heard}
          log={log}
          players={playersNow}
          tab={chatTab}
          inGame={inGame}
          settings={settings}
          compact={compact}
          touch={touchPrimary()}
          onTab={setChatTab}
          onSend={(text) => {
            noteLogin(text);
            return chat.send(text);
          }}
          onRun={runConsole}
          onOpenChange={onChatOpen}
        />
      </div>
    </>
  );
}
