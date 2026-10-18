// The Profile sheet's pieces: a field is a square near-black well with the cell
// line, its key in quiet caps at the well's left (the yes/no rows' idiom); an action is a
// lane (the in-game menu's, 86u, or the small one) or, the one primary act of a view, the
// solid ink button; a failure is the reversed cell. Whatever has the focus fills near-black
// and shows a short ink bar at its left edge (profile.css); the cyan trail is the home
// menu's alone. Keyboard: the client leaves keys to inputs (API.md "Setup"), so typing,
// Space on the checkbox and Enter to submit are the browser's; Tab, the arrows, Esc and
// Enter or Space on a button are handled by App.tsx (the client takes them elsewhere).
import type { ReactNode } from "react";
import type { ProviderInfo } from "../../platform/types";
import { DocLink } from "../Docs";
import { BrandMark } from "./brands";
import { t, tx } from "../../i18n";

// another site's login typed into our page (an old id and its password): password managers must
// neither offer to save it for retrocycles.io nor fill it in later, where it would land in our
// sign-in. autocomplete="off" plus the managers' own opt-outs (some ignore "off" for passwords)
export const NO_PASSWORD_MANAGERS = {
  autoComplete: "off",
  "data-1p-ignore": "",
  "data-lpignore": "true",
  "data-bwignore": "",
  "data-form-type": "other"
} as const;

// the managers' opt-outs without autoComplete, which Field sets itself
const { autoComplete: _off, ...FOREIGN_ATTRS } = NO_PASSWORD_MANAGERS;

interface FieldProps {
  id: string;
  label: string;
  value: string;
  onChange(value: string): void;
  type?: "text" | "email" | "password";
  autoComplete?: string;
  hint?: ReactNode;
  error?: string | null;
  maxLength?: number;
  first?: boolean; // takes the focus when the view opens
  suffix?: ReactNode; // quiet text after the typed value, inside the well (the handle's @authority)
  act?: ReactNode; // a RowAct at the well's right end (the password's FORGOT?)
  onBlur?(): void;
  onEnter?(): void; // Enter in the field (fields outside a <form>)
  foreign?: boolean; // another site's login: NO_PASSWORD_MANAGERS, autoComplete ignored
}

export function Field({ id, label, value, onChange, type = "text", autoComplete, hint, error, maxLength, first, suffix, act, onBlur, onEnter, foreign }: FieldProps) {
  const describedBy = [error ? id + "-err" : "", hint ? id + "-hint" : ""].filter(Boolean).join(" ") || undefined;
  return (
    <div className={"pf-field" + (error ? " is-invalid" : "") + (value ? "" : " is-empty")}>
      <div
        className="pf-well"
        onPointerDown={(e) => {
          // a press anywhere in the well (its padding, the suffix) types in the field
          const input = document.getElementById(id);
          if (!input || e.target === input || (e.target as Element).closest?.("button, a")) return;
          e.preventDefault();
          input.focus();
        }}
      >
        <label className="pf-key" htmlFor={id}>
          {label}
        </label>
        <input
          className="pf-input"
          id={id}
          name={id}
          type={type}
          value={value}
          autoComplete={foreign ? "off" : autoComplete}
          autoCapitalize="off"
          autoCorrect="off"
          spellCheck={false}
          maxLength={maxLength}
          aria-invalid={error ? true : undefined}
          aria-describedby={describedBy}
          data-first={first ? "" : undefined}
          size={1}
          style={suffix !== undefined ? { width: Math.max(1, value.length) + 0.4 + "ch" } : undefined}
          onChange={(e) => onChange(e.target.value)}
          onBlur={onBlur}
          onKeyDown={
            onEnter
              ? (e) => {
                  if (e.key === "Enter") {
                    e.preventDefault();
                    onEnter();
                  }
                }
              : undefined
          }
          {...(foreign ? FOREIGN_ATTRS : {})}
        />
        {suffix !== undefined && (
          <span className="pf-suffix" aria-hidden="true">
            {suffix}
          </span>
        )}
        {act}
      </div>
      {error && (
        <p className="cell pf-cell" id={id + "-err"}>
          {error}
        </p>
      )}
      {hint && (
        <p className="pf-hint" id={id + "-hint"}>
          {hint}
        </p>
      )}
    </div>
  );
}

// a checkbox: the real input (Space toggles it, screen readers say so), drawn as a square
// cell at the left that fills with ink and its tick when checked, the sentence after it
function Check({ id, label, ariaLabel, checked, error, hint, onChange }: { id: string; label: string; ariaLabel: string; checked: boolean; error?: string; hint: ReactNode; onChange(value: boolean): void }) {
  const describedBy = [error ? id + "-err" : "", id + "-hint"].filter(Boolean).join(" ");
  return (
    <div className={"pf-check" + (checked ? " is-checked" : "") + (error ? " is-invalid" : "")}>
      <label className="pf-check-row" htmlFor={id}>
        <input
          className="pf-check-input"
          id={id}
          type="checkbox"
          checked={checked}
          aria-label={ariaLabel}
          aria-invalid={error ? true : undefined}
          aria-describedby={describedBy}
          onChange={(e) => onChange(e.target.checked)}
        />
        <span className="pf-check-box" aria-hidden="true">
          <svg viewBox="0 0 16 16" focusable="false">
            <path d="M3 8.5 6.5 12 13 4.5" />
          </svg>
        </span>
        <span className="pf-check-key">{label}</span>
      </label>
      {error && (
        <p className="cell pf-cell" id={id + "-err"}>
          {error}
        </p>
      )}
      <p className="pf-hint" id={id + "-hint"}>
        {hint}
      </p>
    </div>
  );
}

// "I am <age> or older" at sign-up, when the age policy asks for it (LEGAL.
// requireAgeConfirmation), with the reversed cell under it when the form is refused without it
export function AgeConfirm({ id, age, checked, error, onChange }: { id: string; age: number; checked: boolean; error?: string; onChange(value: boolean): void }) {
  return (
    <Check
      id={id}
      label={t("profile.age.label", { age })}
      ariaLabel={t("profile.age.aria", { age })}
      checked={checked}
      error={error}
      onChange={onChange}
      hint={tx("profile.age.hint", { age, link: <DocLink id="age">{t("profile.age.link")}</DocLink> })}
    />
  );
}

// news emails at sign-up: unticked, never required (opt-in consent; the platform keeps when and with which words)
export function NewsConfirm({ id, checked, onChange }: { id: string; checked: boolean; onChange(value: boolean): void }) {
  return (
    <Check
      id={id}
      label={t("profile.news.label")}
      ariaLabel={t("profile.news.aria")}
      checked={checked}
      onChange={onChange}
      hint={tx("profile.news.hint", { link: <DocLink id="privacy">{t("profile.consent.privacy")}</DocLink> })}
    />
  );
}

// after a refused submit: the focus on the first field that says why (it is
// announced there through aria-describedby), scrolled into view
export function focusFirstInvalid() {
  window.requestAnimationFrame(() => {
    const field = document.querySelector<HTMLElement>('#profile [aria-invalid="true"]');
    if (!field) return;
    field.focus({ preventScroll: true });
    (field.closest<HTMLElement>(".pf-field, .pf-check") ?? field).scrollIntoView?.({ block: "nearest" });
  });
}

// a failure: the reversed cell, announced
export function Cell({ text, id }: { text: string | null; id?: string }) {
  if (!text) return null;
  return (
    <p className="cell pf-cell pf-cell-block" id={id} role="alert">
      {text}
    </p>
  );
}

// a quiet line: what just happened, announced politely
export function Notice({ text }: { text: string | null }) {
  return (
    <p className="pf-notice" id="profile-notice" role="status" hidden={!text}>
      {text || ""}
    </p>
  );
}

interface ActProps {
  id?: string;
  children: ReactNode;
  onClick?(): void;
  submit?: boolean;
  small?: boolean;
  disabled?: boolean;
  first?: boolean;
  describedBy?: string;
}

// an action: a lane (86u; small, 60u, for the lesser ones), the ink bar at its edge on focus
export function Act({ id, children, onClick, submit, small, disabled, first, describedBy }: ActProps) {
  return (
    <button
      id={id}
      className={small ? "lane lane-small pf-lane" : "lane pf-lane"}
      type={submit ? "submit" : "button"}
      disabled={disabled}
      data-first={first ? "" : undefined}
      aria-describedby={describedBy}
      onClick={onClick}
    >
      {children}
    </button>
  );
}

// the one primary act of a view: the solid ink button (the match summary's save)
export function Solid({ id, children, onClick, submit, disabled, first, describedBy }: Omit<ActProps, "small">) {
  return (
    <button
      id={id}
      className="pf-solid"
      type={submit ? "submit" : "button"}
      disabled={disabled}
      data-first={first ? "" : undefined}
      aria-describedby={describedBy}
      onClick={onClick}
    >
      {children}
    </button>
  );
}

// a row's own action (CHANGE, LINK, SHOW): small quiet caps at the row's right end; on
// focus the whole row takes the lane's mark
export function RowAct({ id, children, onClick, disabled, first }: Omit<ActProps, "small" | "submit" | "describedBy">) {
  return (
    <button id={id} className="pf-row-act" type="button" disabled={disabled} data-first={first ? "" : undefined} onClick={onClick}>
      {children}
    </button>
  );
}

// a group of lanes under a view's fields
export function Lanes({ children }: { children: ReactNode }) {
  return <div className="pf-lanes">{children}</div>;
}

// the providers the platform offers (its /v1/platform/config `providers`), as every
// sign-in screen offers them: "Continue with <name>" buttons side by side, the provider's
// own mark before the label (brands.tsx), first on the form; then OR, then the email form
// (Or). A button too narrow for the whole label shows the name alone (profile.css); its
// accessible name stays the whole label
export function Providers({ providers, label, disabled, first, onProvider }: { providers: ProviderInfo[]; label: string; disabled: boolean; first?: boolean; onProvider(p: ProviderInfo): void }) {
  if (!providers.length) return null;
  return (
    <div className="pf-providers" role="group" aria-label={label}>
      {providers.map((p, i) => (
        <button
          key={p.id}
          id={"provider-" + p.id}
          className="pf-provider"
          type="button"
          disabled={disabled}
          data-first={first && i === 0 ? "" : undefined}
          onClick={() => onProvider(p)}
        >
          <BrandMark id={p.id} />
          <span className="pf-provider-label">{providerLabel(p.name)}</span>
        </button>
      ))}
    </div>
  );
}

// "Continue with <name>" in the language's order: the words around the name in .pf-provider-with,
// which a narrow button hides (profile.css), the name itself plain
function providerLabel(name: string): ReactNode {
  const text = t("profile.provider.continueWith", { name: "{name}" });
  const [before, after = ""] = text.split("{name}");
  return (
    <>
      {before && <span className="pf-provider-with">{before}</span>}
      {name}
      {after && <span className="pf-provider-with">{after}</span>}
    </>
  );
}

// the line between the providers and the email form
export function Or() {
  return (
    <p className="pf-or" aria-hidden="true">
      <span>{t("profile.or")}</span>
    </p>
  );
}

// a view's own head under the plate (the panel head: caps over a hairline) and its sentence;
// a form's way to its sibling (sign in, create an account) is a RowAct at the head's right
export function ViewHead({ title, say, act }: { title: ReactNode; say?: ReactNode; act?: ReactNode }) {
  return (
    <div className="pf-head">
      <div className="pf-head-line">
        <h3 className="pf-title" id="profile-view-title">
          {title}
        </h3>
        {act}
      </div>
      {say && <p className="pf-say">{say}</p>}
    </div>
  );
}
