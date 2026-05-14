/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * Atom table: separate chaining in jatom_buckets on the root Jcontext
 * (eval child contexts share the parent's pool and bucket array via
 * Jatomroot). Storage uses jc->pool for amortized DeletePool cleanup.
 *
 **********************************************************************/

#include "awebjs.h"
#include "jprotos.h"
#include "jatom.h"

#include <string.h>

struct Jcontext *Jatomroot(struct Jcontext *jc)
{
   ULONG hops;

   hops = 0UL;
   if(!jc)
   {
      return NULL;
   }
   /* truecontext must terminate; a corrupt cycle would hang or fault. */
   while(jc->truecontext)
   {
      if(++hops > 256UL)
      {
         return NULL;
      }
      jc = jc->truecontext;
   }
   return jc;
}

static ULONG Jatomhashbytes(UBYTE *s)
{
   ULONG h;

   h = 2166136261UL;
   while(s && *s)
   {
      h ^= (ULONG)(UBYTE)*s;
      s++;
      h *= 16777619UL;
   }
   return h;
}

struct JAtomStr *JatomIntern(struct Jcontext *jc, UBYTE *s)
{
   ULONG h;
   ULONG b;
   struct JAtomStr *p;
   struct JAtomStr *n;
   struct Jcontext *root;

   if(!s)
   {
      return NULL;
   }
   root = Jatomroot(jc);
   if(!root)
   {
      return NULL;
   }
   h = Jatomhashbytes(s);
   b = h % (ULONG)JATOM_NUM_BUCKETS;
   for(p = root->jatom_buckets[b]; p; p = p->bucketnext)
   {
      if(p->hash == h && p->str && STREQUAL(p->str, s))
      {
         return p;
      }
   }
   n = ALLOCSTRUCT(JAtomStr, 1, 0, root->pool);
   if(!n)
   {
      return NULL;
   }
   n->hash = h;
   n->bucketnext = root->jatom_buckets[b];
   n->str = Jdupstr(s, -1, root->pool);
   if(!n->str)
   {
      FREE(n);
      return NULL;
   }
   root->jatom_buckets[b] = n;
   return n;
}
