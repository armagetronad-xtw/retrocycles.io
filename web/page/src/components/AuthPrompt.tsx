// The server's password request (the `authRequest` event, API.md): what the desktop client's
// login dialog is. It comes after /login in the chat or the Console (`/login name@forums`), or
// from a server that asks on its own. In the in-game menu's idiom (GameMenu.tsx): one card panel
// in the menu's place, the game dimmed behind it.
// The prompt sits in our page but its words are the server's, and whatever is typed goes to that
// server (scrambled with its salt, prefix and suffix, all of its choosing, so a weak password can
// be cracked from the answer). So: the server is named by the page, its text is quoted as its own,
// every prompt says it is never the Retrocycles password, and a request the player did not ask
// for (`asked`: no /login of theirs in the last minute, App.tsx) first asks whether to log in at
// all. The password goes to aa.auth(), which scrambles it in the wasm; the field is emptied at
// once, PASSWORD_STORAGE is -1 (App.tsx: nothing kept for the run), and the fields keep password
// managers out (a forums password saved for retrocycles.io would be offered on our sign-in).
// The result comes back as the server's console line; a refused one asks again with `failed`.
// Keys: data-aa-keys="page", Enter acts, Escape cancels; closing gives the keys back.
import { useEffect, useRef, useState, type KeyboardEvent as ReactKeyboardEvent } from "react";
import type { AaAuthRequestEvent } from "../aa/types";
import { NO_PASSWORD_MANAGERS } from "./profile/bits";
import { t, tx } from "../i18n";

interface AuthPromptProps {
  request: AaAuthRequestEvent;
  asked: boolean; // the player typed /login a moment ago
  serverName: string; // the room as the page knows it
  onAnswer(username: string, password: string): void;
  onCancel(): void;
}

export function AuthPrompt({ request, asked, serverName, onAnswer, onCancel }: AuthPromptProps) {
  const [user, setUser] = useState(request.username);
  const [confirmed, setConfirmed] = useState(asked || request.failed);
  const passwordRef = useRef<HTMLInputElement>(null);
  const firstRef = useRef<HTMLButtonElement>(null);
  const server = serverName || t("game.auth.thisServer");

  // each request (a refused answer is asked again) starts on the password, or on the question
  useEffect(() => {
    setUser(request.username);
    if (passwordRef.current) passwordRef.current.value = "";
    if (confirmed) passwordRef.current?.focus({ preventScroll: true });
    else firstRef.current?.focus({ preventScroll: true });
  }, [request, confirmed]);

  const send = () => {
    const field = passwordRef.current;
    const password = field ? field.value : "";
    if (field) field.value = "";
    onAnswer(user.trim() || request.username, password);
  };

  const onKeyDown = (e: ReactKeyboardEvent<HTMLDivElement>) => {
    const target = e.target as HTMLElement;
    if (e.key === "Escape") {
      e.preventDefault();
      onCancel();
    } else if (e.key === "Enter" && target.tagName === "INPUT") {
      e.preventDefault();
      send();
    } else if ((e.key === "Enter" || e.key === " ") && target.tagName === "BUTTON") {
      e.preventDefault();
      target.click();
    }
  };

  const said = request.serverMessage ? <q className="auth-said">{request.serverMessage}</q> : null;

  return (
    <div
      className="gm gm-auth"
      id="auth-prompt"
      role="dialog"
      aria-labelledby="auth-title"
      data-aa-keys="page"
      data-step={confirmed ? "password" : "ask"}
      data-failed={request.failed ? "1" : undefined}
      onKeyDown={onKeyDown}
    >
      <section className="gm-panel gm-card">
        <h2 className="gm-name" id="auth-title">
          <span className="gm-name-key">{confirmed ? t("game.auth.logIn") : t("game.auth.logInAsk")}</span> {request.username}
        </h2>
        {!confirmed ? (
          <>
            <p className="gm-sub" id="auth-ask">
              {said ? tx("game.auth.asksSays", { server, said }) : t("game.auth.asks", { server })}
            </p>
            <p className="gm-say">
              {t("game.auth.notYours", { user: request.username })}
            </p>
            <button className="lane" type="button" id="auth-go" ref={firstRef} onClick={() => setConfirmed(true)}>
              {t("game.auth.enter")}
            </button>
            <button className="lane lane-small" type="button" id="auth-cancel" onClick={onCancel}>
              {t("game.auth.notNow")}
            </button>
          </>
        ) : (
          <>
            <p className="gm-sub">
              {said ? tx("game.auth.asksSaid", { server, said }) : t("game.auth.asksPassword", { server })}
            </p>
            {request.failed && (
              <p className="cell" id="auth-failed" role="alert">
                {t("game.auth.refused")}
              </p>
            )}
            <div className="field gm-field">
              <label className="field-key" htmlFor="auth-user">
                {t("game.auth.account")}
              </label>
              <div className="field-val">
                <input
                  className="field-input"
                  id="auth-user"
                  name="rc-old-account"
                  value={user}
                  spellCheck={false}
                  autoCapitalize="off"
                  {...NO_PASSWORD_MANAGERS}
                  onChange={(e) => setUser(e.target.value)}
                />
              </div>
            </div>
            <div className="field gm-field">
              <label className="field-key" htmlFor="auth-password">
                {t("game.auth.password")}
              </label>
              <div className="field-val">
                <input
                  className="field-input"
                  id="auth-password"
                  name="rc-old-secret"
                  type="password"
                  ref={passwordRef}
                  placeholder="—"
                  {...NO_PASSWORD_MANAGERS}
                  aria-describedby="auth-hint"
                />
                <p className="field-hint" id="auth-hint">
                  {t("game.auth.hint")}
                </p>
              </div>
            </div>
            <button className="lane" type="button" id="auth-send" onClick={send}>
              {t("game.auth.logIn")}
            </button>
            <button className="lane lane-small" type="button" id="auth-cancel" onClick={onCancel}>
              {t("common.cancel")}
            </button>
          </>
        )}
      </section>
    </div>
  );
}
