// The plate's swatch, pressable: it opens a small tray of the desktop game's ten cycle colours
// hung under it (Settings' Swatches, settings/controls.tsx), so the colour changes where it is
// shown. Picking one sets COLOR_*_1 at once (App.tsx), the swatch follows and the tray closes;
// CUSTOM opens Settings on PLAYER, where the red/green/blue sliders are. Esc, a second press of
// the swatch or a press elsewhere in the sheet closes the tray. Without the client (nothing to
// set yet) the swatch is the plain square it was.
import { useEffect, useRef, type KeyboardEvent as ReactKeyboardEvent } from "react";
import { COLOUR_PRESETS, css } from "../settings/controls";
import { t } from "../../i18n";

export type Rgb = [number, number, number];

export interface PlateColour {
  rgb: Rgb;
  onPick(rgb: Rgb): void;
  onCustom(): void;
}

interface Props {
  colour: PlateColour | null;
  open: boolean;
  onOpen(open: boolean): void;
}

export function PlateSwatch({ colour, open, onOpen }: Props) {
  const wrap = useRef<HTMLDivElement>(null);
  const at = colour ? COLOUR_PRESETS.findIndex((p) => p.rgb.every((c, i) => c === colour.rgb[i])) : -1;

  // opened: the focus on the colour now (or the first); a press elsewhere in the sheet closes it
  useEffect(() => {
    if (!open) return;
    (wrap.current?.querySelector<HTMLElement>(".pf-tint.is-on") ?? wrap.current?.querySelector<HTMLElement>(".pf-tint"))?.focus({ preventScroll: true });
    const away = (e: PointerEvent) => {
      if (!wrap.current?.contains(e.target as Node)) onOpen(false);
    };
    window.addEventListener("pointerdown", away, true);
    return () => window.removeEventListener("pointerdown", away, true);
  }, [open, onOpen]);

  if (!colour) return <i className="pf-swatch" aria-hidden="true" />;

  const pick = (rgb: Rgb) => {
    colour.onPick(rgb);
    onOpen(false);
    wrap.current?.querySelector<HTMLElement>(".pf-swatch")?.focus({ preventScroll: true });
  };

  // Left / Right move along the row, as on Settings' swatches (Up / Down are the sheet's)
  const key = (e: ReactKeyboardEvent) => {
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    const all = [...(wrap.current?.querySelectorAll<HTMLElement>(".pf-tint") ?? [])];
    const i = all.indexOf(document.activeElement as HTMLElement);
    if (i < 0) return;
    e.preventDefault();
    e.stopPropagation();
    all[(i + (e.key === "ArrowRight" ? 1 : -1) + all.length) % all.length].focus({ preventScroll: true });
  };

  return (
    <div className="pf-swatch-wrap" ref={wrap}>
      <button
        className={"pf-swatch is-press" + (open ? " is-open" : "")}
        type="button"
        aria-label={at >= 0 ? t("profile.colour.ariaNamed", { colour: t(COLOUR_PRESETS[at].name) }) : t("profile.colour.aria")}
        aria-haspopup="true"
        aria-expanded={open}
        aria-controls="profile-tints"
        title={t("profile.colour.title")}
        onClick={() => onOpen(!open)}
      />
      {open && (
        <div className="pf-tints" id="profile-tints" role="group" aria-label={t("profile.colour.aria")} onKeyDown={key}>
          <p className="pf-tints-key">{t("profile.colour.key")}</p>
          <div className="pf-tints-grid">
            {COLOUR_PRESETS.map((p, i) => (
              <button
                key={p.label}
                className={"pf-tint" + (i === at ? " is-on" : "")}
                type="button"
                aria-label={t(p.name)}
                aria-pressed={i === at}
                title={t(p.name)}
                style={{ background: css(p.rgb) }}
                onClick={() => pick(p.rgb)}
              />
            ))}
          </div>
          <button
            className="pf-tints-custom"
            type="button"
            onClick={() => {
              onOpen(false);
              colour.onCustom();
            }}
          >
            {t("profile.colour.custom")} <span aria-hidden="true">{t("profile.colour.inSettings")}</span>
          </button>
        </div>
      )}
    </div>
  );
}
