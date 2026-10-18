// A server that is not ours wants to send the player to another host: the page
// asks, and never follows on its own. Over the game in the Duel Panel's place and idiom: the quiet head with the seconds left, one sentence naming where to, the quiet
// line that Retrocycles doesn't check that server, MOVE solid and STAY lined. Like the duel panel
// it takes no focus and no keys (the game keeps them): its acts take the pointer only, and the
// Esc menu holds the same two answers as lanes (GameMenu's `moveAsk`). Unanswered, it goes after
// 30 s and the player stays. A note (the blocklist refused the target) uses the same panel.
import type { MouseEvent } from "react";
import { useSecondsLeft } from "./Duel";
import { KeyCap } from "./KeyCaps";
import { touchPrimary } from "../compact";
import { t } from "../i18n";

export type MoveAskLine = { kind: "ask"; name: string; address: string; until: number } | { kind: "note"; text: string };

const keepFocus = (e: MouseEvent<HTMLButtonElement>) => e.preventDefault();

export function MoveAsk({ line, onMove, onStay }: { line: MoveAskLine; onMove(): void; onStay(): void }) {
  const left = useSecondsLeft(line.kind === "ask" ? line.until : null);
  const touch = touchPrimary();
  return (
    <section className="duel move-ask" id="move-ask" aria-label={t("game.moveAsk.label")} data-kind={line.kind}>
      <p className="duel-said" aria-live="polite">
        {line.kind === "ask" ? t("game.moveAsk.say", { name: line.name, address: line.address }) : line.text}
      </p>
      <div className="duel-top" aria-hidden="true">
        <h2 className="duel-head">{t("game.moveAsk.title")}</h2>
        {left !== null && <span className="duel-left">{t("game.duel.secondsLeft", { seconds: left })}</span>}
      </div>
      {line.kind === "ask" ? (
        <>
          <p className="duel-say is-who" id="move-ask-say">
            <span>{t("game.moveAsk.say", { name: line.name, address: line.address })}</span>
          </p>
          <p className="duel-foot">{t("game.moveAsk.foot")}</p>
          <div className="duel-acts">
            <button className="duel-act is-solid" type="button" id="move-ask-move" tabIndex={-1} onMouseDown={keepFocus} onClick={onMove}>
              {t("game.moveAsk.move")}
            </button>
            <button className="duel-act" type="button" id="move-ask-stay" tabIndex={-1} onMouseDown={keepFocus} onClick={onStay}>
              {t("game.moveAsk.stay")}
            </button>
          </div>
          {!touch && (
            <p className="duel-keys">
              <KeyCap name="esc" />
              <span>{t("game.duel.inMenu")}</span>
            </p>
          )}
        </>
      ) : (
        <p className="duel-say" id="move-ask-note">
          {line.text}
        </p>
      )}
    </section>
  );
}
