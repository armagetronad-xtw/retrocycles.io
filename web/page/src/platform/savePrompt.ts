// When a guest's match summary asks them to save their progress, and in which words. A guest
// lives in this browser only, so the ask is tied to what there is to lose, never mid-round:
// - `level_up`: the match raised the level (the strong ask);
// - `first`: the guest's first finished match;
// - `repeat`: every REPEAT_EVERY finished matches after the first (4th, 7th, ...), quietly.
// Everything comes from the platform's numbers (`lastMatch`, `stats.matches`), so the page
// keeps no count of its own.
import type { LastMatch, Progression } from "./types";
import { t } from "../i18n";

export const REPEAT_EVERY = 3;

export type SaveTrigger = "level_up" | "first" | "repeat";

// the ask this match's summary carries, or null for none
export function saveTrigger(match: LastMatch, progression: Progression): SaveTrigger | null {
  if (progression.xp <= 0) return null; // nothing to save yet
  if (match.levelAfter > match.levelBefore) return "level_up";
  const matches = progression.stats.matches;
  if (matches <= 1) return "first";
  if ((matches - 1) % REPEAT_EVERY === 0) return "repeat";
  return null;
}

export function strongSave(trigger: SaveTrigger): boolean {
  return trigger === "level_up";
}

// the line under "save your progress"
export function saveWords(trigger: SaveTrigger): string {
  switch (trigger) {
    case "level_up":
      return t("game.save.levelUp");
    case "first":
    case "repeat":
      return t("game.save.guest");
  }
}
