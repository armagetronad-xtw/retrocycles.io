// ADVANCED's escape hatch: every setting of the player's (listConfig: what user.cfg
// keeps, and the items the tabs edit; the rooms' own rules are left out), by its
// raw name, searchable, its value a well; so no veteran is blocked by a setting the curated tabs
// leave out. A value runs through setConfig as typed; one the game refuses keeps its old value.
// Shown 60 at a time: the find narrows it.
import { useMemo, useState } from "react";
import type { AaConfigItem } from "../../aa/types";
import { unescapeValue } from "../../settings/store";
import { t } from "../../i18n";

const PAGE = 60;

interface EveryProps {
  items: AaConfigItem[];
  query: string;
  onQuery(q: string): void;
  onSet(name: string, value: string): void;
}

export function EverySetting({ items, query, onQuery, onSet }: EveryProps) {
  const [more, setMore] = useState(1);
  const q = query.trim().toLowerCase().replace(/\s+/g, "_");
  const hits = useMemo(() => (q ? items.filter((i) => i.name.toLowerCase().includes(q)) : items), [items, q]);
  const shown = hits.slice(0, PAGE * more);
  return (
    <div className="st-every" id="settings-every">
      <div className="pf-well st-every-find">
        <label className="pf-key" htmlFor="settings-every-find">
          {t("settings.every.name")}
        </label>
        <input
          className="pf-input"
          id="settings-every-find"
          value={query}
          spellCheck={false}
          autoComplete="off"
          placeholder="camera_custom"
          data-nav=""
          onChange={(e) => {
            setMore(1);
            onQuery(e.target.value);
          }}
        />
        <span className="st-every-count">
          {t("settings.every.count", { shown: hits.length, all: items.length })}
        </span>
      </div>
      <ul className="st-every-list">
        {shown.map((i) => (
          <EveryRow key={i.name} item={i} onSet={onSet} />
        ))}
      </ul>
      {hits.length > shown.length && (
        <button className="pf-row-act st-every-more" type="button" data-nav="" onClick={() => setMore((m) => m + 1)}>
          {t("settings.more.count", { n: hits.length - shown.length })}
        </button>
      )}
    </div>
  );
}

function EveryRow({ item, onSet }: { item: AaConfigItem; onSet(name: string, value: string): void }) {
  const [draft, setDraft] = useState<string | null>(null);
  const value = draft ?? unescapeValue(item.value);
  const note = !item.writable ? t("settings.every.byRoom") : !item.savable ? t("settings.every.untilReload") : null;
  const id = "every-" + item.name.toLowerCase();
  return (
    <li className={"st-every-row" + (item.writable ? "" : " is-off")}>
      <label className="st-every-name" htmlFor={id}>
        {item.name.toLowerCase()}
      </label>
      <input
        className="st-text st-every-val"
        id={id}
        value={value}
        disabled={!item.writable}
        spellCheck={false}
        autoComplete="off"
        data-nav=""
        onChange={(e) => setDraft(e.target.value)}
        onBlur={() => {
          if (draft !== null && draft !== unescapeValue(item.value)) onSet(item.name, draft);
          setDraft(null);
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter") {
            e.preventDefault();
            if (draft !== null && draft !== unescapeValue(item.value)) onSet(item.name, draft);
            setDraft(null);
          } else if (e.key === "Escape" && draft !== null) {
            e.preventDefault();
            e.stopPropagation();
            setDraft(null);
          }
        }}
      />
      <span className="st-note">{note}</span>
    </li>
  );
}
