// Friends and mutes (platform-contract/README.md section 1, "Friends", "Mutes"). Two small stores the page renders from: the account's friends and requests
// (the Friends panel, the friends cell's count of requests), and the user ids this player
// muted (the chat overlay leaves their lines out in every room). And a hook that asks who the
// players of the room are (GET /v1/rooms/{serverId}/players): the accounts or guests the room's
// server verified, by the network ids every client knows them by, so a card's ADD FRIEND or a
// /mute can never land on another player with the same name. Nothing here without a platform
// whose features say so; friends need an account, mutes any session (a guest's too).
import { useCallback, useEffect, useRef, useState, useSyncExternalStore } from "react";
import * as api from "./client";
import { PlatformError } from "./client";
import { platformState } from "./session";
import type { FriendsList, PlatformState, RoomPlayerInfo } from "./types";
import { t } from "../i18n";

export function friendsOn(platform: PlatformState = platformState()): boolean {
  return platform.mode === "platform" && platform.config.features.friends === true;
}

export function mutesOn(platform: PlatformState = platformState()): boolean {
  return platform.mode === "platform" && platform.config.features.mutes === true && !!platform.user;
}

// an account is signed in (friends are between accounts)
export function friendsAccount(platform: PlatformState = platformState()): boolean {
  return friendsOn(platform) && platform.mode === "platform" && !!platform.user && !platform.user.isGuest;
}

function apiUrl(): string | null {
  const p = platformState();
  return p.mode === "platform" ? p.apiUrl : null;
}

// a failed call in the page's words (the contract's codes)
export function socialText(e: unknown, name?: string): string {
  const code = e instanceof PlatformError ? e.code : null;
  switch (code) {
    case "self":
      return t("social.error.self");
    case "not_an_account":
      return name ? t("social.error.notAnAccount", { name }) : t("social.error.notAnAccountAnon");
    case "player_not_found":
      return name ? t("social.error.noPlayer", { name }) : t("social.error.noSuchPlayer");
    case "blocked":
      return name ? t("social.error.blocked", { name }) : t("social.error.blockedAnon");
    case "too_many_requests":
      return t("social.error.tooManyRequests");
    case "too_many_friends":
      return t("social.error.tooManyFriends");
    case "too_many_mutes":
      return t("social.error.tooManyMutes");
    case "rate_limited":
      return t("social.error.rateLimited");
    case "guest":
      return t("social.error.guest");
    case "request_not_found":
      return t("social.error.requestGone");
    case "friends_only":
      return t("social.error.friendsOnly");
    case "not_accepting":
      return name ? t("social.error.notAccepting", { name }) : t("social.error.notAcceptingAnon");
    case "muted":
      return t("social.error.muted");
    case "message_blocked":
      return t("social.error.messageBlocked");
    case "too_long":
      return t("social.error.tooLong");
  }
  if (e instanceof PlatformError && e.status === 0) return t("social.error.noResponse");
  return t("social.error.refused", { why: e instanceof Error ? e.message : String(e) });
}

// ---- the friends store ------------------------------------------------------------------

let friends: FriendsList | null = null;
let friendsOwner: string | null = null;
const friendsListeners = new Set<() => void>();

function publishFriends(next: FriendsList | null, owner: string | null) {
  friends = next;
  friendsOwner = next ? owner : null;
  for (const listener of friendsListeners) listener();
}

function subscribeFriends(listener: () => void): () => void {
  friendsListeners.add(listener);
  return () => {
    friendsListeners.delete(listener);
  };
}

// the latest list for whoever is signed in now (null before the first read, for a guest, or
// after a sign-in as someone else until the next read)
function friendsSnapshot(): FriendsList | null {
  const p = platformState();
  return p.mode === "platform" && p.user && p.user.id === friendsOwner ? friends : null;
}

export function useFriends(): FriendsList | null {
  return useSyncExternalStore(subscribeFriends, friendsSnapshot, friendsSnapshot);
}

// GET /v1/me/friends; a failure keeps what the page had and is thrown for the caller to say
export async function refreshFriends(): Promise<FriendsList | null> {
  const url = apiUrl();
  const p = platformState();
  if (!url || !friendsAccount(p) || p.mode !== "platform" || !p.user) {
    publishFriends(null, null);
    return null;
  }
  const list = await api.getFriends(url);
  publishFriends(list, p.user.id);
  return list;
}

// ---- the mutes store ----------------------------------------------------------------------

const NO_MUTES: ReadonlySet<string> = new Set();
let mutes: ReadonlySet<string> = NO_MUTES;
let mutesOwner: string | null = null;
const mutesListeners = new Set<() => void>();

function publishMutes(next: ReadonlySet<string>, owner: string | null) {
  mutes = next;
  mutesOwner = owner;
  for (const listener of mutesListeners) listener();
}

function mutesSnapshot(): ReadonlySet<string> {
  const p = platformState();
  return p.mode === "platform" && p.user && p.user.id === mutesOwner ? mutes : NO_MUTES;
}

export function useMutes(): ReadonlySet<string> {
  return useSyncExternalStore(
    (listener) => {
      mutesListeners.add(listener);
      return () => {
        mutesListeners.delete(listener);
      };
    },
    mutesSnapshot,
    mutesSnapshot
  );
}

// GET /v1/me/mutes: the user ids this player muted (a failure keeps the last list)
export async function refreshMutes(): Promise<void> {
  const url = apiUrl();
  const p = platformState();
  if (!url || !mutesOn(p) || p.mode !== "platform" || !p.user) {
    publishMutes(NO_MUTES, null);
    return;
  }
  try {
    const list = await api.getMutes(url);
    publishMutes(new Set(list.map((m) => m.user.userId)), p.user.id);
  } catch (e) {
    console.info("mutes not read: " + String(e));
  }
}

// PUT/DELETE /v1/me/mutes/{userId}; the store follows at once
export async function setMute(userId: string, muted: boolean): Promise<void> {
  const url = apiUrl();
  const p = platformState();
  if (!url || p.mode !== "platform" || !p.user) throw new PlatformError("no session", 401, "not_signed_in");
  await api.setMuted(url, userId, muted);
  const next = new Set(mutesSnapshot());
  if (muted) next.add(userId);
  else next.delete(userId);
  publishMutes(next, p.user.id);
}

// ---- who is in the room -------------------------------------------------------------------

// asked again after this long when the room did not know a player yet (it learns them from
// its next heartbeat, a few seconds after the join)
const RETRY_MS = 6000;

export interface RoomPlayers {
  // the verified player behind a network id (undefined: not known, or not verified)
  byNetId: ReadonlyMap<number, RoomPlayerInfo>;
  // ask about everyone again (after a request, an accept, a mute)
  refresh(): void;
}

// the room's players by network id, asked when new ids appear (once more a little later for
// those the room did not know yet); serverId null: not an official room, nothing is asked
export function useRoomPlayers(serverId: string | null, netIds: number[]): RoomPlayers {
  const [byNetId, setByNetId] = useState<ReadonlyMap<number, RoomPlayerInfo>>(() => new Map());
  const asked = useRef(new Map<number, number>()); // netId -> times asked
  const timer = useRef(0);
  const room = useRef(serverId);
  const ids = netIds.slice().sort((a, b) => a - b).join(",");

  const ask = useCallback(
    async (wanted: number[]) => {
      const url = apiUrl();
      const server = room.current;
      if (!url || !server || !wanted.length || !(friendsOn() || mutesOn())) return;
      try {
        const players = await api.getRoomPlayers(url, server, wanted.slice(0, 64));
        if (room.current !== server) return;
        setByNetId((map) => {
          const next = new Map(map);
          for (const id of wanted) next.delete(id);
          for (const p of players) next.set(p.netId, p);
          return next;
        });
      } catch (e) {
        console.info("room players not read: " + String(e));
      }
    },
    []
  );

  // a new room: nothing known
  useEffect(() => {
    room.current = serverId;
    asked.current = new Map();
    setByNetId(new Map());
    window.clearTimeout(timer.current);
  }, [serverId]);

  useEffect(() => {
    if (!serverId || !ids) return;
    const all = ids.split(",").map(Number);
    const fresh = all.filter((id) => !asked.current.has(id));
    for (const id of fresh) asked.current.set(id, 1);
    if (!fresh.length) return;
    void ask(fresh).then(() => {
      // once more for the ones the room did not know yet
      window.clearTimeout(timer.current);
      timer.current = window.setTimeout(() => {
        const missing = all.filter((id) => asked.current.get(id) === 1 && !byNetIdRef.current.has(id));
        for (const id of missing) asked.current.set(id, 2);
        void ask(missing);
      }, RETRY_MS);
    });
  }, [serverId, ids, ask]);

  const byNetIdRef = useRef(byNetId);
  byNetIdRef.current = byNetId;

  useEffect(() => () => window.clearTimeout(timer.current), []);

  const refresh = useCallback(() => {
    if (ids) void ask(ids.split(",").map(Number));
  }, [ids, ask]);

  return { byNetId, refresh };
}
