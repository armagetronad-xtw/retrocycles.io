// CONTROLS' key cells. An action's keys are chips, each with its cross:
// a click (or Enter, Delete, Backspace on it) removes that key. The last cell, + ADD KEY, listens
// for the next key (PRESS A KEY, ESC CANCELS; a click on it again cancels too). Left / Right move
// between the cells. The key pressed to bind never reaches the game: the sheet is
// data-aa-keys="page" (API.md "Setup"), and the capture takes the key on the window before
// anything else. One key, one action (the game's rule): a key bound elsewhere moves here, and the
// row says what it was with UNDO for a few seconds.
import { useEffect, useRef, useState, type KeyboardEvent as ReactKeyboardEvent } from "react";
import type { AaBinding } from "../../aa/types";
import { keyLabel } from "../../keys";
import { actionWords } from "../../settings/schema";
import { Icon } from "../Icons";
import { t } from "../../i18n";

const MAX_KEYS = 4; // + ADD KEY goes once an action has this many

// keys the browser keeps (API.md "Setup", reserved keys): never offered to the game
export function reservedKey(e: KeyboardEvent): string | null {
  if (e.metaKey) return t("settings.keys.reserved");
  if ((e.ctrlKey || e.altKey) && !/^(Control|Alt|Shift)(Left|Right)$/.test(e.code)) return t("settings.keys.reserved");
  if (e.code === "F5" || e.code === "F11" || e.code === "F12") return t("settings.keys.reserved");
  return null;
}

export interface Capture {
  action: string;
  player: number;
  replace: number | null; // the keysym a new key takes the place of; the cells only add (null)
}

export interface RowNote {
  text: string;
  undo?(): void;
}

interface KeyCellsProps {
  id: string;
  action: string;
  player: number;
  bindings: AaBinding[];
  capture: Capture | null;
  disabled?: boolean;
  onCapture(c: Capture | null): void;
  onUnbind(keysym: number): void;
}

export function KeyCells({ id, action, player, bindings, capture, disabled, onCapture, onUnbind }: KeyCellsProps) {
  const mine = bindings.filter((b) => b.action === action && b.player === player).sort((a, b) => a.keysym - b.keysym);
  const listening = !!capture && capture.action === action && capture.player === player;
  const box = useRef<HTMLDivElement>(null);

  const cells = (): HTMLElement[] => Array.from(box.current?.querySelectorAll<HTMLElement>(".st-keycell") ?? []);

  // a removed chip's focus goes to the cell now in its place (or the add cell)
  const remove = (keysym: number, at: number) => {
    onUnbind(keysym);
    window.requestAnimationFrame(() => {
      const all = cells();
      all[Math.min(at, all.length - 1)]?.focus({ preventScroll: true });
    });
  };

  const key = (e: ReactKeyboardEvent<HTMLButtonElement>, act: () => void, removes: boolean) => {
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") {
      const all = cells();
      const at = all.indexOf(e.currentTarget);
      const next = all[at + (e.key === "ArrowRight" ? 1 : -1)];
      e.preventDefault();
      e.stopPropagation();
      next?.focus({ preventScroll: true });
    } else if (e.key === "Enter" || e.key === " " || (removes && (e.key === "Delete" || e.key === "Backspace"))) {
      e.preventDefault();
      e.stopPropagation();
      act();
    }
  };

  return (
    <div className="st-keys" id={"st-" + id + "-ctl"} ref={box} role="group" aria-labelledby={"st-" + id + "-key"}>
      {mine.map((b, i) => (
        <button
          key={b.keysym}
          className="st-keycell st-keychip"
          type="button"
          disabled={disabled}
          data-nav={i === 0 ? "" : undefined}
          title={t("settings.keys.remove.title", { key: keyLabel(b) })}
          aria-label={t("settings.keys.remove.aria", { key: keyLabel(b) })}
          onKeyDown={(e) => key(e, () => remove(b.keysym, i), true)}
          onClick={() => remove(b.keysym, i)}
        >
          <span className="st-keyname">{keyLabel(b)}</span>
          <Icon name="x" className="st-keyx" />
        </button>
      ))}
      {listening ? (
        <button
          className="st-keycell st-keyadd is-capturing"
          type="button"
          data-nav={mine.length === 0 ? "" : undefined}
          aria-live="polite"
          aria-label={t("settings.keys.capture.aria")}
          onKeyDown={(e) => e.preventDefault()}
          onClick={() => onCapture(null)}
        >
          {t("settings.keys.capture")}
          <span className="st-keyesc">{t("settings.keys.escCancels")}</span>
        </button>
      ) : (
        mine.length < MAX_KEYS && (
          <button
            className={"st-keycell st-keyadd" + (mine.length ? "" : " is-empty")}
            type="button"
            disabled={disabled}
            data-nav={mine.length === 0 ? "" : undefined}
            title={t("settings.keys.add.title", { action: actionWords(action) })}
            aria-label={t("settings.keys.add.aria", { action: actionWords(action) })}
            onKeyDown={(e) => key(e, () => onCapture({ action, player, replace: null }), false)}
            onClick={() => onCapture({ action, player, replace: null })}
          >
            <Icon name="plus" className="st-keyplus" />
            {mine.length ? t("settings.keys.add") : t("settings.keys.addFirst")}
          </button>
        )
      )}
    </div>
  );
}

// the capture itself: the next key on the window, before the client and the sheet see it
export function useKeyCapture(capture: Capture | null, done: (e: KeyboardEvent | null) => void) {
  const cb = useRef(done);
  cb.current = done;
  useEffect(() => {
    if (!capture) return;
    let over = false; // one key per capture (a modifier released after its key must not bind again)
    const finish = (e: KeyboardEvent | null) => {
      if (over) return;
      over = true;
      cb.current(e);
    };
    const take = (e: KeyboardEvent) => {
      // a modifier alone waits for the key it modifies, unless it is released alone (then it
      // is the key: Shift as a brake)
      if (/^(Shift|Control|Alt|Meta)(Left|Right)$/.test(e.code) && e.type === "keydown") {
        e.preventDefault();
        e.stopImmediatePropagation();
        return;
      }
      e.preventDefault();
      e.stopImmediatePropagation();
      if (e.code === "Escape" && e.type === "keydown") return finish(null);
      finish(e);
    };
    const up = (e: KeyboardEvent) => {
      if (/^(Shift|Control|Alt)(Left|Right)$/.test(e.code)) {
        e.preventDefault();
        e.stopImmediatePropagation();
        finish(e);
      }
    };
    // a press anywhere but the listening cell (whose click cancels by itself) cancels
    const press = (e: PointerEvent) => {
      if (!(e.target instanceof Element) || !e.target.closest(".st-keyadd.is-capturing")) finish(null);
    };
    window.addEventListener("keydown", take, true);
    window.addEventListener("keyup", up, true);
    window.addEventListener("pointerdown", press, true);
    return () => {
      window.removeEventListener("keydown", take, true);
      window.removeEventListener("keyup", up, true);
      window.removeEventListener("pointerdown", press, true);
    };
  }, [capture]);
}

// a note under a key row: what moved, with UNDO, gone after a few seconds
export function useRowNotes(): [Record<string, RowNote>, (row: string, note: RowNote | null) => void] {
  const [notes, setNotes] = useState<Record<string, RowNote>>({});
  const timers = useRef<Record<string, number>>({});
  const set = (row: string, note: RowNote | null) => {
    window.clearTimeout(timers.current[row]);
    setNotes((n) => {
      const next = { ...n };
      if (note) next[row] = note;
      else delete next[row];
      return next;
    });
    if (note) timers.current[row] = window.setTimeout(() => set(row, null), 6000);
  };
  useEffect(() => () => Object.values(timers.current).forEach((t) => window.clearTimeout(t)), []);
  return [notes, set];
}
