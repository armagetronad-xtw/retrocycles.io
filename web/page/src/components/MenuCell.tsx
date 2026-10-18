// The menu cell at the top right of the game on a desktop: the touch controls'
// menu cell (TouchControls.tsx) for a mouse, opening the same Esc menu. It never takes the focus
// (tabIndex -1, no focus on the press), so the keys stay the game's; gone with the menu, as the
// touch one is (App.tsx).
import { t } from "../i18n";

export function MenuCell({ onOpen }: { onOpen(): void }) {
  return (
    <button
      type="button"
      tabIndex={-1}
      className="menu-cell"
      id="menu-cell"
      aria-label={t("game.touch.menu")}
      title={t("game.touch.menu")}
      onMouseDown={(e) => e.preventDefault()}
      onClick={(e) => {
        e.currentTarget.blur();
        onOpen();
      }}
      onContextMenu={(e) => e.preventDefault()}
    >
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" aria-hidden="true">
        <path d="M4 7h16M4 12h16M4 17h16" />
      </svg>
    </button>
  );
}
