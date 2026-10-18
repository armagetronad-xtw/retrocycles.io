// What the page says for the platform's error codes (platform-contract/README.md
// section 1). Short, in the page's voice, set in the display face as a reversed cell;
// never more than the API says (no hint whether an account exists: sign-in failures
// are all "wrong email, username or password", as the platform answers them). The codes
// map to catalogue keys (i18n/en/profile.ts), translated when the text is asked for.
import { PlatformError } from "../../platform/client";
import { ago as agoText, t, type PlainKey } from "../../i18n";

const FIELD_KEY: Record<string, PlainKey> = {
  email_invalid: "profile.error.email_invalid",
  email_taken: "profile.error.email_taken",
  handle_required: "profile.error.handle_required",
  handle_invalid: "profile.error.handle_invalid",
  handle_reserved: "profile.error.handle_reserved",
  handle_taken: "profile.error.handle_taken",
  name_too_long: "profile.error.name_too_long",
  password_required: "profile.error.password_required",
  password_too_short: "profile.error.password_too_short",
  password_too_long: "profile.error.password_too_long",
  password_breached: "profile.error.password_breached",
  password_contains_name: "profile.error.password_contains_name",
  current_password_invalid: "profile.error.current_password_invalid",
  label_invalid: "profile.error.label_invalid"
};

const CODE_KEY: Record<string, PlainKey> = {
  invalid_credentials: "profile.error.invalid_credentials",
  invalid_token: "profile.error.invalid_token",
  already_signed_in: "profile.error.already_signed_in",
  handle_already_set: "profile.error.handle_already_set",
  last_sign_in_method: "profile.error.last_sign_in_method",
  not_linked: "profile.error.not_linked",
  session_not_found: "profile.error.session_not_found",
  no_pending_link: "profile.error.no_pending_link",
  provider_in_use: "profile.error.provider_in_use",
  concurrency: "profile.error.concurrency",
  guest: "profile.error.guest",
  linking_off: "profile.error.linking_off",
  game_passwords_off: "profile.error.game_passwords_off",
  too_many_game_passwords: "profile.error.too_many_game_passwords",
  game_password_not_found: "profile.error.game_password_not_found",
  csrf_header_missing: "profile.error.csrf_header_missing",
  origin_not_allowed: "profile.error.origin_not_allowed",
  // linking an old id with its password (POST /v1/me/link-checks, /v1/me/linked-ids)
  link_by_password_off: "profile.error.link_by_password_off",
  not_global_id: "profile.error.not_global_id",
  own_authority: "profile.error.own_authority",
  already_linked: "profile.error.already_linked",
  linked_elsewhere: "profile.error.linked_elsewhere",
  too_many: "profile.error.too_many",
  authority_unreachable: "profile.error.authority_unreachable",
  authority_invalid: "profile.error.authority_invalid",
  authority_unsupported: "profile.error.authority_unsupported",
  unknown_user: "profile.error.unknown_user",
  wrong_password: "profile.error.wrong_password",
  check_expired: "profile.error.check_expired",
  hash_invalid: "profile.error.hash_invalid"
};

// the popup's `error` values
const EXTERNAL_KEY: Record<string, PlainKey> = {
  access_denied: "profile.external.access_denied",
  provider_error: "profile.external.provider_error",
  external_failed: "profile.external.external_failed",
  not_signed_in: "profile.external.not_signed_in",
  provider_in_use: "profile.error.provider_in_use",
  unknown_provider: "profile.external.unknown_provider",
  invalid_mode: "profile.external.external_failed"
};

// the text for the popup's `error` (anything unknown: sign-in did not complete)
export function externalText(code: string | undefined): string {
  return t(EXTERNAL_KEY[code ?? ""] ?? "profile.external.external_failed");
}

// the text for a failed call as a whole (field errors are shown at their fields)
export function problemText(e: unknown): string {
  if (e instanceof PlatformError) {
    if (e.status === 0) return t("profile.problem.noResponse");
    if (e.status === 429) return t("profile.problem.tooMany");
    if (e.code && CODE_KEY[e.code]) return t(CODE_KEY[e.code]);
    if (e.status === 401) return t("profile.problem.signedOut");
    if (Object.keys(e.errors).length) return t("profile.problem.checkFields");
    return t("profile.problem.status", { status: e.status });
  }
  return t("profile.problem.unknown");
}

// field -> text, from a 400's `errors` (the first code per field)
export function fieldTexts(e: unknown): Record<string, string> {
  const out: Record<string, string> = {};
  if (!(e instanceof PlatformError)) return out;
  for (const [field, codes] of Object.entries(e.errors)) {
    const code = codes[0];
    if (code) out[field] = FIELD_KEY[code] ? t(FIELD_KEY[code]) : code.replace(/_/g, " ");
  }
  return out;
}

// the contract's handle rule, checked while typing (the platform decides on submit:
// reserved and taken handles are only known there)
export function handleProblem(handle: string): string | null {
  if (!handle) return null;
  if (!/^[a-z0-9_-]+$/.test(handle)) return t("profile.error.handle_invalid");
  if (handle.startsWith("g-")) return t("profile.check.guestPrefix");
  if (handle.length < 2) return t("profile.check.atLeast2");
  if (handle.length > 24) return t("profile.check.atMost24");
  return null;
}

// "3 minutes ago", for the sessions list (i18n's, in the player's language)
export function ago(unixSeconds: number): string {
  return agoText(unixSeconds);
}
