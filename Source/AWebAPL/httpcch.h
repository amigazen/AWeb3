/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * Shared Cache-Control parsing for HTTP (http.c) and channel fetch (fetch.c).
 **********************************************************************/

#ifndef AWEB_HTTPCC_H
#define AWEB_HTTPCC_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

struct Http_cc_accum
{  BOOL forbid_disk;
   BOOL saw_positive_max_age;
   long min_max_age;       /* smallest positive max-age on this line or merged set */
   BOOL must_revalidate;
};

void Http_cc_reset_accum(struct Http_cc_accum *a);
void Http_cc_parse(UBYTE *v, struct Http_cc_accum *out);
void Http_cc_merge(struct Http_cc_accum *dst, struct Http_cc_accum *src);

#endif
