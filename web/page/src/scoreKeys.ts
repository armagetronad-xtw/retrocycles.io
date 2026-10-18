// Shift with the game's SCORE key while the scoreboard is up moves to its next tab. The
// game reads keys on the window in the capture phase and would hide the table on that press; this
// listener is added when the page starts, before the game's, so it can keep the press from it.
// App sets the handler while the scoreboard is up; it says whether it took the key.
type ScoreKeyHandler = (e: KeyboardEvent) => boolean;

let handler: ScoreKeyHandler | null = null;

window.addEventListener(
  "keydown",
  (e) => {
    if (handler?.(e)) {
      e.preventDefault();
      e.stopImmediatePropagation();
    }
  },
  true
);

export function onScoreKey(next: ScoreKeyHandler | null): void {
  handler = next;
}
