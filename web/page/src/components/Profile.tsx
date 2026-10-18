// The Profile: who is playing and
// their account, opened from the chip: a full-height square sheet on the right edge hung
// from the chip, the in-game menu's grammar mirrored. Its plate is the player: the swatch,
// the name as an input (PLAYER_1: the name the game shows) and a sentence saying who that
// is to the platform (a guest, an account, nobody). Under it one view at a time: home (the
// level, its bar and four numbers, then a guest's SAVE YOUR PROGRESS, or an account's
// lanes), sign in, save your progress (sign up), forgot password, a new password (emailed
// link), the handle after an external sign-up, the account's details, change password,
// sessions, Linked IDs (features.linking), game passwords (features.gamePasswords), "Link
// <provider> to <handle>?", and the result of an
// emailed confirmation link. Whatever has the focus fills near-black and shows a short ink
// bar at its left edge (profile.css); the cyan trail is the home menu's alone. Esc steps
// back to home, then closes (App.tsx calls back()). Only platform/ talks to the platform; this component calls it and keeps the store
// (platform/session.ts) up to date, so the chip, the page's data-platform-user and the
// next connect's login token follow at once.
import { forwardRef, useCallback, useEffect, useImperativeHandle, useRef, useState, type ReactNode } from "react";
import { DISCORD } from "../links";
import { claim } from "../platform/analytics";
import * as api from "../platform/client";
import { onExternalResult, openExternalPopup } from "../platform/external";
import { answerCheck } from "../platform/linking";
import { progressionOn, refreshProgression, useProgression } from "../platform/progression";
import { refreshPlatformUser, setPlatformUser } from "../platform/session";
import type { AccountInfo, ExternalResult, GamePasswordInfo, LinkCode, LinkedIdInfo, NewGamePassword, PendingLink, PlatformState, PlatformUser, ProviderInfo, SessionInfo } from "../platform/types";
import { NAME_MAX, randomName } from "../player";
import { SHARED_COMPUTER_KEY } from "./Settings";
import { ColorInput } from "./ColorInput";
import { SheetClose } from "./SheetClose";
import { PlateSwatch, type PlateColour } from "./profile/colour";
import { Notice, focusFirstInvalid } from "./profile/bits";
import { ForgotForm, HandleForm, PasswordForm, ResetForm, SignInForm, SignUpForm, type FieldErrors, type SignUpValues } from "./profile/forms";
import { externalText, fieldTexts, problemText } from "./profile/messages";
import { t } from "../i18n";
import { AccountHome, AccountView, ConfirmLink, GamePasswords, GuestHome, LinkedIds, Sessions, Verified } from "./profile/views";

// ?gamepasswords shows the Game passwords lane (hidden for now: it confuses new players)
const GAME_PASSWORDS = new URLSearchParams(location.search).has("gamepasswords");

// Settings' "this is a shared computer" (localStorage, a bare flag; blocked storage: no)
function sharedComputer(): boolean {
  try {
    return window.localStorage.getItem(SHARED_COMPUTER_KEY) === "1";
  } catch {
    return false;
  }
}

export type ProfileView = "home" | "signin" | "signup" | "forgot" | "reset" | "verify" | "handle" | "account" | "password" | "sessions" | "linked" | "game-passwords" | "confirm";

// an emailed link the page was opened with (App.tsx reads it from the URL and removes it)
export interface ProfileIntent {
  kind: "verify-email" | "reset-password";
  userId: string;
  token: string;
}

export interface ProfileHandle {
  // Esc: true when it stepped back inside the stage, false when the stage should close
  back(): boolean;
}

interface ProfileProps {
  shown: boolean; // rendered (outlives `open` by the shrink)
  open: boolean; // the sheet is out (it grows down from the chip, and back into it)
  platform: PlatformState;
  inGame: boolean;
  name: string;
  rawName?: string; // PLAYER_1 with its colour codes: what the plate edits, so they survive
  onName(value: string): string; // write PLAYER_1
  onTypedName?(value: string): string; // a name typed in the plate (checked first; else onName)
  nameRefused?: { name: string; at: number } | null; // the last typed name the platform refused
  intent: ProfileIntent | null;
  onIntentTaken(): void;
  // the view to open on instead of home ("save your progress" in the match summary: signup)
  startView?: ProfileView | null;
  onWantOpen(): void; // a popup result arrived while the stage was closed
  onClose(): void; // the head's close cell, clicked
  colour: PlateColour | null; // the cycle colour the swatch sets (null: no client yet)
}

type FocusTarget = "name" | "name-select" | "body";

export const Profile = forwardRef<ProfileHandle, ProfileProps>(function Profile(props, ref) {
  const { shown, open, platform, inGame, name, rawName, onName, intent, onIntentTaken, startView, onWantOpen, onClose, colour } = props;
  const apiUrl = platform.mode === "platform" ? platform.apiUrl : null;
  const user = platform.mode === "platform" ? platform.user : null;
  const accounts = platform.mode === "platform" && platform.config.features.accounts;
  const providers: ProviderInfo[] = platform.mode === "platform" ? platform.config.providers ?? [] : [];
  const authority = platform.mode === "platform" ? platform.config.authority : "";
  const isAccount = !!user && !user.isGuest;
  // Linked IDs: what the platform offers
  const linking = platform.mode === "platform" && !!platform.config.features.linking;
  const linkByPassword = platform.mode === "platform" && !!platform.config.features.linking && !!platform.config.features.linkByPassword;
  // Game passwords confuse new players, so the lane is hidden unless the page is opened with
  // ?gamepasswords; the platform still serves them for desktop logins
  const gamePasswordsOn = platform.mode === "platform" && !!platform.config.features.gamePasswords && GAME_PASSWORDS;

  const [view, setView] = useState<ProfileView>("home");
  const [account, setAccount] = useState<AccountInfo | null>(null);
  const [sessions, setSessions] = useState<SessionInfo[] | null>(null);
  const [linkedIds, setLinkedIds] = useState<LinkedIdInfo[] | null>(null);
  const [linkCode, setLinkCode] = useState<LinkCode | null>(null);
  const [gamePasswords, setGamePasswords] = useState<GamePasswordInfo[] | null>(null);
  const [newGamePassword, setNewGamePassword] = useState<NewGamePassword | null>(null);
  const [pending, setPending] = useState<PendingLink | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [fields, setFields] = useState<FieldErrors>({});
  const [notice, setNotice] = useState<string | null>(null);
  // "this is a shared computer" left the forms for Settings, PLAYER, THIS
  // BROWSER: kept for this browser, it shapes every sign-in from here
  const shared = sharedComputer();
  const [waiting, setWaiting] = useState<string | null>(null);
  const [prefill, setPrefill] = useState<{ login: string; password: string } | null>(null);
  const [forgot, setForgot] = useState<{ email: string; sent: boolean }>({ email: "", sent: false });
  const [reset, setReset] = useState<{ userId: string; token: string; expired: boolean } | null>(null);
  const [verify, setVerify] = useState<"pending" | "ok" | "failed">("pending");
  const plateName = rawName ?? name;
  const [draft, setDraft] = useState(plateName);
  const [focusTick, setFocusTick] = useState(0);
  const [tints, setTints] = useState(false); // the swatch's colour tray

  const titleRef = useRef<HTMLInputElement>(null);
  const bodyRef = useRef<HTMLDivElement>(null);
  const focusTarget = useRef<FocusTarget>("name");
  // guest: the popup started from a guest session (a claim, for the usage events)
  const popup = useRef<{ provider: string; mode: "signin" | "link"; userId: string | null; guest: boolean; at: number } | null>(null);
  const intentTaken = useRef(false); // an emailed link chose the first view, not "home"

  useEffect(() => setDraft(plateName), [plateName]);

  const providerName = useCallback((id: string | undefined) => providers.find((p) => p.id === id)?.name ?? id ?? t("profile.theProvider"), [providers]);

  // ---- view changes and focus --------------------------------------------------------

  const go = useCallback((next: ProfileView, say: string | null = null, focus: FocusTarget = "body") => {
    setView(next);
    setError(null);
    setFields({});
    setNotice(say);
    focusTarget.current = focus;
    setFocusTick((n) => n + 1);
  }, []);

  // the focus follows the view: the view's first field or action (data-first), else the
  // name. Never while a game runs (the page does not take the focus from the game).
  useEffect(() => {
    if (!shown || inGame) return;
    const id = window.requestAnimationFrame(() => {
      const target = focusTarget.current;
      if (target !== "body") {
        titleRef.current?.focus({ preventScroll: true });
        if (target === "name-select") titleRef.current?.select();
        else titleRef.current?.setSelectionRange(titleRef.current.value.length, titleRef.current.value.length);
        return;
      }
      const body = bodyRef.current;
      const first = body?.querySelector<HTMLElement>("[data-first]:not([disabled])") ?? body?.querySelector<HTMLElement>("input, button:not([disabled])");
      first?.focus({ preventScroll: true });
      first?.scrollIntoView?.({ block: "nearest" });
    });
    return () => window.cancelAnimationFrame(id);
  }, [shown, focusTick, inGame]);

  // opening the stage: home, focus in the name (renaming is "the chip, type, Enter")
  useEffect(() => {
    if (!shown) {
      popup.current = null;
      setWaiting(null);
      return;
    }
    if (!intentTaken.current) {
      if (startView) go(startView);
      else go("home", null, "name");
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shown]);

  // a view taller than the body scrolls inside it; while there is more below, the body
  // says so (data-more: its foot fades, profile.css)
  useEffect(() => {
    const body = bodyRef.current;
    if (!body || !shown) return;
    const mark = () => body.toggleAttribute("data-more", body.scrollTop + body.clientHeight < body.scrollHeight - 2);
    mark();
    const watch = new ResizeObserver(mark);
    watch.observe(body);
    for (const child of Array.from(body.children)) watch.observe(child);
    body.addEventListener("scroll", mark, { passive: true });
    return () => {
      watch.disconnect();
      body.removeEventListener("scroll", mark);
    };
  }, [shown, view]);

  // ---- the account ----------------------------------------------------------------

  const loadAccount = useCallback(async (): Promise<AccountInfo | null> => {
    if (!apiUrl) return null;
    try {
      const a = await api.getAccount(apiUrl);
      setAccount(a);
      return a;
    } catch (e) {
      if (e instanceof api.PlatformError && e.status === 401) await refreshPlatformUser();
      setAccount(null);
      return null;
    }
  }, [apiUrl]);

  useEffect(() => {
    if (!shown || !isAccount) {
      if (!isAccount) setAccount(null);
      return;
    }
    void loadAccount();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shown, isAccount, user?.id, user?.gid]);

  // back in this tab (a confirmation link opened elsewhere, another tab signed out):
  // the account again while the stage is up
  useEffect(() => {
    if (!shown) return;
    const again = () => {
      if (document.visibilityState !== "visible") return;
      void refreshPlatformUser().then((u) => {
        if (u && !u.isGuest) void loadAccount();
      });
    };
    document.addEventListener("visibilitychange", again);
    return () => document.removeEventListener("visibilitychange", again);
  }, [shown, loadAccount]);

  // the player's level, XP and stats (platform/progression.ts), fresh whenever the stage opens
  const showProgress = progressionOn(platform);
  const progression = useProgression();
  useEffect(() => {
    if (shown && showProgress) void refreshProgression();
  }, [shown, showProgress, user?.id]);

  // ---- calls ------------------------------------------------------------------------

  // one call at a time; a failure becomes the reversed cell and the field texts
  const run = useCallback(async <T,>(work: () => Promise<T>): Promise<T | undefined> => {
    setBusy(true);
    setError(null);
    try {
      return await work();
    } catch (e) {
      const f = fieldTexts(e);
      setFields(f);
      setError(problemText(e));
      if (Object.keys(f).length) focusFirstInvalid();
      console.info("platform: " + String(e instanceof Error ? e.message : e));
      if (e instanceof api.PlatformError && e.status === 401 && !e.code) void refreshPlatformUser();
      return undefined;
    } finally {
      setBusy(false);
    }
  }, []);

  // someone else is playing now: the page takes their name (their account's), else a new one
  const become = useCallback(
    (next: PlatformUser | null, guestName?: string) => {
      setPlatformUser(next);
      setAccount(null);
      setSessions(null);
      onName(next && !next.isGuest ? next.name : guestName ?? randomName());
    },
    [onName]
  );

  const afterSignIn = useCallback(
    async (next: PlatformUser, say: string) => {
      become(next);
      setPrefill(null);
      const a = await loadAccount();
      if (a?.needsHandle) go("handle", null);
      else go("home", say);
    },
    [become, go, loadAccount]
  );

  // guest -> account (usage events): only from a guest session, only the method and kind
  const signIn = (login: string, password: string) =>
    apiUrl &&
    run(async () => {
      const guest = !!user?.isGuest;
      if (guest) claim(false, "password", "signin");
      const next = await api.signIn(apiUrl, login, password, shared);
      if (guest) claim(true, "password", "signin");
      await afterSignIn(next, t("profile.notice.signedInAs", { gid: next.gid }));
    });

  const signUp = (v: SignUpValues) =>
    apiUrl &&
    run(async () => {
      const guest = !!user?.isGuest;
      if (guest) claim(false, "password", "signup");
      const next = await api.register(apiUrl, { email: v.email, handle: v.handle, name: v.name || undefined, password: v.password, sharedComputer: shared, newsEmails: v.newsEmails });
      if (guest) claim(true, "password", "signup");
      await afterSignIn(next, t("profile.notice.created", { gid: next.gid, email: v.email }));
    });

  const sendForgot = (email: string) =>
    apiUrl &&
    run(async () => {
      await api.forgotPassword(apiUrl, email);
      setForgot({ email, sent: true });
      setNotice(t("profile.notice.resetSent"));
    });

  const setNewPassword = (password: string) => {
    if (!apiUrl || !reset) return;
    void run(async () => {
      try {
        await api.resetPassword(apiUrl, reset.userId, reset.token, password);
      } catch (e) {
        if (e instanceof api.PlatformError && e.code === "invalid_token") setReset({ ...reset, expired: true });
        throw e;
      }
      setReset(null);
      await refreshPlatformUser(); // this browser may have been one of the account's sessions
      setPrefill({ login: "", password });
      go("signin", t("profile.notice.passwordReset"));
    });
  };

  const setHandle = (handle: string) =>
    apiUrl &&
    run(async () => {
      const next = await api.chooseHandle(apiUrl, handle);
      setPlatformUser(next);
      await loadAccount();
      go("home", t("profile.notice.yourId", { gid: next.gid }));
    });

  const changePassword = (current: string | null, next: string) =>
    apiUrl &&
    run(async () => {
      await api.changePassword(apiUrl, current, next);
      await loadAccount();
      go("home", current === null ? t("profile.notice.passwordAdded") : t("profile.notice.passwordChanged"));
    });

  const resend = () =>
    apiUrl &&
    run(async () => {
      await api.resendVerification(apiUrl);
      setNotice(account?.email ? t("profile.notice.sentAgainTo", { email: account.email }) : t("profile.notice.sentAgain"));
    });

  // the home's one-time question; either answer is kept, so it is not asked again
  const newsAsk = (on: boolean) =>
    apiUrl &&
    run(async () => {
      await api.setNewsEmails(apiUrl, on, "ask");
      setAccount((a) => a && { ...a, newsEmails: on, newsEmailsAsk: false });
      go("home", on ? t("profile.notice.newsOn") : t("profile.notice.newsAskNo"));
    });

  const news = (on: boolean) =>
    apiUrl &&
    run(async () => {
      await api.setNewsEmails(apiUrl, on, DISCORD ? "discord" : "account");
      setAccount((a) => a && { ...a, newsEmails: on });
      setNotice(on ? t("profile.notice.newsOn") : t("profile.notice.newsOff"));
    });

  const unlink = (p: ProviderInfo) =>
    apiUrl &&
    run(async () => {
      await api.unlinkProvider(apiUrl, p.id);
      await loadAccount();
      setNotice(t("profile.notice.unlinked", { name: p.name }));
    });

  const showSessions = () => {
    if (!apiUrl) return;
    setSessions(null);
    go("sessions");
    void run(async () => setSessions(await api.getSessions(apiUrl)));
  };

  // ---- Linked IDs ---------------------------------------------------------------------

  const showLinked = () => {
    setLinkCode(null);
    setLinkedIds(null);
    go("linked");
    if (apiUrl && isAccount) void run(async () => setLinkedIds(await api.getLinkedIds(apiUrl)));
  };

  const newLinkCode = () => {
    if (!apiUrl) return;
    void run(async () => {
      setLinkCode(await api.createLinkCode(apiUrl));
      setNotice(null);
      setFocusTick((n) => n + 1);
    });
  };

  // the old id and its password: the platform hands over the authority's method and a salt, the
  // answer is made here (platform/linking.ts) and only it is sent
  const linkWithPassword = (gid: string, password: string) =>
    apiUrl &&
    run(async () => {
      const check = await api.startLinkCheck(apiUrl, gid);
      const linked = await api.finishLinkCheck(apiUrl, check.check, answerCheck(check, password));
      setLinkedIds(await api.getLinkedIds(apiUrl));
      setLinkCode(null);
      setNotice(t("profile.notice.idLinked", { gid: linked.gid }));
      setFocusTick((n) => n + 1);
    });

  const unlinkGid = (gid: string) =>
    apiUrl &&
    run(async () => {
      try {
        await api.unlinkId(apiUrl, gid);
      } catch (e) {
        if (!(e instanceof api.PlatformError && e.code === "not_linked")) throw e;
      }
      setLinkedIds(await api.getLinkedIds(apiUrl));
      setNotice(t("profile.notice.unlinked", { name: gid }));
      setFocusTick((n) => n + 1);
    });

  // while a code is shown, the list follows the game: a /link there shows up here, and the
  // code makes way for a new one once it is used or has run out
  useEffect(() => {
    if (view !== "linked" || !linkCode || !apiUrl) return;
    const known = new Set((linkedIds ?? []).map((l) => l.gid.toLowerCase()));
    const timer = window.setInterval(async () => {
      if (Date.now() / 1000 > linkCode.expiresAt) {
        setLinkCode(null);
        setNotice(t("profile.notice.codeExpired"));
        return;
      }
      const now = await api.getLinkedIds(apiUrl).catch(() => null);
      if (!now) return;
      const added = now.find((l) => !known.has(l.gid.toLowerCase()));
      if (!added) return;
      setLinkedIds(now);
      setLinkCode(null);
      setNotice(t("profile.notice.idLinked", { gid: added.gid }));
    }, 5000);
    return () => window.clearInterval(timer);
  }, [view, linkCode, apiUrl, linkedIds]);

  // ---- game passwords ------------------------------------------------------------------

  const showGamePasswords = () => {
    setNewGamePassword(null);
    setGamePasswords(null);
    go("game-passwords");
    if (apiUrl) void run(async () => setGamePasswords(await api.getGamePasswords(apiUrl)));
  };

  const createGamePassword = (label: string) => {
    if (!apiUrl) return;
    void run(async () => {
      const made = await api.createGamePassword(apiUrl, label);
      setNewGamePassword(made);
      setGamePasswords(await api.getGamePasswords(apiUrl));
      setNotice(t("profile.notice.gamePasswordMade", { label: made.label }));
      setFocusTick((n) => n + 1);
    });
  };

  const showGamePassword = (p: GamePasswordInfo) => {
    if (!apiUrl) return;
    void run(async () => {
      setNewGamePassword(await api.revealGamePassword(apiUrl, p.id));
      setNotice(null);
      setFocusTick((n) => n + 1);
    });
  };

  const revokeGamePassword = (p: GamePasswordInfo) =>
    apiUrl &&
    run(async () => {
      try {
        await api.revokeGamePassword(apiUrl, p.id);
      } catch (e) {
        if (!(e instanceof api.PlatformError && e.code === "game_password_not_found")) throw e;
      }
      if (newGamePassword?.id === p.id) setNewGamePassword(null);
      setGamePasswords(await api.getGamePasswords(apiUrl));
      setNotice(t("profile.notice.gamePasswordRevoked", { label: p.label }));
      setFocusTick((n) => n + 1);
    });

  const signOut = (say = t("profile.notice.signedOut")) =>
    apiUrl &&
    run(async () => {
      await api.logout(apiUrl);
      become(null);
      go("home", say);
    });

  const revoke = (s: SessionInfo) => {
    if (!apiUrl) return;
    if (s.current) return void signOut();
    void run(async () => {
      try {
        await api.revokeSession(apiUrl, s.id);
      } catch (e) {
        if (!(e instanceof api.PlatformError && e.code === "session_not_found")) throw e;
      }
      setSessions(await api.getSessions(apiUrl));
      setNotice(t("profile.notice.signedOutDevice", { device: s.device || t("profile.notice.thatBrowser") }));
      setFocusTick((n) => n + 1);
    });
  };

  const everywhere = () =>
    apiUrl &&
    run(async () => {
      await api.logoutAll(apiUrl);
      become(null);
      go("home", t("profile.notice.signedOutAll"));
    });

  const newPlayer = () =>
    apiUrl &&
    run(async () => {
      const name = randomName();
      const next = await api.newPlayer(apiUrl, name);
      become(next, name);
      go("home", t("profile.notice.newGuest"), "name-select");
    });

  const confirm = () =>
    apiUrl &&
    run(async () => {
      const next = await api.confirmLink(apiUrl);
      setPending(null);
      await afterSignIn(next, pending ? t("profile.notice.providerLinkedSignedIn", { provider: pending.providerName, gid: next.gid }) : t("profile.notice.signedInAs", { gid: next.gid }));
    });

  // ---- external sign-in (popup) ----------------------------------------------------------

  const external = (p: ProviderInfo, mode: "signin" | "link") => {
    if (!apiUrl) return;
    setError(null);
    const guest = mode === "signin" && !!user?.isGuest;
    if (guest) claim(false, p.id, "signin");
    popup.current = { provider: p.id, mode, userId: user?.id ?? null, guest, at: Date.now() };
    openExternalPopup(api.externalStartUrl(apiUrl, p.id, mode, mode === "signin" && shared));
    setWaiting(p.name);
  };

  const onResult = useRef<(r: ExternalResult) => void>(() => {});
  onResult.current = (r: ExternalResult) => {
    const started = popup.current;
    popup.current = null;
    setWaiting(null);
    if (!shown && !inGame) onWantOpen();
    const name = providerName(r.provider);
    if (r.status === "error") {
      setError(externalText(r.error));
      setFocusTick((n) => n + 1);
      return;
    }
    if (!apiUrl) return;
    void run(async () => {
      if (r.status === "confirm_link") {
        const p = await api.getPendingLink(apiUrl);
        if (!p) throw new api.PlatformError("no pending link", 404, "no_pending_link");
        setPending(p);
        go("confirm");
        return;
      }
      if (r.status === "linked") {
        await loadAccount();
        go("home", t("profile.notice.linked", { name }));
        return;
      }
      const next = await api.getMe(apiUrl);
      if (!next) throw new api.PlatformError("not signed in", 401, "external_failed");
      // the guest became the account (a new one: same id) or was merged into an existing one
      if (started?.guest && r.provider) claim(true, r.provider, next.id === started.userId ? "signup" : "signin");
      become(next);
      const a = await loadAccount();
      if (r.needsHandle || a?.needsHandle) go("handle");
      else go("home", t("profile.notice.signedInWith", { provider: name, gid: next.gid }));
    });
  };

  useEffect(() => {
    if (!apiUrl) return;
    return onExternalResult(new URL(apiUrl).origin, (r) => onResult.current(r));
  }, [apiUrl]);

  // no message came (no BroadcastChannel, the relay not configured): when this window
  // gets the focus back, ask the platform what happened
  useEffect(() => {
    if (!waiting || !apiUrl) return;
    let timer = 0;
    const check = () => {
      window.clearTimeout(timer);
      timer = window.setTimeout(async () => {
        const started = popup.current;
        if (!started) return;
        const pendingLink = await api.getPendingLink(apiUrl).catch(() => null);
        if (!popup.current) return;
        if (pendingLink) return onResult.current({ type: "rc-auth", status: "confirm_link", provider: started.provider });
        const now = await api.getMe(apiUrl).catch(() => null);
        if (!popup.current) return;
        if (now && !now.isGuest && (now.id !== started.userId || started.mode === "link"))
          onResult.current({ type: "rc-auth", status: started.mode === "link" ? "linked" : "signed_in", provider: started.provider });
      }, 2500);
    };
    window.addEventListener("focus", check);
    return () => {
      window.removeEventListener("focus", check);
      window.clearTimeout(timer);
    };
  }, [waiting, apiUrl]);

  // ---- emailed links ------------------------------------------------------------------

  useEffect(() => {
    if (!intent || !shown || !apiUrl) return;
    intentTaken.current = true;
    onIntentTaken();
    if (intent.kind === "reset-password") {
      setReset({ userId: intent.userId, token: intent.token, expired: false });
      go("reset");
      return;
    }
    setVerify("pending");
    go("verify");
    void (async () => {
      setBusy(true);
      try {
        await api.verifyEmail(apiUrl, intent.userId, intent.token);
        setVerify("ok");
        if (isAccount) await loadAccount();
      } catch (e) {
        setError(problemText(e));
        setVerify("failed");
      } finally {
        setBusy(false);
        setFocusTick((n) => n + 1);
      }
    })();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [intent, shown, apiUrl]);
  useEffect(() => {
    if (!shown) {
      intentTaken.current = false;
      setTints(false);
    }
  }, [shown]);

  // ---- Esc ------------------------------------------------------------------------

  useImperativeHandle(ref, () => ({
    back() {
      if (tints) {
        setTints(false);
        return true;
      }
      if (view === "home") return false;
      go(backOf(view));
      return true;
    }
  }));

  // ---- the sheet --------------------------------------------------------------------

  const commitName = () => {
    const next = (props.onTypedName ?? onName)(draft);
    setDraft(next);
  };

  // a typed name the platform refused: the plate is back to the old one, the notice says why
  const refusedAt = props.nameRefused?.at;
  useEffect(() => {
    if (refusedAt && props.nameRefused) setNotice(t("profile.notice.nameNotAllowed", { name: props.nameRefused.name }));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [refusedAt]);

  let who: string;
  if (platform.mode !== "platform" || !accounts) who = platform.mode === "community" ? t("profile.who.communityNoAccounts") : t("profile.who.community");
  else if (!user) who = t("profile.who.nobody");
  else if (user.isGuest) who = t("profile.who.guest");
  else who = t("profile.who.account", { gid: user.gid });

  const common = { busy, error, fields, onFields: setFields };

  let body: ReactNode = null;
  if (platform.mode === "platform" && accounts) {
    if (view === "home")
      body =
        user && !user.isGuest ? (
          <AccountHome
            progression={showProgress ? progression : null}
            account={account}
            busy={busy}
            error={error}
            linked={linking}
            gamePasswords={gamePasswordsOn}
            onAccount={() => go("account")}
            onLinked={showLinked}
            onGamePasswords={showGamePasswords}
            onSessions={showSessions}
            onSignOut={() => void signOut()}
            onNewsAsk={newsAsk}
          />
        ) : (
          <GuestHome
            progression={showProgress && user ? progression : null}
            user={user}
            busy={busy}
            error={error}
            linked={linking}
            onSignUp={() => go("signup")}
            onSignIn={() => go("signin")}
            onNewPlayer={newPlayer}
            onLinked={showLinked}
          />
        );
    else if (view === "signin")
      body = (
        <SignInForm
          {...common}
          isGuest={!!user?.isGuest}
          providers={providers}
          prefill={prefill}
          waiting={waiting}
          onSubmit={signIn}
          onForgot={(email) => {
            setForgot({ email, sent: false });
            go("forgot");
          }}
          onSignUp={() => go("signup")}
          onProvider={(p) => external(p, "signin")}
        />
      );
    else if (view === "signup")
      body = (
        <SignUpForm
          {...common}
          isGuest={!!user?.isGuest}
          authority={authority}
          name={name}
          providers={providers}
          waiting={waiting}
          onSubmit={signUp}
          onSignIn={() => go("signin")}
          onProvider={(p) => external(p, "signin")}
        />
      );
    else if (view === "forgot") body = <ForgotForm {...common} email={forgot.email} sent={forgot.sent} onSubmit={sendForgot} onBack={() => go("signin")} />;
    else if (view === "reset")
      body = (
        <ResetForm
          {...common}
          expired={!!reset?.expired}
          onSubmit={setNewPassword}
          onNewLink={() => {
            setForgot({ email: "", sent: false });
            go("forgot");
          }}
        />
      );
    else if (view === "verify") body = <Verified state={verify} error={error} onDone={() => go("home")} />;
    else if (view === "handle")
      body = <HandleForm {...common} suggested={account?.handle ?? user?.handle ?? ""} authority={authority} onSubmit={setHandle} />;
    else if (view === "account" && user && !user.isGuest)
      body = (
        <AccountView
          user={user}
          account={account}
          providers={providers}
          busy={busy}
          error={error}
          onHandle={() => go("handle")}
          onResend={resend}
          onPassword={() => go("password")}
          onNews={news}
          onLink={(p) => external(p, "link")}
          onUnlink={unlink}
          onBack={() => go("home")}
        />
      );
    else if (view === "password") body = <PasswordForm {...common} hasPassword={!!account?.hasPassword} onSubmit={changePassword} onBack={() => go("account")} />;
    else if (view === "sessions")
      body = <Sessions sessions={sessions} busy={busy} error={error} onRevoke={revoke} onEverywhere={everywhere} onBack={() => go("home")} />;
    else if (view === "linked" && linking)
      body = (
        <LinkedIds
          account={isAccount}
          ids={linkedIds}
          code={linkCode}
          byPassword={linkByPassword}
          busy={busy}
          error={error}
          onLink={(gid, password) => void linkWithPassword(gid, password)}
          onCode={newLinkCode}
          onUnlink={(gid) => void unlinkGid(gid)}
          onSave={() => go("signup")}
          onBack={() => go("home")}
        />
      );
    else if (view === "game-passwords" && gamePasswordsOn && user && !user.isGuest)
      body = (
        <GamePasswords
          login={user.gid}
          passwords={gamePasswords}
          shown={newGamePassword}
          busy={busy}
          error={error}
          labelError={fields.label ?? null}
          onCreate={createGamePassword}
          onShow={showGamePassword}
          onRevoke={(p) => void revokeGamePassword(p)}
          onBack={() => go("home")}
        />
      );
    else if (view === "confirm" && pending)
      body = <ConfirmLink pending={pending} busy={busy} error={error} onConfirm={confirm} onCancel={() => go("home", t("profile.notice.nothingLinked"))} />;
  }

  // hovering a lane or a button gives it the focus, as in the in-game menu, unless a field
  // is being typed in
  const hover = (e: React.MouseEvent<HTMLElement>) => {
    const el = (e.target as HTMLElement).closest<HTMLElement>(".pf-lane, .pf-solid, .pf-provider");
    if (!el || document.activeElement === el || (el as HTMLButtonElement).disabled) return;
    const active = document.activeElement as HTMLElement | null;
    if (active && (active.tagName === "INPUT" || active.tagName === "TEXTAREA")) return;
    el.focus({ preventScroll: true });
  };

  return (
    <section
      className={"pf" + (open ? " is-open" : "")}
      id="profile"
      role="dialog"
      aria-modal="true"
      aria-label={t("profile.aria.dialog")}
      data-view={view}
      data-busy={busy ? "1" : "0"}
      hidden={!shown}
      onMouseOver={hover}
    >
      <header className="pf-plate">
        <PlateSwatch colour={colour} open={tints && shown} onOpen={setTints} />
        <div className="pf-who">
          <div className="pf-name-well">
            <ColorInput
              wrapClassName="pf-name-cin"
              className="pf-name"
              id="profile-name"
              ref={titleRef}
              value={draft}
              maxLength={NAME_MAX}
              spellCheck={false}
              autoComplete="off"
              aria-label={t("profile.aria.name")}
              aria-describedby="profile-who"
              onChange={(e) => setDraft(e.target.value.slice(0, NAME_MAX))}
              onBlur={commitName}
              onKeyDown={(e) => {
                if (e.key === "Enter") {
                  e.preventDefault();
                  commitName();
                  go(view, notice, "body");
                }
              }}
            />
          </div>
          <p className="pf-who-say" id="profile-who">
            {who}
          </p>
        </div>
        <SheetClose label={t("profile.title")} onClose={onClose} />
      </header>

      <div className="pf-body" ref={bodyRef}>
        <Notice text={notice} />
        <div className="pf-swap" key={view}>
          {body}
        </div>
      </div>

      {/* Esc's other meaning, inside a view, and the pointer's way to it (closing is the head's cell) */}
      {view !== "home" && (
        <button className="pf-foot" type="button" tabIndex={-1} onClick={() => go(backOf(view))}>
          <span className="pf-foot-key">{t("common.esc")}</span> {t("profile.foot.back")}
        </button>
      )}

    </section>
  );
});

// where Esc goes from a view: the form it came from, else home
function backOf(view: ProfileView): ProfileView {
  if (view === "forgot") return "signin";
  if (view === "password") return "account";
  return "home";
}
