// Polski (pl): draft translation; corrections welcome.
//
// Glossary (one word per concept; the desktop game's Polish where it has one):
//   rubber        guma (HUD: Guma)            brake         hamulec, hamuj (verb)
//   glance        spojrzenie, spójrz w lewo   turn          skręt, skręć
//   round         runda                       match         mecz
//   spectate      obserwuj; spectator: obserwator; spectators: obserwatorzy
//   room          pokój                       server        serwer
//   Quick Play    Szybka gra                  Training      Trening
//   Server Browser Lista serwerów             Ranked        Rankingowa
//   kill (the game's "core dumped")  zniszczył (as the desktop game's player_win_frag)
//   rim           krawędź                     cycle         motor (świetlny motor)
//   trail         ślad                        HUD           hud
//   friend request zaproszenie (do znajomych) friends       znajomi
//   guest         gość                        account       konto
//   handle / username  nazwa użytkownika      id            id / ID (name@authority)
//   level         poziom                      XP            xp
//   sign in / out zaloguj się / wyloguj się   save your progress  zapisz postępy
//   mute / unmute wycisz / wyłącz wyciszenie  block         zablokuj
//   instant chat  szybki czat (game: Szybki Chat)  ping charity  zrównanie pingu
//   camera modes  własna, własna serwera, wewnętrzna, sprytna, zewnętrzna, wolna
//   settings tabs gracz, sterowanie, kamera, grafika, hud i czat, dźwięk, zaawansowane
//   stock (default value)  domyślne           reset         resetuj
// Voice: informal "ty", imperatives on buttons (dołącz, zapisz, wyślij); labels lower case
// (CSS sets them in capitals). Past-tense event verbs use the masculine form (gracz).
import type { Catalogue } from "../..";
import { common } from "./common";
import { home } from "./home";
import { connect } from "./connect";
import { settings } from "./settings";
import { profile } from "./profile";
import { social } from "./social";
import { game } from "./game";

export default { ...common, ...home, ...connect, ...settings, ...profile, ...social, ...game } satisfies Catalogue;
