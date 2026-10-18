#ifndef ArmageTron_RESOURCEMANAGER_H
#define ArmageTron_RESOURCEMANAGER_H

#include "tString.h"

//! resource manager: fetches and caches resources from repositories or arbitrary URIs
class tResourceManager {
public:
    enum Result
    {
        RESULT_Ok = 200,       // all fine
        ERROR_Unknown = -1,    // unknown error
        ERROR_Uri = -2,        // URI not well formed
        ERROR_FileAccess = -3, // target file not writable
        ERROR_NotFound = 404,  // URI not found
        ERROR_NoAccess = 401   // Access denied
    };

    // fetches an URI and stores it in the provided stream
    static Result FetchURI(const char* URI, std::ostream& o);

    //! Return the position of the resource in the cache
    static tString locateResource(const char *uri, const char *file);
    //! opens a resource
    static FILE *openResource(const char *uri, const char *pathname);

    //! server determined resource repository
    static tString resRepoServer;

    //! client determined resource repository
    static tString resRepoClient;

#ifdef __EMSCRIPTEN__
    /* Browser build: FetchURI cannot block, so a resource that is not in the
       local cache is fetched asynchronously before the code that needs it
       runs (gGame.cpp, GS_CREATE_GRID). Begin() returns false when the file
       is already local (nothing to wait for) or a fetch is already running;
       true when a fetch started: poll it once per frame until Poll() returns
       true, then call locateResource/openResource as usual (it finds the
       cached copy, or fails as it does natively). The same URI list as
       locateResource is tried in order; a fetched map's DOCTYPE SYSTEM
       reference is fetched next if it is not local either. */
    static bool BrowserPrefetchBegin(const char *uri, const char *file);
    static bool BrowserPrefetchPoll();
    //! the file of the fetch in progress, empty when none
    static tString const & BrowserPrefetchFile();
#endif
};

#endif //ArmageTron_RESOURCEMANAGER_H
