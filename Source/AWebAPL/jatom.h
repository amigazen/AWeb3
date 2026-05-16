/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * Property-name interning (atoms): one canonical NUL-terminated string per
 * distinct identifier per root Jcontext, shared across Variables for fast
 * keyed lookup and lower fragmentation.
 *
 **********************************************************************/

#ifndef JATOM_H
#define JATOM_H

#include <exec/types.h>

struct Jcontext;

struct JAtomStr
{
   struct JAtomStr *bucketnext;
   ULONG hash;
   UBYTE *str;
};

/* Reject NULL, low memory, and obvious ROM addresses before dereferencing. */
#define JPTR_OK(p)  ((p) && (ULONG)(p) >= 0x00001000UL && (ULONG)(p) <= 0xFFFFFFF0UL)

extern struct Jcontext *Jatomroot(struct Jcontext *jc);
extern struct JAtomStr *JatomIntern(struct Jcontext *jc, UBYTE *s);

#endif
