// The connecting screen: the
// scene goes black and the game's own cycle, in the player's colour, runs a figure 8 on it
// with a short wall behind it, while the connection's steps take turns in one line beneath
// (the session states of API.md in order, after the platform's "finding" step when Quick
// Play asks the platform for a room). A cycle only turns at 90 degrees, so the 8 is two
// squares that meet at a crossing. Under the step, the two keys a newcomer needs first
// (from the key map); at the bottom, how to back out. Neither on a touch device (a phone, a
// tablet, the embed in X's app): it has no keys to name and no Esc.
import { useEffect, useLayoutEffect, useRef, useState } from "react";
import type { SessionState } from "../aa/types";
import type { FindingPhase } from "../platform/quickplay";
import { firstKeys, type HintItem } from "../keys";
import { touchPrimary } from "../compact";
import { KeyGroups } from "./KeyCaps";
import { t, type PlainKey } from "../i18n";

// the steps in the client's own words (the ids are API.md's state names; the words, keys of the
// catalogue)
const LADDER: Array<[SessionState, PlainKey, PlainKey]> = [
  ["connecting", "connect.step.connecting", "connect.step.connecting.say"],
  ["syncing", "connect.step.syncing", "connect.step.syncing.say"],
  ["fetching", "connect.step.fetching", "connect.step.fetching.say"],
  // `playing` shows here only until the first round starts (App.tsx): a room that was empty is
  // still building it
  ["playing", "connect.step.playing", "connect.step.playing.say"]
];

// the finding step's sentence: what the platform is doing for us right now
function findingSay(finding: FindingPhase, now: number): string {
  if (finding.phase === "found" || finding.phase === "signing") return t("connect.finding.found");
  if (finding.phase === "duel") return t("connect.duel.waitingFor");
  if (finding.phase === "arrived") return t("connect.duel.arrived");
  if (finding.phase === "joining") return t("connect.duel.joining", { name: finding.name });
  if (finding.phase === "asking" && finding.duel) return t(finding.duel === "create" ? "connect.finding.duelCreate" : finding.duel === "accept" ? "connect.finding.duelAccept" : "connect.finding.duelRoom");
  if (finding.phase === "asking" && finding.reroute) return t("connect.finding.reroute");
  if (finding.phase === "asking") return finding.friend ? t("connect.finding.friend", { name: finding.friend }) : finding.invite ? t("connect.finding.invite") : t("connect.finding.any");
  const seconds = Math.max(1, Math.ceil((finding.until - now) / 1000));
  const why = !finding.full ? t("connect.finding.noResponse") : finding.duel ? t("connect.finding.duelFull") : finding.friend ? t("connect.finding.friendFull", { name: finding.friend }) : finding.invite ? t("connect.finding.inviteFull") : t("connect.finding.allFull");
  return t("connect.finding.retry", { why, seconds });
}

interface ConnectingProps {
  // "local": the local game is starting (no server, no steps)
  kind: "connect" | "local";
  session: SessionState;
  resource: string | null;
  finding: FindingPhase | null;
  // the room, or a move's line ("moving to X with everyone...")
  sub: string;
  keys: HintItem[];
  // a 1v1: the room's steps say the match's one rule instead of the plumbing
  duel?: boolean;
  // a duel link waiting for someone to open it: copy it (again); true once it is on the clipboard
  onCopy?(): Promise<boolean>;
  // the same through the system's share sheet (where the browser has one)
  onShare?(): void;
  // leave the waiting screen for a Quick Play, the link still open (its arrival comes over the game)
  onPlayWhileWaiting?(): void;
  // back out (Esc does it with keys; a touch device has no Esc: the link's wait shows CANCEL)
  onCancel?(): void;
}

// "9:41": minutes and seconds until `until` (ms)
export function clockLeft(until: number, now: number): string {
  const total = Math.max(0, Math.ceil((until - now) / 1000));
  return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0");
}

export function Connecting({ kind, session, resource, finding, sub, keys, duel, onCopy, onShare, onPlayWhileWaiting, onCancel }: ConnectingProps) {
  const touch = touchPrimary();
  // a tick while the finding step counts down
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!finding || (finding.phase !== "waiting" && finding.phase !== "duel" && finding.phase !== "joining")) return;
    setNow(Date.now());
    // a duel's clock counts seconds
    const t = window.setInterval(() => setNow(Date.now()), finding.phase === "waiting" ? 250 : 1000);
    return () => window.clearInterval(t);
  }, [finding]);
  const canShare = !!onShare && typeof navigator.share === "function";
  // the duel link's act reads COPIED for 2 s after a copy
  const [copiedAt, setCopiedAt] = useState(0);
  useEffect(() => {
    if (!copiedAt) return;
    const t = window.setTimeout(() => setCopiedAt(0), 2000);
    return () => window.clearTimeout(t);
  }, [copiedAt]);
  const copy = () => void onCopy?.().then((ok) => ok && setCopiedAt(Date.now()));

  let key: string;
  let label: string;
  let say: string;
  if (kind === "local") {
    key = "starting";
    label = t("connect.step.starting");
    say = t("connect.step.starting.say");
  } else if (finding?.phase === "signing") {
    key = "signing";
    label = t("connect.step.signing");
    say = t("connect.step.signing.say");
  } else if (finding?.phase === "duel" || finding?.phase === "joining") {
    key = "waiting";
    label = t("connect.step.duel");
    say = findingSay(finding, now);
  } else if (finding?.phase === "arrived") {
    key = "arrived";
    label = t("connect.step.arrived");
    say = findingSay(finding, now);
  } else if (finding && finding.phase !== "found") {
    key = "finding";
    label = t("connect.step.finding");
    say = findingSay(finding, now);
  } else {
    const step = LADDER.find(([s]) => s === session) ?? LADDER[0];
    key = step[0];
    label = t(step[1]);
    say = duel ? t("connect.duel.rule") : key === "fetching" && resource ? resource : t(step[2]);
  }

  return (
    <div className="connect" id="connect" data-step={key}>
      <Eight />
      <div className="connect-say" aria-live="polite">
        <StepLine id={key} label={label} say={say} />
        {finding?.phase === "duel" ? (
          <div className="connect-link" id="duel-link">
            <p className="connect-link-text" id="duel-link-text" title={finding.link}>
              {finding.link}
            </p>
            {onCopy && (
              <button className="connect-link-act" type="button" id="duel-copy" data-done={copiedAt ? "" : undefined} onClick={copy}>
                {copiedAt ? t("connect.duel.done") : t("connect.duel.copy")}
              </button>
            )}
            {canShare && (
              <button className="connect-link-act" type="button" id="duel-share" onClick={onShare}>
                {t("connect.duel.shareLink")}
              </button>
            )}
          </div>
        ) : (
          !touch && keys.length > 0 && finding?.phase !== "arrived" && <KeyGroups className="connect-keys" items={firstKeys(keys)} />
        )}
        {finding?.phase === "duel" && (
          // what to do with it and the time it stays open, quiet; out of the live region's reading (the
          // clock changes on its own)
          <p className="connect-link-left" id="duel-link-left" aria-hidden="true">
            {t("connect.duel.sendLeft", { time: clockLeft(finding.until, now) })}
          </p>
        )}
        {finding?.phase === "joining" && (
          <p className="connect-link-left" id="duel-joining-left" aria-hidden="true">
            {t("connect.duel.joiningLeft", { time: clockLeft(finding.until, now) })}
          </p>
        )}
        {finding?.phase === "duel" && onPlayWhileWaiting && (
          <button className="connect-link-play" type="button" id="duel-play-while" onClick={onPlayWhileWaiting}>
            {t("connect.duel.playWhile")}
          </button>
        )}
        {(finding?.phase === "duel" || finding?.phase === "joining") && touch && onCancel && (
          <button className="connect-link-cancel" type="button" id="duel-cancel-link" onClick={onCancel}>
            {t("connect.cancel")}
          </button>
        )}
        {sub && <p className="connect-room">{sub}</p>}
      </div>
      {!touch && (
      <p className="connect-esc">
        <span className="connect-esc-key">{t("common.esc")}</span> {t("connect.cancel")}
      </p>
      )}
    </div>
  );
}

// the step and its sentence; a new step comes up from below as the old one leaves upward.
// `label` shows instead of the id (the room's message: its own title, the id stays "message")
function StepLine({ id, label, say }: { id: string; label?: string; say: string }) {
  const [old, setOld] = useState<{ id: string; label?: string; say: string } | null>(null);
  const last = useRef<{ id: string; label?: string; say: string }>({ id, label, say });
  useLayoutEffect(() => {
    if (last.current.id !== id) setOld(last.current);
    last.current = { id, label, say };
  }, [id, label, say]);
  const sentence = (step: string) => "connect-sentence" + (step === "message" ? " is-message" : "");
  return (
    <div className="connect-step">
      {old && (
        <div className="connect-line is-leaving" key={"old-" + old.id} aria-hidden="true" onAnimationEnd={() => setOld(null)}>
          <p className="connect-key">{old.label ?? old.id}</p>
          <p className={sentence(old.id)}>{old.say}</p>
        </div>
      )}
      <div className="connect-line" key={id}>
        <p className="connect-key" id="connect-key">
          {label ?? id}
        </p>
        <p className={sentence(id)} id="connect-sentence">
          {say}
        </p>
      </div>
    </div>
  );
}

// the cycle and its wall, as the game draws them: a clip the engine rendered (the attract
// scene's replay with one cycle on a figure 8, a camera above it, no floor; how it was made
// is in public/connect/NOTICE.txt). It was rendered white; the page multiplies it by the
// player's colour, so every player sees their own cycle. With reduced motion, its still.
// The colour comes in with the clip's first frame: before it, the box would show as a plain
// square of the colour (long enough to see through Discord's proxy)
function Eight() {
  const [still] = useState(() => window.matchMedia("(prefers-reduced-motion: reduce)").matches);
  const [shown, setShown] = useState(false);
  const show = () => setShown(true);
  return (
    <div className={"eight" + (shown ? " is-shown" : "")} aria-hidden="true">
      {still ? (
        <img className="eight-clip" src="/connect/eight.png" alt="" onLoad={show} />
      ) : (
        <video className="eight-clip" autoPlay muted loop playsInline preload="auto" poster="/connect/eight.png" onLoadedData={show}>
          <source src="/connect/eight.webm" type="video/webm" />
          <source src="/connect/eight.mp4" type="video/mp4" />
        </video>
      )}
    </div>
  );
}
