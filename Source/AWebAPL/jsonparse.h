/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2025-2026 amigazen project
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

/* jsonparse.h - Minimal JSON parser for ARexx JSONGET command */

#ifndef JSONPARSE_H
#define JSONPARSE_H

struct Arexxcmd;  /* Forward declaration */

/* Extract value(s) from JSON data by dot-separated path.
 *
 * data     - JSON string to parse
 * datalen  - length of data
 * path     - dot-separated path ("query.pages.*.extract")
 *            Special segments:
 *              *   - wildcard, matches first key in object
 *              #   - return count of elements
 *              N   - numeric, 1-based array index
 * ac       - ARexx command (for setting stem variables), may be NULL
 * varname  - if non-NULL, result stored in named variable (via ac)
 * stem     - if non-NULL, array/object expanded into stem vars
 *
 * Returns: Dupstr'd result string (caller must FREE), or NULL on error.
 *
 * Stem variable format for arrays:
 *   STEM.0 = count
 *   STEM.1 = first element value
 *   STEM.2 = second element value, etc.
 *
 * Stem variable format for objects:
 *   STEM.0 = count of keys
 *   STEM.1.KEY = first key name
 *   STEM.1.VALUE = first value
 *   STEM.2.KEY = second key name, etc.
 */
extern UBYTE *Jsonget(UBYTE *data,long datalen,UBYTE *path,
                      struct Arexxcmd *ac,UBYTE *varname,UBYTE *stem);

#endif /* JSONPARSE_H */
