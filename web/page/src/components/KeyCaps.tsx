// Keys as the page shows them (the keys line, the connecting screen): each key a small square
// cell in the cell line, its name in Akshar caps or, for the arrows, the drawn arrow; the
// game's word after the keys of a group, quieter. The arrows are
// drawn because the game's word for one ("left") beside the action ("turn left") read as
// LEFT TURN LEFT.
import type { HintItem } from "../keys";
import { Icon, type IconName } from "./Icons";
import { t, type PlainKey } from "../i18n";

// the names are keys.ts keyLabel's (English, the page's own key names); the words translated
const ARROWS: Record<string, { icon: IconName; say: PlainKey }> = {
  left: { icon: "arrow-left", say: "game.key.left" },
  right: { icon: "arrow-right", say: "game.key.right" },
  up: { icon: "arrow-up", say: "game.key.up" },
  down: { icon: "arrow-down", say: "game.key.down" }
};

export function KeyCap({ name }: { name: string }) {
  const arrow = ARROWS[name];
  if (arrow)
    return (
      <kbd className="keycap is-arrow" role="img" aria-label={t(arrow.say)}>
        <Icon name={arrow.icon} />
      </kbd>
    );
  return <kbd className="keycap">{name}</kbd>;
}

// groups of keys and their word, in one row that wraps between groups, never inside one
// `live`: a status region of its own (the keys line); inside one already (the connecting
// screen's), not
export function KeyGroups({ items, className, id, live }: { items: HintItem[]; className: string; id?: string; live?: boolean }) {
  return (
    <p className={"keygroups " + className} id={id} role={live ? "status" : undefined}>
      {items.map((item) => (
        <span className="keygroup" key={item.words}>
          <span className="keygroup-keys">
            {item.keys.map((k) => (
              <KeyCap name={k} key={k} />
            ))}
          </span>
          <span className="keygroup-words">{item.words}</span>
        </span>
      ))}
    </p>
  );
}
