// The chosen mode pulled forward: its line travels from the queue to the head of
// the stage and its body appears beneath it. Quick Play, a board's connect and Local are
// the connecting screen instead (Connecting.tsx: the scene goes black, the player's cycle
// runs a figure 8, the steps take turns beneath it; no head). The Server Browser is its own
// full page (ServerBrowser.tsx), Ranked one honest line. A ban (the platform refused the login
// token for the room) replaces any body: the reversed cell, the code of conduct and its
// appeals section.
import { useEffect, useLayoutEffect, useRef, type Ref } from "react";
import type { SessionState, ServerEntry } from "../aa/types";
import type { HintItem } from "../keys";
import { modeInfo, type Mode } from "../modes";
import type { FindingPhase } from "../platform/quickplay";
import { Connecting } from "./Connecting";
import { docHref } from "./Docs";
import type { FriendsList } from "../platform/types";
import { ServerBrowser, type ServerBrowserHandle } from "./ServerBrowser";
import { Leaderboards, type LeaderboardsHandle } from "./Leaderboards";
import { t } from "../i18n";

// where the chosen line was in the queue (viewport px), so the title can travel
// from there to the head of the stage
export interface StageBox {
  x: number;
  y: number;
}

interface StageProps {
  mode: Mode | null;
  open: boolean;
  box: StageBox;
  sub: string;
  session: SessionState;
  resource: string | null;
  // Quick Play through the platform: the finding step's state; null: no such step (community mode)
  finding: FindingPhase | null;
  // the platform refused the room's login token: "banned until ...: reason"; null: no ban
  ban: string | null;
  // the turn and brake keys the connecting screen names (keys.ts hintItems)
  keys: HintItem[];
  // the Server Browser (ServerBrowser.tsx)
  servers: ServerEntry[];
  serversLoaded: boolean;
  serversOk: boolean;
  serversAt: number | null;
  friends: FriendsList | null;
  browserRef: Ref<ServerBrowserHandle>;
  // Direct Connect: a ?connect= link to show once, and whether the gateway is there
  connectLink?: { address: string; warn: boolean } | null;
  onConnectLink?(): void;
  directGateway?: boolean;
  // the Leaderboards page (Leaderboards.tsx)
  boardsRef: Ref<LeaderboardsHandle>;
  youColor: string | null; // the player's cycle colour, for their row on the Leaderboards page
  onSaveProgress(): void;
  onQuickPlay(): void;

  onRefreshServers(): void;
  onJoinServer(entry: ServerEntry, spectate: boolean): void;
  onBack(): void;
  // a duel link waiting for its opponent: copy it again
  onCopyDuel(): Promise<boolean>; // true once the link is on the clipboard
  onShareDuel(): void; // the system's share sheet with the link
  onPlayWhileWaiting(): void; // Quick Play meanwhile, the link left open
}

export function Stage(props: StageProps) {
  const { mode, open, box, sub, session, resource, finding, ban, keys } = props;
  const info = mode ? modeInfo(mode) : null;
  // the connecting screen: Quick Play, a board's connect under way (until the first round shows:
  // `playing` before it is still the stage, App.tsx), the local game starting
  const boardConnect = mode === "browser" && (session === "connecting" || session === "syncing" || session === "fetching" || session === "playing");
  const connect = !ban && (mode === "quick" || mode === "custom" || mode === "local" || boardConnect);
  // the Server Browser's own page (no head: its plate says SERVERS)
  const board = !ban && mode === "browser" && !boardConnect;
  // the Leaderboards page, in the same frame
  const boards = !ban && mode === "boards";
  const titleRef = useRef<HTMLHeadingElement>(null);

  // the travel: measured once the stage is displayed and before it opens, as the
  // offset from the head to where the line sat in the queue (transform only)
  useLayoutEffect(() => {
    const el = titleRef.current;
    if (!el || !mode) return;
    const r = el.getBoundingClientRect();
    el.style.setProperty("--fx", box.x - r.left + "px");
    el.style.setProperty("--fy", box.y - r.top + "px");
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [mode]);

  return (
    <section className={"stage" + (open ? " is-open" : "") + (connect ? " is-connect" : "") + (board || boards ? " is-servers" : "")} id="stage" hidden={!mode} aria-label={t("connect.stage.label")}>
      <header className="stage-head">
        <span className="stage-caret" aria-hidden="true" />
        <div>
          <h2 className="stage-title" id="stage-title" ref={titleRef}>
            {info ? info.title : ""}
          </h2>
          <p className="stage-sub" id="stage-sub">
            {sub}
          </p>
        </div>
      </header>

      {connect && mode && (
        <Connecting
          kind={mode === "local" ? "local" : "connect"}
          session={session}
          resource={resource}
          finding={mode === "quick" || mode === "custom" ? finding : null}
          sub={mode === "local" ? "" : sub}
          keys={keys}
          duel={mode === "custom"}
          onCopy={props.onCopyDuel}
          onShare={props.onShareDuel}
          onPlayWhileWaiting={props.onPlayWhileWaiting}
          onCancel={props.onBack}
        />
      )}

      {board && (
        <ServerBrowser
          ref={props.browserRef}
          open={open}
          servers={props.servers}
          loaded={props.serversLoaded}
          feedOk={props.serversOk}
          updatedAt={props.serversAt}
          friends={props.friends}
          onRefresh={props.onRefreshServers}
          onJoin={props.onJoinServer}
          connectLink={props.connectLink}
          onConnectLink={props.onConnectLink}
          gateway={props.directGateway}
          onBack={props.onBack}
        />
      )}

      {boards && <Leaderboards ref={props.boardsRef} open={open} youColor={props.youColor} onBack={props.onBack} onSave={props.onSaveProgress} onPlay={props.onQuickPlay} />}

      <div className="stage-body">
        {ban && <BanPane text={ban} />}
        {mode === "ranked" && <p className="honest">{t("connect.ranked.notYet")}</p>}
      </div>
    </section>
  );
}

function BanPane({ text }: { text: string }) {
  const first = useRef<HTMLAnchorElement>(null);
  // the way on is one of its links (Esc closes the stage)
  useEffect(() => {
    first.current?.focus({ preventScroll: true });
  }, []);
  return (
    <div className="pane ban-pane" id="pane-ban">
      <p className="cell" id="ban-cell" role="alert">
        {text}
      </p>
      <p className="ban-say">
        {t("connect.ban.say")}
      </p>
      <p className="ban-links">
        <a className="docs-link" id="ban-conduct" href={docHref("conduct")} target="_blank" rel="noopener" ref={first}>
          {t("connect.ban.conduct")}
        </a>
        <a className="docs-link" id="ban-appeals" href={docHref("conduct") + "#appeals"} target="_blank" rel="noopener">
          {t("connect.ban.appeals")}
        </a>
      </p>
    </div>
  );
}
