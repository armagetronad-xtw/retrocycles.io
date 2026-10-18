// The paste review: pasted user.cfg text never applies straight
// away. This view says what would change, by tab, in the rows' own words, each group a yes/no
// (name and colour, and keys, start off: pasting someone's camera must not rename you or move
// your brake), then what was refused and why, then the one solid APPLY.
import { useState } from "react";
import type { Review } from "../../settings/cfg";
import { t } from "../../i18n";

interface PasteProps {
  review: Review | null;
  text: string;
  onText(text: string): void;
  onToggle(group: string, on: boolean): void;
  onApply(): void;
}

export function PasteView({ review, text, onText, onToggle, onApply }: PasteProps) {
  const [shown, setShown] = useState(false); // the refused lines, folded
  const count = review ? review.groups.reduce((n, g) => n + (g.on ? g.changes.length : 0), 0) : 0;
  return (
    <div className="st-paste" id="settings-paste">
      <div className="pf-head st-paste-head">
        <h3 className="pf-title">{t("settings.paste.title")}</h3>
        <p className="pf-say">{t("settings.paste.say")}</p>
      </div>
      <textarea
          className="st-paste-text"
          id="settings-paste-text"
          value={text}
          rows={review ? 4 : 8}
          spellCheck={false}
          autoComplete="off"
          placeholder={"CAMERA_CUSTOM_BACK 9\nCAMERA_CUSTOM_RISE 6.5\nKEYBOARD 97 PLAYER_BIND CYCLE_TURN_LEFT 1"}
          aria-label={t("settings.paste.aria")}
          data-nav=""
          data-first=""
          onChange={(e) => onText(e.target.value)}
        />
      {review && (
        <>
          {review.groups.length === 0 && <p className="pf-say st-paste-none">{review.same ? t("settings.paste.noneSame", { n: review.same }) : t("settings.paste.none")}</p>}
          {review.groups.map((g) => (
            <section className={"st-paste-group" + (g.on ? "" : " is-off")} key={g.id} aria-labelledby={"st-paste-" + g.id}>
              <button
                className={"st-paste-switch" + (g.on ? " is-on" : "")}
                id={"st-paste-" + g.id}
                type="button"
                role="switch"
                aria-checked={g.on}
                data-nav=""
                onClick={() => onToggle(g.id, !g.on)}
                onKeyDown={(e) => {
                  if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
                    e.preventDefault();
                    e.stopPropagation();
                    onToggle(g.id, e.key === "ArrowRight");
                  }
                }}
              >
                <span className="st-paste-title">{g.title}</span>
                <span className="st-paste-count">{t("settings.paste.changes", { n: g.changes.length })}</span>
                <span className="st-switch-no">{t("settings.paste.skip")}</span>
                <span className="st-switch-yes">{t("settings.paste.apply")}</span>
              </button>
              <ul className="st-paste-list">
                {g.changes.map((c) => (
                  <li className="st-paste-line" key={c.name}>
                    <span className="st-paste-label">{c.label}</span>
                    <span className="st-paste-from">{c.from}</span>
                    <svg className="st-paste-arrow" viewBox="0 0 24 24" aria-label={t("settings.paste.to")}>
                      <path d="M4 12 H19 M13 6 L19 12 L13 18" />
                    </svg>
                    <span className="st-paste-to">{c.to}</span>
                  </li>
                ))}
              </ul>
            </section>
          ))}
          {review.refused.length > 0 && (
            <section className="st-paste-refused">
              <button className="pf-row-act st-paste-fold" type="button" aria-expanded={shown} data-nav="" onClick={() => setShown((s) => !s)}>
                {t("settings.paste.refused", { n: review.refused.length })}
              </button>
              {shown && (
                <ul className="st-paste-list">
                  {review.refused.map((r, i) => (
                    <li className="st-paste-line is-refused" key={i}>
                      <span className="st-paste-label">{r.line}</span>
                      <span className="st-paste-why">{r.why}</span>
                    </li>
                  ))}
                </ul>
              )}
            </section>
          )}
          <button className="pf-solid st-paste-apply" id="settings-paste-apply" type="button" disabled={count === 0} data-nav="" onClick={onApply}>
            {count === 0 ? t("settings.paste.nothing") : t("settings.paste.applyN", { n: count })}
          </button>
        </>
      )}
    </div>
  );
}
