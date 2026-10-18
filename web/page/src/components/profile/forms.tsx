// The Profile sheet's forms: sign in, sign up (save your progress), forgot password,
// a new password from an emailed link, the handle after an external sign-up, change
// password. Each keeps what is typed; Profile.tsx makes the call and hands back the
// failure (`error`, the reversed cell under the action) and the per-field texts.
// Checks the page can make itself (empty fields, the handle rule, password length)
// are made before calling; everything else is the platform's answer. A form is the
// view's head, its fields (wells), the one solid act, then the lesser ones as small lanes;
// sign in and sign up, which fit the sheet without scrolling, carry theirs as row acts
// instead: the way to the other form at the head's right, FORGOT? in the password's well.
// Creating an account (here or with a provider) is under the terms and the privacy
// policy: the sign-up says so above its action, the sign-in under its providers ("by
// continuing": a first provider sign-in creates an account); with LEGAL.requireAgeConfirmation the sign-up
// also asks for the minimum age (src/legal/config.ts) and refuses without it.
import { useState, type FormEvent } from "react";
import { LEGAL } from "../../legal/config";
import type { ProviderInfo } from "../../platform/types";
import { DocLink } from "../Docs";
import { Act, AgeConfirm, Cell, Field, Lanes, NewsConfirm, Or, Providers, RowAct, Solid, ViewHead, focusFirstInvalid } from "./bits";
import { handleProblem } from "./messages";
import { t, tx } from "../../i18n";

export type FieldErrors = Record<string, string>;

interface Common {
  busy: boolean;
  error: string | null;
  fields: FieldErrors;
  onFields(fields: FieldErrors): void;
}

function submitted(e: FormEvent, go: () => void) {
  e.preventDefault();
  go();
}

// the page's own checks before a call: the field texts, and the focus on the first one
function refuse(onFields: (fields: FieldErrors) => void, errors: FieldErrors): boolean {
  onFields(errors);
  const bad = Object.values(errors).some(Boolean);
  if (bad) focusFirstInvalid();
  return bad;
}

// the one rule worth saying up front; the platform's refusals name the others (too long,
// the handle or email in it, a known leaked one) when they apply
const passwordHint = () => t("profile.form.passwordHint");

function passwordProblem(password: string): string | null {
  if (!password) return t("profile.error.password_required");
  if (password.length < 10) return t("profile.error.password_too_short");
  if (password.length > 128) return t("profile.error.password_too_long");
  return null;
}

function Waiting({ name }: { name: string | null }) {
  if (!name) return null;
  return <p className="pf-say pf-waiting">{t("profile.form.waiting", { name })}</p>;
}

// ---- sign in --------------------------------------------------------------------

interface SignInProps extends Common {
  isGuest: boolean;
  providers: ProviderInfo[];
  prefill: { login: string; password: string } | null;
  waiting: string | null;
  onSubmit(login: string, password: string): void;
  onForgot(login: string): void;
  onSignUp(): void;
  onProvider(p: ProviderInfo): void;
}

export function SignInForm(props: SignInProps) {
  const { busy, error, fields, onFields, isGuest, providers, prefill, waiting } = props;
  const [login, setLogin] = useState(prefill?.login ?? "");
  const [password, setPassword] = useState(prefill?.password ?? "");

  const go = () => {
    const errors: FieldErrors = {};
    if (!login.trim()) errors.login = t("profile.check.enterLogin");
    if (!password) errors.password = t("profile.check.enterPassword");
    if (!refuse(onFields, errors)) props.onSubmit(login.trim(), password);
  };

  return (
    <form className="pf-form" id="form-signin" noValidate onSubmit={(e) => submitted(e, go)}>
      <ViewHead
        title={t("profile.signin.title")}
        act={
          <RowAct id="to-signup" onClick={props.onSignUp}>
            {isGuest ? t("profile.signin.toSignUpGuest") : t("profile.signin.toSignUp")}
          </RowAct>
        }
      />
      <Providers providers={providers} label={t("profile.signin.providers")} disabled={busy} onProvider={props.onProvider} />
      {providers.length > 0 && (
        <p className="pf-consent" id="signin-consent">
          {tx("profile.signin.consent", { terms: <DocLink id="terms">{t("profile.consent.terms")}</DocLink>, privacy: <DocLink id="privacy">{t("profile.consent.privacy")}</DocLink> })}
        </p>
      )}
      {providers.length > 0 && <Or />}
      <Field id="signin-login" label={t("profile.signin.login")} value={login} onChange={setLogin} autoComplete="username" error={fields.login} first={!prefill?.login} />
      <Field
        id="signin-password"
        label={t("profile.signin.password")}
        type="password"
        value={password}
        onChange={setPassword}
        autoComplete="current-password"
        error={fields.password}
        act={
          <RowAct id="to-forgot" onClick={() => props.onForgot(login.includes("@") ? login.trim() : "")}>
            <span aria-hidden="true">{t("profile.signin.forgot")}</span>
            <span className="pf-sr">{t("profile.signin.forgotFull")}</span>
          </RowAct>
        }
      />
      {/* the act stays in reach while the form scrolls (profile.css .pf-commit) */}
      <div className="pf-commit">
        <Solid id="signin-submit" submit disabled={busy}>
          {t("profile.signin.submit")}
        </Solid>
      </div>
      <Cell text={error} id="form-error" />
      <Waiting name={waiting} />
    </form>
  );
}

// ---- sign up (a guest: save your progress) ----------------------------------------

export interface SignUpValues {
  email: string;
  handle: string;
  name: string;
  password: string;
  newsEmails: boolean;
}

interface SignUpProps extends Common {
  isGuest: boolean;
  authority: string;
  name: string;
  providers: ProviderInfo[];
  waiting: string | null;
  onSubmit(values: SignUpValues): void;
  onSignIn(): void;
  onProvider(p: ProviderInfo): void;
}

export function SignUpForm(props: SignUpProps) {
  const { busy, error, fields, onFields, isGuest, providers, waiting } = props;
  const [email, setEmail] = useState("");
  const [handle, setHandle] = useState("");
  const [password, setPassword] = useState("");
  // the age confirmation, when the age policy asks for one (LEGAL.requireAgeConfirmation)
  const [oldEnough, setOldEnough] = useState(false);
  const [newsEmails, setNewsEmails] = useState(false);
  const ageProblem = LEGAL.requireAgeConfirmation && !oldEnough ? t("profile.age.problem", { age: LEGAL.minimumAge }) : null;
  // the handle rule is checked while typing; the rest once a field is left
  const live = handleProblem(handle);

  // a provider creates the account too: the same age check first
  const provider = (p: ProviderInfo) => {
    if (ageProblem) {
      refuse(onFields, { ...fields, age: ageProblem });
      return;
    }
    props.onProvider(p);
  };

  const go = () => {
    const errors: FieldErrors = {};
    if (!/^[^@\s]+@[^@\s]+$/.test(email.trim())) errors.email = email.trim() ? t("profile.error.email_invalid") : t("profile.check.enterEmail");
    const h = handle ? handleProblem(handle) : t("profile.error.handle_required");
    if (h) errors.handle = h;
    const p = passwordProblem(password);
    if (p) errors.password = p;
    if (ageProblem) errors.age = ageProblem;
    // the name is the plate's, edited there (the sheet's one name field)
    if (!refuse(onFields, errors)) props.onSubmit({ email: email.trim(), handle, name: props.name.trim(), password, newsEmails });
  };

  return (
    <form className="pf-form" id="form-signup" noValidate onSubmit={(e) => submitted(e, go)}>
      <ViewHead
        title={isGuest ? t("profile.signup.titleGuest") : t("profile.signup.title")}
        act={
          <RowAct id="to-signin" onClick={props.onSignIn}>
            {t("profile.signup.toSignIn")}
          </RowAct>
        }
      />
      <Providers providers={providers} label={t("profile.signup.providers")} disabled={busy} onProvider={provider} />
      {providers.length > 0 && <Or />}
      <Field id="signup-email" label={t("profile.signup.email")} type="email" value={email} onChange={setEmail} autoComplete="email" error={fields.email} first />
      <Field
        id="signup-handle"
        label={t("profile.signup.handle")}
        value={handle}
        onChange={(v) => {
          setHandle(v.toLowerCase());
          if (fields.handle) onFields({ ...fields, handle: "" });
        }}
        autoComplete="username"
        maxLength={24}
        error={fields.handle || live}
      />
      <Field
        id="signup-password"
        label={t("profile.signup.password")}
        type="password"
        value={password}
        onChange={(v) => {
          setPassword(v);
          if (fields.password) onFields({ ...fields, password: "" });
        }}
        onBlur={() => {
          if (password && password.length < 10) onFields({ ...fields, password: t("profile.error.password_too_short") });
        }}
        autoComplete="new-password"
        maxLength={256}
        error={fields.password}
        hint={passwordHint()}
      />
      {LEGAL.requireAgeConfirmation && (
        <AgeConfirm
          id="signup-age"
          age={LEGAL.minimumAge}
          checked={oldEnough}
          error={fields.age}
          onChange={(v) => {
            setOldEnough(v);
            if (v && fields.age) onFields({ ...fields, age: "" });
          }}
        />
      )}
      <NewsConfirm id="signup-news" checked={newsEmails} onChange={setNewsEmails} />
      {/* the terms stay in sight with the act they are agreed by: both stick to the foot
          while the form scrolls (profile.css .pf-commit) */}
      <div className="pf-commit">
        <p className="pf-consent" id="signup-consent">
          {tx("profile.signup.consent", { terms: <DocLink id="terms">{t("profile.consent.termsOfUse")}</DocLink>, privacy: <DocLink id="privacy">{t("profile.consent.privacy")}</DocLink> })}
        </p>
        <Solid id="signup-submit" submit disabled={busy}>
          {t("profile.signup.submit")}
        </Solid>
      </div>
      <Cell text={error} id="form-error" />
      {/* the name has no field here (the plate's is the one): the platform's word on it */}
      <Cell text={fields.name ? t("profile.signup.nameProblem", { problem: fields.name }) : null} />
      <Waiting name={waiting} />
    </form>
  );
}

// ---- forgot password -------------------------------------------------------------

interface ForgotProps extends Common {
  email: string;
  sent: boolean;
  onSubmit(email: string): void;
  onBack(): void;
}

export function ForgotForm(props: ForgotProps) {
  const { busy, error, fields, onFields, sent } = props;
  const [email, setEmail] = useState(props.email);
  const go = () => {
    const bad = !/^[^@\s]+@[^@\s]+$/.test(email.trim());
    if (!refuse(onFields, bad ? { email: email.trim() ? t("profile.error.email_invalid") : t("profile.check.enterEmail") } : {})) props.onSubmit(email.trim());
  };
  return (
    <form className="pf-form" id="form-forgot" noValidate onSubmit={(e) => submitted(e, go)}>
      <ViewHead title={t("profile.forgot.title")} say={t("profile.forgot.say")} />
      <Field id="forgot-email" label={t("profile.forgot.email")} type="email" value={email} onChange={setEmail} autoComplete="email" error={fields.email} first />
      <Solid id="forgot-submit" submit disabled={busy}>
        {sent ? t("profile.forgot.sendAgain") : t("profile.forgot.send")}
      </Solid>
      <Cell text={error} id="form-error" />
      <Lanes>
        <Act id="forgot-back" small onClick={props.onBack}>
          {t("profile.forgot.back")}
        </Act>
      </Lanes>
    </form>
  );
}

// ---- a new password (the emailed reset link) ----------------------------------------

interface ResetProps extends Common {
  expired: boolean;
  onSubmit(password: string): void;
  onNewLink(): void;
}

export function ResetForm(props: ResetProps) {
  const { busy, error, fields, onFields, expired } = props;
  const [password, setPassword] = useState("");
  const go = () => {
    const p = passwordProblem(password);
    if (!refuse(onFields, p ? { password: p } : {})) props.onSubmit(password);
  };
  return (
    <form className="pf-form" id="form-reset" noValidate onSubmit={(e) => submitted(e, go)}>
      <ViewHead title={t("profile.reset.title")} say={t("profile.reset.say")} />
      <Field
        id="reset-password"
        label={t("profile.reset.password")}
        type="password"
        value={password}
        onChange={setPassword}
        autoComplete="new-password"
        maxLength={256}
        error={fields.password}
        hint={passwordHint()}
        first
      />
      <Solid id="reset-submit" submit disabled={busy || expired}>
        {t("profile.reset.submit")}
      </Solid>
      <Cell text={error} id="form-error" />
      {expired && (
        <Lanes>
          <Act id="reset-new-link" small onClick={props.onNewLink}>
            {t("profile.reset.newLink")}
          </Act>
        </Lanes>
      )}
    </form>
  );
}

// ---- the handle, once, after an external sign-up -------------------------------------

interface HandleProps extends Common {
  suggested: string;
  authority: string;
  onSubmit(handle: string): void;
}

export function HandleForm(props: HandleProps) {
  const { busy, error, fields, onFields } = props;
  const [handle, setHandle] = useState(props.suggested);
  const live = handleProblem(handle);
  const go = () => {
    const h = handle ? handleProblem(handle) : t("profile.error.handle_required");
    if (!refuse(onFields, h ? { handle: h } : {})) props.onSubmit(handle);
  };
  return (
    <form className="pf-form" id="form-handle" noValidate onSubmit={(e) => submitted(e, go)}>
      <ViewHead title={t("profile.handle.title")} say={t("profile.handle.say")} />
      <Field
        id="handle-handle"
        label={t("profile.handle.label")}
        value={handle}
        onChange={(v) => {
          setHandle(v.toLowerCase());
          if (fields.handle) onFields({});
        }}
        autoComplete="username"
        maxLength={24}
        error={fields.handle || live}
        hint={t("profile.handle.hint")}
        first
      />
      <Solid id="handle-submit" submit disabled={busy}>
        {t("profile.handle.submit")}
      </Solid>
      <Cell text={error} id="form-error" />
    </form>
  );
}

// ---- change (or add) the password ----------------------------------------------------

interface PasswordProps extends Common {
  hasPassword: boolean;
  onSubmit(current: string | null, next: string): void;
  onBack(): void;
}

export function PasswordForm(props: PasswordProps) {
  const { busy, error, fields, onFields, hasPassword } = props;
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const go = () => {
    const errors: FieldErrors = {};
    if (hasPassword && !current) errors.currentPassword = t("profile.check.enterCurrent");
    const p = passwordProblem(next);
    if (p) errors.newPassword = p;
    if (!refuse(onFields, errors)) props.onSubmit(hasPassword ? current : null, next);
  };
  return (
    <form className="pf-form" id="form-password" noValidate onSubmit={(e) => submitted(e, go)}>
      <ViewHead
        title={hasPassword ? t("profile.password.titleChange") : t("profile.password.titleAdd")}
        say={hasPassword ? t("profile.password.sayChange") : t("profile.password.sayAdd")}
      />
      {hasPassword && (
        <Field
          id="password-current"
          label={t("profile.password.current")}
          type="password"
          value={current}
          onChange={setCurrent}
          autoComplete="current-password"
          error={fields.currentPassword}
          first
        />
      )}
      <Field
        id="password-new"
        label={t("profile.password.new")}
        type="password"
        value={next}
        onChange={setNext}
        autoComplete="new-password"
        maxLength={256}
        error={fields.newPassword}
        hint={passwordHint()}
        first={!hasPassword}
      />
      <Solid id="password-submit" submit disabled={busy}>
        {hasPassword ? t("profile.password.submitChange") : t("profile.password.submitAdd")}
      </Solid>
      <Cell text={error} id="form-error" />
      <Lanes>
        <Act id="password-back" small onClick={props.onBack}>
          {t("profile.password.back")}
        </Act>
      </Lanes>
    </form>
  );
}
