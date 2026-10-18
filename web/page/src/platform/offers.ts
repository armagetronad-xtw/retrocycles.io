// Offers of a closer server (platform-contract/README.md section 1, "Offers"):
// a player playing far from the region nearest to them may be offered a move to a room there,
// together with the other players near it who accept. The platform decides who is offered
// what and how often; the page reads the offer from the GET /v1/me it makes after every round
// end anyway (useRewards.ts), shows it at the end of the round in the rewards' place
// (components/Offer.tsx), and answers: `shown` once per round end it showed an open offer (the
// platform stops serving it after two), `accept`, `dismiss`. When the group is complete the
// platform moves it at once: the client's `move` event (platform/moves.ts) does the rest.
// Nothing without a platform whose features.moveOffers is on, or without a session.
import { answerOffer } from "./client";
import { offerEvent } from "./analytics";
import { platformState } from "./session";
import type { MoveOffer, PlatformState } from "./types";
import { t } from "../i18n";

// on screen for this long at most, and never past the next round's start
export const OFFER_MS = 10000;

export function offersOn(platform: PlatformState): boolean {
  return platform.mode === "platform" && platform.config.features.moveOffers === true && !!platform.user;
}

export function wellFormedOffer(o: unknown): MoveOffer | null {
  if (!o || typeof o !== "object") return null;
  const m = o as MoveOffer;
  if (typeof m.id !== "string" || typeof m.region !== "string" || typeof m.pingHere !== "number" || typeof m.pingThere !== "number") return null;
  if (m.status !== "open" && m.status !== "accepted") return null; // the page shows only these
  return {
    ...m,
    regionName: typeof m.regionName === "string" && m.regionName ? m.regionName : m.region,
    serverName: typeof m.serverName === "string" ? m.serverName : "",
    accepted: typeof m.accepted === "number" ? m.accepted : 0,
    needed: typeof m.needed === "number" ? m.needed : 1
  };
}

// what the panel shows: an open offer (MOVE / not now), an accepted one ("3 of 4 ready"), or
// the moment the group formed ("moving")
export type OfferPhase = "open" | "accepted" | "moving";

export interface OfferLine {
  key: number;
  offer: MoveOffer;
  phase: OfferPhase;
}

export function phaseOf(offer: MoveOffer): OfferPhase {
  return offer.status === "moving" ? "moving" : offer.status === "accepted" ? "accepted" : "open";
}

// "about 40 ms instead of 230 ms": the numbers to the nearest 10 (a measurement, not a promise)
export function about(ms: number): number {
  return Math.max(10, Math.round(ms / 10) * 10);
}

export function offerTitle(offer: MoveOffer): string {
  return t("game.offer.title", { region: String(offer.regionName) });
}

// a number and its unit never part at a line break (non-breaking spaces)
export function offerFact(offer: MoveOffer): string {
  return t("game.offer.fact", { there: about(offer.pingThere), here: about(offer.pingHere) });
}

export function readyText(offer: MoveOffer): string {
  return t("game.offer.ready", { ready: Math.min(offer.accepted, offer.needed), needed: offer.needed });
}

// the page's answers; a failure is a console note (the offer simply goes away)
export async function reportShown(offer: MoveOffer): Promise<void> {
  offerEvent("shown", offer.region);
  const platform = platformState();
  if (platform.mode !== "platform") return;
  try {
    await answerOffer(platform.apiUrl, offer.id, "shown");
  } catch (e) {
    console.info("platform: offer not marked shown (" + String(e instanceof Error ? e.message : e) + ")");
  }
}

export async function answer(offer: MoveOffer, action: "accept" | "dismiss"): Promise<MoveOffer | null> {
  offerEvent(action === "accept" ? "accepted" : "dismissed", offer.region);
  const platform = platformState();
  if (platform.mode !== "platform") return null;
  try {
    return await answerOffer(platform.apiUrl, offer.id, action);
  } catch (e) {
    console.info("platform: offer " + action + " failed (" + String(e instanceof Error ? e.message : e) + ")");
    return null;
  }
}
