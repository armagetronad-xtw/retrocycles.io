// Español (es): draft translation; corrections welcome.
// Neutral international Spanish, "tú", terse game UI. Lower-case labels are set in capitals by CSS.
//
// GLOSSARY (one word per concept; the desktop game's language/spanish.txt where it has one)
//   rubber ............ rubber (as the desktop game: "medidor de rubber")
//   brake ............. freno / frenar
//   glance ............ vistazo (the camera); the keys: vista izquierda / derecha / trasera
//   round / match ..... ronda / partida
//   spectate .......... mirar (button); espectador (the mode, the game's word)
//   room / server ..... sala / servidor
//   Quick Play ........ Partida rápida
//   Server Browser .... Lista de servidores
//   Training .......... Entrenamiento
//   Ranked ............ Clasificatoria
//   HUD ............... hud
//   cycle ............. moto
//   trail ............. estela; wall: muro; rim: borde
//   core dumped ....... colgar (the game: "provoca el cuelgue de")
//   friend request .... solicitud de amistad / solicitud
//   guest / account ... invitado / cuenta
//   handle / username . usuario
//   id ................ ID / id
//   level / XP ........ nivel / xp
//   sign in / out ..... iniciar sesión / cerrar sesión
//   sign up ........... crear una cuenta / registrarse
//   mute / block ...... silenciar / bloquear
//   report ............ denunciar / denuncia
//   ban ............... expulsión / expulsado
//   stock (value) ..... original
//   settings tabs ..... jugador, controles, cámara, gráficos, hud y chat, sonido, avanzado
import type { Catalogue } from "../..";
import { common } from "./common";
import { home } from "./home";
import { connect } from "./connect";
import { settings } from "./settings";
import { profile } from "./profile";
import { social } from "./social";
import { game } from "./game";

export default { ...common, ...home, ...connect, ...settings, ...profile, ...social, ...game } satisfies Catalogue;
