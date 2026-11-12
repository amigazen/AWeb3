/**********************************************************************
 *
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2026 amigazen project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the AWeb Public License as included in this
 * distribution.
 *
 **********************************************************************/

/* anubis.h - Native Techaro Anubis challenge helper (not Cloudflare).
 *
 * Sites such as eab.abime.net sit behind Anubis. A Mozilla User-Agent gets a
 * JS proof-of-work page; AWeb cannot run that JS, so we solve the "fast"/"slow"
 * SHA-256 challenge in-process and return an HTTP clientpull string that hits
 * pass-challenge. Metarefresh challenges are left to the HTTP Refresh header.
 */

#ifndef AWEB_ANUBIS_H
#define AWEB_ANUBIS_H

/* If html looks like an Anubis fast/slow challenge, solve it and return a
 * Dupstr'd clientpull string ("0; URL=/.within.website/.../pass-challenge?...").
 * Caller must FREE. Returns NULL when not Anubis, already handled, or unsolved.
 * pageurl is the absolute document URL (used for redir=). */
extern UBYTE *Anubis_clientpull(UBYTE *html,long htmllen,UBYTE *pageurl);

#endif /* AWEB_ANUBIS_H */
