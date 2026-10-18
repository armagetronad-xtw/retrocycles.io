// The page inside a social post's frame (?embed, links.ts): the wordmark stays, the queue,
// the status bar and the documents line give way to one solid PLAY over the scene (Quick
// Play; pressed while the client still loads, it plays once ready), the last note under it,
// and the way out to the whole page. The press is also what gives the frame the keys.
import { useEffect, useState } from "react";
import type { Aa } from "../aa/types";
import { Icon } from "./Icons";
import type { Note } from "./Strip";
import { DISCORD } from "../links";
import { openOutside } from "../discord";
import { leaveFullscreen } from "../fullscreen";
import { flush, track } from "../platform/analytics";
import { t } from "../i18n";

interface EmbedProps {
  ready: boolean;
  waiting: boolean; // PLAY was pressed while the client loaded
  note: Note | null;
  href: string;
  // inside a Discord Activity: the others in this launch (Discord's names), null elsewhere
  together?: string[] | null;
  onPlay(): void;
}

// "with Ana", "with Ana and Ben", "with Ana, Ben and 3 more": the people in the launch
function togetherText(names: string[]): string {
  if (names.length === 1) return t("home.embed.withOne", { a: names[0] });
  if (names.length === 2) return t("home.embed.withTwo", { a: names[0], b: names[1] });
  return t("home.embed.withMore", { a: names[0], b: names[1], n: names.length - 2 });
}

// Inside a Discord Activity the home screen is the whole one: Quick Play, the
// Server Browser and Training. With others in the launch, a line under the modes says Quick Play
// is where they ride together (the Server Browser and Training play apart from them).
export function DiscordTogether({ names }: { names: string[] }) {
  return (
    <p className="discord-together" id="discord-together">
      {t("home.discord.together", { together: togetherText(names) })}
    </p>
  );
}

export function EmbedPlay({ ready, waiting, note, href, together, onPlay }: EmbedProps) {
  return (
    <div className="embed" id="embed" data-ready={ready ? "1" : "0"}>
      <button className="embed-play" id="embed-play" type="button" onClick={onPlay} aria-busy={waiting}>
        {waiting ? t("home.embed.loading") : t("home.embed.play")}
      </button>
      <p className={"embed-note" + (note && note.quiet ? " is-quiet" : "")} id="embed-note" role="status" hidden={!note}>
        {note ? note.text : ""}
      </p>
      {together && together.length > 0 ? (
        <p className="embed-say embed-together" id="embed-together">
          {t("home.embed.together", { together: togetherText(together) })}
        </p>
      ) : (
        <p className="embed-say">{t("home.embed.say")}</p>
      )}
      <a
        className="embed-out"
        id="embed-out"
        href={href}
        target="_blank"
        rel="noopener"
        onClick={(e) => {
          track("full_game_opened");
          flush(true);
          // Discord's frame opens nothing itself: its own confirmation, then the browser
          if (!DISCORD) return;
          e.preventDefault();
          openOutside(href);
        }}
      >
        {t("home.embed.openFull")}
      </a>
    </div>
  );
}

// The way to the whole page while the embed plays: a cell at the top,
// in the middle, only while the player is not riding (their cycle dead, watching another, or
// between rounds; aa.getCycle(): not their own and alive). It fades in as they die (after a
// short settle, so the moment a round hands out cycles does not flash it) and lingers a moment
// after they ride again, fading out; while it lingers it takes no press, so a stray tap in the
// small frame never ends a run. While RESPAWN IN counts (RespawnCountdown) it stands just above
// the count in the middle instead, and stays there while it lingers. The Esc
// menu has the same way as a lane (GameMenu.tsx).
const SETTLE_MS = 150;
const LINGER_MS = 1500; // embed.css .full-game-cell.is-leaving runs as long

type CellPhase = "hidden" | "shown" | "leaving";

export function FullGameCell({ aa, href, onOpen }: { aa: Aa; href: string; onOpen(): void }) {
  const [phase, setPhase] = useState<CellPhase>("hidden");
  // over the respawn count (embed.css .is-respawn), else at the top
  const [respawn, setRespawn] = useState(false);
  useEffect(() => {
    let since: number | null = null;
    let rodeAt: number | null = null;
    let current: CellPhase = "hidden";
    const set = (next: CellPhase) => {
      if (next !== current) setPhase((current = next));
    };
    const tick = () => {
      let riding = true;
      let counting = false;
      try {
        const c = aa.getCycle?.();
        riding = !!c && c.own && c.alive;
        counting = c?.respawnIn != null && Number.isFinite(c.respawnIn);
      } catch {
        // an older bundle: never shown
      }
      const now = performance.now();
      if (riding || !aa.getCycle) {
        since = null;
        if (current === "shown") {
          rodeAt = now;
          set("leaving");
        } else if (current === "leaving" && rodeAt !== null && now - rodeAt >= LINGER_MS) set("hidden");
      } else {
        since ??= now;
        if (now - since >= SETTLE_MS) set("shown");
        if (current !== "hidden") setRespawn(counting);
      }
    };
    const id = window.setInterval(tick, 100);
    return () => window.clearInterval(id);
  }, [aa]);
  if (phase === "hidden") return null;
  return (
    <a
      className={"full-game-cell" + (phase === "leaving" ? " is-leaving" : "") + (respawn ? " is-respawn" : "")}
      id="full-game-cell"
      href={href}
      target="_blank"
      rel="noopener"
      onClick={onOpen}
      aria-hidden={phase === "leaving" ? true : undefined}
      tabIndex={phase === "leaving" ? -1 : undefined}
    >
      {t("home.embed.openFull")}
      <Icon name="external" className="full-game-cell-icon" />
    </a>
  );
}

// Full screen for the embed: a cell at the lower right, on the PLAY screen
// and in the game (not beside a touch device's turn buttons, which hold that corner). Only
// where the frame may go full screen (X's frame on the web; not an app's web view, not an
// iPhone); a press goes in, the next comes back out (Esc does too). Discord owns its frame:
// there the way is Discord's own pop out.
type FullscreenDoc = Document & { webkitFullscreenElement?: Element | null; webkitFullscreenEnabled?: boolean; webkitExitFullscreen?: () => void };

function fullscreenNow(): boolean {
  const doc = document as FullscreenDoc;
  return !!(document.fullscreenElement || doc.webkitFullscreenElement);
}

export function fullscreenOffered(): boolean {
  const doc = document as FullscreenDoc;
  return !DISCORD && !!(document.fullscreenEnabled || doc.webkitFullscreenEnabled);
}

export function FullscreenCell() {
  const [full, setFull] = useState(fullscreenNow);
  useEffect(() => {
    const update = () => setFull(fullscreenNow());
    document.addEventListener("fullscreenchange", update);
    document.addEventListener("webkitfullscreenchange", update);
    return () => {
      document.removeEventListener("fullscreenchange", update);
      document.removeEventListener("webkitfullscreenchange", update);
    };
  }, []);
  return (
    <button
      className="fullscreen-cell"
      id="fullscreen-cell"
      type="button"
      aria-label={full ? t("home.embed.leaveFullscreen") : t("home.embed.fullscreen")}
      title={full ? t("home.embed.leaveFullscreen") : t("home.embed.fullscreen")}
      onClick={(e) => {
        // the keys stay the game's: a focused button would take Space and Enter
        e.currentTarget.blur();
        const root = document.documentElement as HTMLElement & { webkitRequestFullscreen?: () => void };
        try {
          if (fullscreenNow()) leaveFullscreen();
          else if (root.requestFullscreen) {
            void root.requestFullscreen({ navigationUI: "hide" }).catch(() => {});
          } else root.webkitRequestFullscreen?.();
        } catch {
          // refused: the frame stays as it is
        }
      }}
    >
      <Icon name={full ? "minimize" : "maximize"} className="fullscreen-cell-icon" />
    </button>
  );
}
