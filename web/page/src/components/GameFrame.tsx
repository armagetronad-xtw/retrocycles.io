// The game view: the canvas below renders everything (HUD, fps, ping; the score table is the page's, Scoreboard.tsx,
// the round's text stay the game's). The page adds only the in-game menu the game's
// INGAME_MENU key (Escape by default) opens through the `menuOpen` event (GameMenu.tsx:
// leaving, the invite link, the Console, the players in the room, a player's card, Report). What a round or a match
// earned (Rewards.tsx) comes in as children, under the menu.
import { forwardRef, type ReactNode } from "react";
import { GameMenu, type CardSocial, type GameMenuHandle, type MenuDuel, type MenuFriends, type MenuMoveAsk } from "./GameMenu";
import type { AaPlayer } from "../aa/types";
import type { ReasonId, ReportResult, ReportTarget } from "../platform/reports";
import { t } from "../i18n";

interface GameFrameProps {
  local: boolean;
  serverName: string;
  menuOpen: boolean;
  players: AaPlayer[];
  canReport: boolean;
  level: number | null;
  invite: string | null;
  onInvite?: (() => void) | null; // inside Discord: its own invite (GameMenu)
  onLeave(): void;
  fullGame?: string | null;
  onFullGame?(): void;
  onCloseMenu(): void;
  onSettings(): void;
  onReport(target: ReportTarget, reason: ReasonId, note: string): Promise<ReportResult>;
  social: CardSocial;
  friends: MenuFriends | null;
  duel: MenuDuel | null; // a challenge to me
  moveAsk?: MenuMoveAsk | null; // a server's move elsewhere
  children?: ReactNode;
}

export const GameFrame = forwardRef<GameMenuHandle, GameFrameProps>(function GameFrame(
  { local, serverName, menuOpen, players, canReport, level, invite, onInvite, onLeave, fullGame, onFullGame, onCloseMenu, onSettings, onReport, social, friends, duel, moveAsk, children },
  ref
) {
  return (
    <section className="gameframe" id="gameframe" aria-label={t("game.view")}>
      <GameMenu
        ref={ref}
        open={menuOpen}
        local={local}
        serverName={serverName}
        players={players}
        canReport={canReport}
        level={level}
        invite={invite}
        onInvite={onInvite}
        onLeave={onLeave}
        fullGame={fullGame}
        onFullGame={onFullGame}
        onClose={onCloseMenu}
        onSettings={onSettings}
        onReport={onReport}
        social={social}
        friends={friends}
        duel={duel}
        moveAsk={moveAsk}
      />
      {children}
    </section>
  );
});
