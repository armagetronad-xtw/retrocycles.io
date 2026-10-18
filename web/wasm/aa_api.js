// aa_api.js - the browser client's command/query API as one plain JS object.
// No build step, no framework: include it before client.js and call
// AA.prepare(Module) on the Emscripten Module object before the script that
// loads the client runs. Contract and examples: API.md next to this file.
//
//   <script src="aa_api.js"></script>
//   <script>
//     var Module = { canvas: ..., arguments: ["--userdatadir", "/aa/user"] };
//     var aa = AA.prepare(Module);
//     aa.on("ready", function () { aa.connect("ws://host:4535/"); });
//     aa.on("chat", function (e) { console.log(e.text); });
//   </script>
//   <script src="client.js"></script>
//
// Everything crosses the wasm boundary as UTF-8 strings and JSON documents
// (cwrap over the aa_* exports of src/tron/gBrowserApi.cpp); events arrive
// through Module.__aaEmit, which the game calls from EM_JS.
var AA = (function () {
    "use strict";

    // the exports and their cwrap signatures
    var exportTable = {
        connect: ["aa_connect", "number", ["string", "string"]],
        disconnect: ["aa_disconnect", null, []],
        startLocal: ["aa_start_local", "number", []],
        stopLocal: ["aa_stop_local", null, []],
        scene: ["aa_scene", "number", ["number"]],
        sceneStop: ["aa_scene_stop", null, []],
        getState: ["aa_get_state", "string", []],
        getPlayers: ["aa_get_players", "string", []],
        getCycle: ["aa_get_cycle", "string", []],
        turn: ["aa_turn", "number", ["number", "number"]],
        brake: ["aa_brake", "number", ["number"]],
        chat: ["aa_chat", "number", ["string"]],
        setChatting: ["aa_set_chatting", null, ["number"]],
        setChatUtf8: ["aa_set_chat_utf8", null, ["number"]],
        setPageScores: ["aa_set_page_scores", null, ["number"]],
        console: ["aa_console", "number", ["string"]],
        getConfig: ["aa_get_config", "string", ["string"]],
        setConfig: ["aa_set_config", "number", ["string", "string"]],
        listConfig: ["aa_list_config", "string", ["string"]],
        saveConfig: ["aa_save_config", null, []],
        getBindings: ["aa_get_bindings", "string", []],
        getActions: ["aa_get_actions", "string", []],
        setBinding: ["aa_set_binding", "number", ["string", "number", "number"]],
        unbind: ["aa_unbind", "number", ["number"]],
        keysymFromCode: ["aa_keysym_from_code", "number", ["string"]],
        codeFromKeysym: ["aa_code_from_keysym", "string", ["number"]],
        getVotes: ["aa_get_votes", "string", []],
        vote: ["aa_vote", "number", ["number", "number"]],
        getTeams: ["aa_get_teams", "string", []],
        joinTeam: ["aa_join_team", "number", ["number"]],
        createTeam: ["aa_create_team", "number", []],
        setSpectating: ["aa_set_spectating", "number", ["number"]],
        login: ["aa_login", "number", ["string"]],
        auth: ["aa_auth", "number", ["string", "string"]],
        authCancel: ["aa_auth_cancel", "number", []],
        platformLogin: ["aa_platform_login", "number", ["string"]],
        exportConfig: ["aa_export_config", "string", []],
        importConfig: ["aa_import_config", "number", ["string"]]
    };

    // a signalling URL from servers.json or a default: a full ws(s):// URL is
    // returned as is, a path ("/signal") is resolved against this page's host
    // (wss on https pages, ws otherwise)
    function resolveSignalUrl(url) {
        url = String(url || "");
        if (/^wss?:\/\//.test(url))
            return url;
        if (typeof location === "undefined")
            return url;
        var scheme = location.protocol === "https:" ? "wss://" : "ws://";
        if (url.charAt(0) !== "/")
            url = "/" + url;
        return scheme + location.host + url;
    }

    // the resource proxy base (API.md "fetching"): "" for direct fetches, a
    // path ("/resource/") resolved against this page's origin, or a full
    // http(s):// URL; always ends with "/"
    function resolveResourceProxy(base) {
        base = String(base || "");
        if (!base)
            return "";
        if (!/^https?:\/\//.test(base) && typeof location !== "undefined" && /^https?:$/.test(location.protocol)) {
            if (base.charAt(0) !== "/")
                base = "/" + base;
            base = location.origin + base;
        }
        if (base.charAt(base.length - 1) !== "/")
            base += "/";
        return base;
    }

    // servers.json: [{name, url, description}] served next to the page (the
    // stand-in for a lobby, API.md); resolves to [] when it is missing
    function loadServers(url) {
        return fetch(url || "servers.json", { cache: "no-cache" }).then(function (response) {
            if (!response.ok)
                throw new Error("HTTP " + response.status);
            return response.json();
        }).then(function (list) {
            if (!Array.isArray(list))
                return [];
            return list.filter(function (s) { return s && s.url && s.name; }).map(function (s) {
                var entry = { name: String(s.name), url: resolveSignalUrl(s.url), description: s.description ? String(s.description) : "" };
                // platform login (API.md "Platform login"): optional, passed through
                if (typeof s.serverId === "string" && s.serverId)
                    entry.serverId = s.serverId;
                if (s.platformLogin === true)
                    entry.platformLogin = true;
                return entry;
            });
        }).catch(function () {
            return [];
        });
    }

    // ICE servers for connect() (API.md "ICE servers"): the RTCIceServer entries that
    // are usable - urls a string or a list of stun:/stuns:/turn:/turns: URLs, username
    // and credential strings - copied, anything else dropped. [] for no usable entry.
    function normalizeIceServers(list) {
        if (!Array.isArray(list))
            return [];
        var out = [];
        list.forEach(function (s) {
            if (!s || typeof s !== "object")
                return;
            var urls = (Array.isArray(s.urls) ? s.urls : [s.urls]).filter(function (u) {
                return typeof u === "string" && /^(stuns?|turns?):/.test(u);
            });
            if (!urls.length)
                return;
            var entry = { urls: urls };
            if (typeof s.username === "string")
                entry.username = s.username;
            if (typeof s.credential === "string")
                entry.credential = s.credential;
            out.push(entry);
        });
        return out;
    }

    // the JSON text aa_connect takes: "" (no ICE servers, host candidates only) or
    // {"iceServers": [...], "iceTransportPolicy": "relay", "transport": "websocket"};
    // it carries TURN credentials, so it is handed to the wasm and never logged
    function iceConfig(options) {
        if (!options)
            return "";
        var servers = normalizeIceServers(options.iceServers);
        var relay = options.iceTransportPolicy === "relay";
        var websocket = options.transport === "websocket";
        if (!servers.length && !relay && !websocket)
            return "";
        var config = { iceServers: servers };
        if (relay)
            config.iceTransportPolicy = "relay";
        if (websocket)
            config.transport = "websocket";
        return JSON.stringify(config);
    }

    // the game's color codes: "0xRRGGBB" and "0xRESETT"
    var colorCode = /0x(?:[0-9a-fA-F]{6}|RESETT)/g;

    function stripColors(text) {
        return String(text || "").replace(colorCode, "");
    }

    // [[text, "#rrggbb" or null], ...] for a colored line
    function colorSpans(text) {
        var spans = [];
        var color = null;
        var last = 0;
        var match;
        colorCode.lastIndex = 0;
        text = String(text || "");
        while ((match = colorCode.exec(text)) !== null) {
            if (match.index > last)
                spans.push([text.substring(last, match.index), color]);
            color = match[0] === "0xRESETT" ? null : "#" + match[0].substring(2).toLowerCase();
            last = match.index + match[0].length;
        }
        if (last < text.length)
            spans.push([text.substring(last), color]);
        return spans;
    }

    // mounts the persistent user directories (IDBFS on /aa/user/var for the
    // configuration and logs, /aa/user/resource for downloaded maps) before
    // the runtime starts; the run waits for the first sync from IndexedDB
    var persistentDirs = ["/aa/user/var", "/aa/user/resource"];
    function mountPersistence(Module, api) {
        var FS = Module.FS;
        var IDBFS = Module.IDBFS;
        if (!FS) {
            api.persistence = "none";
            return;
        }
        persistentDirs.forEach(function (dir) {
            try {
                FS.mkdirTree(dir);
            } catch (e) {
            }
        });
        api.persistence = "memfs";
        if (!IDBFS || typeof indexedDB === "undefined")
            return;
        try {
            persistentDirs.forEach(function (dir) { FS.mount(IDBFS, {}, dir); });
        } catch (e) {
            return;
        }
        Module.addRunDependency("aa-persist");
        FS.syncfs(true, function (err) {
            if (err)
                console.log("BROWSER: persist load failed: " + err);
            else
                api.persistence = "idbfs";
            Module.removeRunDependency("aa-persist");
        });
    }

    function prepare(Module, options) {
        options = options || {};
        var listeners = {};
        var fns = {};
        var api = {
            Module: Module,
            ready: false,
            persistence: "none",
            stripColors: stripColors,
            colorSpans: colorSpans
        };

        function dispatch(type, data) {
            var list = listeners[type];
            if (list)
                list.slice().forEach(function (fn) {
                    try {
                        fn(data, type);
                    } catch (e) {
                        console.log("aa_api: listener for " + type + " threw " + e);
                    }
                });
            var all = listeners["*"];
            if (all)
                all.slice().forEach(function (fn) {
                    try {
                        fn(data, type);
                    } catch (e) {
                        console.log("aa_api: listener for * threw " + e);
                    }
                });
        }

        api.on = function (type, fn) {
            (listeners[type] = listeners[type] || []).push(fn);
            return function () { api.off(type, fn); };
        };
        api.off = function (type, fn) {
            var list = listeners[type];
            if (!list)
                return;
            var i = list.indexOf(fn);
            if (i >= 0)
                list.splice(i, 1);
        };

        // the game's events: type + JSON text. "ready" comes from main() once
        // the configuration and the language files are loaded: only then may
        // the queries be called (onRuntimeInitialized is too early: it runs
        // before main()).
        Module.__aaEmit = function (type, json) {
            var data;
            try {
                data = JSON.parse(json);
            } catch (e) {
                data = { raw: json };
            }
            if (type === "ready") {
                api.ready = true;
                data.persistence = api.persistence;
                followReducedMotion();
            }
            dispatch(type, data);
        };

        // the attract scene's moving cameras hold still for prefers-reduced-motion
        // (SCENE_REDUCED_MOTION, API.md), following the preference when it changes
        function followReducedMotion() {
            if (typeof window === "undefined" || !window.matchMedia)
                return;
            var query = window.matchMedia("(prefers-reduced-motion: reduce)");
            var apply = function () {
                try { api.setConfig("SCENE_REDUCED_MOTION", query.matches ? "1" : "0"); } catch (e) { /* not ready */ }
            };
            apply();
            if (query.addEventListener)
                query.addEventListener("change", apply);
        }

        function need() {
            if (!api.ready)
                throw new Error("aa_api: the runtime is not ready yet (wait for the ready event)");
        }
        function json(text) {
            return text ? JSON.parse(text) : null;
        }
        function keysym(key) {
            if (typeof key === "number")
                return key;
            return fns.keysymFromCode(String(key));
        }

        // options.loginToken: a platform login token for this connection (API.md
        // "Platform login"); handed to the wasm right after the handshake starts.
        // options.iceServers / options.iceTransportPolicy: STUN/TURN servers for this
        // connection's RTCPeerConnection (API.md "ICE servers").
        // options.transport: "websocket" sends the game packets over the signalling
        // socket instead of WebRTC (API.md "WebSocket transport")
        api.connect = function (url, options) {
            need();
            if (fns.connect(String(url), iceConfig(options)) !== 1)
                return false;
            if (options && options.loginToken)
                fns.platformLogin(String(options.loginToken));
            return true;
        };
        // the same for a session that is already running (connecting/syncing/playing)
        api.platformLogin = function (token) { need(); return fns.platformLogin(String(token || "")) === 1; };
        api.disconnect = function () { need(); fns.disconnect(); };

        // the desktop client's local game (standalone, AI opponents) and the
        // attract scene, both over the same session state machine (API.md)
        api.startLocal = function () { need(); return fns.startLocal() === 1; };
        api.stopLocal = function () { need(); fns.stopLocal(); };
        api.scene = function (state) { need(); return fns.scene(Number(state)) === 1; };
        api.sceneStop = function () { need(); fns.sceneStop(); };

        api.getState = function () { need(); return json(fns.getState()); };
        api.getPlayers = function () { need(); return json(fns.getPlayers()); };
        // the cycle the camera follows, as the HUD reads it (API.md "getCycle"); cheap, poll it per frame
        api.getCycle = function () { need(); return json(fns.getCycle()); };
        // a turn as the turn keys make it (API.md "turn"): dir < 0 left, else right; count 2 a double turn
        // the brake held (true) or let go (false), as the brake key does (API.md "brake")
        api.brake = function (on) { need(); return fns.brake(on ? 1 : 0) === 1; };
        api.turn = function (dir, count) { need(); return fns.turn(Number(dir) < 0 ? -1 : 1, Number(count) === 2 ? 2 : 1); };
        api.chat = function (text) { need(); return fns.chat(String(text)) === 1; };
        api.teamChat = function (text) { return api.chat("/team " + text); };
        api.setChatting = function (on) { need(); fns.setChatting(on ? 1 : 0); };
        api.setChatUtf8 = function (on) { need(); fns.setChatUtf8(on ? 1 : 0); };
        api.setPageScores = function (on) { need(); fns.setPageScores(on ? 1 : 0); };
        api.console = function (line) { need(); return fns.console(String(line)) === 1; };
        api.getConfig = function (name) { need(); return json(fns.getConfig(String(name))); };
        api.setConfig = function (name, value) { need(); return fns.setConfig(String(name), String(value)) === 1; };
        api.listConfig = function (prefix) { need(); return json(fns.listConfig(prefix ? String(prefix) : "")); };
        api.saveConfig = function () { need(); fns.saveConfig(); };
        api.getActions = function () { need(); return json(fns.getActions()); };
        api.getBindings = function () {
            need();
            var bindings = json(fns.getBindings());
            bindings.forEach(function (b) { b.code = fns.codeFromKeysym(b.keysym) || null; });
            return bindings;
        };
        // key: a DOM KeyboardEvent.code ("ArrowLeft", "KeyA") or an SDL 1.2 keysym number
        api.setBinding = function (action, player, key) {
            need();
            var sym = keysym(key);
            if (!sym) {
                dispatch("error", { text: "setBinding: no keysym for " + key });
                return false;
            }
            return fns.setBinding(String(action), Number(player), sym) === 1;
        };
        api.unbind = function (key) { need(); return fns.unbind(keysym(key)) === 1; };
        api.keysymFromCode = function (code) { need(); return fns.keysymFromCode(String(code)); };
        api.codeFromKeysym = function (sym) { need(); return fns.codeFromKeysym(Number(sym)) || null; };
        api.getVotes = function () { need(); return json(fns.getVotes()); };

        // audio: the shim (sdl_shim.cpp) publishes Module.__aaAudio with the
        // AudioContext once the game opened its device; browsers keep it
        // suspended until a user gesture, so it is resumed here from the first
        // key/pointer event on the page or by unlockAudio() from a handler
        api.unlockAudio = function () {
            var audio = Module.__aaAudio;
            if (audio && audio.unlock)
                audio.unlock();
        };
        // "none" (no device), "suspended" (waiting for a gesture), "running", "closed"
        api.getAudioState = function () {
            var audio = Module.__aaAudio;
            return audio && audio.context ? audio.context.state : "none";
        };
        if (typeof window !== "undefined") {
            var gesture = function () { api.unlockAudio(); };
            window.addEventListener("keydown", gesture, true);
            window.addEventListener("pointerdown", gesture, true);
        }
        api.vote = function (id, accept) { need(); return fns.vote(Number(id), accept ? 1 : 0) === 1; };

        // teams (the native team menu): the list with joinable flags, join
        // by id, a new team, the spectator switch (leaveTeam = spectate)
        api.getTeams = function () { need(); return json(fns.getTeams()); };
        api.joinTeam = function (id) { need(); return fns.joinTeam(Number(id)) === 1; };
        api.createTeam = function () { need(); return fns.createTeam() === 1; };
        api.setSpectating = function (on) { need(); return fns.setSpectating(on ? 1 : 0) === 1; };
        api.leaveTeam = function () { return api.setSpectating(true); };

        // authentication: login(globalId) asks the server to start a login
        // (the native menu's "Login" item), the server's password request
        // arrives as the authRequest event, auth()/authCancel() answer it.
        // The password goes straight into the wasm scrambler; it is not kept,
        // logged or emitted here.
        api.login = function (globalId) { need(); return fns.login(globalId ? String(globalId) : "") === 1; };
        api.auth = function (username, password) { need(); return fns.auth(String(username || ""), String(password || "")) === 1; };
        api.authCancel = function () { need(); return fns.authCancel() === 1; };

        // settings as text: user.cfg as the game saves it (bindings included,
        // password hashes left out) and the reverse, applied line by line and saved
        api.exportConfig = function () { need(); return fns.exportConfig(); };
        api.importConfig = function (text) { need(); return fns.importConfig(String(text || "")) === 1; };

        // the resource proxy (API.md "fetching"): the game reads
        // Module.__aaResourceProxy at every fetch, so this works any time,
        // before the runtime is ready included
        api.setResourceProxy = function (base) {
            Module.__aaResourceProxy = resolveResourceProxy(base);
            return Module.__aaResourceProxy;
        };
        api.getResourceProxy = function () {
            return Module.__aaResourceProxy || "";
        };
        api.setResourceProxy(options.resourceProxy);

        // wiring into the Module lifecycle
        Module.preRun = Module.preRun || [];
        if (options.persist !== false)
            Module.preRun.push(function () { mountPersistence(Module, api); });

        var previousInit = Module.onRuntimeInitialized;
        Module.onRuntimeInitialized = function () {
            Object.keys(exportTable).forEach(function (name) {
                var entry = exportTable[name];
                fns[name] = Module.cwrap(entry[0], entry[1], entry[2]);
            });
            if (previousInit)
                previousInit();
        };

        var previousExit = Module.onExit;
        Module.onExit = function (code) {
            api.ready = false;
            dispatch("exit", { code: code });
            if (previousExit)
                previousExit(code);
        };

        return api;
    }

    return { prepare: prepare, stripColors: stripColors, colorSpans: colorSpans,
             resolveSignalUrl: resolveSignalUrl, loadServers: loadServers,
             normalizeIceServers: normalizeIceServers };
})();
