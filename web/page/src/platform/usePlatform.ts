// The platform state for rendering: "loading" until platformSession() settles, then
// whatever the store holds (the user changes with sign-in, sign-out, New player).
import { useEffect, useSyncExternalStore } from "react";
import { platformSession, platformState, subscribePlatform } from "./session";
import type { PlatformState } from "./types";

export function usePlatform(): PlatformState {
  useEffect(() => {
    void platformSession();
  }, []);
  return useSyncExternalStore(subscribePlatform, platformState);
}
