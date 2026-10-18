// Direct messages and the account's event stream (contract "Messages" and
// `GET /v1/me/stream`). One EventSource per signed-in page while the platform has friends: it
// carries every message either way (the chat overlay shows them in a game, the Friends panel's
// conversation view anywhere), tells the friends store to read again when requests or friends
// change, and keeps the unread count (the messages cell) and the conversations with their own
// unread counts (the chat dock's tabs, the Friends rows), asked again whenever the stream says
// the count moved. EventSource
// reconnects by itself and resumes after the last message (Last-Event-ID); while it is down the
// page asks for the conversations every 30 s, so nothing waits for the stream to come back.
import { useEffect, useSyncExternalStore } from "react";
import * as api from "./client";
import { friendsAccount, refreshFriends } from "./friends";
import { platformState, subscribePlatform } from "./session";
import type { ConversationInfo, MessageInfo } from "./types";

type Listener = (m: MessageInfo) => void;

let unread = 0;
const unreadListeners = new Set<() => void>();
const messageListeners = new Set<Listener>();
// the newest message id seen, so a message is handed on once (the stream and a fallback read can both bring it)
let newest = 0;
// how new messages reach the page: the stream ("live"), polling by design (a bearer session, a
// Discord Activity: "polling"), or polling because the stream went down ("down"; the dock says so)
export type StreamState = "live" | "polling" | "down";
let streamState: StreamState = "polling";
const streamListeners = new Set<() => void>();

function setStreamState(next: StreamState) {
  if (next === streamState) return;
  streamState = next;
  for (const l of streamListeners) l();
}

export function useStreamState(): StreamState {
  return useSyncExternalStore(
    (l) => {
      streamListeners.add(l);
      return () => {
        streamListeners.delete(l);
      };
    },
    () => streamState,
    () => streamState
  );
}

// the conversations, newest first, as last asked (each with its unread count and last message)
let conversations: ConversationInfo[] = [];
const conversationListeners = new Set<() => void>();

function setConversations(list: ConversationInfo[]) {
  conversations = list;
  for (const l of conversationListeners) l();
}

// the conversations with their unread counts (the dock's tabs, the Friends rows)
export function useConversations(): ConversationInfo[] {
  return useSyncExternalStore(
    (l) => {
      conversationListeners.add(l);
      return () => {
        conversationListeners.delete(l);
      };
    },
    () => conversations,
    () => conversations
  );
}

// one conversation read here: its count drops at once, the platform is told, the counts asked again
export function markRead(userId: string): void {
  const c = conversations.find((x) => x.user.userId === userId);
  if (c && c.unread > 0) {
    setUnread(Math.max(0, unread - c.unread));
    setConversations(conversations.map((x) => (x === c ? { ...x, unread: 0 } : x)));
  }
  const p = platformState();
  if (p.mode !== "platform" || !friendsAccount(p)) return;
  void api.readConversation(p.apiUrl, userId).then(refreshUnread, () => {});
}

function setUnread(n: number) {
  if (n === unread) return;
  unread = n;
  for (const l of unreadListeners) l();
}

// unread messages to this account, over every conversation
export function useUnread(): number {
  return useSyncExternalStore(
    (l) => {
      unreadListeners.add(l);
      return () => {
        unreadListeners.delete(l);
      };
    },
    () => unread,
    () => unread
  );
}

// every new message either way, as it arrives (the chat overlay, an open conversation)
export function onMessage(fn: Listener): () => void {
  messageListeners.add(fn);
  return () => {
    messageListeners.delete(fn);
  };
}

function deliver(m: MessageInfo) {
  if (m.id <= newest) return;
  newest = m.id;
  for (const l of messageListeners) l(m);
}

// the unread count again (after a conversation was read here)
export async function refreshUnread(): Promise<void> {
  const p = platformState();
  if (p.mode !== "platform" || !friendsAccount(p)) {
    setConversations([]);
    return setUnread(0);
  }
  try {
    const list = await api.getConversations(p.apiUrl);
    setConversations(list.conversations);
    setUnread(list.unread);
  } catch (e) {
    console.info("conversations not read: " + String(e));
  }
}

// the stream for whoever is signed in now; App mounts it once
export function useEventStream(): void {
  const user = useSyncExternalStore(subscribePlatform, () => {
    const p = platformState();
    return p.mode === "platform" && friendsAccount(p) && p.user ? p.apiUrl + "|" + p.user.id : "";
  });

  useEffect(() => {
    if (!user) {
      setUnread(0);
      setConversations([]);
      return;
    }
    const apiUrl = user.slice(0, user.indexOf("|"));
    let source: EventSource | null = null;
    let poll = 0;
    let closed = false;
    // the stream's read events come in bursts (a message, then its read): one ask after them
    let again = 0;
    const askAgain = () => {
      window.clearTimeout(again);
      again = window.setTimeout(() => void refreshUnread(), 250);
    };

    const startPolling = () => {
      if (poll) return;
      void refreshUnread();
      poll = window.setInterval(() => void refreshUnread(), 30000);
    };
    const stopPolling = () => {
      window.clearInterval(poll);
      poll = 0;
    };

    // a bearer session (a Discord Activity): an EventSource cannot send it, and a ticket in its
    // URL would be spent by the first reconnect, so the count comes by polling
    if (api.hasBearer()) {
      setStreamState("polling");
      startPolling();
      return () => stopPolling();
    }
    // the conversations as they stand when the page opens (the stream only says what changes)
    void refreshUnread();

    try {
      source = new EventSource(api.streamUrl(apiUrl), { withCredentials: true });
    } catch (e) {
      console.info("event stream not opened: " + String(e));
      setStreamState("down");
      startPolling();
      return () => stopPolling();
    }
    source.addEventListener("ready", (e) => {
      stopPolling();
      setStreamState("live");
      try {
        const after = Number((JSON.parse((e as MessageEvent).data) as { after?: number }).after);
        // the first ready says where this page starts: what came before is history, not news
        if (Number.isFinite(after) && newest === 0) newest = after;
      } catch {
        /* a malformed line: nothing to set */
      }
    });
    source.addEventListener("message", (e) => {
      try {
        deliver(JSON.parse((e as MessageEvent).data) as MessageInfo);
      } catch {
        /* ignored */
      }
    });
    source.addEventListener("read", (e) => {
      try {
        setUnread(Number((JSON.parse((e as MessageEvent).data) as { unread?: number }).unread) || 0);
      } catch {
        /* ignored */
      }
      askAgain();
    });
    source.addEventListener("friends", () => void refreshFriends().catch(() => {}));
    // down (the platform restarted, the network went): EventSource retries; meanwhile the count by polling
    source.onerror = () => {
      if (closed) return;
      setStreamState("down");
      startPolling();
    };
    return () => {
      closed = true;
      window.clearTimeout(again);
      source?.close();
      stopPolling();
    };
  }, [user]);
}
