// Deutsch: the home texts. Draft translation; corrections welcome.
import type { Translation } from "../..";
import type { home as En } from "../../en/home";

export const home = {
  // ---- the play modes
  "home.mode.quick": "Schnellspiel",
  "home.mode.ranked": "Gewertet",
  "home.mode.browser": "Serverliste",
  "home.mode.leaderboards": "Bestenlisten",
  "home.mode.local": "Training",
  "home.mode.custom": "Eigenes Spiel",
  "home.mode.private": "Freund herausfordern",
  "home.livePlaying": "{live} spielen",
  "home.aria.modes": "Spielmodi",

  // ---- the note under the queue
  "home.note.noServer": "kein Server zum Verbinden: servers.json ist leer",
  "home.note.notLoaded": "der Client ist nicht geladen",
  "home.note.inviteNotHere": "der eingeladene Raum ist nicht auf dieser Seite",
  "home.note.finding": "Suche: {why}",
  "home.note.moving": "Umzug: {why}",
  "home.note.connectionLost": "Verbindung verloren",
  "home.note.notInGame": "nicht in einem Spiel",
  "home.note.loadFailed": "der Client konnte nicht laden: {error}",
  "home.sub.local": "eigenständig, ohne Server",

  // ---- the Console's notes
  "home.console.notLoaded": "nicht ausgeführt: der Client ist nicht geladen",
  "home.console.trainingNoRoom": "nicht gesendet: im Training gibt es keinen Raum für Befehle",
  "home.console.waitRound": "nicht gesendet: der Raum nimmt Befehle an, sobald deine erste Runde beginnt",
  "home.console.joinFirst": "nicht gesendet: tritt zuerst einem Spiel bei",
  "home.console.refused": "nicht ausgeführt: der Client hat die Zeile abgelehnt",

  // ---- chat and the player card
  "home.social.friendsOff": "Freunde gibt es auf dieser Plattform nicht",
  "home.social.needAccount": "erstelle ein Konto, um Freunde hinzuzufügen (Profil, oben rechts)",
  "home.social.noAccount": "{name} hat kein Konto und kann nicht hinzugefügt werden",
  "home.social.friendsNow": "du und {name} seid jetzt Freunde",
  "home.social.requestSent": "Freundschaftsanfrage an {name} gesendet",
  "home.social.mutedEverywhere": "{name} ist in jedem Raum stummgeschaltet",
  "home.social.unmuted": "{name} ist nicht mehr stummgeschaltet",
  "home.social.notMuted": "nicht stummgeschaltet: {why}",
  "home.social.notSent": "nicht gesendet: {why}",
  "home.social.noPlatform": "nicht gesendet: keine Plattform",
  "home.social.aFriend": "ein Freund",

  // ---- Discord
  "home.discord.tapToReturn": "Tippen, um zurückzukehren",
  "home.discord.presencePlaying": "Schnellspiel",
  "home.discord.presenceChoosing": "Wählt ein Spiel",
  "home.discord.shareMessage": "Fahr mit mir in Retrocycles",

  // ---- the embed
  "home.embed.play": "Spielen",
  "home.embed.loading": "Lädt",
  "home.embed.say": "Lightcycles im Browser. Ohne Download.",
  "home.embed.together": "Du bist hier {together}. Alle, die SPIELEN drücken, fahren in einem Raum.",
  "home.discord.together": "Du bist hier {together}. SCHNELLSPIEL bringt euch alle in einen Raum.",
  "home.embed.withOne": "mit {a}",
  "home.embed.withTwo": "mit {a} und {b}",
  "home.embed.withMore": { one: "mit {a}, {b} und {n} weiteren", other: "mit {a}, {b} und {n} weiteren" },
  "home.embed.openFull": "Ganzes Spiel öffnen",
  "home.embed.fullscreen": "Vollbild",
  "home.embed.leaveFullscreen": "Vollbild beenden",

  // ---- the documents line
  "home.docs.aria": "Über uns und Richtlinien",
  "home.docs.about": "über",
  "home.docs.conduct": "Verhalten",
  "home.docs.privacy": "Datenschutz",
  "home.docs.terms": "Nutzung",
  "home.docs.news": "neuigkeiten",
  "home.docs.conductLine": "im Chat hier gilt der {link}",
  "home.docs.codeOfConduct": "Verhaltenskodex",
  "home.community.name": "The Grid",
  "home.community.sub": "Community-Discord",
  "home.community.online": "{online} online",
  "home.community.members": "{members} Mitglieder",
  "home.community.aria": "The Grid, der Community-Discord-Server (öffnet in einem neuen Tab)",

  // ---- iPhone Safari (Safari's own German button names)
  "home.hint.homeScreen": "Vollbild: tippe auf {share}, dann {add}",
  "home.hint.share": "Teilen",
  "home.hint.addToHome": "Zum Home-Bildschirm",
  "home.hint.dismiss": "Ausblenden",

  // ---- the cookie question
  "home.cookies.aria": "Cookies",
  "home.cookies.ask": "Dürfen wir Nutzungsstatistiken erheben und Google-Analytics-Cookies setzen, um zu sehen, wie Leute das Spiel finden und spielen?",
  "home.cookies.privacy": "Datenschutz",
  "home.cookies.decline": "Ablehnen",
  "home.cookies.accept": "Annehmen",
  // ---- the bell (NewsBell.tsx): the cell (aria-label, title) and its panel
  "home.news.cell": "Neuigkeiten",
  "home.news.cellUnread": { one: "Neuigkeiten, {n} neuer Beitrag", other: "Neuigkeiten, {n} neue Beiträge" },
  "home.news.title": "neuigkeiten",
  "home.news.all": "alle neuigkeiten",
  "home.news.new": "neu",
  "home.news.empty": "Noch keine Neuigkeiten.",
  "home.news.asking": "Neuigkeiten werden geladen.",
  "home.news.failed": "Die Neuigkeiten wurden nicht geladen. Beim nächsten Öffnen der Glocke wird es erneut versucht.",
  "home.news.opens": "Öffnet in einem neuen Tab."
} satisfies Translation<typeof En>;
