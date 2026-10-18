/* config.h for the Emscripten build of the client.

   Hand written counterpart of what configure generates, derived from a Linux
   "minimal" server build's config.h:
   - DEDICATED undefined: this is a client.
   - KRAWALL_SERVER undefined: nKrawallPrivate.cpp would want libxml2's nanohttp.
   - HAVE_PTHREAD / HAVE_LIBPTHREAD undefined: single threaded, no zthread.
   - HAVE_LIBDATACHANNEL / HAVE_WEBRTC undefined: the browser is the WebRTC
     implementation, nTransportRtcJs.cpp talks to it.
   - ENABLE_BINRELOC undefined: no /proc/self/maps in a browser.
   - HAVE_LIBXML2 / LIBCURL_PROTOCOL_HTTP undefined: neither is linked.
*/

#ifndef ArmageTron_WASM_CONFIG_H
#define ArmageTron_WASM_CONFIG_H

/* #undef AIX */
/* #undef BEOS */
/* #undef BR_PTHREAD */
/* #undef CAUTION_WALL */
/* #undef DEDICATED */
/* #undef DIRTY */

/* no custom memory manager */
#define DONTUSEMEMMANAGER 1

/* #undef ENABLE_BINRELOC */

#define HAVE_ATAN2F 1
#define HAVE_CLEARENV 1
#define HAVE_COSF 1
#define HAVE_EXPF 1
#define HAVE_FABSF 1
#define HAVE_FLOORF 1
/* #undef HAVE_INTTYPES_H */
/* #undef HAVE_LIBDATACHANNEL */
/* #undef HAVE_LIBPTHREAD */
/* #undef HAVE_LIBSDL */
/* #undef HAVE_LIBSDL_IMAGE */
/* #undef HAVE_LIBSDL_MIXER */
/* #undef HAVE_LIBWSOCK32 */
/* #undef HAVE_LIBX11 */
/* #undef HAVE_LIBXML2 */
#define HAVE_LOGF 1
/* #undef HAVE_PTHREAD */
/* #undef HAVE_SDL_IMAGE_H */
/* #undef HAVE_SDL_SDL_IMAGE_H */
#define HAVE_SELECT 1
#define HAVE_SINF 1
#define HAVE_SOCKLEN_T 1
#define HAVE_SQRTF 1
/* #undef HAVE_STDINT_H */
/* #undef HAVE_STDIO_H */
/* #undef HAVE_STDLIB_H */
/* #undef HAVE_STRINGS_H */
/* #undef HAVE_STRING_H */
/* #undef HAVE_SYS_STAT_H */
/* #undef HAVE_SYS_TYPES_H */
#define HAVE_TANF 1
#define HAVE_UNISTD_H 1
/* #undef HAVE_WEBRTC */
#define HAVE_WMEMSET 1

/* #undef KRAWALL */
/* #undef KRAWALL_SERVER */
/* #undef LIBCURL_PROTOCOL_HTTP */

/* the browser is not Linux, but the POSIX bits the netcode uses are there */
#define LINUX 1

/* #undef MACOSX */
/* #undef MAXCLIENTS */

#define PACKAGE "armagetronad"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "armagetronad"
#define PACKAGE_STRING "armagetronad 0.2.9.3_alpha"
#define PACKAGE_TARNAME "armagetronad"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "0.2.9.3_alpha"

#define PREFIX "/aa"
#define PROGDIR_SUFFIX ""
#define PROGNAME "armagetronad-probe"
#define PROGNAMEBASE "armagetronad"
#define PROGTITLE "Armagetron Advanced"

/* #undef PTHREAD_CREATE_JOINABLE */
/* #undef RESPAWN_HACK */
/* #undef SOLARIS */
/* #undef STDC_HEADERS */

#define TOP_SOURCE_DIR "../.."

#define VERSION "0.2.9.3_alpha"

/* #undef WIN32 */
/* #undef X_DISPLAY_MISSING */
/* #undef const */

#endif // ArmageTron_WASM_CONFIG_H
