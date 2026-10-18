// Leaderboards: the scoreboard's match tally, the boards, and what the death note says.
// Nothing here without a platform whose features.leaderboards is on. The boards list accounts only;
// a guest's own row comes back to the guest alone, where it would stand (marked not saved).
//
// The death note compares the caller's place with the last place it showed them (or the one read
// when the game started), so it only ever reports news: all time first, today when all time did
// not move. A kill reaches the platform a moment after it happens, so a death reads the places
// twice at most and a change that arrives after the respawn waits for the next death.
import { useCallback, useEffect, useRef, useState } from "react";
import * as api from "./client";
import { platformState } from "./session";
import type { BoardPage, BoardRow, BoardSort, BoardWindow, MyPlace, MyPlaces, PlatformState } from "./types";

export function leaderboardsOn(platform: PlatformState = platformState()): boolean {
  return platform.mode === "platform" && platform.config.features.leaderboards === true;
}

function apiUrl(): string | null {
  const p = platformState();
  return p.mode === "platform" ? p.apiUrl : null;
}

const TALLY_MS = 2000;

export interface TallyLine {
  kills: number;
  deaths: number;
}

// the room's current match by player name, read every 2 s while the scoreboard is up; serverId
// null (not an official room) or a platform without leaderboards: nothing, and the table shows dashes
export function useMatchTally(serverId: string | null, open: boolean): ReadonlyMap<string, TallyLine> | null {
  const [tally, setTally] = useState<ReadonlyMap<string, TallyLine> | null>(null);
  useEffect(() => setTally(null), [serverId]);
  useEffect(() => {
    const url = apiUrl();
    if (!open || !serverId || !url || !leaderboardsOn()) return;
    let gone = false;
    const read = async () => {
      try {
        const match = await api.getRoomMatch(url, serverId);
        if (!gone) setTally(new Map(match.players.map((p) => [p.name, { kills: p.kills, deaths: p.deaths }])));
      } catch {
        // no match yet, or the platform is away: keep what was shown
      }
    };
    void read();
    const timer = window.setInterval(read, TALLY_MS);
    return () => {
      gone = true;
      window.clearInterval(timer);
    };
  }, [serverId, open]);
  return tally;
}

export interface BoardState {
  page: BoardPage | null;
  failed: boolean;
  loading: boolean;
  reload(): void;
}

const BOARD_MS = 20000;

// one board page, read while shown and again every 20 s; from undefined opens on the caller
export function useBoard(board: string | null, window_: BoardWindow, sort: BoardSort, shown: boolean, from?: number, count?: number): BoardState {
  const [page, setPage] = useState<BoardPage | null>(null);
  const [failed, setFailed] = useState(false);
  const [loading, setLoading] = useState(false);
  const [nonce, setNonce] = useState(0);
  const reload = useCallback(() => setNonce((n) => n + 1), []);
  useEffect(() => {
    const url = apiUrl();
    if (!shown || !board || !url || !leaderboardsOn()) return;
    let gone = false;
    const read = async () => {
      setLoading(true);
      try {
        const next = await api.getBoard(url, board, window_, sort, from, count);
        if (gone) return;
        setPage(next);
        setFailed(false);
      } catch {
        if (!gone) setFailed(true);
      } finally {
        if (!gone) setLoading(false);
      }
    };
    void read();
    const timer = window.setInterval(read, BOARD_MS);
    return () => {
      gone = true;
      window.clearInterval(timer);
    };
  }, [board, window_, sort, shown, from, count, nonce]);
  return { page: page && page.board === board && page.window === window_ && page.sort === sort ? page : null, failed, loading, reload };
}

// ---- the Leaderboards page's board: every place, scrolled through

export const BOARD_BLOCK = 50; // the platform's most rows per request

export interface BoardRows {
  // the newest answer's facts (ranked, me, resetsAt, killsToQualify); its own rows are not the board's
  meta: BoardPage | null;
  total: number; // the places the board holds: the accounts, and a guest's own row among them
  row(rank: number): BoardRow | undefined;
  want(first: number, last: number): void; // the places in view: their blocks are read once per refresh
  failed: boolean;
  reload(): void;
}

// the whole board as one list of places, read in blocks of 50 as they come into view: the first read
// opens on the caller; every 20 s the blocks in view are read again (the rest when they come back)
export function useBoardRows(board: string | null, window_: BoardWindow, sort: BoardSort, shown: boolean): BoardRows {
  const [meta, setMeta] = useState<BoardPage | null>(null);
  const [failed, setFailed] = useState(false);
  const [, setVersion] = useState(0);
  const rows = useRef(new Map<number, BoardRow>());
  const read = useRef(new Set<number>()); // blocks read (or on their way) since the last refresh
  const wanted = useRef<[number, number]>([1, 1]);
  const total = useRef(0);
  const gen = useRef(0); // a new board, window or sort: answers to the old one are dropped

  const get = useCallback((from: number | undefined, count: number): Promise<BoardPage> | null => {
    const url = apiUrl();
    if (!url || !board || !leaderboardsOn()) return null;
    return api.getBoard(url, board, window_, sort, from, count);
  }, [board, window_, sort]);

  const take = useCallback((page: BoardPage) => {
    for (const r of page.rows) rows.current.set(r.rank, r);
    total.current = page.ranked + (page.me && !page.me.saved ? 1 : 0);
    // a board that shrank: the places past its end go
    for (const rank of rows.current.keys()) if (rank > total.current) rows.current.delete(rank);
    setMeta(page);
    setFailed(false);
    setVersion((v) => v + 1);
  }, []);

  const block = useCallback((k: number) => {
    if (read.current.has(k)) return;
    const asked = get(k * BOARD_BLOCK + 1, BOARD_BLOCK);
    if (!asked) return;
    read.current.add(k);
    const g = gen.current;
    asked.then(
      (page) => g === gen.current && take(page),
      () => {
        if (g !== gen.current) return;
        read.current.delete(k);
        setFailed(true);
      }
    );
  }, [get, take]);

  const want = useCallback((first: number, last: number) => {
    wanted.current = [first, last];
    if (!total.current) return;
    const lo = Math.max(1, first);
    const hi = Math.min(total.current, last);
    for (let k = Math.floor((lo - 1) / BOARD_BLOCK); k <= Math.floor((hi - 1) / BOARD_BLOCK); k++) block(k);
  }, [block]);

  const [nonce, setNonce] = useState(0);
  const reload = useCallback(() => setNonce((n) => n + 1), []);

  // a new board, window or sort: start again, on the caller
  useEffect(() => {
    gen.current++;
    rows.current = new Map();
    read.current = new Set();
    total.current = 0;
    setMeta(null);
    setFailed(false);
  }, [board, window_, sort]);

  useEffect(() => {
    if (!shown || !board) return;
    const g = gen.current;
    const open = () => {
      if (total.current) return;
      const asked = get(undefined, BOARD_BLOCK);
      asked?.then(
        (page) => {
          if (g !== gen.current) return;
          take(page);
          want(...wanted.current);
        },
        () => g === gen.current && setFailed(true)
      );
    };
    open();
    // a reload (RETRY) asks again at once: the blocks in view, or the opening read
    if (total.current) {
      read.current = new Set();
      want(...wanted.current);
    }
    const timer = window.setInterval(() => {
      if (!total.current) return open();
      read.current = new Set();
      want(...wanted.current);
    }, BOARD_MS);
    return () => window.clearInterval(timer);
  }, [board, window_, sort, shown, nonce, get, take, want]);

  return { meta, total: meta ? total.current : 0, row: (rank) => rows.current.get(rank), want, failed, reload };
}

// ---- the caller's places (the death note, the match summary)

export async function readMyPlaces(board?: string, sort?: BoardSort): Promise<MyPlaces | null> {
  const url = apiUrl();
  if (!url || !leaderboardsOn() || platformState().mode !== "platform") return null;
  try {
    return await api.getMyPlaces(url, board, sort);
  } catch {
    return null;
  }
}

export type NoteWindow = "allTime" | "today";

export interface DeathNoteLine {
  window: NoteWindow;
  boardName: string;
  place: MyPlace;
  was: number | null; // the place last shown, null when new to the board
  saved: boolean;
}

// what was last shown, per board and window: module state, so a new room keeps it for the visit
const shown = new Map<string, number>();

function key(board: string, window: NoteWindow): string {
  return board + "/" + window;
}

// remember the places as they are now, without a note (a game starting, a note shown)
export function notePlaces(places: MyPlaces): void {
  for (const w of ["allTime", "today"] as const) {
    const p = places[w];
    if (p) shown.set(key(places.board, w), p.rank);
  }
}

// the note a death brings, or null when neither all time nor today changed since last shown
export function deathNote(places: MyPlaces): DeathNoteLine | null {
  for (const w of ["allTime", "today"] as const) {
    const p = places[w];
    if (!p) continue;
    const before = shown.get(key(places.board, w));
    if (before === p.rank) continue;
    return { window: w, boardName: places.boardName, place: p, was: before ?? null, saved: places.saved };
  }
  return null;
}

// the places read once when a game with leaderboards starts: the baseline the first death compares with
export function useLeaderboardBaseline(active: boolean): void {
  const done = useRef(false);
  useEffect(() => {
    if (!active) {
      done.current = false;
      return;
    }
    if (done.current) return;
    done.current = true;
    void readMyPlaces().then((p) => p && notePlaces(p));
  }, [active]);
}

// the boards the platform keeps (read once per load); empty without leaderboards
let boardList: { id: string; name: string }[] | null = null;
let boardListRead: Promise<void> | null = null;

export function useBoardList(enabled: boolean): { id: string; name: string }[] {
  const [list, setList] = useState(boardList ?? []);
  useEffect(() => {
    const url = apiUrl();
    if (!enabled || !url || !leaderboardsOn()) return;
    if (boardList) return setList(boardList);
    boardListRead ??= api.getBoards(url).then(
      (info) => {
        boardList = info.boards;
      },
      () => {
        boardListRead = null;
      }
    );
    let gone = false;
    void boardListRead.then(() => !gone && boardList && setList(boardList));
    return () => {
      gone = true;
    };
  }, [enabled]);
  return list;
}
