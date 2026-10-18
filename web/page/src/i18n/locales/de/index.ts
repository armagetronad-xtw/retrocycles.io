// Deutsch (de): draft translation; corrections welcome.
//
// GLOSSARY (one term per concept; the desktop game's words from language/deutsch.txt where it has them)
//   Quick Play          Schnellspiel           Ranked          Gewertet
//   Server Browser      Serverliste            Training        Training
//   Custom Game         Eigenes Spiel          room            Raum
//   round / match       Runde / Match          spectate        zuschauen (Zuschauer)
//   rubber              Gummi (game)           brake           Bremse / bremsen (game)
//   glance left/right   Linksblick / Rechtsblick (game)       glance back   Rückspiegel (game)
//   glance (camera)     Blick                  speed           Tempo
//   custom camera       einstellbare Kamera (game "einstellbar"); server custom: serverbestimmt
//   internal/external   intern / extern; smart; free: frei (game)
//   instant chat        Kurzmeldung (game)     console         Konsole
//   ping charity        Ping-Spende (game)     prediction      Vorhersage (game)
//   HUD                 HUD                    lag-o-meter     Lag-O-Meter
//   cycle               Lightcycle             trail           Spur
//   friend request      Anfrage / Freundschaftsanfrage
//   guest / account     Gast / Konto           username/handle Benutzername
//   level / XP          Level / XP             sign in / out   anmelden / abmelden
//   mute / block        stumm(schalten) / blockieren
//   core dumped (kill)  erledigte              stock (default) Standard
//   Settings tabs       Spieler, Steuerung, Kamera, Grafik, HUD & Chat, Sound, erweitert
// Address: informal "du". Nouns keep their capital even where English is lower case (CSS sets caps).
import type { Catalogue } from "../..";
import { common } from "./common";
import { home } from "./home";
import { connect } from "./connect";
import { settings } from "./settings";
import { profile } from "./profile";
import { social } from "./social";
import { game } from "./game";

export default { ...common, ...home, ...connect, ...settings, ...profile, ...social, ...game } satisfies Catalogue;
