/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2025 amigazen project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the AWeb Public License as included in this
 * distribution.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * AWeb Public License for more details.
 *
 **********************************************************************/

/* arexx.h - AWeb arexx interface */

struct Arexxcmd            /* Communication between ARexx and enevt handler */
{  ULONG command;          /* command, see below */
   ULONG windowkey;        /* window for this command */
   ULONG parameter[10];    /* multi-purpose parameters */
   UBYTE *result;          /* result string to be returned */
   UBYTE *varname;         /* return into this variable name, not in RESULT */
   ULONG errorlevel;       /* errorlevel */
   USHORT flags;           /* FALSE if bogus, variables, wait not allowed */
 /* private fields */
   struct RexxMsg *msg;
   struct Arexxport *port;
};

#define ARXCF_TRUEREXX     0x0001   /* Real ARexx command from the port */
#define ARXCF_ALLOWGET     0x0002   /* Allow GET commands */
#define ARXCF_ALLOWSTEM    0x0004   /* Allow STEM results */

#define RXERR_OK        0
#define RXERR_INFORM    1
#define RXERR_WARNING   5
#define RXERR_INVARGS   10
#define RXERR_INVCMD    11
#define RXERR_FATAL     20

enum AREXX_COMMANDS        /* (parameters) */
{  ARX_Dummy=0,
   ARX_ACTIVATEWINDOW,     /* (void) */
   ARX_ADDHOTLIST,         /* (UBYTE *url,UBYTE *title,UBYTE *target,UBYTE *group) */
   ARX_ALLOWCMD,           /* (void) */
   ARX_BACKGROUND,         /* (BOOL on,BOOL off) */
   ARX_BGSOUND,            /* (BOOL on,BOOL off) */
   ARX_CANCEL,             /* (long *loadid,BOOL all) */
   ARX_CHANCLOSE,          /* (UBYTE *channel) */
   ARX_CHANDATA,           /* (UBYTE *channel,UBYTE *data,BOOL newline) */
   ARX_CHANHEADER,         /* (UBYTE *channel,UBYTE *header) */
   ARX_CHANOPEN,           /* (UBYTE *url) */
   ARX_CLEARSELECTION,     /* (void) */
   ARX_CLOSE,              /* (BOOL force) */
   ARX_COPYBLOCK,          /* (void) */
   ARX_COPYURL,            /* (UBYTE *target) */
   ARX_DELETECACHE,        /* (BOOL images,BOOL docs,BOOL force,UBYTE *pattern) */
   ARX_DRAGGING,           /* (BOOL on,BOOL off) */
   ARX_EDITSOURCE,         /* (UBYTE *url) */
   ARX_FIXCACHE,           /* (BOOL force) */
   ARX_FLUSHCACHE,         /* (BOOL images,BOOL docs,BOOL all,UBYTE *url) */
   ARX_FOCUS,              /* (UBYTE *target) */
   ARX_GET,                /* (UBYTE *item,UBYTE *target,UBYTE *varname,UBYTE *stem,
                                 UBYTE *patternn,BOOL all) */
   ARX_GETCFG,             /* (UBYTE *item,UBYTE *var,UBYTE *stem) */
   ARX_GETHISTORY,         /* (long *window,BOOL mainline,UBYTE *stem) */
   ARX_GO,                 /* (long *n,BOOL back,BOOL fwd,BOOL home) */
   ARX_HOTLIST,            /* (UBYTE *set,BOOL save,BOOL restore) */
   ARX_HTTPGET,            /* (UBYTE *url,UBYTE *stem,BOOL headers,long *timeout) */
   ARX_ICONIFY,            /* (BOOL hide,BOOL show) */
   ARX_IMAGELOADING,       /* (BOOL off,BOOL maps,BOOL all) */
   ARX_INFO,               /* (UBYTE *target) */
   ARX_JAVASCRIPT,         /* (UBYTE *source,UBYTE *file,UBYTE *target,UBYTE *var) */
   ARX_JSONGET,            /* (UBYTE *data,UBYTE *path,UBYTE *var,UBYTE *stem) */
   ARX_JSBREAK,            /* (void) */
   ARX_JSDEBUG,            /* (BOOL on,BOOL off) */
   ARX_LOAD,               /* (UBYTE *url,UBYTE *saveas,BOOL reload,BOOL append,
                                 BOOL savereq,BOOL noicon,UBYTE *post) */
   ARX_LOADIMAGES,         /* (UBYTE *target,BOOL maps,BOOL restrict) */
   ARX_LOADSETTINGS,       /* (UBYTE *name,BOOL request) */
   ARX_MIMETYPE,           /* (UBYTE *name) */
   ARX_NEW,                /* (UBYTE *url,UBYTE *name,BOOL reload,UBYTE *post,BOOL smart) */
   ARX_OPEN,               /* (UBYTE *url,UBYTE *target,BOOL reload,UBYTE *post,BOOL smart) */
   ARX_OPENREQ,            /* (BOOL file,UBYTE *pattern) */
   ARX_PLAYBGSOUND,        /* (void) */
   ARX_PLUGIN,             /* (UBYTE *plugin,UBYTE *command) */
   ARX_PRINT,              /* (long *scale,BOOL center,BOOL noformfeed,BOOL nobackground,
                                 BOOL wait,BOOL request,BOOL debug) */
   ARX_QUIT,               /* (BOOL force) */
   ARX_RELOAD,             /* (UBYTE *target,BOOL images) */
   ARX_REQUEST,            /* (UBYTE *title,UBYTE *body,UBYTE *gadgets,BOOL nowait) */
   ARX_REQUESTFILE,        /* (UBYTE *title,UBYTE *file,UBYTE *pattern,
                                 BOOL savemode,BOOL dirsonly) */
   ARX_REQUESTSTRING,      /* (UBYTE *title,UBYTE *body,UBYTE *gadgets,UBYTE *default) */
   ARX_RESETFRAME,         /* (UBYTE *target) */
   ARX_RUN,                /* (UBYTE *name) */
   ARX_SAVEAS,             /* (UBYTE *name,BOOL append,UBYTE *target,BOOL noicon) */
   ARX_SAVEAUTHORIZE,      /* (void) */
   ARX_SAVEIFF,            /* (UBYTE *name,BOOL noicon,BOOL wait) */
   ARX_SAVESETTINGS,       /* (UBYTE *name,BOOL request) */
   ARX_SCREENTOFRONT,      /* (void) */
   ARX_SCREENTOBACK,       /* (void) */
   ARX_SCROLL,             /* (long *n,BOOL up,BOOL down,BOOL left,BOOL right,
                                 BOOL page,BOOL far,UBYTE *target) */
   ARX_SEARCH,             /* (UBYTE *target) */
   ARX_SETCFG,             /* (UBYTE *item,UBYTE *value,UBYTE *stem,BOOL add) */
   ARX_SETCLIP,            /* (UBYTE *value) */
   ARX_SETCOOKIES,         /* (UBYTE *stem,BOOL add) */
   ARX_SNAPSHOT,           /* (void) */
   ARX_STATUSFIELD,        /* (UBYTE *set) */
   ARX_SUBWINDOW,          /* (UBYTE *type,BOOL open,BOOL close) */
   ARX_SYSTEM,             /* (UBYTE *cmd) */
   ARX_URLENCODE,          /* (UBYTE *string,UBYTE *var) */
   ARX_URLFIELD,           /* (BOOL activate,long *pos,UBYTE *set,BOOL paste) */
   ARX_USEPROXY,           /* (BOOL enable,BOOL disable) */
   ARX_VIEWSOURCE,         /* (UBYTE *url) */
   ARX_WAIT,               /* (UBYTE *url,BOOL document,BOOL images,BOOL all) */
   ARX_WINDOW,             /* (UBYTE *rect,BOOL activate,BOOL tofront,BOOL toback,
                                 BOOL zip,long *next) */
   ARX_WINDOWTOBACK,       /* (void) */
   ARX_WINDOWTOFRONT,      /* (void) */
   ARX_HTTPDEBUG,          /* (BOOL on) */
};
