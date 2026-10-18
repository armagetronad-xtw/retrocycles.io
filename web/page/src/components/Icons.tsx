// The page's icons: one line set, 24-unit grid, 1.75 stroke, square caps and mitred joins
// so they turn like the trails do. Paths adapted from Lucide (ISC, some icons MIT via Feather;
// full notice in public/icons/LICENSE-lucide.txt); the play mark is filled.
import type { ReactElement, SVGProps } from "react";

export type IconName = "play" | "trophy" | "list" | "users" | "wrench" | "settings" | "bell" | "plus" | "minus" | "x" | "message" | "chevron" | "arrow-left" | "arrow-right" | "arrow-up" | "arrow-down" | "external" | "refresh" | "maximize" | "minimize" | "info" | "bookmark" | "link";

const PATHS: Record<IconName, ReactElement> = {
  play: <path d="M5 2.5v19l16-9.5z" fill="currentColor" stroke="none" />,
  trophy: (
    <>
      <path d="M8 4h8v6a4 4 0 0 1-8 0z" />
      <path d="M8 6H4v2a4 4 0 0 0 4 4M16 6h4v2a4 4 0 0 1-4 4" />
      <path d="M12 14v4M8 20h8" />
    </>
  ),
  list: (
    <>
      <path d="M9 6h12M9 12h12M9 18h12" />
      <path d="M3 6h2M3 12h2M3 18h2" />
    </>
  ),
  users: (
    <>
      <path d="M16 20v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2" />
      <circle cx="9" cy="7" r="4" />
      <path d="M22 20v-2a4 4 0 0 0-3-3.87M16 3.13a4 4 0 0 1 0 7.75" />
    </>
  ),
  wrench: (
    <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94z" />
  ),
  settings: (
    <>
      <path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z" />
      <circle cx="12" cy="12" r="3" />
    </>
  ),
  bell: (
    <>
      <path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9" />
      <path d="M10.3 21a1.94 1.94 0 0 0 3.4 0" />
    </>
  ),
  plus: <path d="M12 5v14M5 12h14" />,
  minus: <path d="M5 12h14" />,
  x: <path d="M6 6l12 12M18 6L6 18" />,
  message: <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z" />,
  chevron: <path d="M6 9l6 6 6-6" />,
  "arrow-left": <path d="M19 12H5M11 6l-6 6 6 6" />,
  "arrow-right": <path d="M5 12h14M13 6l6 6-6 6" />,
  "arrow-up": <path d="M12 19V5M6 11l6-6 6 6" />,
  "arrow-down": <path d="M12 5v14M6 13l6 6 6-6" />,
  external: <path d="M7 17L17 7M8 7h9v9" />,
  maximize: <path d="M8 3H3v5M16 3h5v5M8 21H3v-5M16 21h5v-5" />,
  minimize: <path d="M3 8h5V3M21 8h-5V3M3 16h5v5M21 16h-5v5" />,
  // Lucide's info without its circle: the square cell it sits in is the frame
  info: <path d="M12 10.5v8M12 5.5h.01" strokeWidth={2.25} />,
  // Lucide's bookmark with square corners; a page fills it (fill: currentColor) when it is on
  bookmark: <path d="M19 21l-7-4.5L5 21V3h14z" />,
  // Lucide's link-2: Direct Connect (a server by its address)
  link: <path d="M9 17H7A5 5 0 0 1 7 7h2M15 7h2a5 5 0 1 1 0 10h-2M8 12h8" />,
  refresh: (
    <>
      <path d="M21 12a9 9 0 0 1-15.5 6.2L3 15.5M3 12a9 9 0 0 1 15.5-6.2L21 8.5" />
      <path d="M21 3v5.5h-5.5M3 21v-5.5h5.5" />
    </>
  )
};

export function Icon({ name, ...rest }: { name: IconName } & SVGProps<SVGSVGElement>) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.75}
      strokeLinecap="square"
      strokeLinejoin="miter"
      aria-hidden="true"
      focusable="false"
      {...rest}
    >
      {PATHS[name]}
    </svg>
  );
}
