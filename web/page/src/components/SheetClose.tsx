// The sheets' close cell (Profile, Friends, Settings): a square cell at the head's right end,
// the key and the cross, so the pointer has a visible way out and the keys learn theirs.
// The foot is left to Esc's other meaning, back. Esc stays the keyboard's way (App.tsx), so
// the cell keeps out of the Tab order like the foot did.
import { Icon } from "./Icons";
import { t } from "../i18n";

// label: the sheet's name, in the page's language ("Close {label}")
export function SheetClose({ onClose, label }: { onClose(): void; label: string }) {
  return (
    <button className="sheet-x" type="button" tabIndex={-1} aria-label={t("game.sheet.close", { what: label })} aria-keyshortcuts="Escape" title={t("game.sheet.closeTitle")} onClick={onClose}>
      <span className="sheet-x-key" aria-hidden="true">
        {t("common.esc")}
      </span>
      <Icon name="x" className="sheet-x-icon" />
    </button>
  );
}
