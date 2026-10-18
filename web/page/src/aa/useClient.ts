// One React hook around the wasm client: loads it once, mirrors the API's
// `state` object and the `scene`, `error`, `menuOpen`, `chatOpen` (+ its prefill), `consoleOpen`, `round` and `players` events into React
// state (and `authRequest`, until it is answered), toggles fullscreen on the `fullscreen` event, and polls the audio state. Everything the page knows about the client
// comes through here.
import { useEffect, useRef, useState } from "react";
import { attachLog } from "../chat/log";
import { loadClient } from "./loader";
import { ARENA_TEST, installArenaTest } from "../arenaTest";
import { applyClassicLook } from "../settings/classicLook";
import { applyFrameRate } from "../settings/hudStyle";
import { toggleFullscreen } from "../fullscreen";
import type { Aa, AaAuthRequestEvent, AaChatOpenEvent, AaErrorEvent, AaPlayer, AaRoundEvent, AaSceneEvent, AaState, AudioState } from "./types";

export type LoadPhase = "loading" | "ready" | "failed";

export interface ClientView {
  aa: Aa | null;
  phase: LoadPhase;
  loadFraction: number | null;
  loadError: string;
  // the last `state` event (API.md "The state object"); null before `ready`
  state: AaState | null;
  // the last `scene` event: {state, transitioning}
  scene: AaSceneEvent | null;
  // the last `error` event's text and a counter so the same text can repeat
  error: { text: string; n: number } | null;
  // menuOpen events (the game's INGAME_MENU key), counted so each press toggles
  menuPresses: number;
  // chatOpen events (the game's CHAT key, or a template instant chat), counted so each
  // press opens the chat line again, with the prefill of the last one; the first one in an
  // official room also shows the code of conduct
  chatPresses: number;
  chatPrefill: string;
  // consoleOpen events (the game's CONSOLE_INPUT key, ` by default), counted: each press
  // opens the chat panel on its Console view
  consolePresses: number;
  // the score table shown (the `scores` event: the game's SCORE key, a round's end, a new round);
  // the page draws it (setPageScores). The count says each show apart
  scores: { shown: boolean; n: number };
  // the `round` events of this page load, counted: the page's one definition of a round
  // (rounds started while playing, a round's end for the XP line; API.md "Events")
  rounds: { started: number; ended: number };
  // the `players` event: who is in the game now (the in-game menu's list)
  players: AaPlayer[];
  // the server's password request while it waits for auth()/authCancel(): a /login in the
  // chat or the Console, or a server that asks on its own (AuthPrompt.tsx)
  auth: AaAuthRequestEvent | null;
  audio: AudioState;
}

const INITIAL_STATE: AaState = {
  state: "idle",
  reason: "",
  resource: null,
  url: "",
  ice: null,
  netstate: 0,
  ping: null,
  gametime: null,
  fps: null,
  synced: false,
  authPending: false,
  sceneState: 0,
  sceneTransitioning: false,
  players: []
};


export function useClient(canvas: HTMLCanvasElement | null): ClientView {
  const [view, setView] = useState<ClientView>({
    aa: null,
    phase: "loading",
    loadFraction: null,
    loadError: "",
    state: null,
    scene: null,
    error: null,
    menuPresses: 0,
    scores: { shown: false, n: 0 },
    chatPresses: 0,
    chatPrefill: "",
    consolePresses: 0,
    rounds: { started: 0, ended: 0 },
    players: [],
    auth: null,
    audio: "none"
  });
  const started = useRef(false);

  useEffect(() => {
    if (!canvas || started.current) return;
    started.current = true;
    let unsubscribe: Array<() => void> = [];
    let audioTimer = 0;

    loadClient(canvas, {
      onProgress: (fraction) => setView((v) => ({ ...v, loadFraction: fraction })),
      onLog: () => {},
      // the Console view's log hears the game from its first line (chat/log.ts)
      onPrepared: attachLog
    })
      .then((aa) => {
        const first = safeState(aa) || INITIAL_STATE;
        setView((v) => ({ ...v, aa, phase: "ready", loadFraction: 1, state: first, audio: aa.getAudioState() }));
        unsubscribe = [
          // games in the stadium arena (arenaTest.ts): first, so its state handler runs before the page reacts
          ...(ARENA_TEST ? [installArenaTest(aa)] : []),
          // a request ends with its answer or with the session (state.authPending goes false)
          aa.on("state", (s: AaState) => setView((v) => ({ ...v, state: s, auth: s.authPending ? v.auth : null }))),
          aa.on("authRequest", (e: AaAuthRequestEvent) => setView((v) => ({ ...v, auth: e }))),
          aa.on("scene", (e: AaSceneEvent) => setView((v) => ({ ...v, scene: e }))),
          aa.on("error", (e: AaErrorEvent) =>
            setView((v) => ({ ...v, error: { text: e.text, n: (v.error ? v.error.n : 0) + 1 } }))
          ),
          aa.on("menuOpen", () => setView((v) => ({ ...v, menuPresses: v.menuPresses + 1 }))),
          aa.on("consoleOpen", () => setView((v) => ({ ...v, consolePresses: v.consolePresses + 1 }))),
          aa.on("scores", (e: { show?: boolean }) => setView((v) => ({ ...v, scores: { shown: !!e?.show, n: v.scores.n + 1 } }))),
          aa.on("chatOpen", (e: AaChatOpenEvent) =>
            setView((v) => ({ ...v, chatPresses: v.chatPresses + 1, chatPrefill: e && typeof e.prefill === "string" ? e.prefill : "" }))
          ),
          // the game's TOGGLE_FULLSCREEN key (p by default): toggled here, at once, while the
          // key press's user activation still allows it (not through React state)
          aa.on("fullscreen", toggleFullscreen),
          aa.on("players", (e: { players: AaPlayer[] }) => setView((v) => ({ ...v, players: Array.isArray(e.players) ? e.players : [] }))),
          aa.on("round", (e: AaRoundEvent) =>
            setView((v) => ({
              ...v,
              rounds:
                e.state === "started"
                  ? { ...v.rounds, started: v.rounds.started + 1 }
                  : e.state === "ended"
                    ? { ...v.rounds, ended: v.rounds.ended + 1 }
                    : v.rounds
            }))
          )
        ];
        // the page draws the score table (Scoreboard.tsx); a bundle without the call keeps the game's
        aa.setPageScores?.(true);
        // the CLASSIC look again (settings/classicLook.ts): some of its values are not saved
        applyClassicLook(aa);
        // the frame rate counter off unless the player turned it on (an older autoexec.cfg turns it on every start)
        applyFrameRate(aa);
        // a closed or reloaded tab leaves its room now: disconnect() sends the logout at once, so the
        // server frees the slot and the login instead of keeping both until the connection times out
        const leave = () => {
          const s = safeState(aa);
          if (s && s.netstate !== 0) aa.disconnect();
        };
        window.addEventListener("pagehide", leave);
        unsubscribe.push(() => window.removeEventListener("pagehide", leave));
        // the audio device is created suspended until a user gesture (API.md unlockAudio)
        audioTimer = window.setInterval(() => {
          const audio = aa.getAudioState();
          setView((v) => (v.audio === audio ? v : { ...v, audio }));
        }, 1000);
      })
      .catch((err: Error) => {
        setView((v) => ({ ...v, phase: "failed", loadError: err.message }));
      });

    return () => {
      unsubscribe.forEach((off) => off());
      if (audioTimer) window.clearInterval(audioTimer);
    };
  }, [canvas]);

  return view;
}

function safeState(aa: Aa): AaState | null {
  try {
    return aa.getState();
  } catch {
    return null;
  }
}
