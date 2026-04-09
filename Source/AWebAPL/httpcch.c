/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * httpcch.c - Cache-Control directive parsing (RFC 9111 subset).
 **********************************************************************/

#include "aweb.h"
#include "httpcch.h"
#include <ctype.h>
#include <stdio.h>

void Http_cc_reset_accum(struct Http_cc_accum *a)
{
   if(!a)
      return;
   a->forbid_disk = FALSE;
   a->must_revalidate = FALSE;
   a->saw_positive_max_age = FALSE;
   a->min_max_age = -1;
}

void Http_cc_parse(UBYTE *v, struct Http_cc_accum *z)
{
   UBYTE *h;
   UBYTE *eq;
   long n;
   long line_min;

   Http_cc_reset_accum(z);
   if(!v)
      return;
   line_min = -1;
   for(h = v; *h; h++)
   {
      if(STRNIEQUAL(h, "must-revalidate", 15))
      {
         if(!h[15] || h[15] == ',' || isspace((UBYTE)h[15]))
            z->must_revalidate = TRUE;
      }
      if(STRNIEQUAL(h, "no-store", 8))
      {
         if(!h[8] || h[8] == ',' || isspace((UBYTE)h[8]))
            z->forbid_disk = TRUE;
      }
      if(STRNIEQUAL(h, "no-cache", 8))
      {
         if(!h[8] || h[8] == ',' || isspace((UBYTE)h[8]))
            z->forbid_disk = TRUE;
      }
      if(STRNIEQUAL(h, "max-age", 7))
      {
         eq = h + 7;
         while(*eq == ' ' || *eq == '\t')
            eq++;
         if(*eq != '=')
            continue;
         eq++;
         while(*eq == ' ' || *eq == '\t')
            eq++;
         n = -999999;
         if(sscanf((char *)eq, "%ld", &n) != 1)
            continue;
         if(n <= 0)
            z->forbid_disk = TRUE;
         else
         {
            if(line_min < 0 || n < line_min)
               line_min = n;
         }
      }
   }
   if(line_min >= 0)
   {
      z->saw_positive_max_age = TRUE;
      z->min_max_age = line_min;
   }
}

void Http_cc_merge(struct Http_cc_accum *dst, struct Http_cc_accum *src)
{
   if(!dst || !src)
      return;
   dst->forbid_disk |= src->forbid_disk;
   dst->must_revalidate |= src->must_revalidate;
   if(src->saw_positive_max_age)
   {
      if(!dst->saw_positive_max_age || src->min_max_age < dst->min_max_age)
      {
         dst->min_max_age = src->min_max_age;
         dst->saw_positive_max_age = TRUE;
      }
   }
}
