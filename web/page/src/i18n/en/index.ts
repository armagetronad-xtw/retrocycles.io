// The English catalogue: the source of every key (../index.ts). One module per part of the page,
// each with its own key prefix, merged here.
import { common } from "./common";
import { home } from "./home";
import { connect } from "./connect";
import { settings } from "./settings";
import { profile } from "./profile";
import { social } from "./social";
import { game } from "./game";

export const EN = { ...common, ...home, ...connect, ...settings, ...profile, ...social, ...game };
export { common, home, connect, settings, profile, social, game };
