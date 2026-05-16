#ifndef _AWEBJS_REGEXP_H
#define _AWEBJS_REGEXP_H

/* Forward declaration for PCRE compiled pattern */
struct pcre;

/* Internal object value */
struct Regexp
{
    UBYTE *pattern;
    UWORD flags;
    struct pcre  *compiled;
    int   lastIndex;
};

#define REF_GLOBAL 0x0001
#define REF_MULTI  0x0002
#define REF_NOCASE 0x0004

/* PCRE constants - these should match pcre.h */
#ifndef PCRE_INFO_CAPTURECOUNT
#define PCRE_INFO_CAPTURECOUNT 2
#endif

#ifndef PCRE_INFO_BACKREFMAX
#define PCRE_INFO_BACKREFMAX 3
#endif

#ifndef PCRE_MULTILINE
#define PCRE_MULTILINE 0x00000008
#endif

#ifndef PCRE_CASELESS
#define PCRE_CASELESS 0x00000001
#endif

#endif

