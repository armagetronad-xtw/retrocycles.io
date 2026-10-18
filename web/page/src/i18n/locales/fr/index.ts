// Français (fr): draft translation; corrections welcome.
//
// Glossary (one word per concept; the desktop game's own French where it has one, language/french.txt):
//   Quick Play       Partie rapide          Training        Entraînement
//   Server Browser   Serveurs               Ranked          Classé
//   room             salon                  server          serveur
//   round            round (the game's)     match           match (the game's)
//   rubber           pneus (the game's "état des pneus")
//   brake            frein (the game's)     glance          vue rapide (the game's)
//   spectate         regarder; spectator: spectateur (the game's mode spectateur)
//   cycle            moto                   trail           traînée
//   HUD              hud (the game's TDB is too obscure)
//   custom / smart / internal / external / free camera: perso / futée / interne / externe / libre
//   friend request   demande d'ami          guest           invité
//   account          compte                 handle, username: pseudo
//   id               ID (global ID: ID global)
//   level, XP        niveau, xp             kill            frag
//   sign in / out    se connecter / se déconnecter
//   mute             mettre en sourdine; unmute: rétablir le son
//   chat             chat; whisper: chuchoter
//   settings tabs    joueur, contrôles, caméra, graphismes, hud & chat, son, avancé
//   stock (value)    d'origine              reset           réinitialiser
//   core dumped      planté (the game's kill message, as a crash)
// Voice: "tu", terse; labels lower case (CSS sets them in capitals), sentences as written.
import type { Catalogue } from "../..";
import { common } from "./common";
import { home } from "./home";
import { connect } from "./connect";
import { settings } from "./settings";
import { profile } from "./profile";
import { social } from "./social";
import { game } from "./game";

export default { ...common, ...home, ...connect, ...settings, ...profile, ...social, ...game } satisfies Catalogue;
