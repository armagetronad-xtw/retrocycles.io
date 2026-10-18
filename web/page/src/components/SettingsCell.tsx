// The settings cell: the status bar's last cell, the gear. One
// activation opens the Settings sheet hung from the bar at the right edge, a second puts it
// away; lit as pressed while the sheet is up, as the chip is for the Profile.
import { forwardRef } from "react";
import { Icon } from "./Icons";
import { t } from "../i18n";

interface SettingsCellProps {
  pressed: boolean;
  disabled: boolean;
  onOpen(): void;
}

export const SettingsCell = forwardRef<HTMLButtonElement, SettingsCellProps>(function SettingsCell({ pressed, disabled, onOpen }, ref) {
  return (
    <button
      className="status-cell status-icon st-cell"
      id="settings-cell"
      type="button"
      ref={ref}
      disabled={disabled}
      aria-haspopup="dialog"
      aria-controls="settings"
      aria-expanded={pressed}
      aria-label={t("settings.cell")}
      onClick={onOpen}
    >
      <Icon name="settings" className="status-glyph" />
    </button>
  );
});
