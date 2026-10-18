// The chip: who is playing, the first cell of the status bar at the top right. The cycle
// colour swatch, the name (the game's PLAYER_1) and one quieter line under it: the signed-in
// account's handle, or "not saved" for a guest with progress to lose (the Profile, one
// activation away, saves it), or the platform's level; nothing when none of those.
// The chip is one button: activating it opens the Profile stage, whose head is the
// name, editable there (the title travels from here to the stage head, Stage.tsx's
// travel); renaming stays "the chip, type, Enter".
import { ColorText } from "./ColorInput";
import { forwardRef } from "react";
import { t } from "../i18n";

interface ChipProps {
  name: string;
  raw?: string; // the name with its colour codes, drawn in its colours
  handle: string | null; // the signed-in account's handle, null for a guest or nobody
  unsaved: boolean; // a guest with progress, on a platform with accounts
  level: number | null; // the platform's level of this player, null without one
  disabled: boolean;
  onOpen(): void;
}

// the chip's accessible name: who, signed in or not saved, the level
function chipLabel(name: string, handle: string | null, unsaved: boolean, level: number | null): string {
  if (handle) return level !== null ? t("profile.chip.ariaAccountLevel", { name, handle, level }) : t("profile.chip.ariaAccount", { name, handle });
  if (unsaved) return level !== null ? t("profile.chip.ariaUnsavedLevel", { name, level }) : t("profile.chip.ariaUnsaved", { name });
  return level !== null ? t("profile.chip.ariaLevel", { name, level }) : t("profile.chip.aria", { name });
}

export const Chip = forwardRef<HTMLButtonElement, ChipProps>(function Chip({ name, raw, handle, unsaved, level, disabled, onOpen }, ref) {
  const sub = handle ?? (unsaved ? t("profile.chip.notSaved") : level !== null ? t("profile.chip.level", { level }) : null);
  return (
    <button
      className={"chip status-cell" + (handle ? " has-account" : "")}
      id="chip"
      type="button"
      ref={ref}
      disabled={disabled}
      aria-haspopup="dialog"
      aria-label={chipLabel(name, handle, unsaved, level)}
      onClick={onOpen}
    >
      <span className="chip-swatch" aria-hidden="true" />
      <span className="chip-who">
        <span className="chip-name" id="player-name">
          {raw ? <ColorText text={raw} /> : name}
        </span>
        {sub !== null && (
          <span className={"chip-sub" + (!handle && unsaved ? " chip-unsaved" : "")} id={handle ? "chip-account" : unsaved ? "chip-unsaved" : "chip-level"}>
            {sub}
          </span>
        )}
      </span>
    </button>
  );
});
