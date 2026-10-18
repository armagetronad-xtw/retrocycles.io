// The bell (features.news): the newest news posts staff sent to every player, and which are
// unread (sent after the player last opened the bell, and after they first came: a newcomer's
// bell starts with nothing unread). The posts themselves are the site's /news pages (the
// platform writes them); the bell links there in a new tab, or outside Discord. A small store:
// asked when the platform is ready, every ten minutes, and when the bell opens; opening it
// tells the platform everything so far is read.
import { useEffect, useSyncExternalStore } from "react";
import { PlatformError, getNewsBell, markNewsSeen as postSeen } from "./client";
import { platformState, subscribePlatform } from "./session";
import type { PlatformState } from "./types";

export interface NewsItem {
  id: string;
  title: string; // the line it was sent with, else the post's title
  summary: string;
  url: string; // the post on the site, absolute
  sentAt: string; // ISO
  unread: boolean;
}

export interface NewsBell {
  items: NewsItem[];
  unread: number;
  allUrl: string; // the news page
}

export type NewsState = { status: "off" } | { status: "loading" } | { status: "failed" } | ({ status: "ready" } & NewsBell);

const REFRESH_MS = 10 * 60 * 1000;

export function newsOn(platform: PlatformState = platformState()): boolean {
  return platform.mode === "platform" && platform.config.features.news === true && !!platform.user;
}

let state: NewsState = { status: "off" };
const listeners = new Set<() => void>();
let asking: Promise<void> | null = null;

function set(next: NewsState) {
  state = next;
  for (const l of listeners) l();
}

function subscribe(l: () => void) {
  listeners.add(l);
  return () => listeners.delete(l);
}

function apiOf(platform: PlatformState): string | null {
  return platform.mode === "platform" ? platform.apiUrl : null;
}

function asBell(data: unknown): NewsBell | null {
  if (!data || typeof data !== "object") return null;
  const d = data as Record<string, unknown>;
  if (!Array.isArray(d.items) || typeof d.allUrl !== "string") return null;
  const items: NewsItem[] = [];
  for (const raw of d.items) {
    if (!raw || typeof raw !== "object") continue;
    const i = raw as Record<string, unknown>;
    // only the site's own pages (an http(s) URL), never anything a bad answer could run
    if (typeof i.id !== "string" || typeof i.title !== "string" || typeof i.url !== "string" || !/^https?:\/\//.test(i.url)) continue;
    items.push({
      id: i.id,
      title: i.title,
      summary: typeof i.summary === "string" ? i.summary : "",
      url: i.url,
      sentAt: typeof i.sentAt === "string" ? i.sentAt : "",
      unread: i.unread === true
    });
  }
  return { items, unread: items.filter((i) => i.unread).length, allUrl: /^https?:\/\//.test(d.allUrl) ? d.allUrl : "/news" };
}

// ask the platform again (quietly: a failure keeps what the bell already shows)
export function refreshNews(): Promise<void> {
  const platform = platformState();
  const apiUrl = apiOf(platform);
  if (!apiUrl || !newsOn(platform)) {
    if (state.status !== "off") set({ status: "off" });
    return Promise.resolve();
  }
  if (asking) return asking;
  if (state.status !== "ready") set({ status: "loading" });
  asking = (async () => {
    try {
      const bell = asBell(await getNewsBell(apiUrl));
      if (bell) set({ status: "ready", ...bell });
      else if (state.status !== "ready") set({ status: "failed" });
    } catch (e) {
      if (e instanceof PlatformError && e.status === 404) set({ status: "off" });
      else if (state.status !== "ready") set({ status: "failed" });
    } finally {
      asking = null;
    }
  })();
  return asking;
}

// the bell opened: everything sent so far is read. The rows keep their NEW until it opens again,
// so the player sees which ones were new; the count goes at once
export function markNewsSeen() {
  const apiUrl = apiOf(platformState());
  if (state.status !== "ready" || state.unread === 0 || !apiUrl) return;
  set({ ...state, unread: 0 });
  void postSeen(apiUrl).catch(() => {});
}

// the rows' NEW goes once the bell has been put away after being seen
export function settleNews() {
  if (state.status !== "ready" || !state.items.some((i) => i.unread) || state.unread > 0) return;
  set({ ...state, items: state.items.map((i) => (i.unread ? { ...i, unread: false } : i)) });
}

// the bell's state; asks when the platform's user is known (a new user: sign-in, New player)
export function useNews(): NewsState {
  const platform = useSyncExternalStore(subscribePlatform, platformState);
  const userId = platform.mode === "platform" ? platform.user?.id ?? null : null;
  const on = newsOn(platform);
  useEffect(() => {
    if (!on) {
      if (state.status !== "off") set({ status: "off" });
      return;
    }
    void refreshNews();
    const timer = window.setInterval(() => {
      if (document.visibilityState === "visible") void refreshNews();
    }, REFRESH_MS);
    return () => window.clearInterval(timer);
  }, [on, userId]);
  return useSyncExternalStore(subscribe, () => state);
}
