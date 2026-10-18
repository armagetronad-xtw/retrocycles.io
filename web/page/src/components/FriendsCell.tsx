// The friends cell: the status bar's second cell, after the chip, with the
// platform's features.friends. One activation opens the Friends panel hung from it; it shows
// how many friend requests wait for an answer (nothing when none: no presence yet, so it
// never claims a count of friends online). Lit as pressed while its panel is up.
import { forwardRef } from "react";
import { Icon } from "./Icons";
import { t } from "../i18n";

interface FriendsCellProps {
  requests: number;
  disabled: boolean;
  onOpen(): void;
}

export const FriendsCell = forwardRef<HTMLButtonElement, FriendsCellProps>(function FriendsCell({ requests, disabled, onOpen }, ref) {
  return (
    <button
      className="status-cell status-icon status-friends fr-cell"
      id="friends-cell"
      type="button"
      ref={ref}
      disabled={disabled}
      aria-haspopup="dialog"
      aria-controls="friends"
      aria-label={requests ? t("social.cell.labelRequests", { n: requests }) : t("social.cell.label")}
      onClick={onOpen}
    >
      <Icon name="users" className="status-glyph" />
      {requests > 0 && (
        <span className="status-count" id="friends-requests-count">
          {requests}
        </span>
      )}
    </button>
  );
});

// The messages cell (the chat dock): beside the friends cell while the account has
// any conversation, the way back to them; the count of unread messages while some wait. One
// activation opens the newest unread conversation in the dock, else the newest one.
export function MessagesCell({ unread, disabled, onOpen }: { unread: number; disabled: boolean; onOpen(): void }) {
  const label = unread > 0 ? t("social.cell.messages", { n: unread }) : t("social.cell.messagesNone");
  return (
    <button className={"status-cell status-icon status-messages" + (unread > 0 ? " has-new" : "")} id="messages-cell" type="button" disabled={disabled} aria-label={label} title={label} onClick={onOpen}>
      <Icon name="message" className="status-glyph" />
      {unread > 0 && (
        <span className="status-count" id="messages-count">
          {unread}
        </span>
      )}
    </button>
  );
}
