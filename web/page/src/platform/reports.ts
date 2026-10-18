// Reports: the player card's Report, only with the platform's
// `features.moderation` and only in an official room (a server with platform login).
//
// Two ways to the moderators' queue, both the contract's (section 5, "Reports"):
// - POST /v1/reports names the target by gid or user id. The 0.2.9 protocol does not
//   carry the authenticated gid of other players to a client, so the page only knows it
//   when something tells it (a room players lookup, not on this branch); then this way.
// - Otherwise the room's own `/report <name> <reason>` (gPlatform, PLATFORM_REPORTS): the
//   server finds the player by name as `/msg` does, takes the identity it verified (gid,
//   user id, connection, the address of a player who is not logged in) and its recent
//   chat window as the players saw it, and sends a `report` event; the platform files it
//   with the reason code read from the first word. The room answers the reporter on the
//   console; the page reads that answer back (its words, not ours).
// The note is size-limited for both (the chat line carries it).
import { PlatformError, createReport } from "./client";
import { platformState } from "./session";
import { t, type PlainKey } from "../i18n";

export type ReasonId = "cheating" | "harassment" | "spam" | "name" | "other";

// the presets, in the order the card lists them; `code` is the contract's reason (English, data);
// `label` the words' key (t(label) where shown)
export const REASONS: ReadonlyArray<{ id: ReasonId; label: PlainKey; code: string }> = [
  { id: "cheating", label: "game.reason.cheating", code: "cheating" },
  { id: "harassment", label: "game.reason.harassment", code: "abusive_chat" },
  { id: "spam", label: "game.reason.spam", code: "other" },
  { id: "name", label: "game.reason.name", code: "offensive_name" },
  { id: "other", label: "game.reason.other", code: "other" }
];

// what the player may add; the room's /report keeps 200 characters of the reason
export const NOTE_MAX = 140;

// the platform takes reports (features.moderation)
export function reportsOffered(): boolean {
  const platform = platformState();
  return platform.mode === "platform" && platform.config.features.moderation === true;
}

export interface ReportTarget {
  name: string; // the player's name as the room shows it (colour codes stripped)
  gid?: string | null; // the authenticated global id, when the page knows it
}

// the running game's chat line and console, from App.tsx (only App talks to the client)
export interface RoomLine {
  say(line: string): boolean;
  listen(fn: (text: string) => void): () => void;
}

export type ReportResult = { ok: true; text: string } | { ok: false; text: string; limited: boolean };

// the text the moderators read: a preset without a code of its own leads with its name
function noteText(reason: ReasonId, note: string): string {
  const clean = note.replace(/\s+/g, " ").trim().slice(0, NOTE_MAX);
  if (reason === "spam") return clean ? "spam: " + clean : "spam";
  return clean;
}

// a name as one word of a chat command: the server compares filtered names, where blanks
// are underscores (ePlayerNetID::FilterName)
export function commandName(name: string): string {
  return name.trim().replace(/\s+/g, "_");
}

const ROOM_ANSWER_MS = 8000;

// the room's answers to /report (gPlatformModeration::HandleReport, the /msg lookup's
// $msg_nomatch / $msg_toomanymatches, $chat_command_unknown)
function roomAnswer(text: string): ReportResult | null {
  const line = text.trim();
  if (/^Thank you\. Your report about /.test(line)) return { ok: true, text: line };
  if (/^Please wait a minute before sending another report/.test(line)) return { ok: false, text: line, limited: true };
  if (/^You cannot report yourself/.test(line)) return { ok: false, text: line, limited: false };
  if (/^No matches were found that contained |^Too many matches found for the search term /.test(line))
    return { ok: false, text: line, limited: false };
  if (/^Unknown chat command "\/report"/.test(line)) return { ok: false, text: t("game.report.noRoomReports"), limited: false };
  if (/^Usage: \/report /.test(line)) return { ok: false, text: line, limited: false };
  return null;
}

function viaRoom(target: ReportTarget, code: string, note: string, room: RoomLine): Promise<ReportResult> {
  return new Promise((resolve) => {
    let done = false;
    const finish = (result: ReportResult) => {
      if (done) return;
      done = true;
      stop();
      window.clearTimeout(timer);
      resolve(result);
    };
    const stop = room.listen((text) => {
      const answer = roomAnswer(text);
      if (answer) finish(answer);
    });
    const timer = window.setTimeout(() => finish({ ok: false, text: t("game.report.noAnswer"), limited: false }), ROOM_ANSWER_MS);
    if (!room.say("/report " + commandName(target.name) + " " + code + (note ? " " + note : "")))
      finish({ ok: false, text: t("game.report.notInGame"), limited: false });
  });
}

async function viaPlatform(gid: string, code: string, note: string, serverId: string | null): Promise<ReportResult> {
  const platform = platformState();
  if (platform.mode !== "platform") return { ok: false, text: t("game.report.noPlatform"), limited: false };
  try {
    await createReport(platform.apiUrl, { target: gid, reason: code, ...(note ? { note } : {}), ...(serverId ? { serverId } : {}) });
    return { ok: true, text: t("game.report.sent") };
  } catch (e) {
    if (e instanceof PlatformError) {
      if (e.status === 429) return { ok: false, text: t("game.report.tooMany"), limited: true };
      if (e.code === "cannot_report_self") return { ok: false, text: t("game.report.self"), limited: false };
      if (e.code === "target_not_found") return { ok: false, text: t("game.report.unknownTarget"), limited: false };
    }
    return { ok: false, text: t("game.report.failed"), limited: false };
  }
}

export function sendReport(
  target: ReportTarget,
  reason: ReasonId,
  note: string,
  serverId: string | null,
  room: RoomLine
): Promise<ReportResult> {
  const preset = REASONS.find((r) => r.id === reason) ?? REASONS[REASONS.length - 1];
  const text = noteText(reason, note);
  if (target.gid) return viaPlatform(target.gid, preset.code, text, serverId);
  return viaRoom(target, preset.code, text, room);
}
