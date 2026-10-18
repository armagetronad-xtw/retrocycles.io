// Português (Brasil) (pt-BR): draft translation; corrections welcome.
//
// GLOSSARY (keep these consistent; the desktop game's portugese.txt is barely translated, so
// these are the words Brazilian players use)
//   lightcycle / cycle   moto (de luz)            rubber        borracha
//   brake                freio / frear            glance        olhada / olhar
//   round                rodada                   match         partida
//   spectate             assistir                 spectator     espectador
//   room                 sala                     server        servidor
//   Quick Play           Partida rápida           Training      Treino
//   Server Browser       Servidores               Ranked        Ranqueada
//   HUD                  hud                      trail         rastro
//   rim (arena wall)     borda                    team          equipe
//   friend request       pedido de amizade        mute          silenciar
//   guest                convidado                account       conta
//   handle / username    usuário                  level         nível
//   XP                   xp                       sign in/out   entrar / sair
//   setting              ajuste                   stock (value) padrão
//   kill ("core dumped") deu core dump (kept)     instant chat  chat rápido
//   settings tabs        jogador, controles, câmera, gráficos, hud e chat, som, avançado
//   "você" throughout; labels lower case (CSS sets them in capitals).
import type { Catalogue } from "../..";
import { common } from "./common";
import { home } from "./home";
import { connect } from "./connect";
import { settings } from "./settings";
import { profile } from "./profile";
import { social } from "./social";
import { game } from "./game";

export default { ...common, ...home, ...connect, ...settings, ...profile, ...social, ...game } satisfies Catalogue;
