#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fstream>
#include <sys/types.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <iostream>
#include <string>
#include <vector>
#else
#include <libxml/nanohttp.h>
#endif

#include "tConfiguration.h"
#include "tDirectories.h"
#include "tResourceManager.h"
#include "tString.h"

#ifdef LIBCURL_PROTOCOL_HTTP
#include <curl/curl.h>

class tCurlGlobal
{
public:
    tCurlGlobal()
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    };

    ~tCurlGlobal()
    {
        curl_global_cleanup();
    }
};

class tCurlLocal
{
private:
    CURL* _handle;

public:
    tCurlLocal()
    {
        static tCurlGlobal curlGlobal;
        _handle = curl_easy_init();
    };

    ~tCurlLocal()
    {
        curl_easy_cleanup(_handle);
    }

    operator CURL*()
    {
        return _handle;
    }

    static size_t write_to_ostream(char* data, size_t size, size_t nmemb, void* ostream)
    {
        // Cast the user pointer to an ostream and write the data to it
        static_cast<std::ostream*>(ostream)->write(data, size * nmemb);
        // Return the number of bytes processed
        return size * nmemb;
    }
};
#endif

// server determined resource repository
tString tResourceManager::resRepoServer("http://resource.armagetronad.net/resource/");
// the nSettingItem is in gStuff.cpp

// client determined resource repository
tString tResourceManager::resRepoClient("http://resource.armagetronad.net/resource/");
static tSettingItem<tString> conf_res_repo("RESOURCE_REPOSITORY_CLIENT", tResourceManager::resRepoClient);

#ifdef __EMSCRIPTEN__
/* The asynchronous fetch. One request at a time: fetch(url) in the page,
   the body written into the resource cache (tPathResource::GetWritePath,
   /aa/user/resource/automatic, an IDBFS mount set up by web/wasm/aa_api.js
   so it survives reloads) with FS.writeFile, then the mount is flushed.
   Status: 0 running, 1 done, 2 failed (network, CORS, non-2xx). The
   repository must allow the page's origin (Access-Control-Allow-Origin);
   the official one does not, so the page normally names a resource proxy
   (Module.__aaResourceProxy, set by aa_api.js setResourceProxy(): the
   deploy's /resource/ route, web/deploy/Caddyfile) and every URL is then
   requested as <proxy><host>/<path> instead (BrowserProxyUrl). */
EM_JS_DEPS(tResourceManager, "$UTF8ToString,$stringToNewUTF8");

EM_JS(void, tResourceFetchStart, (char const * urlPtr, char const * pathPtr), {
    var url = UTF8ToString(urlPtr);
    var path = UTF8ToString(pathPtr);
    var state = Module["__aaFetch"] = { status: 0, bytes: 0, error: "" };
    fetch(url, { mode: "cors", credentials: "omit", cache: "default" }).then(function (response) {
        if (!response.ok)
            throw new Error("HTTP " + response.status);
        return response.arrayBuffer();
    }).then(function (buffer) {
        FS.writeFile(path, new Uint8Array(buffer));
        state.bytes = buffer.byteLength;
        state.status = 1;
        if (FS.syncfs)
            FS.syncfs(false, function (err) { if (err) console.log("BROWSER: resource cache sync failed: " + err); });
    }).catch(function (e) {
        state.error = String(e && e.message ? e.message : e);
        state.status = 2;
    });
});

EM_JS(int, tResourceFetchStatus, (), {
    var state = Module["__aaFetch"];
    return state ? state.status : 2;
});

EM_JS(int, tResourceFetchBytes, (), {
    var state = Module["__aaFetch"];
    return state ? state.bytes : 0;
});

EM_JS(char *, tResourceFetchError, (), {
    var state = Module["__aaFetch"];
    return stringToNewUTF8(state ? state.error : "no fetch");
});

// the resource proxy base set by the page ("" = fetch directly)
EM_JS(char *, tResourceProxyBase, (), {
    var base = Module["__aaResourceProxy"];
    return stringToNewUTF8(base ? String(base) : "");
});

// the page's origin ("http://host:port"; "" outside a page)
EM_JS(char *, tResourcePageOrigin, (), {
    var origin = (typeof location !== "undefined" && /^https?:$/.test(location.protocol)) ? location.origin : "";
    return stringToNewUTF8(origin);
});

namespace
{
// stdout (the browser console); the in-game console only on request
void BrowserResourceLog( std::string const & line )
{
    tConsole::BrowserLog( "resource " + line );
}

struct tBrowserFetch
{
    bool running = false;
    tString file;                       // the resource being fetched
    tString savePath;                   // where it goes
    std::vector< std::string > urls;    // candidates (original URLs), tried in order
    size_t next = 0;                    // index of the URL being fetched
    bool chained = false;               // this is the DTD of the map fetched before
};
tBrowserFetch st_fetch;

bool BrowserResourceLocal( char const * file )
{
    return tDirectories::Resource().GetReadPath( file ).Len() > 1;
}

//! the URL to request: the original one, or through the resource proxy the
//! page configured: <base><host>/<path>, the scheme dropped (the proxy
//! picks https for a plain host, http for host:port; web/deploy/Caddyfile).
//! URLs on the page's own origin need no CORS and go direct.
std::string BrowserProxyUrl( std::string const & url, bool & proxied )
{
    proxied = false;
    char * basePtr = tResourceProxyBase();
    std::string base( basePtr );
    free( basePtr );
    size_t const scheme = url.find( "://" );
    if ( base.empty() || scheme == std::string::npos )
        return url;
    char * originPtr = tResourcePageOrigin();
    std::string const origin( originPtr );
    free( originPtr );
    if ( !origin.empty() && url.compare( 0, origin.size() + 1, origin + "/" ) == 0 )
        return url;
    if ( base[base.size() - 1] != '/' )
        base += '/';
    proxied = true;
    // http://host/path -> <base>host/path
    return base + url.substr( scheme + 3 );
}

void BrowserFetchNext()
{
    tBrowserFetch & f = st_fetch;
    bool proxied = false;
    std::string const request = BrowserProxyUrl( f.urls[f.next], proxied );
    BrowserResourceLog( std::string( static_cast< char const * >( f.file ) ) + " fetching from " + f.urls[f.next]
                        + ( proxied ? " via proxy " + request : std::string() ) );
    tResourceFetchStart( request.c_str(), f.savePath );
}

//! the DOCTYPE SYSTEM reference of an XML file, empty when none
std::string BrowserDoctypeSystem( char const * path )
{
    std::ifstream in( path, std::ios::binary );
    char buffer[4096];
    in.read( buffer, sizeof( buffer ) );
    std::string const head( buffer, static_cast< size_t >( in.gcount() ) );
    size_t const doctype = head.find( "<!DOCTYPE" );
    if ( doctype == std::string::npos )
        return std::string();
    size_t const system = head.find( "SYSTEM", doctype );
    if ( system == std::string::npos )
        return std::string();
    size_t const open = head.find_first_of( "\"'", system );
    if ( open == std::string::npos )
        return std::string();
    size_t const close = head.find( head[open], open + 1 );
    if ( close == std::string::npos )
        return std::string();
    return head.substr( open + 1, close - open - 1 );
}
}

bool tResourceManager::BrowserPrefetchBegin( char const * uri, char const * file )
{
    tBrowserFetch & f = st_fetch;
    if ( !file || !file[0] || !tPath::IsValidPath( file ) )
        return false;
    // a fetch still marked running was left by a game that ended while it ran
    // (nothing polls it any more); the new one takes over. The old request may
    // still finish and cache its file, which does no harm.
    if ( f.running )
    {
        BrowserResourceLog( std::string( static_cast< char const * >( f.file ) ) + " fetch abandoned." );
        f.running = false;
    }
    if ( BrowserResourceLocal( file ) )
    {
        BrowserResourceLog( std::string( file ) + " cached." );
        return false;
    }
    tString const savePath = tDirectories::Resource().GetWritePath( file );
    if ( savePath.Len() <= 1 )
        return false;

    f = tBrowserFetch();
    f.file = file;
    f.savePath = savePath;
    if ( uri && uri[0] && strcmp( "0", uri ) )
        f.urls.push_back( uri );
    if ( resRepoServer.Len() > 2 )
        f.urls.push_back( std::string( static_cast< char const * >( resRepoServer ) ) + file );
    if ( resRepoClient.Len() > 2 && resRepoClient != resRepoServer )
        f.urls.push_back( std::string( static_cast< char const * >( resRepoClient ) ) + file );
    if ( f.urls.empty() )
        return false;

    f.running = true;
    BrowserFetchNext();
    return true;
}

bool tResourceManager::BrowserPrefetchPoll()
{
    tBrowserFetch & f = st_fetch;
    if ( !f.running )
        return true;

    int const status = tResourceFetchStatus();
    if ( status == 0 )
        return false;

    if ( status == 2 )
    {
        char * error = tResourceFetchError();
        BrowserResourceLog( std::string( static_cast< char const * >( f.file ) ) + " fetch failed: " + error );
        free( error );
        if ( ++f.next < f.urls.size() )
        {
            BrowserFetchNext();
            return false;
        }
        remove( f.savePath );
        f.running = false;
        return true;
    }

    BrowserResourceLog( std::string( static_cast< char const * >( f.file ) ) + " fetched, " + std::to_string( tResourceFetchBytes() ) + " bytes." );

    // the map's DTD, if it is not local either
    std::string const dtd = f.chained ? std::string() : BrowserDoctypeSystem( f.savePath );
    f.running = false;
    if ( !dtd.empty() && dtd.find( "://" ) == std::string::npos && !BrowserResourceLocal( dtd.c_str() ) )
    {
        if ( BrowserPrefetchBegin( NULL, dtd.c_str() ) )
        {
            st_fetch.chained = true;
            return false;
        }
    }
    return true;
}

tString const & tResourceManager::BrowserPrefetchFile()
{
    static tString const none;
    return st_fetch.running ? st_fetch.file : none;
}
#endif

tResourceManager::Result tResourceManager::FetchURI(const char* URI, std::ostream& o)
{
#ifdef __EMSCRIPTEN__
    {
        /* No blocking download in the event driven browser build: the
           asynchronous prefetch above runs before the map is needed and
           fills the cache; a resource that still is not there fails here
           and the caller falls back like it does natively. */
        (void) o;
        con << tOutput("$resource_fetcherror_noconnect", URI);
        return Result::ERROR_Uri;
    }
#elif defined(LIBCURL_PROTOCOL_HTTP)
    {
        tCurlLocal handle;
        if (nullptr == handle)
            return Result::ERROR_Unknown;

        // Set the URL to request
        curl_easy_setopt(handle, CURLOPT_URL, URI);
        // Set the callback function to handle the response
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &tCurlLocal::write_to_ostream);
        // Set the user pointer to be an ostream to which the response will be written
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &o);
        // activate failure on HTTP errors
        curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
        // activate automatic redirection following
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        // activate SSL verification
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        // shorten timeouts (10s connect, 30s total)
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
        // set user agent
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "Armagetron Advanced"); // using that instead of the variable progtitle so servers always know what to expect
#ifdef DEBUG
        // more detailed error reporting
        char errbuf[CURL_ERROR_SIZE];
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);
        // curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
#endif
        // Perform the request
        CURLcode result = curl_easy_perform(handle);
        // Check the result
        if (result != CURLE_OK)
        {
            long http_code = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
            // If the request failed, print an error message
            con << "curl_easy_perform() failed: " << curl_easy_strerror(result) << "\n";
            return Result::ERROR_Unknown;
        }
        // tCurlLocal cleans the handle up
    }
#else
#ifdef LIBXML_HTTP_ENABLED
    {
        void* ctxt = NULL;
        int len, rc;

        ctxt = xmlNanoHTTPOpen(URI, NULL);
        if (ctxt == NULL)
        {
            con << tOutput("$resource_fetcherror_noconnect", URI);
            return ERROR_Uri;
        }

        if ((rc = xmlNanoHTTPReturnCode(ctxt)) != 200)
        {
            con << tOutput(rc == 404 ? "$resource_fetcherror_404" : "$resource_fetcherror", rc);
            return static_cast<tResourceManager::Result>(rc);
        }

        // xmlNanoHTTPFetchContent( ctxt, &buf, &len );
        char buf[10000];
        while ((len = xmlNanoHTTPRead(ctxt, buf, sizeof(buf))) > 0)
        {
            o.write(buf, len);
        }

        xmlNanoHTTPClose(ctxt);
    }
#else
#error libcurl or libxml nanohttp required; configure should have told you. Please file a bug.
    return Result::ERROR_Unknown;
#endif
#endif
    con << "OK\n";
    return Result::RESULT_Ok;
}

static int myHTTPFetch(const char* URI, const char* filename, const char* savepath)
{
    con << tOutput("$resource_downloading", URI);
    // con << "Downloading " << URI << "...\n";

    try
    {
        std::ofstream o{savepath};
        tResourceManager::Result ret = tResourceManager::FetchURI(URI, o);
        o.close();
        if (ret == tResourceManager::Result::RESULT_Ok)
            return 0;

        // some error
        remove(savepath);
        return ret;
    }
    catch (...)
    {
        remove(savepath);

        return tResourceManager::Result::ERROR_FileAccess;
    }

    return 0;
}

static int myFetch(const char *URIs, const char *filename, const char *savepath) {
    const char *r = URIs, *p, *n;
    char *u;
    size_t len;
    int rv = -1;
    // r = unprocessed data		p = end-of-item + 1		u = item
    // n = to-be r				len = length of item	savepath = result filepath

    while (r[0] != '\0') {
        while (r[0] == ' ') ++r;			// skip spaces at the start of the item
        p = strchr(r, ';');
        if ( !p )
            p = strchr(r, '\0');
        n = (p[0] == '\0') ? p : (p + 1);	// next item starts after the semicolon
        // NOTE: skip semicolons, *NOT* nulls
        while (p > r && p[-1] == ' ')
            --p;   // skip spaces at the end of the item
        if (p > r) // skip this for null-length items
        {
            len = (size_t)(p - r);
            u = (char*)malloc((len + 1) * sizeof(char));
            strncpy(u, r, len);
            u[len] = '\0';					// u now contains the individual URI
            rv = myHTTPFetch(u, filename, savepath);	// TODO: handle other protocols?
            free(u);
            if (rv == 0) return 0;		// If successful, return the file retrieved
        }
        r = n;								// move onto the next item
    }

    return rv;	// last error
}

/*
Allows for the fetching and caching of ressources available on the web,
such as maps (xml), texture (jpg, gif, bmp), sound and models.
Nota: On some forums (such as forums3.armagetronad.net), it is possible
for the download link not give information about the filename or type,
ie: https://forums3.armagetronad.net/download/file.php?id=1191. This is
why the filename parameter is required.
Parameters:
uri: The full uri to obtain the ressource
filename: The filename to use for the local ressource
Return a file handle to the ressource
NOTE: There must be *at least* one directory level, even if it is ./
*/
tString tResourceManager::locateResource(const char *uri, const char *file) {
    tString filepath, a_uri = tString(), savepath;
    int rv;

    char * to_free = NULL; // string to delete later

    {
        char const *pos, *posb;
        char *nf;
        size_t l;

        // Step 1: If 'file' has an open paren, cut everything after it off
        if ( (pos = strchr(file, '(')) ) {
            l = (size_t)(pos - file);
            nf = (char*)malloc((l + 1) * sizeof(char));
            strncpy(nf, file, l);
            nf[l] = '\0';
            file = nf;
            to_free = nf;

            // Step 2: Extract URI, if any
            ++pos;
            if ( (posb = strchr(pos, ')')) ) {
                l = (size_t)(posb - pos);
                nf = (char*)malloc((l + 1) * sizeof(char));
                strncpy(nf, pos, l);
                nf[l] = '\0';
                a_uri << nf << ';';
                free( nf );
            }
        }
    }
    // Validate paths and determine detination savepath
    if (!file || file[0] == '\0') {
        con << tOutput( "$resource_no_filename" );
        free( to_free );
        return (tString) NULL;
    }
    if (file[0] == '/' || file[0] == '\\') {
        con << tOutput( "$resource_abs_path" );
        free( to_free );
        return (tString) NULL;
    }
    savepath = tDirectories::Resource().GetWritePath(file);
    if (savepath == "") {
        con << tOutput( "$resource_no_writepath" );
        free( to_free );
        return (tString) NULL;
    }

    // Do we have this file locally ?
    filepath = tDirectories::Resource().GetReadPath(file);

    if (filepath != "")
    {
        if ( NULL != to_free )
            free( to_free );
        return filepath;
    }

    // Some sort of File not found
    if (uri && strcmp("0", uri))
        a_uri << uri << ';';

    // add repositories to uri
    if ( resRepoServer.Len() > 2 )
        a_uri << resRepoServer << file << ';';

    if ( resRepoClient.Len() > 2 && resRepoClient != resRepoServer )
        a_uri << resRepoClient << file << ';';

    con << tOutput( "$resource_not_cached", file );

    rv = myFetch((const char *)a_uri, file, (const char *)savepath);

    if ( NULL != to_free )
        free( to_free );

    if (rv)
        return (tString) NULL;
    return savepath;
}

FILE* tResourceManager::openResource(const char *uri, const char *file) {
    tString filepath;
    filepath = locateResource(uri, file);
    if ( filepath.Len() <= 1 )
        return NULL;
    return fopen((const char *)filepath, "r");
}

static void RInclude(std::istream& s)
{
    // forbid CASACL
    tCasaclPreventer preventer;

    tString file;
    s >> file;

    tString rclcl = tResourceManager::locateResource(NULL, file);
    if ( rclcl ) {
        std::ifstream rc(rclcl);
        tConfItemBase::LoadAll(rc, false );
        return;
    }

    con << tOutput( "$config_rinclude_not_found", file );
}

static tConfItemFunc s_RInclude("RINCLUDE",  &RInclude);

