// Google Analytics' cookies and the first-party usage statistics (platform/analytics.ts), asked where the law wants consent first (googleTag.ts: the EEA, the
// UK, Switzerland, or /?cookies anywhere): one quiet cell at the foot of the home screen, as the
// home screen hint (HomeScreenHint.tsx), until Accept or Decline. Closing it is neither: it is
// asked again on the next visit. Nothing waits for it; the game plays either way.
import { useSyncExternalStore } from "react";
import { chooseCookies, cookieBannerUp, subscribeCookieBanner } from "../googleTag";
import { t } from "../i18n";

export function useCookieBanner(): boolean {
  return useSyncExternalStore(subscribeCookieBanner, cookieBannerUp);
}

export function CookieBanner() {
  return (
    <section className="ck-banner" id="cookie-banner" aria-label={t("home.cookies.aria")}>
      <p>
        {t("home.cookies.ask")} <a href="./privacy/#google-analytics">{t("home.cookies.privacy")}</a>
      </p>
      <div className="ck-actions">
        <button type="button" className="ck-button" id="cookie-decline" onClick={() => chooseCookies("denied")}>
          {t("home.cookies.decline")}
        </button>
        <button type="button" className="ck-button ck-accept" id="cookie-accept" onClick={() => chooseCookies("granted")}>
          {t("home.cookies.accept")}
        </button>
      </div>
    </section>
  );
}
