// The XP line after a round and the match summary after a match: while a game on a server with platform login runs, every `round`
// ended event (the API's, the page's one definition of a round) is followed by one
// GET /v1/me after ROUND_DELAY_MS (the server reports the round, the platform credits it);
// if that answer has nothing new, once more RETRY_MS later. The answer before the first round
// only records where the player stands. A new XP entry becomes the line (RoundLine), a new
// `lastMatch` the summary (MatchLine); both go away on their own (LINE_MS, SUMMARY_MS) and
// with the game. The summary lands as the next match counts down (the next round starts at once
// after the last ends), so it stays briefly at the right edge, the pointer resting on it holds it
// (SUMMARY_LEAVE_MS more once the pointer leaves) and its cross closes it. Nothing without features.progression or a session. A guest's summary may
// carry "save your progress" (savePrompt.ts decides which matches do).
//
// The same read may carry an offer of a closer server (features.moveOffers, offers.ts): it is
// shown in the rewards' place (a match summary of the same round end gives way to it; the XP
// line stays), reported `shown` while unanswered, and goes away after OFFER_MS or when the next
// round starts. MOVE accepts (the panel then says "3 of 4 ready" until it goes), "not now"
// dismisses.
import { useEffect, useRef, useState } from "react";
import { offerEvent, savePrompt } from "./analytics";
import { OFFER_MS, answer, offersOn, phaseOf, reportShown, type OfferLine } from "./offers";
import { XpTracker, progressionOn, refreshMe, refreshProgression, type Fresh } from "./progression";
import { saveTrigger, type SaveTrigger } from "./savePrompt";
import type { LastMatch, MoveOffer, PlatformState, Progression } from "./types";

export const ROUND_DELAY_MS = 1500;
export const RETRY_MS = 3000;
export const LINE_MS = 6000;
export const SUMMARY_MS = 8000;
export const SUMMARY_LEAVE_MS = 2500;

export interface RoundLine {
  key: number;
  amount: number;
  level: number;
  levelUp: boolean;
}

export interface MatchLine {
  key: number;
  match: LastMatch;
  progression: Progression;
  save: SaveTrigger | null; // a guest's ask to save their progress, null for none
}

interface RewardsInput {
  platform: PlatformState;
  // a network game on a server that logged the player in through the platform
  active: boolean;
  roundsEnded: number;
  roundsStarted: number;
}

export interface Rewards {
  line: RoundLine | null;
  summary: MatchLine | null;
  offer: OfferLine | null;
  closeSummary(): void;
  holdSummary(hold: boolean): void;
  acceptOffer(): void;
  dismissOffer(): void;
}

export function useRewards({ platform, active, roundsEnded, roundsStarted }: RewardsInput): Rewards {
  const [line, setLine] = useState<RoundLine | null>(null);
  const [summary, setSummary] = useState<MatchLine | null>(null);
  const [offer, setOfferState] = useState<OfferLine | null>(null);
  const offerRef = useRef<OfferLine | null>(null);
  const tracker = useRef(new XpTracker());
  const keys = useRef(0);
  const lastEnded = useRef(roundsEnded);
  const lastStarted = useRef(roundsStarted);
  const timers = useRef<number[]>([]);
  const summaryTimer = useRef(0);
  const withProgression = progressionOn(platform);
  const on = active && (withProgression || offersOn(platform));
  const userId = platform.mode === "platform" && platform.user ? platform.user.id : "";
  const guest = platform.mode === "platform" && !!platform.user && platform.user.isGuest;
  const guestRef = useRef(guest);
  guestRef.current = guest;

  const later = (fn: () => void, ms: number) => {
    const id = window.setTimeout(() => {
      timers.current = timers.current.filter((t) => t !== id);
      fn();
    }, ms);
    timers.current.push(id);
    return id;
  };

  const forget = (id: number) => {
    window.clearTimeout(id);
    timers.current = timers.current.filter((t) => t !== id);
  };

  // the summary goes ms from now, unless another took its place meanwhile
  const armSummary = (key: number, ms: number) => {
    forget(summaryTimer.current);
    summaryTimer.current = later(() => setSummary((s) => (s && s.key === key ? null : s)), ms);
  };

  const closeSummary = () => {
    forget(summaryTimer.current);
    setSummary(null);
  };

  // the pointer rests on the panel: it stays; the pointer leaves: a little longer, then it goes
  const summaryRef = useRef<MatchLine | null>(null);
  summaryRef.current = summary;
  const holdSummary = (hold: boolean) => {
    const s = summaryRef.current;
    if (!s) return;
    if (hold) forget(summaryTimer.current);
    else armSummary(s.key, SUMMARY_LEAVE_MS);
  };

  const setOffer = (next: OfferLine | null) => {
    offerRef.current = next;
    setOfferState(next);
  };

  // the panel goes (its time passed, the next round started, the game ended): an offer still
  // unanswered went away unanswered
  const hideOffer = () => {
    const o = offerRef.current;
    if (!o) return;
    if (o.phase === "open") offerEvent("expired", o.offer.region);
    setOffer(null);
  };

  const keepOffer = (o: MoveOffer) => {
    const key = ++keys.current;
    setOffer({ key, offer: o, phase: phaseOf(o) });
    later(() => {
      if (offerRef.current && offerRef.current.key === key) hideOffer();
    }, OFFER_MS);
  };

  const showOffer = (o: MoveOffer) => {
    if (offerRef.current) return; // one panel per round end
    setSummary(null); // the offer takes the rewards' place
    keepOffer(o);
    if (o.status === "open") void reportShown(o);
  };

  const acceptOffer = () => {
    const o = offerRef.current;
    if (!o || o.phase !== "open") return;
    setOffer({ ...o, offer: { ...o.offer, accepted: o.offer.accepted + 1 }, phase: "accepted" }); // at once: the answer follows
    void answer(o.offer, "accept").then((r) => {
      if (!offerRef.current || offerRef.current.offer.id !== o.offer.id) return;
      if (r && (r.status === "accepted" || r.status === "moving")) keepOffer(r); // "3 of 4 ready" for another 10 s
      else setOffer(null);
    });
  };

  const dismissOffer = () => {
    const o = offerRef.current;
    if (!o) return;
    setOffer(null);
    void answer(o.offer, "dismiss");
  };

  const show = (fresh: Fresh) => {
    if (fresh.match && fresh.match.rounds > 0) {
      // (someone who joined as the match ended played none of it: no summary)
      if (offerRef.current) return; // the offer holds the rewards' place this round
      const key = ++keys.current;
      const save = guestRef.current ? saveTrigger(fresh.match, fresh.progression) : null;
      if (save) savePrompt(save, fresh.progression.stats.matches);
      setSummary({ key, match: fresh.match, progression: fresh.progression, save });
      setLine(null); // the summary carries the match's XP
      armSummary(key, SUMMARY_MS);
      return;
    }
    if (fresh.gain) {
      const key = ++keys.current;
      setLine({ key, amount: fresh.gain.amount, level: fresh.gain.level, levelUp: fresh.gain.levelUp });
      later(() => setLine((l) => (l && l.key === key ? null : l)), LINE_MS);
    }
  };

  // a game starts (or someone else signs in): where the player stands now
  useEffect(() => {
    tracker.current.reset();
    lastEnded.current = roundsEnded;
    if (!on) {
      for (const t of timers.current) window.clearTimeout(t);
      timers.current = [];
      setLine(null);
      setSummary(null);
      hideOffer();
      return;
    }
    let live = true;
    if (withProgression) {
      void refreshProgression().then((p) => {
        if (live && p && !tracker.current.hasBaseline) tracker.current.take(p);
      });
    }
    return () => {
      live = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [on, userId]);

  // a round ended: ask what it earned
  useEffect(() => {
    if (roundsEnded <= lastEnded.current) {
      lastEnded.current = roundsEnded;
      return;
    }
    lastEnded.current = roundsEnded;
    if (!on) return;
    const look = (retry: boolean) =>
      refreshMe().then((me) => {
        if (me.offer) showOffer(me.offer);
        const p = me.progression;
        if (!p) return;
        const baseline = !tracker.current.hasBaseline;
        const fresh = tracker.current.take(p);
        if (fresh.gain || fresh.match) show(fresh);
        else if (retry && !baseline) later(() => void look(false), RETRY_MS);
      });
    later(() => void look(true), ROUND_DELAY_MS);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [roundsEnded]);

  // the next round starts: the offer's moment has passed
  useEffect(() => {
    if (roundsStarted > lastStarted.current) hideOffer();
    lastStarted.current = roundsStarted;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [roundsStarted]);

  useEffect(
    () => () => {
      for (const t of timers.current) window.clearTimeout(t);
    },
    []
  );

  return { line, summary, offer, closeSummary, holdSummary, acceptOffer, dismissOffer };
}
