// Direct Connect's pane: in the Server Browser's details place, for a server
// reached by its address rather than from the board. Built in the Server Browser pane's grammar:
// a head, the ADDRESS well (the field well, its key inside), MORE for a
// different browser port, the preview the platform's probe gives (the name, players, version,
// whether its browser port answers), then the acts: JOIN as the solid act, SAVE (My servers)
// beside it. A server that cannot be joined from here says why before anyone presses JOIN; a
// blocked one refuses. Before the first join to an address the acts give way to the warning:
// the sentence, a checkbox and CONTINUE. A saved server (a My servers row) shows its address
// as a fact, with REMOVE in SAVE's place.
import type { RefObject } from "react";
import type { DirectAddress, AddressError } from "../directAddress";
import { DEFAULT_GAME_PORT, signalPortFor } from "../directAddress";
import type { Probe } from "../platform/direct";
import { Colored } from "./Colored";
import { Icon } from "./Icons";
import { num, t } from "../i18n";

export interface DirectPaneProps {
  saved: boolean; // a My servers row: the address is a fact, not a field
  savedName?: string | null; // the name it gave last (a My servers row)
  address: string;
  parsed: DirectAddress | { error: AddressError };
  signal: string; // MORE's browser port, as typed ("" for the default)
  more: boolean;
  probe: Probe | null;
  blocked: boolean; // on the blocklist (the typed host or the address it resolved to)
  gateway: boolean; // the platform's gateway is there (host:port can be joined)
  warning: boolean; // the warning is up, before the first join to this address
  understood: boolean;
  failure: string | null; // why the last JOIN did not go (the reversed cell)
  addressRef: RefObject<HTMLInputElement | null>;
  joinRef: RefObject<HTMLButtonElement | null>;
  onAddress(v: string): void;
  onSignal(v: string): void;
  onMore(): void;
  onJoin(): void;
  onUnderstood(v: boolean): void;
  onContinue(): void;
  onCancelWarning(): void;
  onSave(): void;
  onRemove(): void;
  onDone(): void; // Esc in the field: back to the board
}

function errorText(e: AddressError): string | null {
  if (e === "empty") return null;
  return e === "port" ? t("connect.direct.error.port") : t("connect.direct.error.invalid");
}

export function DirectPane(p: DirectPaneProps) {
  const ok = !("error" in p.parsed);
  const addr = ok ? (p.parsed as DirectAddress) : null;
  const answer = p.probe?.state === "done" ? p.probe.probe : null;
  const refused = p.blocked || !!answer?.blocked;
  const title = answer?.name || p.savedName || null;
  // host:port goes through the platform's gateway: without it there is no way in from a page
  const noWay = !!addr && addr.kind === "ip" && !p.gateway;
  const gamePort = addr?.kind === "ip" ? addr.port : DEFAULT_GAME_PORT;
  const signalShown = addr?.kind === "ip" ? signalPortFor(addr.port, null) : DEFAULT_GAME_PORT + 1;
  const chosen = /^\d{1,5}$/.test(p.signal) ? Number(p.signal) : null;
  const browserPort = chosen ?? answer?.signalPort ?? signalShown;
  // what the probe says, before any JOIN: one sentence, the worst first
  const why = !addr
    ? null
    : refused
      ? null
      : noWay
        ? t("connect.direct.needsPlatform")
        : answer && !answer.reachable
          ? t("connect.direct.unreachable", { port: gamePort })
          : answer && answer.browser === "closed" && chosen === null
            ? t("connect.direct.browserClosed", { port: browserPort })
            : addr.kind === "wss"
              ? t("connect.direct.ownAddress")
              : p.probe?.state === "none"
                ? t("connect.direct.noProbe")
                : null;
  const canJoin = !!addr && !refused && !noWay;
  const fieldError = !ok ? errorText((p.parsed as { error: AddressError }).error) : null;
  const signalBad = p.signal !== "" && (chosen === null || chosen < 1 || chosen > 65535);

  return (
    <div className="sb-d dc" id="direct-pane" data-probe={p.probe?.state ?? ""} data-warning={p.warning ? "1" : undefined}>
      {!p.saved && <p className="dc-head">{t("connect.direct.title")}</p>}

      {p.saved ? (
        // a My servers row: the server's name heads the pane, its address under it
        <>
          <h3 className="sb-d-h dc-saved-name" id="direct-name">
            {title ? <Colored text={title} /> : p.address}
          </h3>
          <p className="sb-d-badges dc-saved-line">
            <span className="sb-badge is-direct is-big">{t("connect.badge.direct")}</span>
            <span className="dc-address">{p.address}</span>
          </p>
        </>
      ) : (
        <>
          <label className={"dc-well" + (fieldError ? " is-invalid" : "")}>
            <span className="dc-key">{t("connect.direct.address")}</span>
            <input
              ref={p.addressRef}
              id="direct-address"
              type="text"
              inputMode="url"
              value={p.address}
              spellCheck={false}
              autoComplete="off"
              autoCapitalize="off"
              placeholder="203.0.113.7:4534"
              aria-invalid={fieldError ? true : undefined}
              aria-describedby={fieldError ? "direct-address-err" : "direct-address-hint"}
              onChange={(e) => p.onAddress(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter") {
                  e.preventDefault();
                  e.stopPropagation();
                  if (canJoin && !p.warning) p.onJoin();
                } else if (e.key === "Escape") {
                  e.preventDefault();
                  e.stopPropagation();
                  if (p.address) p.onAddress("");
                  else p.onDone();
                }
              }}
            />
          </label>
          {fieldError ? (
            <p className="cell dc-cell" id="direct-address-err">
              {fieldError}
            </p>
          ) : (
            <p className="dc-hint" id="direct-address-hint">
              {t("connect.direct.hint")}
            </p>
          )}
          <button type="button" className={"dc-more" + (p.more ? " is-open" : "")} aria-expanded={p.more} aria-controls="direct-more" onClick={p.onMore}>
            <Icon name="chevron" className="dc-more-chev" />
            {t("connect.direct.more")}
          </button>
          {p.more && (
            <div className="dc-more-body" id="direct-more">
              <label className={"dc-well is-small" + (signalBad ? " is-invalid" : "")}>
                <span className="dc-key">{t("connect.direct.signal")}</span>
                <input
                  id="direct-signal"
                  type="text"
                  inputMode="numeric"
                  value={p.signal}
                  spellCheck={false}
                  autoComplete="off"
                  placeholder={String(signalShown)}
                  aria-describedby="direct-signal-hint"
                  onChange={(e) => p.onSignal(e.target.value.replace(/\D/g, "").slice(0, 5))}
                  onKeyDown={(e) => {
                    if (e.key === "Escape") {
                      e.preventDefault();
                      e.stopPropagation();
                      p.onMore();
                    }
                  }}
                />
              </label>
              <p className="dc-hint" id="direct-signal-hint">
                {t("connect.direct.signalHint", { port: signalShown })}
              </p>
            </div>
          )}
        </>
      )}

      {addr && (
        <div className={"dc-preview" + (p.saved ? " is-saved" : "")} aria-live="polite">
          {refused ? (
            <p className="cell dc-cell" id="direct-blocked">
              {t("connect.direct.blocked")}
            </p>
          ) : p.probe?.state === "probing" ? (
            <p className="dc-name is-quiet" id="direct-probing">
              {t("connect.direct.probing")}
            </p>
          ) : answer && answer.reachable ? (
            <>
              {!p.saved && (
                <h3 className="dc-name" id="direct-name">
                  {answer.name ? <Colored text={answer.name} /> : <span className="is-quiet">{addr.kind === "ip" ? addr.key : addr.host}</span>}
                </h3>
              )}
              <dl className="dc-facts">
                <div>
                  <dt>{t("connect.detail.players")}</dt>
                  <dd>{answer.players === null ? "—" : num(answer.players) + (answer.maxPlayers !== null ? " / " + num(answer.maxPlayers) : "")}</dd>
                </div>
                <div>
                  <dt>{t("connect.direct.browsers")}</dt>
                  <dd className={answer.browser === "closed" && chosen === null ? "is-quiet" : ""}>
                    {chosen !== null ? t("connect.direct.browserPort", { port: chosen }) : answer.browser === "ok" ? t("connect.direct.browserOk") : answer.browser === "closed" ? t("connect.direct.browserNo") : t("connect.direct.browserUnknown")}
                  </dd>
                </div>
                <div className="dc-facts-wide">
                  <dt>{t("connect.detail.version")}</dt>
                  <dd className={answer.version ? "" : "is-quiet"}>{answer.version ?? t("connect.detail.notReported")}</dd>
                </div>
              </dl>
            </>
          ) : null}
          {why && <p className="dc-why" id="direct-why">{why}</p>}
        </div>
      )}

      <div className="sb-d-acts dc-acts">
        {p.failure && (
          <p className="cell dc-cell dc-fail" id="direct-failure" role="alert">
            {p.failure}
          </p>
        )}
        {p.warning ? (
          <div className="dc-warn" id="direct-warning" role="group" aria-labelledby="direct-warn-say">
            <p className="dc-warn-head">{t("connect.direct.warnHead")}</p>
            <p className="dc-warn-say" id="direct-warn-say">
              {t("connect.direct.warn")}
            </p>
            <label className={"dc-check" + (p.understood ? " is-checked" : "")} htmlFor="direct-understood">
              <input
                className="dc-check-input"
                id="direct-understood"
                type="checkbox"
                checked={p.understood}
                onChange={(e) => p.onUnderstood(e.target.checked)}
              />
              <span className="dc-check-box" aria-hidden="true">
                <svg viewBox="0 0 16 16" focusable="false">
                  <path d="M3 8.5 6.5 12 13 4.5" />
                </svg>
              </span>
              <span className="dc-check-key">{t("connect.direct.understood")}</span>
            </label>
            <div className="sb-d-row is-unwatched">
              <button type="button" className="sb-join" id="direct-continue" disabled={!p.understood} onClick={p.onContinue}>
                {t("connect.direct.continue")}
              </button>
              <button type="button" className="sb-spectate dc-cancel" id="direct-cancel" onClick={p.onCancelWarning}>
                {t("connect.direct.cancel")}
              </button>
            </div>
          </div>
        ) : (
          <div className="sb-d-row is-unwatched">
            <button ref={p.joinRef} type="button" className="sb-join" id="direct-join" disabled={!canJoin} onClick={p.onJoin}>
              {refused ? t("connect.direct.refused") : t("connect.detail.join")}
            </button>
            {p.saved ? (
              <button type="button" className="sb-bookmark" id="direct-remove" onClick={p.onRemove}>
                <Icon name="x" className="sb-bookmark-icon" />
                <span className="sb-bookmark-w">{t("connect.direct.remove")}</span>
              </button>
            ) : (
              <button type="button" className="sb-bookmark" id="direct-save" disabled={!addr || refused} aria-label={t("connect.direct.saveLabel")} onClick={p.onSave}>
                <Icon name="plus" className="sb-bookmark-icon" />
                <span className="sb-bookmark-w">{t("connect.direct.save")}</span>
              </button>
            )}
          </div>
        )}
      </div>
    </div>
  );
}
