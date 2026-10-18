// The community Discord server (The Grid): a cell at the end of the documents line, in the
// bottom right corner of the home screen (in line with the links, with more
// weight than they have). A link out, in a new tab so the loaded game stays. Under the name, how
// many are online there, as the platform last asked Discord (GET /v1/community/discord; the
// visitor's browser never asks Discord itself); "community Discord" while that is unknown, or
// without a platform. The documents line is not shown in an embed (X's frame, the Discord
// Activity), behind a stage or in the game.
import { useEffect, useState } from "react";
import { num, t } from "../i18n";
import { getDiscordCommunity } from "../platform/client";
import { platformSession } from "../platform/session";
import { EMBED } from "../links";

const INVITE = "https://discord.gg/HqH2KDhu8Z";
const REFRESH_MS = 5 * 60_000;

// the Discord mark (Simple Icons 13.21.0, CC0 1.0; as in profile/brands.tsx), naming where the link goes
const MARK =
  "M20.317 4.3698a19.7913 19.7913 0 00-4.8851-1.5152.0741.0741 0 00-.0785.0371c-.211.3753-.4447.8648-.6083 1.2495-1.8447-.2762-3.68-.2762-5.4868 0-.1636-.3933-.4058-.8742-.6177-1.2495a.077.077 0 00-.0785-.037 19.7363 19.7363 0 00-4.8852 1.515.0699.0699 0 00-.0321.0277C.5334 9.0458-.319 13.5799.0992 18.0578a.0824.0824 0 00.0312.0561c2.0528 1.5076 4.0413 2.4228 5.9929 3.0294a.0777.0777 0 00.0842-.0276c.4616-.6304.8731-1.2952 1.226-1.9942a.076.076 0 00-.0416-.1057c-.6528-.2476-1.2743-.5495-1.8722-.8923a.077.077 0 01-.0076-.1277c.1258-.0943.2517-.1923.3718-.2914a.0743.0743 0 01.0776-.0105c3.9278 1.7933 8.18 1.7933 12.0614 0a.0739.0739 0 01.0785.0095c.1202.099.246.1981.3728.2924a.077.077 0 01-.0066.1276 12.2986 12.2986 0 01-1.873.8914.0766.0766 0 00-.0407.1067c.3604.698.7719 1.3628 1.225 1.9932a.076.076 0 00.0842.0286c1.961-.6067 3.9495-1.5219 6.0023-3.0294a.077.077 0 00.0313-.0552c.5004-5.177-.8382-9.6739-3.5485-13.6604a.061.061 0 00-.0312-.0286zM8.02 15.3312c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9555-2.4189 2.157-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.9555 2.4189-2.1569 2.4189zm7.9748 0c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9554-2.4189 2.1569-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.946 2.4189-2.1568 2.4189Z";

interface Community {
  invite: string;
  members: number | null;
  online: number | null;
}

export function CommunityLink() {
  const [community, setCommunity] = useState<Community | null>(null);

  useEffect(() => {
    if (EMBED) return;
    let gone = false;
    const ask = async () => {
      const state = await platformSession();
      if (gone || state.mode !== "platform") return;
      try {
        const answer = await getDiscordCommunity(state.apiUrl);
        if (!gone) setCommunity(answer);
      } catch {
        // keep what was shown: the counts are a nicety, the link works without them
      }
    };
    void ask();
    const timer = window.setInterval(() => {
      if (document.visibilityState === "visible") void ask();
    }, REFRESH_MS);
    return () => {
      gone = true;
      window.clearInterval(timer);
    };
  }, []);

  if (EMBED) return null;
  const online = community?.online ?? null;
  const members = community?.members ?? null;
  const grouped = (n: number) => num(n, { useGrouping: true });
  return (
    <a
      className="community-cell"
      id="docs-community"
      href={community?.invite || INVITE}
      target="_blank"
      rel="noopener"
      aria-label={t("home.community.aria")}
      title={members !== null ? t("home.community.members", { members: grouped(members) }) : undefined}
    >
      <svg className="community-mark" viewBox="0 0 24 24" aria-hidden="true">
        <path d={MARK} />
      </svg>
      <span className="community-who">
        <span className="community-name">{t("home.community.name")}</span>
        <span className={"community-sub" + (online !== null ? " is-online" : "")}>
          {online !== null ? t("home.community.online", { online: grouped(online) }) : t("home.community.sub")}
        </span>
      </span>
      <svg className="community-out" viewBox="0 0 12 12" aria-hidden="true">
        <path d="M3.5 2.5h6v6M9.5 2.5 2.5 9.5" />
      </svg>
    </a>
  );
}
