// The death note: when the player's cycle dies and their place on the board moved since
// they were last told, a small panel under the respawn count says where they stand now: all time,
// or today when all time did not move. The new place large, how far it moved (up in online green,
// down in failed red, always with the arrow and the number), and the kills to pass the player just
// ahead. It shows at once (the places are read after every kill while the cycle rides, and again
// right at the death) and stays at least MIN_SHOWN_MS, fading out once the cycle rides again
//; takes no pointer, no focus, no keys. A guest's says
// NOT SAVED. Nothing when nothing moved, or without the platform's leaderboards.
import { useEffect, useRef, useState } from "react";
import type { Aa } from "../aa/types";
import { deathNote, notePlaces, readMyPlaces, type DeathNoteLine } from "../platform/leaderboards";
import type { MyPlaces } from "../platform/types";

interface DeathNoteProps {
  aa: Aa;
}
import { Move } from "./BoardTable";
import { num, t } from "../i18n";

const POLL_MS = 100;
// a kill reaches the platform a moment after it happens: the places are read this long after each
// kill while riding, ready for the next death
const AFTER_KILL_MS = 1500;
// at the death: at once, then twice more (a kill just before it may not have reached the platform)
const READS_MS = [0, 1100, 2600];
// the note stays at least this long from when it showed, fading out once the cycle rides again
const MIN_SHOWN_MS = 3200;
// the own score is looked at every few polls (a kill means news is coming)
const SCORE_EVERY = 5;

function ownScore(aa: Aa): number | null {
  try {
    const me = aa.getState().players?.find((p) => p.local);
    return me ? me.score : null;
  } catch {
    return null;
  }
}

function riding(aa: Aa): boolean {
  try {
    const cycle = aa.getCycle?.();
    return !!cycle && cycle.own && cycle.alive;
  } catch {
    return false;
  }
}

export function DeathNote({ aa }: DeathNoteProps) {
  const [note, setNote] = useState<DeathNoteLine | null>(null);
  // riding again while the note stays: its fade, over the time left (ms)
  const [leaving, setLeaving] = useState(0);

  const alive = useRef(true);
  const timers = useRef<number[]>([]);
  // places read after a kill while riding, not shown yet; when the note showed
  const fresh = useRef<MyPlaces | null>(null);
  const shownAt = useRef(0);
  const score = useRef<number | null>(null);
  const polls = useRef(0);

  useEffect(() => {
    const clear = () => {
      for (const id of timers.current) window.clearTimeout(id);
      timers.current = [];
    };
    const show = (places: MyPlaces | null): void => {
      if (!places || alive.current) return;
      const line = deathNote(places);
      if (!line) return;
      notePlaces(places);
      shownAt.current = Date.now();
      setLeaving(0);
      setNote(line);
    };
    const tick = window.setInterval(() => {
      const now = riding(aa);
      // a kill while riding: the places a moment later, ready for the next death
      if (now && ++polls.current % SCORE_EVERY === 0) {
        const s = ownScore(aa);
        if (s !== null && score.current !== null && s > score.current)
          window.setTimeout(() => void readMyPlaces().then((p) => p && (fresh.current = p)), AFTER_KILL_MS);
        score.current = s;
      }
      if (now === alive.current) return;
      alive.current = now;
      clear();
      if (now) {
        // back on the floor: the note stays out its time, fading
        const left = shownAt.current ? MIN_SHOWN_MS - (Date.now() - shownAt.current) : 0;
        shownAt.current = 0;
        if (left <= 0) {
          setNote(null);
          return;
        }
        setLeaving(left);
        timers.current.push(
          window.setTimeout(() => {
            setNote(null);
            setLeaving(0);
          }, left)
        );
        return;
      }
      // just died: what the last kill brought at once, then the platform's word
      show(fresh.current);
      fresh.current = null;
      for (const ms of READS_MS) timers.current.push(window.setTimeout(() => void readMyPlaces().then(show), ms));
    }, POLL_MS);
    return () => {
      window.clearInterval(tick);
      clear();
    };
  }, [aa]);

  if (!note) return null;
  const { place, was } = note;
  const moved = was === null ? null : was - place.rank;
  return (
    <div
      className={"dn" + (leaving ? " is-leaving" : "")}
      id="death-note"
      role="status"
      style={leaving ? { animationDuration: leaving + "ms" } : undefined}
    >
      <div className="dn-head">
        <span className="dn-board">
          <span className="dn-board-name">{note.boardName}</span>
          <span>{t(note.window === "allTime" ? "game.score.allTime" : "game.score.today")}</span>
        </span>
        {note.saved ? (
          was !== null && <span>{t("game.note.was", { rank: num(was) })}</span>
        ) : (
          <span className="dn-unsaved">{t("game.score.notSaved")}</span>
        )}
      </div>
      <div className="dn-line">
        <span className="dn-rank">{t("game.note.rank", { rank: num(place.rank) })}</span>
        {moved !== 0 && <Move row={{ move: moved, new: moved === null }} />}
      </div>
      <p className="dn-say">
        {place.rank === 1
          ? t("game.note.first")
          : place.next && place.killsToNext
            ? t("game.note.toNext", { n: place.killsToNext, name: place.next })
            : null}
        {!note.saved && " " + t("game.note.saveIt")}
      </p>
    </div>
  );
}
