// The Profile sheet's other views: home (a guest, nobody, or an account), the account's
// details, the sessions list, Linked IDs, the "Link
// <provider> to <handle>?" question and the result of an emailed
// confirmation link. Home leads with the player: the level, its bar and four numbers in cells, then
// for a guest the one solid act, SAVE YOUR PROGRESS, then the lanes. The details are rows:
// key in quiet caps, the value, the row's own action at its right end.
import { useEffect, useState, type ReactNode } from "react";
import { playedShort } from "../../platform/progression";
import type { AccountInfo, GamePasswordInfo, LinkCode, LinkedIdInfo, NewGamePassword, PendingLink, PlatformUser, Progression, ProviderInfo, SessionInfo } from "../../platform/types";
import { XpBar } from "../Rewards";
import { Act, Cell, Field, Lanes, RowAct, Solid, ViewHead } from "./bits";
import { ago } from "./messages";
import { t, tx } from "../../i18n";
import { DISCORD, fullPageLink } from "../../links";
import { DocLink } from "../Docs";
import { openOutside } from "../../discord";

// Inside a Discord Activity the profile keeps what stays on the page and drops
// what leaves it or ends it: linking or unlinking a sign-in service (a popup Discord's frame cannot
// open), signing out and the sessions list with its sign-outs (the Activity's session is Discord's own). One lane opens the site, through Discord's
// own confirmation, where those are.
function ManageOutside({ first = false }: { first?: boolean }) {
  return (
    <Act id="act-manage-outside" small first={first} onClick={() => openOutside(fullPageLink("embed-profile"))}>
      {t("profile.discord.manageOutside")}
    </Act>
  );
}

// ---- progress: level, XP and stats (platform/progression.ts) -------------------------

export function Progress({ progression }: { progression: Progression }) {
  const s = progression.stats;
  const played = playedShort(s.seconds);
  const stats: Array<[string, string, string]> = [
    ["stat-matches", t("profile.stat.matches"), String(s.matches)],
    ["stat-rounds", t("profile.stat.rounds"), String(s.rounds)],
    ["stat-kills", t("profile.stat.kills"), String(s.kills)],
    ["stat-played", t("profile.stat.played"), played]
  ];
  return (
    <section className="pf-progress" id="progress-facts" aria-label={t("profile.progress.aria")} data-level={progression.level}>
      <div className="pf-level">
        <span className="pf-level-key">{t("profile.progress.level")}</span>
        <span className="pf-level-n" id="progress-level">
          {progression.level}
        </span>
      </div>
      <XpBar progression={progression} id="profile-bar" />
      <p className="pf-xp">
        {t("profile.progress.xp", { have: progression.xp - progression.levelXp, need: progression.nextLevelXp - progression.levelXp })}
      </p>
      <dl className="pf-stats">
        {stats.map(([id, key, value]) => (
          <div className="pf-stat" id={id} key={id}>
            <dt className="pf-stat-key">{key}</dt>
            <dd className={"pf-stat-n" + (value.length > 4 ? " is-long" : "")}>{value}</dd>
          </div>
        ))}
      </dl>
    </section>
  );
}

// ---- home: a guest or nobody --------------------------------------------------------

interface GuestHomeProps {
  progression: Progression | null;
  user: PlatformUser | null;
  busy: boolean;
  error: string | null;
  linked: boolean; // Linked IDs is shown (features.linking)
  onSignUp(): void;
  onSignIn(): void;
  onNewPlayer(): void;
  onLinked(): void;
}

export function GuestHome({ progression, user, busy, error, linked, onSignUp, onSignIn, onNewPlayer, onLinked }: GuestHomeProps) {
  if (!user)
    return (
      <div className="pf-view pf-home" id="home-nobody">
        <Lanes>
          <Act id="pick-signin" first disabled={busy} onClick={onSignIn}>
            {t("profile.home.signIn")}
          </Act>
          <Act id="pick-signup" disabled={busy} onClick={onSignUp}>
            {t("profile.home.signUp")}
          </Act>
          <Act id="pick-new" disabled={busy} onClick={onNewPlayer}>
            {t("profile.home.newPlayer")}
          </Act>
        </Lanes>
        <Cell text={error} id="form-error" />
      </div>
    );
  const level = progression && progression.level > 1 ? progression.level : null;
  return (
    <div className="pf-view pf-home" id="home-guest">
      {progression && <Progress progression={progression} />}
      <div className="pf-save">
        <Solid id="pick-signup" first disabled={busy} describedBy="pick-signup-say" onClick={onSignUp}>
          {t("profile.home.save")}
        </Solid>
        <p className="pf-save-say" id="pick-signup-say">
          {level !== null ? t("profile.home.levelLocal") : t("profile.home.guestLocal")}
        </p>
      </div>
      <Cell text={error} id="form-error" />
      <Lanes>
        <Act id="pick-signin" disabled={busy} onClick={onSignIn}>
          {t("profile.home.signIn")}
        </Act>
        {linked && (
          <Act id="pick-linked" disabled={busy} onClick={onLinked}>
            {t("profile.home.linked")}
          </Act>
        )}
        <Act id="pick-new" disabled={busy} onClick={onNewPlayer}>
          {t("profile.home.newPlayer")}
        </Act>
      </Lanes>
    </div>
  );
}

// ---- home: an account ----------------------------------------------------------------

interface AccountHomeProps {
  progression: Progression | null;
  account: AccountInfo | null;
  busy: boolean;
  error: string | null;
  linked: boolean;
  gamePasswords: boolean; // the Game passwords lane (features.gamePasswords)
  onAccount(): void;
  onLinked(): void;
  onGamePasswords(): void;
  onSessions(): void;
  onSignOut(): void;
  onNewsAsk(on: boolean): void;
}

// a lane's quiet note at its right end (EMAIL NOT CONFIRMED)
function Note({ children }: { children: ReactNode }) {
  return <span className="pf-lane-note">{children}</span>;
}

// news emails, asked once (the platform's newsEmailsAsk: an account that never saw the sign-up
// form's box): the question and its two answers side by side, the same size and weight, since
// a consent is only one if no thanks is as easy as yes. Not inside the Discord Activity, whose
// developer policy keeps us from courting its players' email
function NewsAsk({ busy, onAnswer }: { busy: boolean; onAnswer(on: boolean): void }) {
  return (
    <div className="pf-ask" role="group" aria-labelledby="news-ask-say">
      <p className="pf-ask-say" id="news-ask-say">
        {tx("profile.newsAsk.say", { link: <DocLink id="privacy">{t("profile.consent.privacy")}</DocLink> })}
      </p>
      <div className="pf-ask-acts">
        <button id="news-ask-yes" className="pf-provider" type="button" disabled={busy} onClick={() => onAnswer(true)}>
          {t("profile.newsAsk.yes")}
        </button>
        <button id="news-ask-no" className="pf-provider" type="button" disabled={busy} onClick={() => onAnswer(false)}>
          {t("profile.newsAsk.no")}
        </button>
      </div>
    </div>
  );
}

// the account's places (account, linked ids, game passwords, sessions) as lanes, then what
// changes who is playing (sign out) as a small one, so a home of five
// fits the sheet like the guest's of three (profile.css .pf-home-account)
export function AccountHome(props: AccountHomeProps) {
  const { account, busy, error } = props;
  const todo = account?.needsHandle ? t("profile.home.todoHandle") : account?.email && !account.emailVerified ? t("profile.home.todoEmail") : null;
  return (
    <div className="pf-view pf-home pf-home-account" id="home-account">
      {props.progression && <Progress progression={props.progression} />}
      {account?.newsEmailsAsk && !DISCORD && <NewsAsk busy={busy} onAnswer={props.onNewsAsk} />}
      <Cell text={error} id="form-error" />
      <Lanes>
        <Act id="pick-account" first disabled={busy} onClick={props.onAccount}>
          {t("profile.home.account")}
          {todo && <Note>{todo}</Note>}
        </Act>
        {props.linked && (
          <Act id="pick-linked" disabled={busy} onClick={props.onLinked}>
            {t("profile.home.linked")}
          </Act>
        )}
        {props.gamePasswords && (
          <Act id="pick-game-passwords" disabled={busy} onClick={props.onGamePasswords}>
            {t("profile.home.gamePasswords")}
          </Act>
        )}
        {!DISCORD && (
          <Act id="act-sessions" disabled={busy} onClick={props.onSessions}>
            {t("profile.home.sessions")}
          </Act>
        )}
        {DISCORD ? (
          <ManageOutside />
        ) : (
          <Act id="pick-signout" small disabled={busy} onClick={props.onSignOut}>
            {t("profile.home.signOut")}
          </Act>
        )}
      </Lanes>
    </div>
  );
}

// ---- the account's details ----------------------------------------------------------

interface AccountViewProps {
  user: PlatformUser;
  account: AccountInfo | null;
  providers: ProviderInfo[];
  busy: boolean;
  error: string | null;
  onHandle(): void;
  onResend(): void;
  onPassword(): void;
  onNews(on: boolean): void;
  onLink(p: ProviderInfo): void;
  onUnlink(p: ProviderInfo): void;
  onBack(): void;
}

// key | value (+ its note) | the row's action; a row whose action has the focus takes the lane's mark
export function Row({ id, keyText, value, note, children }: { id: string; keyText: string; value: string | null; note?: ReactNode; children?: ReactNode }) {
  return (
    <div className="pf-row" id={id}>
      <dt className="pf-row-key">{keyText}</dt>
      <dd className="pf-row-val">
        <span className={value === null ? "pf-none" : "pf-row-text"}>{value === null ? "—" : value}</span>
        {note && <span className="pf-row-note">{note}</span>}
      </dd>
      <dd className="pf-row-end">{children}</dd>
    </div>
  );
}

export function AccountView(props: AccountViewProps) {
  const { user, account, providers, busy, error } = props;
  // the providers the platform offers, plus any linked one it no longer offers
  const known = new Map<string, ProviderInfo>(providers.map((p) => [p.id, p]));
  for (const l of account?.logins ?? []) if (!known.has(l.provider)) known.set(l.provider, { id: l.provider, name: l.name });
  const linked = new Set((account?.logins ?? []).map((l) => l.provider));
  let first = true;
  const isFirst = () => {
    const was = first;
    first = false;
    return was;
  };

  return (
    <div className="pf-view" id="account">
      <ViewHead title={t("profile.account.title")} say={account ? t("profile.account.created", { ago: ago(account.createdAt) }) : t("profile.asking")} />
      <dl className="pf-rows">
        <Row id="fact-handle" keyText={t("profile.account.id")} value={user.gid} note={account?.needsHandle ? t("profile.account.suggested") : undefined}>
          {account?.needsHandle && (
            <RowAct id="act-handle" first={isFirst()} disabled={busy} onClick={props.onHandle}>
              {t("profile.account.choose")}
            </RowAct>
          )}
        </Row>
        <Row
          id="fact-email"
          keyText={t("profile.account.email")}
          value={account ? account.email : null}
          note={account?.email ? (account.emailVerified ? t("profile.account.confirmed") : t("profile.account.notConfirmed")) : undefined}
        >
          {account?.email && !account.emailVerified && (
            <RowAct id="act-resend" first={isFirst()} disabled={busy} onClick={props.onResend}>
              {t("profile.account.sendAgain")}
            </RowAct>
          )}
        </Row>
        <Row id="fact-password" keyText={t("profile.account.password")} value={account ? (account.hasPassword ? t("profile.account.passwordSet") : null) : null}>
          {account && (
            <RowAct id="act-password" first={isFirst()} disabled={busy} onClick={props.onPassword}>
              {account.hasPassword ? t("profile.account.change") : t("profile.account.add")}
            </RowAct>
          )}
        </Row>
        {/* news emails: opt-in, said what they are; only with an email to send them to */}
        <Row
          id="fact-news"
          keyText={t("profile.account.news")}
          value={account ? (account.newsEmails ? t("profile.account.newsOn") : t("profile.account.newsOff")) : null}
          note={account ? (account.email ? t("profile.account.newsNote") : t("profile.account.newsNeedsEmail")) : undefined}
        >
          {account?.email && (
            <RowAct id="act-news" first={isFirst()} disabled={busy} onClick={() => props.onNews(!account.newsEmails)}>
              {account.newsEmails ? t("profile.account.turnOff") : t("profile.account.turnOn")}
            </RowAct>
          )}
        </Row>
        {[...known.values()].map((p) => (
          <Row key={p.id} id={"fact-login-" + p.id} keyText={p.name} value={linked.has(p.id) ? t("profile.account.linked") : null}>
            {account &&
              !DISCORD &&
              (linked.has(p.id) ? (
                <RowAct id={"act-unlink-" + p.id} first={isFirst()} disabled={busy} onClick={() => props.onUnlink(p)}>
                  {t("profile.account.unlink")}
                </RowAct>
              ) : (
                providers.some((o) => o.id === p.id) && (
                  <RowAct id={"act-link-" + p.id} first={isFirst()} disabled={busy} onClick={() => props.onLink(p)}>
                    {t("profile.account.link")}
                  </RowAct>
                )
              ))}
          </Row>
        ))}
      </dl>
      <Cell text={error} id="form-error" />
      <Lanes>
        {DISCORD && <ManageOutside first={first} />}
        <Act id="account-back" small first={first && !DISCORD} onClick={props.onBack}>
          {t("profile.account.back")}
        </Act>
      </Lanes>
    </div>
  );
}

// ---- sessions ------------------------------------------------------------------------

interface SessionsProps {
  sessions: SessionInfo[] | null;
  busy: boolean;
  error: string | null;
  onRevoke(s: SessionInfo): void;
  onEverywhere(): void;
  onBack(): void;
}

export function Sessions({ sessions, busy, error, onRevoke, onEverywhere, onBack }: SessionsProps) {
  return (
    <div className="pf-view" id="sessions">
      <ViewHead title={t("profile.sessions.title")} say={t("profile.sessions.say")} />
      {sessions === null ? (
        <p className="pf-say">{t("profile.asking")}</p>
      ) : (
        <ul className="pf-sessions" id="session-list">
          {sessions.map((s, i) => (
            <li className={"pf-session session" + (s.current ? " is-current" : "")} key={s.id} data-session={s.id} data-current={s.current ? "1" : "0"}>
              <span className="pf-session-device">{s.device || t("profile.sessions.unknownDevice")}</span>
              <span className="pf-session-say">
                {t(
                  s.current
                    ? s.sharedComputer
                      ? "profile.sessions.thisLastUsedShared"
                      : "profile.sessions.thisLastUsed"
                    : s.sharedComputer
                      ? "profile.sessions.lastUsedShared"
                      : "profile.sessions.lastUsed",
                  { ago: ago(s.lastSeenAt) }
                )}
              </span>
              <span className="pf-row-end">
                <RowAct id={"revoke-" + s.id} first={i === 0} disabled={busy} onClick={() => onRevoke(s)}>
                  {s.current ? t("profile.sessions.signOutHere") : t("profile.sessions.signOut")}
                </RowAct>
              </span>
            </li>
          ))}
        </ul>
      )}
      <Cell text={error} id="form-error" />
      <Lanes>
        <Act id="logout-all" disabled={busy || !sessions} onClick={onEverywhere}>
          {t("profile.sessions.everywhere")}
        </Act>
        <Act id="sessions-back" small onClick={onBack}>
          {t("profile.sessions.back")}
        </Act>
      </Lanes>
    </div>
  );
}

// ---- Linked IDs ----------------------------------

interface LinkedProps {
  account: boolean; // old IDs link to an account, never to a guest
  ids: LinkedIdInfo[] | null; // null: still asking
  code: LinkCode | null; // the code /link takes in game, once asked for
  // features.linkByPassword: the ID and its password on this screen, the code
  // for the desktop game below it
  byPassword: boolean;
  busy: boolean;
  error: string | null;
  onLink(gid: string, password: string): void;
  onCode(): void;
  onUnlink(gid: string): void;
  onSave(): void;
  onBack(): void;
}

export function LinkedIds({ account, ids, code, byPassword, busy, error, onLink, onCode, onUnlink, onSave, onBack }: LinkedProps) {
  const [gid, setGid] = useState("");
  const [password, setPassword] = useState("");
  const [gidError, setGidError] = useState<string | null>(null);
  // linked: the field is done with
  useEffect(() => {
    const typed = gid.trim().toLowerCase();
    if (typed && ids?.some((l) => l.gid.toLowerCase() === typed)) setGid("");
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ids]);
  // the host the answer goes to when it is not one of armagetronad.net's names, until confirmed
  const [unknownHost, setUnknownHost] = useState<string | null>(null);
  const [trustedHost, setTrustedHost] = useState<string | null>(null);
  const link = (confirmed = false) => {
    const typed = gid.trim();
    const at = typed.lastIndexOf("@");
    if (at <= 0 || at === typed.length - 1) return setGidError(t("profile.linked.badForm"));
    setGidError(null);
    // whoever runs the authority gets a salted md5 of the password and can try to guess it
    // (a reused password above all): a host outside armagetronad.net is named and confirmed first
    const host = foreignAuthority(typed.slice(at + 1));
    if (host && !confirmed && host !== trustedHost) return setUnknownHost(host);
    if (host) setTrustedHost(host);
    setUnknownHost(null);
    // the password is kept no longer than the answer takes
    setPassword("");
    onLink(typed, password);
  };
  const codeBlock = code && (
    <div className="pf-code-block" id="link-code" data-expires={code.expiresAt}>
      <p className="pf-key">{t("profile.linked.codeKey")}</p>
      <p className="pf-code">{code.code}</p>
      <p className="pf-say">{tx("profile.linked.codeSay", { n: code.minutes, command: <span className="pf-command">/link {code.code}</span> })}</p>
    </div>
  );
  return (
    <div className="pf-view" id="linked">
      <ViewHead title={t("profile.linked.title")} say={t("profile.linked.say")} />
      {!account ? (
        <>
          <p className="pf-say">{t("profile.linked.needAccount")}</p>
          <Solid id="linked-save" first onClick={onSave}>
            {t("profile.linked.save")}
          </Solid>
        </>
      ) : (
        <>
          {ids === null ? (
            <p className="pf-say">{t("profile.asking")}</p>
          ) : (
            <dl className="pf-rows" id="linked-list">
              {ids.map((l) => (
                <Row key={l.gid} id={"linked-" + l.gid} keyText={t("profile.linked.key")} value={l.gid} note={t("profile.linked.since", { ago: ago(l.linkedAt) })}>
                  <RowAct id={"unlink-" + l.gid} disabled={busy} onClick={() => onUnlink(l.gid)}>
                    {t("profile.linked.unlink")}
                  </RowAct>
                </Row>
              ))}
              {!ids.length && <Row id="linked-none" keyText={t("profile.linked.key")} value={null} />}
            </dl>
          )}
          {byPassword ? (
            <>
              {/* not a <form>, and the fields opt out of password managers (bits.tsx
                  NO_PASSWORD_MANAGERS): this is another site's login */}
              <div className="pf-form" id="form-link">
                <Field
                  id="link-gid"
                  label={t("profile.linked.oldId")}
                  value={gid}
                  onChange={(v) => {
                    setGid(v);
                    setUnknownHost(null);
                  }}
                  onEnter={() => link()}
                  foreign
                  maxLength={128}
                  error={gidError ?? undefined}
                  first
                />
                <Field
                  id="link-password"
                  label={t("profile.linked.password")}
                  type="password"
                  value={password}
                  onChange={setPassword}
                  onEnter={() => link()}
                  foreign
                  maxLength={256}
                  hint={t("profile.linked.passwordHint")}
                />
                {unknownHost ? (
                  <div className="pf-confirm" id="link-host" role="alert">
                    <p className="pf-say">{tx("profile.linked.unknownHost", { host: <span className="pf-command">{unknownHost}</span> })}</p>
                    <Solid id="link-host-send" disabled={busy || !password} onClick={() => link(true)}>
                      {t("profile.linked.sendTo", { host: unknownHost })}
                    </Solid>
                  </div>
                ) : (
                  <Solid id="link-submit" disabled={busy || !gid.trim() || !password} onClick={() => link()}>
                    {t("profile.linked.submit")}
                  </Solid>
                )}
              </div>
              {codeBlock}
            </>
          ) : code ? (
            codeBlock
          ) : (
            <Solid id="linked-start" first disabled={busy} onClick={onCode}>
              {t("profile.linked.start")}
            </Solid>
          )}
        </>
      )}
      <Cell text={error} id="form-error" />
      <Lanes>
        {account && byPassword && !code && (
          <Act id="linked-start" small disabled={busy} onClick={onCode}>
            {t("profile.linked.desktopCode")}
          </Act>
        )}
        <Act id="linked-back" small first={account && !!code && !byPassword} onClick={onBack}>
          {t("profile.linked.back")}
        </Act>
      </Lanes>
    </div>
  );
}

// The host of an authority that is not a name under authentication.armagetronad.net (what a game
// server reads as `@forums`), else null: those are upstream's names for the community's authorities.
export function foreignAuthority(authority: string): string | null {
  const host = authority.split("/")[0].split(":")[0].toLowerCase();
  if (!host.includes(".") || host.endsWith(".authentication.armagetronad.net")) return null;
  return authority.split("/")[0].toLowerCase();
}

// ---- game passwords ------------------------------------------------------

interface GamePasswordsProps {
  login: string; // "nova@retrocycles"
  passwords: GamePasswordInfo[] | null; // null: still asking
  shown: NewGamePassword | null; // just made, or SHOW: its words on screen until the view is left
  busy: boolean;
  error: string | null;
  labelError: string | null;
  onCreate(label: string): void;
  onShow(p: GamePasswordInfo): void;
  onRevoke(p: GamePasswordInfo): void;
  onBack(): void;
}

export function GamePasswords({ login, passwords, shown, busy, error, labelError, onCreate, onShow, onRevoke, onBack }: GamePasswordsProps) {
  const [label, setLabel] = useState("");
  return (
    <div className="pf-view" id="game-passwords">
      <ViewHead
        title={t("profile.gamePasswords.title")}
        say={t("profile.gamePasswords.say", { login })}
      />
      {passwords === null ? (
        <p className="pf-say">{t("profile.asking")}</p>
      ) : (
        <dl className="pf-rows" id="game-password-list">
          {passwords.map((p) => (
            <Row key={p.id} id={"game-password-" + p.id} keyText={p.label} value={p.lastUsedAt ? t("profile.gamePasswords.used", { ago: ago(p.lastUsedAt) }) : t("profile.gamePasswords.neverUsed")} note={t("profile.gamePasswords.made", { ago: ago(p.createdAt) })}>
              {shown?.id !== p.id && (
                <RowAct id={"show-" + p.id} disabled={busy} onClick={() => onShow(p)}>
                  {t("profile.gamePasswords.show")}
                </RowAct>
              )}
              <RowAct id={"revoke-" + p.id} disabled={busy} onClick={() => onRevoke(p)}>
                {t("profile.gamePasswords.revoke")}
              </RowAct>
            </Row>
          ))}
          {!passwords.length && <Row id="game-password-none" keyText={t("profile.gamePasswords.none")} value={null} />}
        </dl>
      )}
      {shown ? (
        <div className="pf-code-block" id="game-password-new" data-id={shown.id}>
          <p className="pf-key">{shown.label}</p>
          <p className="pf-code is-long">{shown.password}</p>
          <p className="pf-say">
            {tx("profile.gamePasswords.howTo", { login: <span className="pf-command">{shown.login}</span>, command: <span className="pf-command">/login {shown.login}</span> })}
          </p>
        </div>
      ) : (
        <form
          className="pf-form"
          id="form-game-password"
          noValidate
          onSubmit={(e) => {
            e.preventDefault();
            onCreate(label.trim());
          }}
        >
          <Field id="game-password-label" label={t("profile.gamePasswords.for")} value={label} onChange={setLabel} maxLength={40} error={labelError} hint={t("profile.gamePasswords.forHint")} first />
          <Solid id="game-password-create" submit disabled={busy}>
            {t("profile.gamePasswords.create")}
          </Solid>
        </form>
      )}
      <Cell text={error} id="form-error" />
      <Lanes>
        <Act id="game-passwords-back" small first={!!shown} onClick={onBack}>
          {t("profile.gamePasswords.back")}
        </Act>
      </Lanes>
    </div>
  );
}

// ---- Link <provider> to <handle>? ----------------------------------------------------

interface ConfirmProps {
  pending: PendingLink;
  busy: boolean;
  error: string | null;
  onConfirm(): void;
  onCancel(): void;
}

export function ConfirmLink({ pending, busy, error, onConfirm, onCancel }: ConfirmProps) {
  return (
    <div className="pf-view" id="confirm">
      <ViewHead
        title={tx("profile.confirm.title", { provider: pending.providerName, handle: <span className="as-written">{pending.handle}</span> })}
        say={t("profile.confirm.say", { provider: pending.providerName, email: pending.email, handle: pending.handle })}
      />
      <Solid id="confirm-link" first disabled={busy} onClick={onConfirm}>
        {t("profile.confirm.submit")}
      </Solid>
      <Cell text={error} id="form-error" />
      <Lanes>
        <Act id="confirm-cancel" small onClick={onCancel}>
          {t("profile.confirm.cancel")}
        </Act>
      </Lanes>
    </div>
  );
}

// ---- the emailed confirmation link ----------------------------------------------------

export function Verified({ state, error, onDone }: { state: "pending" | "ok" | "failed"; error: string | null; onDone(): void }) {
  return (
    <div className="pf-view" id="verify" data-state={state}>
      {state === "pending" && <ViewHead title={t("profile.verify.pendingTitle")} say={t("profile.verify.pendingSay")} />}
      {state === "ok" && <ViewHead title={t("profile.verify.okTitle")} say={t("profile.verify.okSay")} />}
      {state === "failed" && <ViewHead title={t("profile.verify.failedTitle")} say={t("profile.verify.failedSay")} />}
      <Cell text={state === "failed" ? error : null} id="form-error" />
      {state !== "pending" && (
        <Solid id="verify-done" first onClick={onDone}>
          {t("profile.verify.done")}
        </Solid>
      )}
    </div>
  );
}
