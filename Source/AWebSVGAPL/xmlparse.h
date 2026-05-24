/**********************************************************************
 *
 * This file is part of the AWeb distribution
 *
 * Copyright (C) 2026 amigazen project
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

/* xmlparse.h - Small DOM-style XML parser for the SVG plugin.
 *
 * Design notes:
 *   - The parser is destructive: it overwrites separator bytes in the
 *     input buffer with NULs so that element and attribute names can
 *     be used directly as C strings.  Attribute values and text nodes
 *     are entity-decoded into the caller's pool.  The caller must not
 *     free the input buffer until it is finished with the tree.
 *   - All nodes and attributes are allocated from a caller-supplied
 *     memory pool (exec.library CreatePool).  When the caller is done
 *     it calls DeletePool() and the whole tree disappears in one step,
 *     no per-node bookkeeping required.
 *   - The parser is intentionally lenient: malformed XML is recovered
 *     by skipping forward to the next '<'.  This matches the way most
 *     real-world SVG files behave (and the way browsers handle them).
 */

#ifndef XMLPARSE_H
#define XMLPARSE_H

#include <exec/types.h>

/* Node types */
#define XMLN_ELEMENT   1
#define XMLN_TEXT      2

struct XmlAttr
{  struct XmlAttr *next;
   UBYTE *name;            /* NUL-terminated (in-place in buffer) */
   UBYTE *value;           /* NUL-terminated, pool allocated */
   LONG namelen;
   LONG valuelen;
};

struct XmlNode
{  USHORT type;
   USHORT pad;
   UBYTE *name;            /* element name, NUL-terminated */
   UBYTE *text;            /* text content for XMLN_TEXT, pool allocated */
   LONG namelen;
   LONG textlen;
   struct XmlAttr *attrs;
   struct XmlNode *parent;
   struct XmlNode *firstchild;
   struct XmlNode *lastchild;
   struct XmlNode *nextsibling;
};

/* Parse a buffer in place.  pool must be a valid memory pool created
 * with CreatePool().  Returns the root element node (the outermost
 * tag, e.g. <svg>), or NULL on out-of-memory or completely-empty
 * input.  The buffer is modified during parsing. */
struct XmlNode *XmlParse(APTR pool, UBYTE *buffer, LONG length);

/* Find an attribute by name (case-insensitive).  Returns NULL if no
 * such attribute exists.  Returns a NUL-terminated string owned by the
 * parser pool. */
UBYTE *XmlAttrValue(struct XmlNode *node, const char *name);

/* Same, but also returns the length. */
UBYTE *XmlAttrValueLen(struct XmlNode *node, const char *name, LONG *length);

/* Case-insensitive name match (n bytes of element name vs. literal). */
BOOL XmlNameIs(struct XmlNode *node, const char *name);

#endif
