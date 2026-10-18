/* Hand written stand-in for the tUniversalVariables.h the autotools build
   generates from the installation prefix. A browser build installs nowhere;
   these paths only feed tDirectories, which finds nothing under them. */

#ifndef INITDIR
    #define INITDIR "/aa/etc/init.d"
#endif
#ifndef PREFIX
    #define PREFIX "/aa"
#endif
#ifndef EXEC_PREFIX
    #define EXEC_PREFIX "/aa"
#endif
#ifndef BINDIR
    #define BINDIR "/aa/bin"
#endif
#ifndef SBINDIR
    #define SBINDIR "/aa/sbin"
#endif
#ifndef DATADIR
    #define DATADIR "/aa/share"
#endif
#ifndef DATADIR_SUFFIX
    #define DATADIR_SUFFIX ""
#endif
#ifndef AA_DATADIR
    #define AA_DATADIR "/aa/share"
#endif
#ifndef SYSCONFDIR
    #define SYSCONFDIR "/aa/etc"
#endif
#ifndef SYSCONFDIR_SUFFIX
    #define SYSCONFDIR_SUFFIX ""
#endif
#ifndef AA_SYSCONFDIR
    #define AA_SYSCONFDIR "/aa/etc"
#endif
#ifndef LOCALSTATEDIR
    #define LOCALSTATEDIR "/aa/var"
#endif
#ifndef LOCALSTATEDIR_SUFFIX
    #define LOCALSTATEDIR_SUFFIX ""
#endif
#ifndef AA_LOCALSTATEDIR
    #define AA_LOCALSTATEDIR "/aa/var"
#endif
#ifndef INFODIR
    #define INFODIR ""
#endif
#ifndef INFODIR_SUFFIX
    #define INFODIR_SUFFIX ""
#endif
#ifndef AA_INFODIR
    #define AA_INFODIR ""
#endif
#ifndef MANDIR
    #define MANDIR ""
#endif
#ifndef MANDIR_SUFFIX
    #define MANDIR_SUFFIX ""
#endif
#ifndef AA_MANDIR
    #define AA_MANDIR ""
#endif
#ifndef SCRIPTDIR
    #define SCRIPTDIR "/aa/share/scripts"
#endif
#ifndef DOCDIR
    #define DOCDIR "/aa/share/doc"
#endif
#ifndef DOCDIR_SUFFIX
    #define DOCDIR_SUFFIX ""
#endif
#ifndef AA_DOCDIR
    #define AA_DOCDIR "/aa/share/doc"
#endif
#ifndef RUNDIR
    #define RUNDIR "/aa/var/run"
#endif
#ifndef RUNDIR_SUFFIX
    #define RUNDIR_SUFFIX ""
#endif
#ifndef AA_RUNDIR
    #define AA_RUNDIR "/aa/var/run"
#endif
#ifndef LOGDIR
    #define LOGDIR "/aa/var/log"
#endif
#ifndef LOGDIR_SUFFIX
    #define LOGDIR_SUFFIX ""
#endif
#ifndef AA_LOGDIR
    #define AA_LOGDIR "/aa/var/log"
#endif
#ifndef OLDVARDIR
    #define OLDVARDIR "/aa/var"
#endif
#ifndef OLDVARDIR_SUFFIX
    #define OLDVARDIR_SUFFIX ""
#endif
#ifndef AA_OLDVARDIR
    #define AA_OLDVARDIR "/aa/var"
#endif
#ifndef ENABLE_MIGRATESTATE
    #define ENABLE_MIGRATESTATE "no"
#endif
#ifndef RELOCATABLE
    #define RELOCATABLE "no"
#endif
