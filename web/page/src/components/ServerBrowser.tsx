// The Server Browser: a full page over the arena, not a panel. The wordmark (the way
// home) and the status bar stay in the top band (App.tsx, `body.is-servers`); under them:
//   plate    SERVERS, FIND (`/`) and REFRESH
//   filters  ALL · OFFICIAL · COMMUNITY with counts (hidden for now) and REGION, a dropdown
//            (Enter, Space or Down opens it; Left/Right step it closed); they persist in
//            localStorage (servers.ts)
//   board    the ledger in its Best tiers (WITH FRIENDS, BOOKMARKED, PLAYING NOW, NO PLAYER
//            COUNT, FULL) with a gap between them, or flat when a column head sorts it; then
//            EMPTY, folded: its head (a stop for the keys, Enter/Space/Right/Left or a click open
//            and fold it; open for the tab's session) with its count. A search, or a list with
//            nothing but empty rooms, shows them unfolded under a plain head
//   details  the focused server, docked at the right and following the focus: badges, the
//            sentence, who is on (friends first, a logged-in player's handle in parentheses),
//            JOIN / SPECTATE (not for an empty room) and BOOKMARK; the release behind the info
//            cell beside the name (open, it
//            stays open from server to server). Under 760px it replaces the board (Right or
//            Enter opens it, Esc goes back)
//   direct   Direct Connect: DIRECT CONNECT in the
//            plate puts its pane in the details' place (DirectConnect.tsx: the address, the
//            probe's preview, the warning before the first join to an address, JOIN, SAVE);
//            MY SERVERS heads the board with the saved addresses (localStorage, platform/direct.ts),
//            each a row the keys stop on, its pane with REMOVE. A ?connect= link fills the
//            address and shows the warning; it never joins on its own
// A refresh never moves the board: while the page is up the rows keep their places and their
// values change where they stand (a count that changed turns over in place); the order is
// taken again on REFRESH, a filter, a search or a sort. It shows what the platform or servers.json
// says, a dash where it says nothing. The keys are App's: it hands them to key() first.
import { forwardRef, useCallback, useEffect, useImperativeHandle, useMemo, useRef, useState, type ReactNode } from "react";
import type { ServerEntry } from "../aa/types";
import { isTextField, moveFocus } from "../keyboard";
import type { FriendsList } from "../platform/types";
import {
  applyFilters,
  bestOrder,
  choices,
  filtersOn,
  friendHandles,
  inScope,
  isFull,
  loadBookmarks,
  loadEmptyOpen,
  loadFilters,
  NO_FILTERS,
  saveFilters,
  sortBy,
  storeBookmarks,
  storeEmptyOpen,
  TIER_ORDER,
  TIER_TITLE,
  tierOf,
  toView,
  type Filters,
  type Scope,
  type ServerView,
  type SortKey,
  type Tier
} from "../servers";
import { Colored, stripColors } from "./Colored";
import { DirectPane } from "./DirectConnect";
import { parseAddress, type DirectAddress } from "../directAddress";
import { DIRECT_OFFERED, directEntry, isBlocked, loadSaved, noteWarned, probe as probeAddress, storeSaved, warnedFor, type Probe, type SavedServer } from "../platform/direct";
import { Icon } from "./Icons";
import { timeUrl } from "../platform/quickplay";
import { num, t, type PlainKey } from "../i18n";

export interface ServerBrowserHandle {
  // a key press while the page is up: true when the page took it (App does nothing more)
  key(e: KeyboardEvent): boolean;
}

interface ServerBrowserProps {
  open: boolean;
  servers: ServerEntry[];
  loaded: boolean;
  feedOk: boolean; // false: the list could not be read (the fallback entries are shown)
  updatedAt: number | null; // when the list was last read (ms)
  friends: FriendsList | null;
  onRefresh(): void;
  onJoin(entry: ServerEntry, spectate: boolean): void;
  onBack(): void;
  // Direct Connect: a ?connect= link's address, once (it fills the field and shows the
  // warning, never joins); onConnectLink: it was taken
  connectLink?: { address: string; warn: boolean } | null;
  onConnectLink?(): void;
  // the platform's features.directConnect: DIRECT CONNECT, My servers and host:port (its gateway); off, only a
  // ?connect=wss:// link opens the pane (a server's own address needs no platform)
  gateway?: boolean;
}

// a My servers row's key among the board's stops (a server's key is its URL, never this)
const SAVED_STOP = "\u0001direct:";
const savedStop = (s: SavedServer) => SAVED_STOP + s.address;
const NO_SAVED: SavedServer[] = [];

const NARROW = "(max-width: 760px)";
const REFRESH_MS = 20000;

// a group of the board: a tier's head with its rows (tier null: rows with no head)
type Section = { tier: Tier | null; rows: ServerView[] };

// the EMPTY head's place among the keys' stops (a server's key is its URL, never this)
const EMPTY_STOP = "\u0000empty";
const EMPTY_HEAD_ID = "sb-empty-head";

// the scope tabs and the OFFICIAL badge, hidden for now
const SHOW_SCOPES = false;
const SHOW_OFFICIAL = false;

const SCOPES: Array<{ id: Scope; title: PlainKey }> = [
  { id: "all", title: "connect.browser.scope.all" },
  { id: "official", title: "connect.browser.scope.official" },
  { id: "community", title: "connect.browser.scope.community" }
];

const COLUMNS: Array<{ key: Exclude<SortKey, "best">; title: PlainKey; cls: string }> = [
  { key: "players", title: "connect.browser.col.players", cls: "sb-c-players" },
  { key: "ping", title: "connect.browser.col.ping", cls: "sb-c-ping" },
  { key: "region", title: "connect.browser.col.region", cls: "sb-c-region" }
];

function cycle<T>(list: T[], at: T, step: number): T {
  const i = Math.max(0, list.indexOf(at));
  return list[(i + step + list.length) % list.length];
}

// a column's sort: one flat list, with the empty rooms after it as in Best
function flatTier(s: ServerView, bookmarks: ReadonlySet<string>): Tier {
  return tierOf(s, bookmarks) === "empty" ? "empty" : "playing";
}

export const ServerBrowser = forwardRef<ServerBrowserHandle, ServerBrowserProps>(function ServerBrowser(props, ref) {
  const { open, servers, loaded, feedOk, updatedAt, friends, onRefresh, onJoin, onBack, connectLink, onConnectLink, gateway = false } = props;
  const [filters, setFilters] = useState<Filters>(loadFilters);
  const [query, setQuery] = useState("");
  const [sort, setSort] = useState<{ key: SortKey; asc: boolean }>({ key: "best", asc: false });
  const [bookmarks, setBookmarks] = useState<Set<string>>(loadBookmarks);
  const [emptyOpen, setEmptyOpen] = useState(loadEmptyOpen); // the EMPTY group unfolded
  const [focusKey, setFocusKey] = useState<string | null>(null);
  const [detail, setDetail] = useState(false); // under 760px: the details replace the board
  const [reorder, setReorder] = useState(0); // bumped when the order is to be taken again
  const [release, setRelease] = useState(false); // the details show the release (the info cell)
  const [regionOpen, setRegionOpen] = useState(false); // the REGION dropdown is down
  const [regionAt, setRegionAt] = useState(0); // the option the keys are on while it is
  const manual = useRef(false);
  const listRef = useRef<HTMLUListElement>(null);
  const findRef = useRef<HTMLInputElement>(null);
  const detailRef = useRef<HTMLElement>(null);
  const joinRef = useRef<HTMLButtonElement>(null);
  const regionRef = useRef<HTMLButtonElement>(null);
  const emptyRef = useRef<HTMLLIElement>(null);
  const lastRow = useRef<string | null>(null);

  const narrow = () => window.matchMedia(NARROW).matches;

  useEffect(() => saveFilters(filters), [filters]);

  // ---- Direct Connect: the pane, My servers, the probes, the warning ----
  const [saved, setSaved] = useState<SavedServer[]>(() => (DIRECT_OFFERED ? loadSaved() : []));
  // My servers only while the platform's Direct Connect is on (its probes and its gateway)
  const shownSaved = DIRECT_OFFERED && gateway ? saved : NO_SAVED;
  const [probes, setProbes] = useState<ReadonlyMap<string, Probe>>(() => new Map());
  const [blockedKeys, setBlockedKeys] = useState<ReadonlySet<string>>(() => new Set());
  const [directOpen, setDirectOpen] = useState(false); // the pane, for a typed address
  const [typed, setTyped] = useState("");
  const [signal, setSignal] = useState("");
  const [more, setMore] = useState(false);
  const [warning, setWarning] = useState<string | null>(null); // the address whose warning is up
  const [understood, setUnderstood] = useState(false);
  const [directFailure, setDirectFailure] = useState<string | null>(null);
  const addressRef = useRef<HTMLInputElement>(null);
  const directJoinRef = useRef<HTMLButtonElement>(null);
  const asked = useRef(new Map<string, AbortController>());

  // the probe for one address, then the blocklist on its host and on what it resolved to
  const ask = useCallback((addr: DirectAddress) => {
    asked.current.get(addr.key)?.abort();
    const abort = new AbortController();
    asked.current.set(addr.key, abort);
    setProbes((m) => new Map(m).set(addr.key, { state: "probing" }));
    void probeAddress(addr, abort.signal).then(async (answer) => {
      if (abort.signal.aborted) return;
      setProbes((m) => new Map(m).set(addr.key, answer));
      const blocked = await isBlocked(addr.host, answer.state === "done" ? answer.probe.address : null);
      setBlockedKeys((b) => {
        if (blocked === b.has(addr.key)) return b;
        const next = new Set(b);
        if (blocked) next.add(addr.key);
        else next.delete(addr.key);
        return next;
      });
    });
  }, []);

  // My servers: each asked once the page is up (and again on REFRESH)
  useEffect(() => {
    if (!open || !DIRECT_OFFERED) return;
    for (const s of shownSaved) {
      const addr = parseAddress(s.address);
      if (!("error" in addr) && !probes.has(addr.key)) ask(addr);
    }
  }, [open, shownSaved, probes, ask]);

  // the typed address: asked once the typing stops
  const typedAddr = useMemo(() => parseAddress(typed), [typed]);
  const typedKnown = !("error" in typedAddr) && probes.has(typedAddr.key);
  useEffect(() => {
    if (!open || !directOpen || "error" in typedAddr || typedKnown) return;
    const timer = window.setTimeout(() => ask(typedAddr), 450);
    return () => window.clearTimeout(timer);
  }, [open, directOpen, typedAddr, typedKnown, ask]);

  // the address the pane is about: a My servers row's, else the one typed
  const openDirect = useCallback((address?: string, warn = false) => {
    setDirectOpen(true);
    setDirectFailure(null);
    setUnderstood(false);
    if (address !== undefined) {
      setTyped(address);
      setSignal("");
      setMore(false);
    }
    const parsed = address !== undefined ? parseAddress(address) : null;
    setWarning(warn && parsed && !("error" in parsed) ? parsed.key : null);
    if (narrow()) setDetail(true);
    window.setTimeout(() => {
      if (warn) document.getElementById("direct-understood")?.focus({ preventScroll: true });
      else addressRef.current?.focus({ preventScroll: true });
    }, 0);
  }, []);

  // ?connect=host:port: the address in the field and the warning up; nothing joins on its own
  useEffect(() => {
    if (!open || !connectLink || !DIRECT_OFFERED) return;
    openDirect(connectLink.address, connectLink.warn);
    onConnectLink?.();
  }, [open, connectLink, onConnectLink, openDirect]);

  // read the list again while the page is up
  useEffect(() => {
    if (!open) return;
    const read = window.setInterval(onRefresh, REFRESH_MS);
    return () => window.clearInterval(read);
  }, [open, onRefresh]);

  // a REFRESH press takes the order again once the new list is in
  useEffect(() => {
    if (manual.current) {
      manual.current = false;
      setReorder((n) => n + 1);
    }
  }, [updatedAt]);

  // only what a browser can join is listed (a server without a signalling URL is not)
  // each server's ping: its ping host timed from this browser, once per host while the page is up
  const [pings, setPings] = useState<ReadonlyMap<string, number>>(() => new Map());
  const timing = useRef(new Set<string>());
  useEffect(() => {
    if (!open) return;
    for (const s of servers) {
      const url = s.lobby?.pingUrl;
      if (!url || timing.current.has(url)) continue;
      timing.current.add(url);
      void timeUrl(url).then((ms) => setPings((old) => new Map(old).set(url, ms)));
    }
  }, [open, servers]);

  const handles = useMemo(() => friendHandles(friends), [friends]);
  const views = useMemo(
    () => servers.map((s) => toView(s, friends, pings, handles)).filter((v) => v.browser),
    [servers, friends, pings, handles]
  );

  // the order: taken from the list when something asks for it (the first list, REFRESH, a filter,
  // a search, a sort, a server bookmarked), kept while only the values change
  const order = useRef<{ stamp: string; place: Map<string, { tier: Tier; rank: number }> }>({ stamp: "", place: new Map() });
  // the order is taken again when the first player counts come in (the platform's list after
  // servers.json's, which knows none), so the busy rooms do not stay where name order put them
  const counted = views.some((v) => v.humans !== null);
  // mine: My servers head the list, so the board's own groups keep their heads under it
  const mine = shownSaved.length > 0;
  const stamp = JSON.stringify([filters, query, sort, Array.from(bookmarks), reorder, loaded, counted, mine]);
  // the board: its groups (a tier's head and rows; one untitled group when there are no heads),
  // then the empty rooms, which fold
  const board = useMemo<{ main: Section[]; empty: ServerView[] }>(() => {
    const shown = applyFilters(views, filters, query);
    if (order.current.stamp !== stamp) {
      const place = new Map<string, { tier: Tier; rank: number }>();
      if (sort.key === "best") {
        bestOrder(shown, bookmarks).forEach((g) => g.servers.forEach((s, rank) => place.set(s.key, { tier: g.tier, rank })));
      } else {
        sortBy(shown, sort.key, sort.asc).forEach((s, rank) => place.set(s.key, { tier: flatTier(s, bookmarks), rank }));
      }
      order.current = { stamp, place };
    }
    const place = order.current.place;
    // a server new since the order was taken goes to the end of its tier
    const extra = new Map<string, number>();
    const at = (s: ServerView) => {
      const p = place.get(s.key);
      if (p) return p;
      const tier: Tier = sort.key === "best" ? tierOf(s, bookmarks) : flatTier(s, bookmarks);
      const n = (extra.get(tier) ?? 0) + 1;
      extra.set(tier, n);
      return { tier, rank: 100000 + n };
    };
    const placed = shown.map((s) => ({ s, ...at(s) }));
    placed.sort((a, b) => TIER_ORDER.indexOf(a.tier) - TIER_ORDER.indexOf(b.tier) || a.rank - b.rank);
    const main = placed.filter((p) => p.tier !== "empty");
    const empty = placed.filter((p) => p.tier === "empty").map((p) => p.s);
    // group heads only where there is more than one group to tell apart (EMPTY counts), and
    // only for Best
    const heads = sort.key === "best" && (mine || new Set(main.map((p) => p.tier)).size + (empty.length ? 1 : 0) > 1);
    const sections: Section[] = [];
    for (const p of main) {
      const last = sections[sections.length - 1];
      if (!last || (heads && last.tier !== p.tier)) sections.push({ tier: heads ? p.tier : null, rows: [] });
      sections[sections.length - 1].rows.push(p.s);
    }
    return { main: sections, empty };
    // `stamp` carries filters, query, sort, bookmarks and reorder
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [views, stamp]);

  const mainRows = useMemo(() => board.main.flatMap((s) => s.rows), [board]);
  // a search, or a list with nothing but empty rooms, shows them unfolded under a plain head (a
  // search for an empty room finds it); otherwise EMPTY folds, its head a stop for the keys
  const emptyForced = query.trim() !== "" || mainRows.length === 0;
  const emptyShown = board.empty.length > 0 && (emptyForced || emptyOpen);
  const emptyHead = board.empty.length > 0 && !emptyForced;
  const rows = useMemo(() => (emptyShown ? [...mainRows, ...board.empty] : mainRows), [emptyShown, mainRows, board]);
  // where the board's keys stop: the rows, the EMPTY head among them
  const stops = useMemo(() => {
    // My servers first, above the board's own groups
    const out = [...shownSaved.map(savedStop), ...mainRows.map((r) => r.key)];
    if (emptyHead) out.push(EMPTY_STOP);
    if (emptyShown) out.push(...board.empty.map((r) => r.key));
    return out;
  }, [shownSaved, mainRows, emptyHead, emptyShown, board]);
  const focusIndex = Math.max(0, stops.indexOf(focusKey ?? ""));
  const onHead = stops[focusIndex] === EMPTY_STOP;
  // a My servers row: the pane is Direct Connect's, for its address
  const onSaved = stops[focusIndex]?.startsWith(SAVED_STOP) ? (shownSaved.find((s) => savedStop(s) === stops[focusIndex]) ?? null) : null;
  const focused: ServerView | null = onHead || onSaved ? null : (rows.find((r) => r.key === stops[focusIndex]) ?? null);
  if (focused) lastRow.current = focused.key;
  // on the EMPTY head the details keep the server last looked at
  const shown: ServerView | null = focused ?? rows.find((r) => r.key === lastRow.current) ?? rows[0] ?? null;
  const focusId = onHead ? EMPTY_HEAD_ID : onSaved ? "sb-row-" + rowId(savedStop(onSaved)) : focused ? "sb-row-" + rowId(focused.key) : null;
  // the pane: Direct Connect's for a typed address or a My servers row, else the focused server's
  const directShown = DIRECT_OFFERED && (directOpen || !!onSaved);
  const paneAddress = directOpen || !onSaved ? typed : onSaved.address;
  const paneSignal = directOpen || !onSaved ? signal : onSaved.signalPort !== null ? String(onSaved.signalPort) : "";
  const paneAddr = directOpen || !onSaved ? typedAddr : parseAddress(onSaved.address);
  const paneKey = "error" in paneAddr ? null : paneAddr.key;
  const paneProbe = paneKey ? (probes.get(paneKey) ?? null) : null;

  // the focus stays on its server through refreshes; when it leaves the list, the EMPTY head
  // (its group folded under it) or the first row
  useEffect(() => {
    if (!stops.length || stops.includes(focusKey ?? "")) return;
    setFocusKey(emptyHead && board.empty.some((r) => r.key === focusKey) ? EMPTY_STOP : stops[0]);
  }, [stops, focusKey, emptyHead, board]);

  useEffect(() => {
    const el = focusId ? document.getElementById(focusId) : null;
    el?.scrollIntoView?.({ block: "nearest" });
  }, [focusId]);

  // the page opens with the board's focus
  useEffect(() => {
    if (open) listRef.current?.focus({ preventScroll: true });
    else {
      setDetail(false);
      setRegionOpen(false);
    }
  }, [open]);

  const counts = useMemo(() => {
    const scoped = (sc: Scope) => views.filter((v) => inScope(v, sc)).length;
    return { all: views.length, official: scoped("official"), community: scoped("community") };
  }, [views]);
  // the regions on the list (a stored one no server is in stays choosable, with 0)
  const regions = useMemo(() => {
    const list = choices(views, "region");
    return filters.region && !list.includes(filters.region) ? [...list, filters.region] : list;
  }, [views, filters.region]);
  const regionCounts = useMemo(() => {
    const m = new Map<string | null, number>([[null, views.length]]);
    for (const v of views) if (v.region) m.set(v.region, (m.get(v.region) ?? 0) + 1);
    return m;
  }, [views]);

  const move = useCallback(
    (to: number) => {
      if (!stops.length) return;
      setFocusKey(stops[Math.max(0, Math.min(stops.length - 1, to))]);
      leaveDirect();
    },
    // leaveDirect only sets state
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [stops]
  );

  // the board's focus moves on: the typed address's pane gives way (what was typed stays)
  function leaveDirect() {
    setDirectOpen(false);
    setWarning(null);
    setDirectFailure(null);
  }

  // JOIN for an address: the blocklist, the way in, then the warning before the first join to it
  const joinDirect = async (addr: DirectAddress, signalText: string, skipWarning: boolean) => {
    setDirectFailure(null);
    const p = probes.get(addr.key);
    const answer = p?.state === "done" ? p.probe : null;
    if (answer?.blocked || (await isBlocked(addr.host, answer?.address))) {
      setBlockedKeys((b) => new Set(b).add(addr.key));
      return;
    }
    const chosen = /^\d{1,5}$/.test(signalText) && Number(signalText) >= 1 && Number(signalText) <= 65535 ? Number(signalText) : null;
    const entry = directEntry(addr, chosen, answer);
    if (!entry) {
      setDirectFailure(t("connect.direct.needsPlatform"));
      return;
    }
    if (!skipWarning && !warnedFor(addr.key)) {
      setWarning(addr.key);
      setUnderstood(false);
      window.setTimeout(() => document.getElementById("direct-understood")?.focus({ preventScroll: true }), 0);
      return;
    }
    onJoin(entry, false);
  };

  const saveDirect = (addr: DirectAddress, signalText: string) => {
    const p = probes.get(addr.key);
    const name = p?.state === "done" ? p.probe.name : null;
    const address = addr.kind === "ip" ? addr.key : addr.url;
    const entry: SavedServer = { address, signalPort: /^\d{1,5}$/.test(signalText) ? Number(signalText) : null, name };
    const next = [entry, ...saved.filter((s) => s.address !== address)];
    setSaved(next);
    storeSaved(next);
    // the saved row takes the focus, the pane its own (REMOVE in SAVE's place)
    setDirectOpen(false);
    setWarning(null);
    setFocusKey(savedStop(entry));
    window.setTimeout(() => directJoinRef.current?.focus({ preventScroll: true }), 0);
  };

  const removeSaved = (s: SavedServer) => {
    const at = stops.indexOf(savedStop(s));
    const next = saved.filter((x) => x.address !== s.address);
    setSaved(next);
    storeSaved(next);
    const rest = stops.filter((k) => k !== savedStop(s));
    setFocusKey(rest[Math.min(Math.max(0, at), rest.length - 1)] ?? null);
    listRef.current?.focus({ preventScroll: true });
    if (narrow()) setDetail(false);
  };

  const join = useCallback(
    (v: ServerView | null, spectate: boolean) => {
      if (!v || (!spectate && isFull(v))) return;
      onJoin(v.entry, spectate);
    },
    [onJoin]
  );

  const openDetail = useCallback(() => {
    if (!focused && !onSaved) return;
    setDetail(true);
    // the act that leads: JOIN, else the first one that can be pressed
    window.setTimeout(() => {
      const join = joinRef.current ?? directJoinRef.current;
      const first = join && !join.disabled ? join : detailRef.current?.querySelector<HTMLElement>("button:not([disabled])");
      first?.focus({ preventScroll: true });
    }, 0);
  }, [focused, onSaved]);

  // EMPTY unfolds (its group brought up under the board's top) or folds, the focus on its head
  const foldEmpty = useCallback((unfold: boolean) => {
    setEmptyOpen(unfold);
    storeEmptyOpen(unfold);
    setFocusKey(EMPTY_STOP);
    if (!unfold) return;
    window.requestAnimationFrame(() => {
      const list = listRef.current;
      const group = emptyRef.current;
      if (!list || !group) return;
      const by = group.getBoundingClientRect().top - list.getBoundingClientRect().top;
      if (by > 0) list.scrollTo({ top: list.scrollTop + by, behavior: "smooth" });
    });
  }, []);

  const toggleBookmark = (key: string) => {
    setBookmarks((old) => {
      const next = new Set(old);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      storeBookmarks(next);
      return next;
    });
  };

  const openRegion = () => {
    setRegionAt(Math.max(0, regions.indexOf(filters.region)));
    setRegionOpen(true);
  };
  const pickRegion = (r: string | null) => {
    setFilters((f) => ({ ...f, region: r }));
    setRegionOpen(false);
    regionRef.current?.focus({ preventScroll: true });
  };
  // a press anywhere else puts the dropdown away
  useEffect(() => {
    if (!regionOpen) return;
    const down = (e: PointerEvent) => {
      if (!(e.target instanceof Element && e.target.closest(".sb-pick-wrap"))) setRegionOpen(false);
    };
    document.addEventListener("pointerdown", down, true);
    return () => document.removeEventListener("pointerdown", down, true);
  }, [regionOpen]);

  const refresh = () => {
    manual.current = true;
    onRefresh();
    // My servers are asked again (the effect above asks what has no answer)
    setProbes((m) => {
      const next = new Map(m);
      for (const s of saved) {
        const addr = parseAddress(s.address);
        if (!("error" in addr)) next.delete(addr.key);
      }
      return next;
    });
  };

  const clear = () => {
    setFilters(NO_FILTERS);
    setQuery("");
    listRef.current?.focus({ preventScroll: true });
  };

  const sortOn = (key: SortKey) =>
    setSort((s) => (key === "best" ? { key, asc: false } : s.key === key ? { key, asc: !s.asc } : { key, asc: key !== "players" }));

  useImperativeHandle(ref, () => ({
    key(e: KeyboardEvent): boolean {
      const target = e.target as HTMLElement | null;
      const inDetail = !!(target && detailRef.current?.contains(target));
      // the REGION dropdown, down: its keys until it is put away (Tab is App's: the blur puts it away)
      if (regionOpen) {
        const last = regions.length - 1;
        switch (e.key) {
          case "ArrowDown":
            setRegionAt((i) => Math.min(last, i + 1));
            break;
          case "ArrowUp":
            setRegionAt((i) => Math.max(0, i - 1));
            break;
          case "Home":
          case "PageUp":
            setRegionAt(0);
            break;
          case "End":
          case "PageDown":
            setRegionAt(last);
            break;
          case "Enter":
          case " ":
            pickRegion(regions[Math.min(regionAt, last)] ?? null);
            break;
          case "Escape":
            setRegionOpen(false);
            regionRef.current?.focus({ preventScroll: true });
            break;
          default: {
            // a letter: the next region that starts with it
            if (e.key.length !== 1 || e.ctrlKey || e.metaKey || e.altKey) return false;
            const k = e.key.toLowerCase();
            for (let j = 1; j <= regions.length; j++) {
              const i = (regionAt + j) % regions.length;
              if ((regions[i] ?? t("connect.region.any")).toLowerCase().startsWith(k)) {
                setRegionAt(i);
                break;
              }
            }
          }
        }
        e.preventDefault();
        return true;
      }
      if (e.key === "/" && !e.ctrlKey && !e.metaKey && !(isTextField(target) && target !== findRef.current)) {
        e.preventDefault();
        findRef.current?.focus();
        return true;
      }
      if (e.key === "Escape") {
        // the warning steps back to the acts first
        if (warning && inDetail) {
          e.preventDefault();
          setWarning(null);
          window.setTimeout(() => directJoinRef.current?.focus({ preventScroll: true }), 0);
          return true;
        }
        if (detail && narrow()) {
          e.preventDefault();
          setDetail(false);
          leaveDirect();
          listRef.current?.focus({ preventScroll: true });
          return true;
        }
        if (inDetail) {
          e.preventDefault();
          leaveDirect();
          listRef.current?.focus({ preventScroll: true });
          return true;
        }
        return false; // App: back to the queue
      }
      // REGION, put away: Down, Up, Enter or Space bring it down; Left / Right step it
      if (target && target === regionRef.current) {
        if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
          e.preventDefault();
          setFilters((f) => ({ ...f, region: cycle(regions, f.region, e.key === "ArrowLeft" ? -1 : 1) }));
          return true;
        }
        if (e.key === "ArrowDown" || e.key === "ArrowUp" || e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          openRegion();
          return true;
        }
      }
      if (inDetail) {
        // a field's caret keeps Left (Direct Connect's address)
        if (e.key === "ArrowLeft" && !narrow() && !(isTextField(target) && target?.getAttribute("type") !== "checkbox")) {
          e.preventDefault();
          listRef.current?.focus({ preventScroll: true });
          return true;
        }
        if (e.key === "ArrowUp" || e.key === "ArrowDown") {
          e.preventDefault();
          moveFocus(e.key === "ArrowUp", detailRef.current);
          return true;
        }
      }
      // a button: Enter or Space presses it (the client takes the keys from the page)
      if ((e.key === "Enter" || e.key === " ") && target && target.tagName === "BUTTON") {
        e.preventDefault();
        if (!(target as HTMLButtonElement).disabled) target.click();
        return true;
      }
      const onBoard = !target || target === listRef.current || target === document.body || target.id === "canvas";
      if (!onBoard) return false;
      const page = Math.max(1, Math.floor((listRef.current?.clientHeight ?? 600) / 72) - 1);
      switch (e.key) {
        case "ArrowDown":
          move(focusIndex + 1);
          break;
        case "ArrowUp":
          move(focusIndex - 1);
          break;
        case "PageDown":
          move(focusIndex + page);
          break;
        case "PageUp":
          move(focusIndex - page);
          break;
        case "Home":
          move(0);
          break;
        case "End":
          move(stops.length - 1);
          break;
        case "ArrowRight":
          if (onHead) foldEmpty(true);
          else openDetail();
          break;
        case "ArrowLeft":
          // on the EMPTY head it folds the group; on one of its rows, back to the head
          if (onHead && emptyOpen) foldEmpty(false);
          else if (emptyHead && focused && board.empty.includes(focused)) setFocusKey(EMPTY_STOP);
          else return false;
          break;
        case " ":
          if (!onHead) return false;
          foldEmpty(!emptyOpen);
          break;
        case "Enter":
          // EMPTY's head folds; wide: the details are already up, so Enter joins (or opens them
          // when it cannot)
          if (onHead) foldEmpty(!emptyOpen);
          else if (!narrow() && onSaved) {
            const addr = parseAddress(onSaved.address);
            if (!("error" in addr) && !blockedKeys.has(addr.key)) void joinDirect(addr, onSaved.signalPort !== null ? String(onSaved.signalPort) : "", false);
          } else if (!narrow() && focused && !isFull(focused)) join(focused, false);
          else openDetail();
          break;
        default:
          return false;
      }
      e.preventDefault();
      return true;
    }
  }));

  // ---- the page ----

  const renderRow = (v: ServerView) => (
    <ServerRow
      key={v.key}
      view={v}
      on={!directOpen && !!focused && focused.key === v.key}
      onPick={() => {
        setFocusKey(v.key);
        listRef.current?.focus({ preventScroll: true });
        if (narrow()) openDetail();
      }}
      onJoin={() => join(v, false)}
    />
  );

  return (
    <div className={"sb" + (detail ? " is-detail" : "")} id="servers">
      <button className="sb-home" type="button" aria-label={t("connect.browser.home")} tabIndex={-1} onClick={onBack} />
      <button
        className="sb-back"
        type="button"
        aria-keyshortcuts="Escape"
        onClick={() => (detail && narrow() ? (setDetail(false), listRef.current?.focus({ preventScroll: true })) : onBack())}
      >
        <Icon name="arrow-left" className="sb-back-icon" />
        {t("connect.browser.back")}
      </button>

      <header className="sb-plate">
        <h2 className="sb-h">{t("connect.browser.title")}</h2>
        <div className="sb-tools">
          <label className="sb-find">
            <span className="sb-find-key">{t("connect.browser.find")}</span>
            <input
              ref={findRef}
              type="search"
              value={query}
              spellCheck={false}
              autoComplete="off"
              aria-keyshortcuts="/"
              onChange={(e) => setQuery(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Escape") {
                  e.preventDefault();
                  e.stopPropagation();
                  if (query) setQuery("");
                  else listRef.current?.focus({ preventScroll: true });
                } else if (e.key === "ArrowDown" || e.key === "Enter") {
                  e.preventDefault();
                  listRef.current?.focus({ preventScroll: true });
                }
              }}
            />
            {!query && <kbd className="sb-find-kbd">/</kbd>}
          </label>
          {DIRECT_OFFERED && gateway && (
            <button
              className={"sb-cell sb-direct" + (directOpen ? " is-on" : "")}
              type="button"
              id="direct-open"
              aria-label={t("connect.direct.openLabel")}
              aria-expanded={directOpen}
              aria-controls="direct-pane"
              onClick={() => (directOpen ? addressRef.current?.focus({ preventScroll: true }) : openDirect())}
            >
              <Icon name="link" className="sb-cell-icon" />
              <span className="sb-cell-w">{t("connect.direct.open")}</span>
            </button>
          )}
          <button className="sb-cell" type="button" onClick={refresh} aria-label={t("connect.browser.refreshLabel")}>
            <Icon name="refresh" className="sb-cell-icon" />
            {t("connect.browser.refresh")}
          </button>
        </div>
      </header>

      <div className="sb-filters" role="toolbar" aria-label={t("connect.browser.filters")}>
        {/* ALL · OFFICIAL · COMMUNITY are hidden for now: no server is
            called official on the board */}
        {SHOW_SCOPES && (
          <>
            <div className="sb-scopes" role="radiogroup" aria-label={t("connect.browser.scopes")}>
              {SCOPES.map((s) => (
                <button
                  key={s.id}
                  type="button"
                  role="radio"
                  aria-checked={filters.scope === s.id}
                  className={"sb-tab" + (filters.scope === s.id ? " is-on" : "")}
                  onClick={() => setFilters((f) => ({ ...f, scope: s.id }))}
                >
                  {t(s.title)}
                  <span className="sb-tab-n">{num(counts[s.id])}</span>
                </button>
              ))}
            </div>
            <span className="sb-sep" aria-hidden="true" />
          </>
        )}
        <RegionPick
          buttonRef={regionRef}
          value={filters.region}
          options={regions}
          counts={regionCounts}
          open={regionOpen}
          at={regionAt}
          onToggle={() => (regionOpen ? setRegionOpen(false) : openRegion())}
          onHover={setRegionAt}
          onPick={pickRegion}
          onBlur={() => setRegionOpen(false)}
        />
      </div>

      {loaded && !feedOk && (
        <p className="sb-fail" role="alert">
          <span className="sb-fail-cell">{t("connect.browser.failCell")}</span>
          <span className="sb-fail-say">{t("connect.browser.failSay")}</span>
        </p>
      )}

      <div className="sb-main">
        <section className="sb-board" aria-label={t("connect.browser.list")}>
          <div className="sb-head" role="presentation">
            <span className="sb-c-name">
              <button type="button" className={"sb-sort" + (sort.key === "name" ? " is-on" + (sort.asc ? " is-asc" : "") : "")} onClick={() => sortOn("name")}>
                {t("connect.browser.col.server")}
              </button>
              <span className="sb-dot" aria-hidden="true">
                ·
              </span>
              <button type="button" className={"sb-sort" + (sort.key === "best" ? " is-on is-best" : "")} onClick={() => sortOn("best")}>
                {sort.key === "best" ? t("connect.browser.sortedBest") : t("connect.browser.best")}
              </button>
            </span>
            {COLUMNS.map((c) => (
              <span key={c.key} className={c.cls}>
                <button type="button" className={"sb-sort" + (sort.key === c.key ? " is-on" + (sort.asc ? " is-asc" : "") : "")} onClick={() => sortOn(c.key)}>
                  {t(c.title)}
                </button>
              </span>
            ))}
          </div>

          {!loaded ? (
            <ul className="sb-list is-loading" aria-busy="true" aria-label={t("connect.browser.list")}>
              {[0, 1, 2, 3, 4, 5].map((i) => (
                <li key={i} className="sb-row is-ghost" aria-hidden="true">
                  <span className="sb-name">
                    <b className="sb-name-text sb-dash">—</b>
                    <small className="sb-sub">—</small>
                  </span>
                  <span className="sb-c-players sb-dash">—</span>
                  <span className="sb-c-ping sb-dash">—</span>
                  <span className="sb-c-region sb-dash">—</span>
                </li>
              ))}
            </ul>
          ) : stops.length === 0 ? (
            <div className="sb-none">
              <p className="sb-none-h">{t("connect.browser.none")}</p>
              <p className="sb-none-say">{query ? (filtersOn(filters) ? t("connect.browser.noneQueryFiltered", { query }) : t("connect.browser.noneQuery", { query })) : t("connect.browser.noneFilters")}</p>
              <button type="button" className="sb-act" onClick={clear}>
                {t("connect.browser.clear")}
              </button>
            </div>
          ) : (
            <ul
              className="sb-list"
              id="server-list"
              ref={listRef}
              role="listbox"
              aria-label={t("connect.browser.list")}
              tabIndex={0}
              aria-activedescendant={focusId ?? undefined}
            >
              {shownSaved.length > 0 && (
                <li className="sb-tier is-mine" role="presentation">
                  <ul className="sb-tier-rows" role="group" aria-label={t("connect.tier.mine")}>
                    <li className="sb-group" role="presentation">
                      {t("connect.tier.mine")}
                      <span className="sb-group-n">{num(shownSaved.length)}</span>
                    </li>
                    {shownSaved.map((s) => {
                      const addr = parseAddress(s.address);
                      const key = "error" in addr ? s.address : addr.key;
                      return (
                        <SavedRow
                          key={savedStop(s)}
                          id={"sb-row-" + rowId(savedStop(s))}
                          server={s}
                          probe={probes.get(key) ?? null}
                          blocked={blockedKeys.has(key)}
                          on={!directOpen && !!onSaved && onSaved.address === s.address}
                          onPick={() => {
                            setFocusKey(savedStop(s));
                            leaveDirect();
                            listRef.current?.focus({ preventScroll: true });
                            if (narrow()) openDetail();
                          }}
                          onJoin={() => !("error" in addr) && void joinDirect(addr, s.signalPort !== null ? String(s.signalPort) : "", false)}
                        />
                      );
                    })}
                  </ul>
                </li>
              )}
              {board.main.map((sec, si) => {
                if (!sec.tier) return sec.rows.map(renderRow);
                return (
                  <li key={"g-" + sec.tier + si} className="sb-tier" role="presentation">
                    <ul className="sb-tier-rows" role="group" aria-label={t(TIER_TITLE[sec.tier])}>
                      <li className="sb-group" role="presentation">
                        {t(TIER_TITLE[sec.tier])}
                        <span className="sb-group-n">{num(sec.rows.length)}</span>
                      </li>
                      {sec.rows.map(renderRow)}
                    </ul>
                  </li>
                );
              })}
              {board.empty.length > 0 && (
                <li className="sb-tier is-empty" role="presentation" ref={emptyRef}>
                  <ul className="sb-tier-rows" role="group" aria-label={t(TIER_TITLE.empty)}>
                    {emptyHead ? (
                      // the fold: a stop for the board's keys (Enter, Space, Right, Left) and a press
                      <li
                        id={EMPTY_HEAD_ID}
                        className={"sb-group sb-fold" + (emptyShown ? " is-open" : "") + (onHead ? " is-on" : "")}
                        role="option"
                        aria-selected={onHead}
                        aria-expanded={emptyShown}
                        onClick={() => {
                          listRef.current?.focus({ preventScroll: true });
                          foldEmpty(!emptyOpen);
                        }}
                      >
                        <Icon name="chevron" className="sb-fold-chev" />
                        {t(TIER_TITLE.empty)}
                        <span className="sb-group-n">{num(board.empty.length)}</span>
                        <span className="sb-fold-say">{emptyShown ? t("connect.browser.hide") : t("connect.browser.show")}</span>
                      </li>
                    ) : (
                      <li className="sb-group" role="presentation">
                        {t(TIER_TITLE.empty)}
                        <span className="sb-group-n">{num(board.empty.length)}</span>
                      </li>
                    )}
                    {emptyShown && board.empty.map(renderRow)}
                  </ul>
                </li>
              )}
            </ul>
          )}
        </section>

        <aside className="sb-detail" ref={detailRef} aria-label={t("connect.detail.label")}>
          {directShown ? (
            <DirectPane
              key={directOpen || !onSaved ? "typed" : savedStop(onSaved)}
              saved={!directOpen && !!onSaved}
              savedName={!directOpen && onSaved ? onSaved.name : null}
              address={paneAddress}
              parsed={paneAddr}
              signal={paneSignal}
              more={more}
              probe={paneProbe}
              blocked={!!paneKey && blockedKeys.has(paneKey)}
              gateway={gateway}
              warning={!!paneKey && warning === paneKey}
              understood={understood}
              failure={directFailure}
              addressRef={addressRef}
              joinRef={directJoinRef}
              onAddress={(v) => {
                setTyped(v);
                setWarning(null);
                setDirectFailure(null);
              }}
              onSignal={setSignal}
              onMore={() => setMore((m) => !m)}
              onJoin={() => !("error" in paneAddr) && void joinDirect(paneAddr, paneSignal, false)}
              onUnderstood={setUnderstood}
              onContinue={() => {
                if ("error" in paneAddr || !understood) return;
                noteWarned(paneAddr.key);
                setWarning(null);
                void joinDirect(paneAddr, paneSignal, true);
              }}
              onCancelWarning={() => {
                setWarning(null);
                window.setTimeout(() => directJoinRef.current?.focus({ preventScroll: true }), 0);
              }}
              onSave={() => !("error" in paneAddr) && saveDirect(paneAddr, paneSignal)}
              onRemove={() => onSaved && removeSaved(onSaved)}
              onDone={() => {
                leaveDirect();
                if (narrow()) setDetail(false);
                listRef.current?.focus({ preventScroll: true });
              }}
            />
          ) : shown ? (
            <Details
              view={shown}
              bookmarked={bookmarks.has(shown.key)}
              release={release}
              joinRef={joinRef}
              onRelease={() => setRelease((r) => !r)}
              onJoin={(spectate) => join(shown, spectate)}
              onBookmark={() => toggleBookmark(shown.key)}
            />
          ) : null}
        </aside>
      </div>

    </div>
  );
});

// an id from a URL that the DOM takes
function rowId(key: string): string {
  let h = 0;
  for (let i = 0; i < key.length; i++) h = (h * 31 + key.charCodeAt(i)) | 0;
  return (h >>> 0).toString(36);
}

// REGION: a dropdown cell in the filter line's words. The focus stays on the cell (a select-only
// combobox: the options are its activedescendant); the keys are the board's key() above
function RegionPick({
  buttonRef,
  value,
  options,
  counts,
  open,
  at,
  onToggle,
  onHover,
  onPick,
  onBlur
}: {
  buttonRef: React.RefObject<HTMLButtonElement | null>;
  value: string | null;
  options: Array<string | null>;
  counts: ReadonlyMap<string | null, number>;
  open: boolean;
  at: number;
  onToggle(): void;
  onHover(i: number): void;
  onPick(r: string | null): void;
  onBlur(): void;
}) {
  const word = (r: string | null) => r ?? t("connect.region.any");
  useEffect(() => {
    if (open) document.getElementById("sb-region-o-" + at)?.scrollIntoView?.({ block: "nearest" });
  }, [open, at]);
  return (
    <span className="sb-pick-wrap">
      <span className="sb-choice-key" aria-hidden="true">
        {t("connect.region.key")}
      </span>
      <button
        ref={buttonRef}
        type="button"
        role="combobox"
        aria-haspopup="listbox"
        aria-expanded={open}
        aria-controls="sb-region-list"
        aria-activedescendant={open ? "sb-region-o-" + at : undefined}
        aria-label={t("connect.region.label", { region: word(value) })}
        className={"sb-pick" + (value ? " is-set" : "") + (open ? " is-open" : "")}
        onClick={onToggle}
        onBlur={onBlur}
      >
        <span className="sb-pick-v">{word(value)}</span>
        <Icon name="chevron" className="sb-pick-chev" />
      </button>
      {open && (
        // a press on an option must not take the focus from the cell
        <ul className="sb-menu" id="sb-region-list" role="listbox" aria-label={t("connect.region.key")} onMouseDown={(e) => e.preventDefault()}>
          {options.map((r, i) => (
            <li
              key={r ?? "\u0000any"}
              id={"sb-region-o-" + i}
              role="option"
              aria-selected={r === value}
              className={"sb-menu-o" + (i === at ? " is-at" : "") + (r === value ? " is-on" : "")}
              onMouseEnter={() => onHover(i)}
              onClick={() => onPick(r)}
            >
              <span className="sb-menu-v">{word(r)}</span>
              <span className="sb-menu-n">{num(counts.get(r) ?? 0)}</span>
            </li>
          ))}
        </ul>
      )}
    </span>
  );
}

// a count that turns over in place when a refresh changes it
function Count({ value, children }: { value: string; children: ReactNode }) {
  const prev = useRef(value);
  const turned = prev.current !== value;
  useEffect(() => {
    prev.current = value;
  });
  return (
    <span key={value} className={"sb-turn" + (turned ? " is-new" : "")}>
      {children}
    </span>
  );
}

function Players({ v }: { v: ServerView }) {
  if (v.humans === null) return <span className="sb-dash">—</span>;
  const max = v.max !== null ? " / " + num(v.max) : "";
  if (isFull(v))
    return (
      <span className="sb-pl">
        <span className="sb-full">{t("connect.browser.full")}</span>
        <span className="sb-of">{num(v.humans) + max}</span>
      </span>
    );
  return (
    <span className={"sb-pl" + (v.humans > 0 ? " is-live" : "")}>
      {v.humans > 0 && <i className="sb-live" aria-hidden="true" />}
      <Count value={String(v.humans)}>{num(v.humans)}</Count>
      <span className="sb-max">{max}</span>
      {!!v.bots && <span className="sb-bots" title={t("connect.browser.bots", { n: v.bots })}>+{num(v.bots)}</span>}
    </span>
  );
}

function Ping({ v }: { v: ServerView }) {
  if (v.ping === null) return <span className="sb-dash">—</span>;
  return <span className={"sb-ping" + (v.ping >= 80 ? " is-far" : "")}>{(v.pingEstimated ? "~" : "") + num(v.ping)}</span>;
}

function Badges({ v, big }: { v: ServerView; big?: boolean }) {
  return (
    <>
      {SHOW_OFFICIAL && v.official && <span className={"sb-badge is-official" + (big ? " is-big" : "")}>{t("connect.badge.official")}</span>}
      {v.moderated && <span className={"sb-badge is-moderated" + (big ? " is-big" : "")}>{t("connect.badge.moderated")}</span>}
    </>
  );
}

function ServerRow({ view: v, on, onPick, onJoin }: { view: ServerView; on: boolean; onPick(): void; onJoin(): void }) {
  return (
    <li
      id={"sb-row-" + rowId(v.key)}
      className={"sb-row" + (on ? " is-on" : "")}
      role="option"
      aria-selected={on}
      onClick={onPick}
      onDoubleClick={onJoin}
    >
      <span className="sb-name">
        <b className="sb-name-line">
          <span className="sb-name-text">
            <Colored text={v.name} />
          </span>
        </b>
        <small className="sb-sub">
          <Badges v={v} />
          {v.friends > 0 && (
            <span className="sb-friends">
              <i className="sb-here" aria-hidden="true" />
              {t("connect.browser.friends", { n: v.friends })}
            </span>
          )}
          {v.friends > 0 && v.description && " · "}
          <span className="sb-desc">{v.description}</span>
        </small>
      </span>
      <span className="sb-c-players">
        <Players v={v} />
      </span>
      <span className="sb-c-ping">
        <Ping v={v} />
      </span>
      <span className="sb-c-region sb-region">{v.region ?? <span className="sb-dash">—</span>}</span>
    </li>
  );
}

// a My servers row: the name the server gives (else the one it gave last, else the
// address), the address under it, and the players its probe counted; no ping, no region
function SavedRow({ id, server, probe, blocked, on, onPick, onJoin }: { id: string; server: SavedServer; probe: Probe | null; blocked: boolean; on: boolean; onPick(): void; onJoin(): void }) {
  const answer = probe?.state === "done" ? probe.probe : null;
  const name = answer?.name || server.name;
  const down = !!answer && !answer.reachable;
  return (
    <li id={id} className={"sb-row is-saved" + (on ? " is-on" : "")} role="option" aria-selected={on} onClick={onPick} onDoubleClick={onJoin}>
      <span className="sb-name">
        <b className="sb-name-line">
          <span className="sb-name-text">{name ? <Colored text={name} /> : server.address}</span>
        </b>
        <small className="sb-sub">
          <span className="sb-badge is-direct">{t("connect.badge.direct")}</span>
          <span className="sb-address">{server.address}</span>
          {blocked ? <span className="sb-saved-state">{t("connect.direct.rowBlocked")}</span> : down ? <span className="sb-saved-state">{t("connect.direct.rowDown")}</span> : null}
        </small>
      </span>
      <span className="sb-c-players">
        {answer && answer.reachable && answer.players !== null ? (
          <span className={"sb-pl" + (answer.players > 0 ? " is-live" : "")}>
            {answer.players > 0 && <i className="sb-live" aria-hidden="true" />}
            <Count value={String(answer.players)}>{num(answer.players)}</Count>
            <span className="sb-max">{answer.maxPlayers !== null ? " / " + num(answer.maxPlayers) : ""}</span>
          </span>
        ) : (
          <span className="sb-dash">—</span>
        )}
      </span>
      <span className="sb-c-ping sb-dash">—</span>
      <span className="sb-c-region sb-dash">—</span>
    </li>
  );
}

// a logged-in player's handle, unless the name already says it
function shownHandle(name: string, handle: string | null): handle is string {
  return !!handle && stripColors(name).trim().toLowerCase() !== handle.toLowerCase();
}

function Details({
  view: v,
  bookmarked,
  release,
  joinRef,
  onJoin,
  onBookmark,
  onRelease
}: {
  view: ServerView;
  bookmarked: boolean;
  release: boolean;
  joinRef: React.RefObject<HTMLButtonElement | null>;
  onJoin(spectate: boolean): void;
  onBookmark(): void;
  onRelease(): void;
}) {
  const full = isFull(v);
  // nobody to watch in an empty room: no SPECTATE (a room with no count keeps it)
  const watchable = v.humans !== 0;
  // the roster shows whole rows: its height is cut on a row's edge and the rest scrolls; the acts
  // follow it, so what is left of the pane falls below them, not in a gap above them
  const paneRef = useRef<HTMLDivElement>(null);
  const bodyRef = useRef<HTMLDivElement>(null);
  const rosterRef = useRef<HTMLUListElement>(null);
  const actsRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const pane = paneRef.current;
    const body = bodyRef.current;
    if (!pane || !body || typeof ResizeObserver === "undefined") return;
    const fit = () => {
      const list = rosterRef.current;
      if (!list) return;
      list.style.maxHeight = "";
      const row = list.firstElementChild as HTMLElement | null;
      if (!row || getComputedStyle(body).overflowY === "visible") return;
      const h = row.getBoundingClientRect().height;
      const room = pane.getBoundingClientRect().bottom - (actsRef.current?.offsetHeight ?? 0) - list.getBoundingClientRect().top;
      if (h > 0) list.style.maxHeight = Math.max(1, Math.floor(room / h)) * h + "px";
    };
    fit();
    const ro = new ResizeObserver(fit);
    ro.observe(pane);
    return () => ro.disconnect();
  }, [v.key, v.roster?.length, release]);
  // friends first, then by score; the bots last
  const players = v.roster ? v.roster.filter((p) => !p.bot) : [];
  const bots = v.roster ? v.roster.filter((p) => p.bot) : [];
  players.sort((a, b) => Number(b.friend) - Number(a.friend) || (b.score ?? -1) - (a.score ?? -1));
  return (
    <div className="sb-d" key={v.key} ref={paneRef}>
      <div className="sb-d-top">
        <h3 className="sb-d-h">
          <Colored text={v.name} />
        </h3>
        {/* what few players need: the release the server runs, its build included */}
        <button
          type="button"
          className={"sb-info" + (release ? " is-on" : "")}
          aria-expanded={release}
          aria-controls="sb-release"
          aria-label={release ? t("connect.detail.hideVersion") : t("connect.detail.showVersion")}
          title={release ? t("connect.detail.hideVersionTitle") : t("connect.detail.versionTitle")}
          onClick={onRelease}
        >
          <Icon name="info" />
        </button>
      </div>
      {((SHOW_OFFICIAL && v.official) || v.moderated || v.friends > 0) && (
        <p className="sb-d-badges">
          <Badges v={v} big />
          {v.friends > 0 && (
            <span className="sb-badge is-friends is-big">
              <i className="sb-here" aria-hidden="true" />
              {t("connect.browser.friends", { n: v.friends })}
            </span>
          )}
        </p>
      )}
      {v.description && <p className="sb-d-say">{v.description}</p>}
      {release && (
        <dl className="sb-release" id="sb-release">
          <dt>{t("connect.detail.version")}</dt>
          <dd className={v.release === null ? "is-none" : ""}>{v.release ?? t("connect.detail.notReported")}</dd>
        </dl>
      )}

      <div className="sb-d-body" ref={bodyRef}>
        <p className="sb-roster-h">
          {t("connect.detail.players")}
          <span className="sb-roster-n">
            {v.humans === null ? "—" : num(v.humans) + (v.max !== null ? " / " + num(v.max) : "")}
            {!!v.bots && " · " + t("connect.detail.bots", { n: v.bots })}
          </span>
        </p>
        {v.roster === null ? (
          <p className="sb-d-quiet">{t("connect.detail.noRoster")}</p>
        ) : players.length === 0 && bots.length === 0 ? (
          <p className="sb-d-quiet">{t("connect.detail.nobody")}</p>
        ) : (
          <ul className="sb-roster" ref={rosterRef}>
            {[...players, ...bots].map((p, i) => (
              <li key={i} className={"sb-player" + (p.bot ? " is-bot" : "") + (p.friend ? " is-friend" : "")}>
                {p.color && <i className="sb-swatch" style={{ background: p.color }} aria-hidden="true" />}
                <span className="sb-player-name">
                  <span className="sb-player-text">
                    <Colored text={p.name} />
                  </span>
                  {shownHandle(p.name, p.handle) && <span className="sb-handle">({p.handle})</span>}
                </span>
                {p.friend && (
                  <span className="sb-mark is-friend">
                    <i className="sb-here" aria-hidden="true" />
                    {t("connect.detail.friend")}
                  </span>
                )}
                {p.bot && <span className="sb-mark is-quiet">{t("connect.detail.bot")}</span>}
                <span className="sb-score">{p.score ?? ""}</span>
              </li>
            ))}
          </ul>
        )}
      </div>

      <div className="sb-d-acts" ref={actsRef}>
        {full && <p className="sb-d-why">{t("connect.detail.fullWhy")}</p>}
        <div className={"sb-d-row" + (watchable ? "" : " is-unwatched")}>
          <button ref={joinRef} type="button" className="sb-join" disabled={full} onClick={() => onJoin(false)}>
            {full ? t("connect.detail.full") : t("connect.detail.join")}
          </button>
          {watchable && (
            <button type="button" className="sb-spectate" onClick={() => onJoin(true)}>
              {t("connect.detail.spectate")}
            </button>
          )}
          <button
            type="button"
            className={"sb-bookmark" + (bookmarked ? " is-on" : "")}
            aria-pressed={bookmarked}
            aria-label={t("connect.detail.bookmark")}
            title={bookmarked ? t("connect.detail.bookmarkedTitle") : t("connect.detail.bookmarkTitle")}
            onClick={onBookmark}
          >
            <Icon name="bookmark" className="sb-bookmark-icon" />
            <span className="sb-bookmark-w">{bookmarked ? t("connect.detail.bookmarked") : t("connect.detail.bookmark")}</span>
          </button>
        </div>
      </div>
    </div>
  );
}
