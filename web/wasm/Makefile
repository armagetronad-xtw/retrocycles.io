# web/wasm/Makefile - Emscripten builds of the browser client.
#
# Run it through emmake inside the emsdk image (web/deploy/README.md):
#   emmake make -C web/wasm -j$(nproc) [client|client-opt|dist]
#
# Two targets share the source lists of src/Makefile.am:
#   client.js      the full client: every layer, no SDL (sdl_shim.cpp), GL through
#                  src/render/rGLES2.cpp on WebGL, libxml2 from deps.sh, data files
#                  preloaded into a MEMFS tree under /aa; -O1 -g -sASSERTIONS
#   client_opt.js  the same client at -O2 without -g/assertions (release shape)
# Sources that need libcurl (tResourceManager: guarded), libdatachannel
# (nTransportRtc) or a real socket (nTransportLoopback, tests only) are left out;
# the browser transport is nTransportRtcJs.

TOP     := $(abspath ../..)
SRC     := $(TOP)/src
BUILD   := build
DEPS    ?= /work/deps/wasm

# --- sources ----------------------------------------------------------------

# libtools.a
TOOLS_SOURCES := \
	tArray.cpp tCallback.cpp tColor.cpp tConfiguration.cpp tConsole.cpp \
	tCommandLine.cpp tCrypt.cpp tDirectories.cpp tError.cpp tEventQueue.cpp \
	tException.cpp tHeap.cpp tLinkedList.cpp tLocale.cpp tMemManager.cpp \
	tMemStack.cpp tRandom.cpp tRecorder.cpp tRecorderInternal.cpp tRing.cpp \
	tSafePTR.cpp tString.cpp tSysTime.cpp tToDo.cpp

# libnetwork.a, without nTransportRtc.cpp (libdatachannel) and
# nTransportLoopback.cpp (tests only)
NETWORK_SOURCES := \
	md5.cpp nAuthentication.cpp nConfig.cpp nKrawall.cpp nKrawallPrivate.cpp \
	nNetObject.cpp nNetwork.cpp nObserver.cpp nPriorizing.cpp nServerInfo.cpp \
	nSocket.cpp nSpamProtection.cpp nTransport.cpp nTransportRtcJs.cpp

# libengine.a
ENGINE_SOURCES := \
	eAdvWall.cpp eAuthentication.cpp eAxis.cpp eCamera.cpp eDebugLine.cpp \
	eDisplay.cpp eFloor.cpp eGameObject.cpp eGrid.cpp eKrawall.cpp \
	eNetGameObject.cpp ePath.cpp ePlayer.cpp eSensor.cpp eSound.cpp eTeam.cpp \
	eTimer.cpp eVoter.cpp eWall.cpp eRectangle.cpp eLagCompensation.cpp eChat.cpp \
	ePlatform.cpp

# librender.a
RENDER_SOURCES := \
	rConsole.cpp rConsoleGraph.cpp rFont.cpp rGL.cpp rGLRender.cpp rModel.cpp \
	rRender.cpp rDisplayList.cpp rScreen.cpp rSysdep.cpp rTexture.cpp rViewport.cpp \
	rGLES2.cpp

# libui.a
UI_SOURCES := uInput.cpp uInputQueue.cpp uMenu.cpp

# libtron.a plus the armagetronad_main sources and the browser glue
TRON_SOURCES := \
	gAIBase.cpp gAICharacter.cpp gArena.cpp gCamera.cpp gCycle.cpp \
	gCycleMovement.cpp gExplosion.cpp gGame.cpp gHud.cpp gLanguageMenu.cpp \
	gLogo.cpp gMenus.cpp gParser.cpp gParticles.cpp gSensor.cpp gServerBrowser.cpp \
	gSparks.cpp gSpawn.cpp gStuff.cpp gTeam.cpp gWall.cpp gWinZone.cpp \
	gFriends.cpp gServerFavorites.cpp \
	gFloor.cpp gArmagetron.cpp gBrowser.cpp gBrowserApi.cpp gBrowserQuality.cpp gScene.cpp \
	gStadium.cpp \
	gPlatformEvents.cpp

# relative to $(BUILD)/<variant>/
CLIENT_OBJECTS := \
	$(addprefix tools/,$(TOOLS_SOURCES:.cpp=.o)) tools/tResourceManager.o \
	$(addprefix network/,$(NETWORK_SOURCES:.cpp=.o)) \
	$(addprefix engine/,$(ENGINE_SOURCES:.cpp=.o)) \
	$(addprefix render/,$(RENDER_SOURCES:.cpp=.o)) \
	$(addprefix ui/,$(UI_SOURCES:.cpp=.o)) \
	$(addprefix tron/,$(TRON_SOURCES:.cpp=.o)) \
	local/sdl_shim.o

# --- flags ------------------------------------------------------------------

# "." first: our hand written config.h and tUniversalVariables.h win over the
# ones a native build may have left in src/
CPPFLAGS := -I. -I$(BUILD) -I$(SRC) \
	-iquote $(SRC)/tools -iquote $(SRC)/network -iquote $(SRC)/render \
	-iquote $(SRC)/ui -iquote $(SRC)/engine -iquote $(SRC)/tron \
	-iquote $(SRC)/thirdparty/particles

# The debug variant (client): -O1 -g keeps the build quick to iterate on and
# readable in a stack trace. The optimized variant
# (client_opt) is the release shape: -O2, no debug info, no assertions.
# Warnings that only fire in this configuration are silenced here, not in the
# shared sources.
DEBUG_CXXFLAGS := -O1 -g
DEBUG_LDFLAGS  := -O1 -g -sASSERTIONS
OPT_CXXFLAGS   := -O2
OPT_LDFLAGS    := -O2
# Native wasm exception handling (compile and link): the game throws
# eSensorFinished for every wall a sensor hits, dozens of times per frame,
# and the JavaScript based -fexceptions path spent 0.3 ms per frame in
# __Unwind_RaiseException. The legacy encoding is the one every current
# browser supports (Chrome 95+, Firefox 100+, Safari 15.2+); the exnref one
# (=0) needs Chrome 137+. The value must be given explicitly: the port builds
# (libpng) reject the unset default.
EXCEPTIONS := -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=1
CXXFLAGS := -std=c++17 $(EXCEPTIONS) \
	-Wno-deprecated-declarations -Wno-unused-command-line-argument

# Emscripten ports for the client: libpng and zlib (IMG_Load in sdl_shim.cpp).
# Needed at compile time for the include paths and at link time for the libs.
# No SDL: -sUSE_SDL=0 keeps Emscripten's own SDL headers and JS library out,
# sdlshim/ provides the SDL 1.2 shaped API the sources include (see
# sdlshim/SDL.h and sdl_shim.cpp).
PORTS := --use-port=libpng --use-port=zlib -sUSE_SDL=0

CLIENT_CPPFLAGS := $(CPPFLAGS) -Isdlshim -I$(DEPS)/include/libxml2 -DHAVE_LIBSDL -DHAVE_LIBSDL_IMAGE -DHAVE_SDL_IMAGE_H -DAA_GLES2
CLIENT_CXXFLAGS := $(CXXFLAGS) $(PORTS)

# No Asyncify: the client is event driven (gBrowser.cpp registers the frame
# with emscripten_set_main_loop, tDelay is a no-op, every wait is a polled
# state). EXIT_RUNTIME=1 so the exit() at the end of the session runs
# Module.onExit.
LDFLAGS_COMMON := \
	-sALLOW_MEMORY_GROWTH \
	-sEXIT_RUNTIME=1 \
	-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,stringToNewUTF8,UTF8ToString \
	-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_nRtcJsDeliver \
	$(EXCEPTIONS)

# the page's command API (src/tron/gBrowserApi.cpp, web/wasm/aa_api.js, API.md)
comma := ,
empty :=
space := $(empty) $(empty)
API_EXPORTS := $(subst $(space),$(comma),$(strip _aa_connect _aa_disconnect _aa_get_state _aa_get_players _aa_get_cycle _aa_turn _aa_brake \
	_aa_chat _aa_set_chatting _aa_set_chat_utf8 _aa_set_page_scores _aa_console _aa_get_config _aa_set_config _aa_list_config _aa_save_config \
	_aa_get_bindings _aa_get_actions _aa_set_binding _aa_unbind _aa_keysym_from_code _aa_code_from_keysym \
	_aa_get_votes _aa_vote _aa_get_teams _aa_join_team _aa_create_team _aa_set_spectating 	_aa_login _aa_auth _aa_auth_cancel _aa_platform_login _aa_export_config _aa_import_config \
	_aa_start_local _aa_stop_local _aa_scene _aa_scene_stop \
	_aa_visibility _aa_background_tick _aa_uncapped_tick _sdlshim_audio_fill _sdlshim_canvas_changed))

# The client: fixed function GL through src/render/rGLES2.cpp (AA_GLES2) on
# a WebGL 2 context with a WebGL 1 fallback (both allowed by the two version
# settings; no LEGACY_GL_EMULATION), a bigger stack for the game code, the data
# tree preloaded, browser only. Optimization flags come from the variant
# (CLIENT_VARIANT below).
CLIENT_LDFLAGS := $(LDFLAGS_COMMON) $(PORTS) \
	-sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=1 \
	-sSTACK_SIZE=4194304 \
	-sENVIRONMENT=web \
	-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_nRtcJsDeliver,$(API_EXPORTS) \
	-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,stringToNewUTF8,UTF8ToString,FS,IDBFS \
	-lidbfs.js \
	-L$(DEPS)/lib -lxml2 \
	--preload-file $(BUILD)/data/aa@/aa

# --- rules ------------------------------------------------------------------

.PHONY: all client client-opt clean data
all: client
client: client.js
client-opt: client_opt.js

# nNetwork.cpp includes it; the autotools build generates the same fallback
$(BUILD)/nTrueVersion.h:
	@mkdir -p $(dir $@)
	@echo '#define TRUE_ARMAGETRONAD_VERSION VERSION' > $@

# the data tree, laid out like an installation with prefix /aa (see
# tUniversalVariables.h): data under share/, configuration under etc/
data: $(BUILD)/data.stamp
$(BUILD)/data.stamp: $(wildcard $(TOP)/language/*.txt*) $(wildcard $(TOP)/config/*.cfg*) \
		$(wildcard $(TOP)/textures/*) $(wildcard $(TOP)/models/*) $(wildcard $(TOP)/sound/*) \
		$(shell find $(TOP)/resource/proto -type f) autoexec.cfg browser_user.cfg browser_keys.cfg
	rm -rf $(BUILD)/data
	mkdir -p $(BUILD)/data/aa/share $(BUILD)/data/aa/etc
	cp -r $(TOP)/language $(TOP)/textures $(TOP)/models $(TOP)/sound $(BUILD)/data/aa/share/
	rm -f $(BUILD)/data/aa/share/*/AGENTS.md $(BUILD)/data/aa/share/textures/*.xcf $(BUILD)/data/aa/share/models/*.obj
	rm -rf $(BUILD)/data/aa/share/models/tools $(BUILD)/data/aa/share/textures/tools
	sed -e 's/@progtitle@/Armagetron Advanced/' $(TOP)/language/languages.txt.in > $(BUILD)/data/aa/share/language/languages.txt
	rm -f $(BUILD)/data/aa/share/language/languages.txt.in
	cp $(TOP)/config/*.cfg $(TOP)/config/*.srv $(BUILD)/data/aa/etc/
	cp $(TOP)/config/aiplayers.cfg.in $(BUILD)/data/aa/etc/aiplayers.cfg
	cp autoexec.cfg $(BUILD)/data/aa/etc/autoexec.cfg
	cp browser_user.cfg $(BUILD)/data/aa/etc/browser_user.cfg
	cp browser_keys.cfg $(BUILD)/data/aa/etc/browser_keys.cfg
	mkdir -p $(BUILD)/data/aa/share/resource
	sh $(TOP)/batch/make/sortresources $(TOP)/resource/proto $(BUILD)/data/aa/share/resource/included $(TOP)/batch/make/sortresources.py
	touch $@

# one compile rule per layer and variant: $(1) variant, $(2) layer, $(3) flags
define CLIENT_COMPILE_RULE
$(BUILD)/$(1)/$(2)/%.o: $(SRC)/$(2)/%.cpp $(BUILD)/nTrueVersion.h
	@mkdir -p $$(dir $$@)
	$$(CXX) $(CLIENT_CPPFLAGS) $(CLIENT_CXXFLAGS) $(3) -c $$< -o $$@

endef

# a client variant: $(1) name (outputs $(1).js/.wasm/.data, objects in
# $(BUILD)/$(1)/), $(2) compile flags, $(3) link flags
define CLIENT_VARIANT
$(1)_OBJECTS := $$(addprefix $(BUILD)/$(1)/,$(CLIENT_OBJECTS))
$(1).js: $$($(1)_OBJECTS) $(BUILD)/data.stamp
	$$(CXX) $$($(1)_OBJECTS) $(CLIENT_LDFLAGS) $(3) -o $$@

$(foreach layer,tools network engine render ui tron,$(call CLIENT_COMPILE_RULE,$(1),$(layer),$(2)))
$(BUILD)/$(1)/local/%.o: %.cpp $(BUILD)/nTrueVersion.h
	@mkdir -p $$(dir $$@)
	$$(CXX) $(CLIENT_CPPFLAGS) $(CLIENT_CXXFLAGS) $(2) -c $$< -o $$@
endef

$(eval $(call CLIENT_VARIANT,client,$(DEBUG_CXXFLAGS),$(DEBUG_LDFLAGS)))
$(eval $(call CLIENT_VARIANT,client_opt,$(OPT_CXXFLAGS),$(OPT_LDFLAGS)))

# the deployable bundle (web/dist, git-ignored): the API wrapper, the release
# build, servers.json, plus gzip (and brotli when the tool exists) copies of the
# big files for Caddy's precompressed file_server (web/deploy/Caddyfile). The
# API test page ships as client.html. If the play page (web/page) has been built
# with `npm run build`, web/page/dist/ is copied over the bundle and is the
# index.html; otherwise client.html is.
DIST := $(TOP)/web/dist
PAGE_DIST := $(TOP)/web/page/dist
.PHONY: dist
dist: client_opt.js
	rm -rf $(DIST)
	mkdir -p $(DIST)
	sed -e 's/data-build="debug"/data-build="opt"/' -e 's|data-resource-proxy=""|data-resource-proxy="/resource/"|' client.html > $(DIST)/client.html
	cp aa_api.js client_opt.js client_opt.wasm client_opt.data servers.json $(DIST)/
	if [ -f $(PAGE_DIST)/index.html ]; then 		cp -a $(PAGE_DIST)/. $(DIST)/; 		echo "dist: play page from $(PAGE_DIST)"; 	else 		cp $(DIST)/client.html $(DIST)/index.html; 		echo "dist: no play page build (web/page/dist), client.html is index.html"; 	fi
	node compress.cjs $(DIST)/client_opt.js $(DIST)/client_opt.wasm $(DIST)/client_opt.data $(DIST)/aa_api.js $$(ls $(DIST)/assets/*.js $(DIST)/assets/*.css 2>/dev/null)
	@ls -l $(DIST)

# raw and compressed sizes of the outputs; compressed is what a web server ships
.PHONY: sizes
sizes:
	@for f in client.js client.wasm client.data client_opt.js client_opt.wasm client_opt.data; do \
		[ -f $$f ] || continue; \
		printf '%-16s %9d raw %9d gzip-9' $$f $$(stat -c %s $$f) $$(gzip -9c $$f | wc -c); \
		command -v brotli >/dev/null && printf ' %9d brotli' $$(brotli -c $$f | wc -c); \
		echo; \
	done

clean:
	rm -rf $(BUILD) \
		client.js client.wasm client.wasm.map client.data \
		client_opt.js client_opt.wasm client_opt.data
