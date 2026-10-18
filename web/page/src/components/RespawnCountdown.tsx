// RESPAWN IN 3, 2, 1 while the player's cycle is dead in a room that brings it back (a server
// that tells its browser clients its RESPAWN_TIME: the Retrocycles rooms). Read from
// aa.getCycle().respawnIn on animation frames, at most every 50 ms; it holds at 1 for the moment
// the new cycle takes to arrive and is gone once the cycle is back (null: alive, spectating,
// between rounds, a room without respawns). Each new figure remounts, so its CSS entrance plays
// once per second. Large in the middle of the screen, the whole game view gray and dim under it
// (hud.css: a backdrop-filter layer over the frame; the touch buttons stay lit above it); takes
// no pointer, no focus.
//
// JOINING IN 2, 1 the same way for a player who joined a respawn room mid-game: the server holds
// their first cycle until the client says its game timer is in sync, then RESPAWN_JOIN_READY
// seconds (the server line's gGame.cpp), so the cycle never rides while the screen still reads
// "Syncing with server..."; the client counts those seconds (getState().joinIn, API.md). Nothing
// on a server that does not hold, or with an older bundle. Same size and place, no gray (nothing
// died): the words on a near-black cell.
import { useEffect, useState } from "react";
import type { Aa } from "../aa/types";
import { t } from "../i18n";

const POLL_MS = 50;

// the figure to show, or null for nothing
function secondsOf(respawnIn: number | null | undefined): number | null {
  if (respawnIn == null || !Number.isFinite(respawnIn) || respawnIn < -1) return null;
  return Math.max(1, Math.ceil(respawnIn));
}

export function RespawnCountdown({ aa }: { aa: Aa }) {
  const [shown, setShown] = useState<{ seconds: number; joining: boolean } | null>(null);

  useEffect(() => {
    let frame = 0;
    let last = -POLL_MS;
    const tick = (t: number) => {
      frame = requestAnimationFrame(tick);
      if (t - last < POLL_MS) return;
      last = t;
      try {
        const respawn = secondsOf(aa.getCycle?.()?.respawnIn);
        if (respawn !== null) return setShown({ seconds: respawn, joining: false });
        const joining = secondsOf(aa.getState().joinIn);
        setShown(joining === null ? null : { seconds: joining, joining: true });
      } catch {
        setShown(null);
      }
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [aa]);

  if (shown === null) return null;
  const { seconds, joining } = shown;

  return (
    <div className="respawn" id="respawn-countdown" data-kind={joining ? "joining" : "respawn"} aria-hidden="true">
      <div className="respawn-cell">
        <span className="respawn-label">{joining ? t("game.respawn.joining") : t("game.respawn.respawn")}</span>
        <span className="respawn-count" key={seconds}>
          {seconds}
        </span>
      </div>
    </div>
  );
}
