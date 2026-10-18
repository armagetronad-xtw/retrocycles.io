// An offer of a closer server at the end of a round, in the
// rewards' place (mid-left, where the match summary sits): never in the way. Like the match
// summary it takes no focus and no keys (the game keeps them), takes no pointer events except
// its two actions, and goes away on its own (10 s, or the next round's start). In the page's
// idiom: the region in the game's face at headline size, the numbers as
// a fact line, MOVE as the one action (ivory with the small amber caret on hover), "not now" a
// step back; once accepted, "3 of 4 ready" where the actions were. Nothing enclosed.
import type { MouseEvent } from "react";
import { Colored } from "./Colored";
import { offerFact, offerTitle, readyText, type OfferLine } from "../platform/offers";
import { t } from "../i18n";

interface OfferProps {
  line: OfferLine;
  onAccept(): void;
  onDismiss(): void;
}

// a click the player chose; the focus stays with the game
const keepFocus = (e: MouseEvent<HTMLButtonElement>) => e.preventDefault();

export function OfferPanel({ line, onAccept, onDismiss }: OfferProps) {
  const { offer, phase } = line;
  return (
    <section className="offer" id="move-offer" role="status" aria-label={t("game.offer.label")} data-phase={phase} key={line.key}>
      <h2 className="offer-title">{offerTitle(offer)}</h2>
      <p className="offer-fact">{offerFact(offer)}</p>
      {offer.serverName && (
        <p className="offer-server">
          <Colored text={offer.serverName} />
        </p>
      )}
      {phase === "open" ? (
        <>
          <div className="offer-acts">
            <button className="act" type="button" id="offer-move" tabIndex={-1} onMouseDown={keepFocus} onClick={onAccept}>
              {t("game.offer.move")}
            </button>
            <button className="act act-small offer-not-now" type="button" id="offer-dismiss" tabIndex={-1} onMouseDown={keepFocus} onClick={onDismiss}>
              {t("game.offer.notNow")}
            </button>
          </div>
          <p className="offer-say">
            {offer.needed > 1
              ? t("game.offer.togetherOnce", { needed: offer.needed })
              : t("game.offer.together")}
          </p>
        </>
      ) : (
        <>
          <p className="offer-ready" id="offer-ready">{phase === "moving" ? t("game.offer.moving") : readyText(offer)}</p>
          <p className="offer-say">
            {phase === "moving" ? t("game.offer.movingNow") : t("game.offer.movingOnce", { needed: offer.needed })}
          </p>
        </>
      )}
    </section>
  );
}
