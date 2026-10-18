// On an iPhone's Safari (fullscreen.ts iphoneSafari; not from the home screen, not the embed), one
// quiet cell on the home screen: the way to play with no browser bars, Add to Home Screen, until
// it is put away (rc.homeScreenHint in localStorage, a bare flag; blocked storage shows it again).
import { t, tx } from "../i18n";

const KEY = "rc.homeScreenHint";

export function homeScreenHintSeen(): boolean {
  try {
    return localStorage.getItem(KEY) === "dismissed";
  } catch {
    return false;
  }
}

export function HomeScreenHint({ onClose }: { onClose(): void }) {
  const close = () => {
    try {
      localStorage.setItem(KEY, "dismissed");
    } catch {
      // shown again next time
    }
    onClose();
  };
  return (
    <p className="hs-hint" id="home-screen-hint" role="note">
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="square" aria-hidden="true">
        <path d="M12 3v12M7.5 7.5L12 3l4.5 4.5M5 11v10h14V11" />
      </svg>
      <span>
        {tx("home.hint.homeScreen", { share: <b>{t("home.hint.share")}</b>, add: <b>{t("home.hint.addToHome")}</b> })}
      </span>
      <button type="button" className="hs-close" id="home-screen-hint-close" aria-label={t("home.hint.dismiss")} onClick={close}>
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" aria-hidden="true">
          <path d="M6 6l12 12M18 6L6 18" />
        </svg>
      </button>
    </p>
  );
}
