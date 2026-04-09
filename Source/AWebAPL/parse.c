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

/* parse.c aweb html markup parse */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <stdarg.h>
#include "aweb.h"
#include "html.h"
#include "application.h"
#include "docprivate.h"
#include "jslib.h"

#define MAXATTRS 40
static struct Tagattr tagattr[MAXATTRS];

static LIST(Tagattr) attrs;
static short nextattr;

struct Tagdes
{  UBYTE *name;
   USHORT type;
   BOOL container;
};

static struct Tagdes tags[]=
{  "A",        MARKUP_A,            TRUE,
   "ADDRESS",  MARKUP_ADDRESS,      TRUE,
   "AREA",     MARKUP_AREA,         FALSE,
   "B",        MARKUP_B,            TRUE,
   "BASE",     MARKUP_BASE,         FALSE,
   "BASEFONT", MARKUP_BASEFONT,     FALSE,
   "BGSOUND",  MARKUP_BGSOUND,      FALSE,
   "BIG",      MARKUP_BIG,          TRUE,
   "BLINK",    MARKUP_BLINK,        TRUE,
   "BLOCKQUOTE",MARKUP_BLOCKQUOTE,  TRUE,
   "BODY",     MARKUP_BODY,         TRUE,
   "BQ",       MARKUP_BLOCKQUOTE,   TRUE,
   "BR",       MARKUP_BR,           FALSE,
   "BUTTON",   MARKUP_BUTTON,       TRUE,
   "CAPTION",  MARKUP_CAPTION,      TRUE,
   "CENTER",   MARKUP_CENTER,       TRUE,
   "CITE",     MARKUP_CITE,         TRUE,
   "CODE",     MARKUP_CODE,         TRUE,
   "COL",      MARKUP_COL,          FALSE,
   "COLGROUP", MARKUP_COLGROUP,     TRUE,
   "DD",       MARKUP_DD,           FALSE,
   "DEL",      MARKUP_DEL,          TRUE,
   "DFN",      MARKUP_DFN,          TRUE,
   "DIR",      MARKUP_DIR,          TRUE,
   "DIV",      MARKUP_DIV,          TRUE,
   "DL",       MARKUP_DL,           TRUE,
   "DT",       MARKUP_DT,           FALSE,
   "EM",       MARKUP_EM,           TRUE,
   "EMBED",    MARKUP_EMBED,        FALSE,
   "FIELDSET", MARKUP_FIELDSET,     TRUE,
   "FONT",     MARKUP_FONT,         TRUE,
   "FORM",     MARKUP_FORM,         TRUE,
   "FRAME",    MARKUP_FRAME,        FALSE,
   "FRAMESET", MARKUP_FRAMESET,     TRUE,
   "H1",       MARKUP_H1,           TRUE,
   "H2",       MARKUP_H2,           TRUE,
   "H3",       MARKUP_H3,           TRUE,
   "H4",       MARKUP_H4,           TRUE,
   "H5",       MARKUP_H5,           TRUE,
   "H6",       MARKUP_H6,           TRUE,
   "HEAD",     MARKUP_HEAD,         TRUE,
   "HR",       MARKUP_HR,           FALSE,
   "HTML",     MARKUP_HTML,         TRUE,
   "I",        MARKUP_I,            TRUE,
   "IFRAME",   MARKUP_IFRAME,       TRUE,
   "IMAGE",    MARKUP_IMG,          FALSE,
   "IMG",      MARKUP_IMG,          FALSE,
   "INPUT",    MARKUP_INPUT,        FALSE,
   "INS",      MARKUP_INS,          TRUE,
   "ISINDEX",  MARKUP_ISINDEX,      FALSE,
   "KBD",      MARKUP_KBD,          TRUE,
   "LEGEND",   MARKUP_LEGEND,       TRUE,
   "LI",       MARKUP_LI,           FALSE,
   "LINK",     MARKUP_LINK,         FALSE,
   "LISTING",  MARKUP_LISTING,      TRUE,
   "MAP",      MARKUP_MAP,          TRUE,
   "MARQUEE",  MARKUP_MARQUEE,      TRUE,
   "MENU",     MARKUP_MENU,         TRUE,
   "META",     MARKUP_META,         FALSE,
   "NOBR",     MARKUP_NOBR,         TRUE,
   "NOFRAME",  MARKUP_NOFRAMES,     TRUE,
   "NOFRAMES", MARKUP_NOFRAMES,     TRUE,
   "NOSCRIPT", MARKUP_NOSCRIPT,     TRUE,
   "OBJECT",   MARKUP_OBJECT,       TRUE,
   "OL",       MARKUP_OL,           TRUE,
   "OPTION",   MARKUP_OPTION,       FALSE,
   "P",        MARKUP_P,            TRUE,
   "PARAM",    MARKUP_PARAM,        FALSE,
   "PRE",      MARKUP_PRE,          TRUE,
   "S",        MARKUP_STRIKE,       TRUE,
   "SAMP",     MARKUP_SAMP,         TRUE,
   "SCRIPT",   MARKUP_SCRIPT,       TRUE,
   "SELECT",   MARKUP_SELECT,       TRUE,
   "SMALL",    MARKUP_SMALL,        TRUE,
   "SPAN",     MARKUP_SPAN,         TRUE,
   "STRIKE",   MARKUP_STRIKE,       TRUE,
   "STRONG",   MARKUP_STRONG,       TRUE,
   "STYLE",    MARKUP_STYLE,        TRUE,
   "SUB",      MARKUP_SUB,          TRUE,
   "SUP",      MARKUP_SUP,          TRUE,
   "TABLE",    MARKUP_TABLE,        TRUE,
   "TBODY",    MARKUP_TBODY,        TRUE,
   "TD",       MARKUP_TD,           TRUE,
   "TEXTAREA", MARKUP_TEXTAREA,     TRUE,
   "TFOOT",    MARKUP_TFOOT,        TRUE,
   "TH",       MARKUP_TH,           TRUE,
   "THEAD",    MARKUP_THEAD,        TRUE,
   "TITLE",    MARKUP_TITLE,        TRUE,
   "TR",       MARKUP_TR,           TRUE,
   "TT",       MARKUP_TT,           TRUE,
   "U",        MARKUP_U,            TRUE,
   "UL",       MARKUP_UL,           TRUE,
   "VAR",      MARKUP_VAR,          TRUE,
   "WBR",      MARKUP_WBR,          FALSE,
   "XMP",      MARKUP_XMP,          TRUE,
};
#define NRTAGS (sizeof(tags)/sizeof(struct Tagdes))

struct Attrdes
{  UBYTE *name;
   USHORT type;
};

static struct Attrdes tagattrs[]=
{  "ACTION",            TAGATTR_ACTION,
   "ALIGN",             TAGATTR_ALIGN,
   "ALINK",             TAGATTR_ALINK,
   "ALT",               TAGATTR_ALT,
   "BACKGROUND",        TAGATTR_BACKGROUND,
   "BEHAVIOR",          TAGATTR_BEHAVIOR,
   "BGCOLOR",           TAGATTR_BGCOLOR,
   "BORDER",            TAGATTR_BORDER,
   "BORDERCOLOR",       TAGATTR_BORDERCOLOR,
   "BORDERCOLORDARK",   TAGATTR_BORDERCOLORDARK,
   "BORDERCOLORLIGHT",  TAGATTR_BORDERCOLORLIGHT,
   "CELLPADDING",       TAGATTR_CELLPADDING,
   "CELLSPACING",       TAGATTR_CELLSPACING,
   "CHECKED",           TAGATTR_CHECKED,
   "CLASS",             TAGATTR_CLASS,
   "CLASSID",           TAGATTR_CLASSID,
   "CLEAR",             TAGATTR_CLEAR,
   "CODEBASE",          TAGATTR_CODEBASE,
   "CODETYPE",          TAGATTR_CODETYPE,
   "COLOR",             TAGATTR_COLOR,
   "COLS",              TAGATTR_COLS,
   "COLSPAN",           TAGATTR_COLSPAN,
   "CONTENT",           TAGATTR_CONTENT,
   "CONTINUE",          TAGATTR_CONTINUE,
   "COORDS",            TAGATTR_COORDS,
   "DATA",              TAGATTR_DATA,
   "DECLARE",           TAGATTR_DECLARE,
   "DINGBAT",           TAGATTR_DINGBAT,
   "DIRECTION",        TAGATTR_DIRECTION,
   "ENCTYPE",           TAGATTR_ENCTYPE,
   "FACE",              TAGATTR_FACE,
   "FRAME",             TAGATTR_FRAME,
   "FRAMEBORDER",       TAGATTR_FRAMEBORDER,
   "FRAMESPACING",      TAGATTR_FRAMESPACING,
   "HEIGHT",            TAGATTR_HEIGHT,
   "HIDDEN",            TAGATTR_HIDDEN,
   "HREF",              TAGATTR_HREF,
   "HSPACE",            TAGATTR_HSPACE,
   "HTTP-EQUIV",        TAGATTR_HTTP_EQUIV,
   "ID",                TAGATTR_ID,
   "ISMAP",             TAGATTR_ISMAP,
   "LANGUAGE",          TAGATTR_LANGUAGE,
   "LEFTMARGIN",        TAGATTR_LEFTMARGIN,
   "LINK",              TAGATTR_LINK,
   "LOOP",              TAGATTR_LOOP,
   "MARGINHEIGHT",      TAGATTR_MARGINHEIGHT,
   "MARGINWIDTH",       TAGATTR_MARGINWIDTH,
   "MAXLENGTH",         TAGATTR_MAXLENGTH,
   "METHOD",            TAGATTR_METHOD,
   "MULTIPLE",          TAGATTR_MULTIPLE,
   "NAME",              TAGATTR_NAME,
   "NOHREF",            TAGATTR_NOHREF,
   "NORESIZE",          TAGATTR_NORESIZE,
   "NOSHADE",           TAGATTR_NOSHADE,
   "NOWRAP",            TAGATTR_NOWRAP,
   "ONABORT",           TAGATTR_ONABORT,
   "ONBLUR",            TAGATTR_ONBLUR,
   "ONCHANGE",          TAGATTR_ONCHANGE,
   "ONCLICK",           TAGATTR_ONCLICK,
   "ONERROR",           TAGATTR_ONERROR,
   "ONFOCUS",           TAGATTR_ONFOCUS,
   "ONLOAD",            TAGATTR_ONLOAD,
   "ONMOUSEOUT",        TAGATTR_ONMOUSEOUT,
   "ONMOUSEOVER",       TAGATTR_ONMOUSEOVER,
   "ONRESET",           TAGATTR_ONRESET,
   "ONSELECT",          TAGATTR_ONSELECT,
   "ONSUBMIT",          TAGATTR_ONSUBMIT,
   "ONUNLOAD",          TAGATTR_ONUNLOAD,
   "PLAIN",             TAGATTR_PLAIN,
   "PROMPT",            TAGATTR_PROMPT,
   "REL",               TAGATTR_REL,
   "ROWS",              TAGATTR_ROWS,
   "ROWSPAN",           TAGATTR_ROWSPAN,
   "RULES",             TAGATTR_RULES,
   "SCROLLING",         TAGATTR_SCROLLING,
   "SCROLLAMOUNT",      TAGATTR_SCROLLAMOUNT,
   "SCROLLDELAY",       TAGATTR_SCROLLDELAY,
   "SELECTED",          TAGATTR_SELECTED,
   "SEQNUM",            TAGATTR_SEQNUM,
   "SHAPE",             TAGATTR_SHAPE,
   "SHAPES",            TAGATTR_SHAPES,
   "SIZE",              TAGATTR_SIZE,
   "SKIP",              TAGATTR_SKIP,
   "SPAN",              TAGATTR_SPAN,
   "SRC",               TAGATTR_SRC,
   "STANDBY",           TAGATTR_STANDBY,
   "START",             TAGATTR_START,
   "STYLE",             TAGATTR_STYLE,
   "TARGET",            TAGATTR_TARGET,
   "TEXT",              TAGATTR_TEXT,
   "TITLE",             TAGATTR_TITLE,
   "TOPMARGIN",         TAGATTR_TOPMARGIN,
   "TYPE",              TAGATTR_TYPE,
   "USEMAP",            TAGATTR_USEMAP,
   "VALIGN",            TAGATTR_VALIGN,
   "VALUE",             TAGATTR_VALUE,
   "VALUETYPE",         TAGATTR_VALUETYPE,
   "VLINK",             TAGATTR_VLINK,
   "VSPACE",            TAGATTR_VSPACE,
   "WIDTH",             TAGATTR_WIDTH,
};
#define NRATTRS   (sizeof(tagattrs)/sizeof(struct Attrdes))

struct Chardes
{  UBYTE *name;
   USHORT ch;
};

static struct Chardes chars[]=
{  "AElig", 198,
   "Aacute",193,
   "Acirc", 194,
   "Agrave",192,
   "Aring", 197,
   "Atilde",195,
   "Auml",  196,
   "Ccedil",199,
   "Dagger",135,  /* Latin-1 double dagger (Windows-1252 extension) */
   "ETH",   208,
   "Eacute",201,
   "Ecirc", 202,
   "Egrave",200,
   "Euml",  203,
   "Iacute",205,
   "Icirc", 206,
   "Igrave",204,
   "Iuml",  207,
   "Ntilde",209,
   "OElig", 338,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "Oacute",211,
   "Ocirc", 212,
   "Ograve",210,
   "Oslash",216,
   "Otilde",213,
   "Ouml",  214,
   "Prime", 148,  /* Approximated as right double quote in Latin-1 */
   "Scaron",352,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "THORN", 222,
   "Uacute",218,
   "Ucirc", 219,
   "Ugrave",217,
   "Uuml",  220,
   "Yacute",221,
   "Yuml",  376,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "aacute",225,
   "acirc", 226,
   "acute", 180,
   "aelig", 230,
   "agrave",224,
   "amp",   38,
   "apos",  39,
   "aring", 229,
   "atilde",227,
   "auml",  228,
   "bdquo", 132,  /* Latin-1 double low-9 quotation mark (Windows-1252 extension) */
   "brkbar",166,  /* Latin-1 broken vertical bar (alternate name for brvbar) */
   "brvbar",166,
   "bull",  183,  /* Latin-1 MIDDLE DOT (·) for bullet - U+00B7 */
   "ccedil",231,
   "cedil", 184,
   "cent",  162,
   "circ",  136,  /* Latin-1 circumflex accent (Windows-1252 extension) */
   "copy",  169,
   "curren",164,
   "dagger",134,  /* Latin-1 dagger (Windows-1252 extension) */
   "deg",   176,
   "die",   168,  /* Latin-1 spacing dieresis or umlaut (alternate name for uml) */
   "divide",247,
   "eacute",233,
   "ecirc", 234,
   "egrave",232,
   "empty", 8709,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "emsp",  8195,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "ensp",  8194,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "eth",   240,
   "euml",  235,
   "euro",  164,  /* Approximated as currency symbol (¤) in Latin-1 */
   "fnof",  402,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "frac12",189,
   "frac14",188,
   "frac34",190,
   "frasl", 8260,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "half",  189,  /* Latin-1 fraction 1/2 (alternate name for frac12) */
   "ge",    8805,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "gt",    62,
   "hellip",133,  /* Latin-1 horizontal ellipsis (Windows-1252 extension) */
   "hibar", 175,  /* Latin-1 spacing macron (alternate name for macr) */
   "iacute",237,
   "icirc", 238,
   "iexcl", 161,
   "igrave",236,
   "iquest",191,
   "iuml",  239,
   "lang",  9001,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "laquo", 171,
   "larr",  8592,  /* Left arrow (←) - Unicode U+2190 */
   "ldquo", 147,  /* Latin-1 left double quotation mark (Windows-1252 extension) */
   "le",    8804,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "lowast",8727,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "loz",   9674,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "lsaquo",139,  /* Latin-1 single left-pointing angle quotation mark (Windows-1252 extension) */
   "lsquo", 145,  /* Latin-1 left single quotation mark (Windows-1252 extension) */
   "lt",    60,
   "macr",  175,
   "mdash", 151,  /* Latin-1 em dash (Windows-1252 extension) */
   "micro", 181,
   "middot",183,
   "minus", 8722,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "nbsp",  160,
   "ndash", 150,  /* Latin-1 en dash (Windows-1252 extension) */
   "not",   172,
   "ntilde",241,
   "oacute",243,
   "ocirc", 244,
   "oelig", 339,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "ograve",242,
   "oline", 175,  /* Approximated as macron in Latin-1 */
   "ordf",  170,
   "ordm",  186,
   "oslash",248,
   "otilde",245,
   "ouml",  246,
   "para",  182,
   "permil",137,  /* Latin-1 per mille sign (Windows-1252 extension) */
   "plusmn",177,
   "pound", 163,
   "prime", 146,  /* Approximated as right single quote in Latin-1 */
   "quot",  34,
   "rang",  9002,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "rarr",  8594,  /* Right arrow (→) - Unicode U+2192 */
   "raquo", 187,
   "reg",   174,  /* Latin-1 Registered trademark symbol */
   "rdquo", 148,  /* Latin-1 right double quotation mark (Windows-1252 extension) */
   "rsaquo",155,  /* Latin-1 single right-pointing angle quotation mark (Windows-1252 extension) */
   "rsquo", 146,  /* Latin-1 right single quotation mark (Windows-1252/ISO 8859-1 extension) */
   "sbquo", 130,  /* Latin-1 single low-9 quotation mark (Windows-1252 extension) */
   "scaron",353,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "sdot",  8901,
   "sect",  167,
   "shy",   173,
   "sim",   8764,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "sup1",  185,
   "sup2",  178,
   "sup3",  179,
   "szlig", 223,
   "thinsp",8201,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "thorn", 254,
   "tilde", 152,  /* Latin-1 small tilde (Windows-1252 extension) */
   "times", 215,
   "trade", 153,  /* Approximated as trademark symbol in Windows-1252 extension (Latin-1 compatible) */
   "uacute",250,
   "ucirc", 251,
   "ugrave",249,
   "uml",   168,
   "uuml",  252,
   "yacute",253,
   "yen",   165,
   "yuml",  255,
   "zwj",   8205,  /* No Latin-1 equivalent, kept as Unicode for translation */
   "zwnj",  8204,  /* No Latin-1 equivalent, kept as Unicode for translation */
};
#define NRCHARS (sizeof(chars)/sizeof(struct Chardes))

static UBYTE *icons[]=
{  "archive",
   "audio",
   "binary.document",
   "binhex.document",
   "calculator",
   "caution",
   "cd.i",
   "cd.rom",
   "clock",
   "compressed.document",
   "disk.drive",
   "diskette",
   "display",
   "document",
   "fax",
   "filing.cabinet",
   "film",
   "fixed.disk",
   "folder",
   "form",
   "ftp",
   "glossary",
   "gopher",
   "home",
   "html",
   "image",
   "index",
   "keyboard",
   "mail",
   "mail.in",
   "mail.out",
   "map",
   "mouse",
   "network",
   "new",
   "next",
   "notebook",
   "parent",
   "play.fast.forward",
   "play.fast.reverse",
   "play.pause",
   "play.start",
   "play.stop",
   "previous",
   "printer",
   "sadsmiley",
   "smiley",
   "stop",
   "summary",
   "telephone",
   "telnet",
   "text.document",
   "tn3270",
   "toc",
   "trash",
   "unknown.document",
   "uuencoded.document",
   "work",
   "www",
};
#define NRICONS (sizeof(icons)/sizeof(UBYTE *))

static struct Tagdes *Findtag(UBYTE *name)
{  short a=0,b=NRTAGS-1,m;
   long c;
   while(a<=b)
   {  m=(a+b)/2;
      c=Stricmp(tags[m].name,name);
      if(c==0) return &tags[m];
      if(c<0) a=m+1;
      else b=m-1;
   }
   return NULL;
}

static struct Attrdes *Findattr(UBYTE *name)
{  short a=0,b=NRATTRS-1,m;
   long c;
   while(a<=b)
   {  m=(a+b)/2;
      c=Stricmp(tagattrs[m].name,name);
      if(c==0) return &tagattrs[m];
      if(c<0) a=m+1;
      else b=m-1;
   }
   return NULL;
}

static struct Chardes *Findchar(UBYTE *name)
{  short i;
   /* Use linear search with case-sensitive comparison */
   for(i=0;i<NRCHARS;i++)
   {  if(!strcmp(chars[i].name,name)) return &chars[i];
   }
   return NULL;
}

static UBYTE *Findicon(UBYTE *name)
{  short a=0,b=NRICONS-1,m;
   long c;
   while(a<=b)
   {  m=(a+b)/2;
      c=strcmp(icons[m],name);
      if(c==0) return icons[m];
      if(c<0) a=m+1;
      else b=m-1;
   }
   return NULL;
}

static struct Tagattr *Nextattr(struct Document *doc)
{  struct Tagattr *ta;
   if(nextattr==MAXATTRS) REMOVE(&tagattr[--nextattr]);
   ta=&tagattr[nextattr++];
   ADDTAIL(&attrs,ta);
   ta->attr=0;
   ta->valuepos=doc->args.length;
   ta->length=0;
   return ta;
}

/* Remove newlines, and all spaces surrounding newlines, from attribute value */
static void Removenl(struct Buffer *buf,struct Tagattr *ta)
{  UBYTE *p=buf->buffer+ta->valuepos;
   UBYTE *q,*s;
   UBYTE *end=p+ta->length;
   BOOL skipsp=FALSE;
   q=p;     /* destination */
   s=NULL;  /* first space written to destination */
   while(p<end)
   {  if(Isspace(*p))
      {  if(!s) s=q;
         if(*p=='\n')
         {  skipsp=TRUE;
            q=s;
         }
         if(!skipsp) *q++=*p;
      }
      else
      {  *q++=*p;
         s=NULL;
         skipsp=FALSE;
      }
      p++;
   }
   *q='\0';
   ta->length=q-(buf->buffer+ta->valuepos);
}

static void Translate(struct Document *doc,struct Buffer *buf,struct Tagattr *ta,
   BOOL isattr)
{  UBYTE *p=buf->buffer+ta->valuepos,*q,*r,*s;
   UBYTE *end=p+ta->length;
   UBYTE ebuf[12];
   ULONG n;
   BOOL valid;
   BOOL strict=(doc->htmlmode==HTML_STRICT),lf=(doc->pmode==DPM_TEXTAREA);
   short l;
   
   /* Lookup tables for Latin Extended-A (U+0100-U+017F) and Latin Extended-B (U+0180-U+024F) */
   /* These map UTF-8 sequences 0xC4 and 0xC5 (Latin Extended-A) and 0xC6 and 0xC7 (Latin Extended-B) */
   static const UBYTE latin_ext_a_c4[] =
      "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiiiJjKkkLlLlLlL";
   static const UBYTE latin_ext_a_c5[] =
      "lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
   static const UBYTE latin_ext_b_c6[] =
      "bBBbbbCCcDDDddEaEFfGGhIIKkllWNnOCoOoPpRS2EetTttUuUVYyZzEEee255?w";
   static const UBYTE latin_ext_b_c7[] =
      "||||DDdLLlNNnAaIiOoUuUuUuUuUueAaAaAaGgGgKkOoOoEejDDdGgHWNnAaAaOo";
   
   while(p<end)
   {  /* Detect and decode UTF-8 sequences.
       * 2-byte UTF-8: 0xC0-0xDF followed by 0x80-0xBF (range 0x80-0x7FF)
       * 3-byte UTF-8: 0xE0-0xEF followed by 0x80-0xBF, 0x80-0xBF (range 0x800-0xFFFF)
       * 4-byte UTF-8: 0xF0-0xF7 followed by 0x80-0xBF, 0x80-0xBF, 0x80-0xBF (range 0x10000-0x10FFFF)
       * This handles common Unicode characters like:
       * - Copyright (0xC2 0xA9) -> 0xA9
       * - Right single quotation mark (0xE2 0x80 0x99) -> 0x27 (')
       * - Left single quotation mark (0xE2 0x80 0x98) -> 0x60 (`)
       * - Em dash (0xE2 0x80 0x94) -> -- (two dashes) */
      ULONG utf8_char = 0;
      ULONG utf8_bytes = 0;
      UBYTE replacement = 0;
      UBYTE *replacement_str = NULL;
      BOOL skip_char = FALSE;
      
      /* Check for 4-byte UTF-8 sequence (0xF0-0xF7) */
      if((*p & 0xF8) == 0xF0 && p+3 < end && 
         (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80)
      {  /* 4-byte UTF-8: characters outside BMP (U+10000-U+10FFFF) */
         /* Replace with bullet character (0xB7) as these can't be represented in Latin-1 */
         utf8_bytes = 4;
         replacement = 0xB7;  /* Bullet character */
      }
      /* Check for 3-byte UTF-8 sequence (0xE0-0xEF) */
      else if((*p & 0xF0) == 0xE0 && p+2 < end && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
      {  /* 3-byte UTF-8: extract 16 bits from the three bytes */
         utf8_char = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
         utf8_bytes = 3;
         
         /* Handle specific 3-byte sequences */
         if(*p == 0xE2 && p[1] == 0x80)
         {  /* U+2000-U+20FF range (General Punctuation) */
            switch(p[2])
            {  case 0x8A: replacement = 0x20; break;  /* Hair space -> space */
               case 0x93: replacement = 0x2D; break;  /* En dash -> - */
               case 0x94: replacement_str = "--"; break;  /* Em dash -> -- (two dashes) */
               case 0x98: replacement = 0x60; break;  /* Left single quotation mark -> ` */
               case 0x99: replacement = 0x27; break;  /* Right single quotation mark -> ' */
               case 0x9C: replacement = 0x22; break;  /* Left double quotation mark -> " */
               case 0x9D: replacement = 0x22; break;  /* Right double quotation mark -> " */
               case 0xA6: replacement_str = "..."; break;  /* Horizontal ellipsis -> ... */
               case 0xAF: replacement = 0x20; break;  /* Narrow no-break space -> space */
               default:
                  replacement = 0xB7;  /* Bullet character for unmapped */
                  break;
            }
         }
         else if(*p == 0xE2 && p[1] == 0x96)
         {  /* U+2500-U+25FF range (Box Drawing) */
            if(p[2] == 0x88)
            {  replacement = 0x80;  /* Full block (U+2588) -> 0x80 (Amiga block */
            }
            else
            {  replacement = 0xB7;  /* Bullet character for other box drawing */
            }
         }
         else
         {  /* Other 3-byte sequences - map common characters */
            switch(utf8_char)
            {  case 0x2018: replacement = 0x60; break;  /* Left single quotation mark -> ` */
               case 0x2019: replacement = 0x27; break;  /* Right single quotation mark -> ' */
               case 0x201A: replacement = 0x27; break;  /* Single low-9 quotation mark -> ' */
               case 0x201C: replacement = 0x22; break;  /* Left double quotation mark -> " */
               case 0x201D: replacement = 0x22; break;  /* Right double quotation mark -> " */
               case 0x201E: replacement = 0x22; break;  /* Double low-9 quotation mark -> " */
               case 0x2013: replacement = 0x2D; break;  /* En dash -> - */
               case 0x2014: replacement_str = "--"; break;  /* Em dash -> -- (two dashes) */
               case 0x2022: replacement = 0x95; break;  /* Bullet -> 0x95 */
               case 0x2026: replacement_str = "..."; break;  /* Horizontal ellipsis -> ... */
               case 0x2039: replacement = 0x3C; break;  /* Single left-pointing angle quotation mark -> < */
               case 0x203A: replacement = 0x3E; break;  /* Single right-pointing angle quotation mark -> > */
               case 0x2122: replacement_str = "TM"; break;  /* Trade mark sign -> TM */
               case 0x00A0: 
                  /* Preserve non-breaking space in preformat mode to maintain column alignment */
                  if(doc->pflags&DPF_PREFORMAT) replacement = 0xA0;
                  else replacement = 0x20;  /* Non-breaking space -> space */
                  break;
               case 0x00A9: replacement = 0xA9; break;  /* Copyright sign -> 0xA9 */
               case 0x00AE: replacement = 0xAE; break;  /* Registered sign -> 0xAE */
               default:
                  /* For other 3-byte UTF-8 characters, try to map to Latin-1 if possible */
                  if(utf8_char >= 0x80 && utf8_char <= 0xFF)
                  {  replacement = (UBYTE)utf8_char;
                  }
                  else
                  {  /* Character outside Latin-1 range - replace with bullet */
                     replacement = 0xB7;
                  }
                  break;
            }
         }
      }
      /* Check for 2-byte UTF-8 sequence (0xC0-0xDF) */
      else if((*p & 0xE0) == 0xC0 && p+1 < end && (p[1] & 0xC0) == 0x80)
      {  /* 2-byte UTF-8: extract 11 bits from the two bytes */
         utf8_char = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
         utf8_bytes = 2;
         
         /* Handle specific 2-byte sequences */
         if(*p == 0xC2)
         {  /* U+0080-U+00BF (Latin-1 Supplement, first half) */
            if((p[1] & 0xE0) == 0xA0)
            {  /* U+00A0-U+00BF range */
               if(p[1] == 0xAD)
               {  /* Soft hyphen (U+00AD) - skip it */
                  skip_char = TRUE;
               }
               else if(p[1] == 0xA0)
               {  /* Non-breaking space (U+00A0) */
                  /* Preserve non-breaking space in preformat mode to maintain column alignment */
                  if(doc->pflags&DPF_PREFORMAT) replacement = 0xA0;
                  else replacement = 0x20;  /* Non-breaking space -> space */
               }
               else
               {  /* Map directly to Latin-1 */
                  replacement = p[1];
               }
            }
            else
            {  /* Other 0xC2 sequences - map directly if in range */
               if(utf8_char >= 0x80 && utf8_char <= 0xFF)
               {  replacement = (UBYTE)utf8_char;
               }
               else
               {  replacement = 0xB7;  /* Bullet character */
               }
            }
         }
         else if(*p == 0xC3)
         {  /* U+00C0-U+00FF (Latin-1 Supplement, second half) */
            /* Add 0x40 offset to map to Latin-1 */
            replacement = p[1] + 0x40;
         }
         else if(*p == 0xC4)
         {  /* U+0100-U+017F (Latin Extended-A, first half */
            if(p[1] >= 0x80 && p[1] <= 0xBF)
            {  UBYTE idx = p[1] - 0x80;
               if(idx < sizeof(latin_ext_a_c4) - 1)
               {  replacement = latin_ext_a_c4[idx];
               }
               else
               {  replacement = 0xB7;
               }
            }
            else
            {  replacement = 0xB7;
            }
         }
         else if(*p == 0xC5)
         {  /* U+0180-U+01FF (Latin Extended-A, second half) */
            if(p[1] >= 0x80 && p[1] <= 0xBF)
            {  UBYTE idx = p[1] - 0x80;
               if(idx < sizeof(latin_ext_a_c5) - 1)
               {  replacement = latin_ext_a_c5[idx];
               }
               else
               {  replacement = 0xB7;
               }
            }
            else
            {  replacement = 0xB7;
            }
         }
         else if(*p == 0xC6)
         {  /* U+0180-U+01BF (Latin Extended-B, first half) */
            if(p[1] >= 0x80 && p[1] <= 0xBF)
            {  UBYTE idx = p[1] - 0x80;
               if(idx < sizeof(latin_ext_b_c6) - 1)
               {  replacement = latin_ext_b_c6[idx];
               }
               else
               {  replacement = 0xB7;
               }
            }
            else
            {  replacement = 0xB7;
            }
         }
         else if(*p == 0xC7)
         {  /* U+01C0-U+01FF (Latin Extended-B, second half) */
            if(p[1] >= 0x80 && p[1] <= 0xBF)
            {  UBYTE idx = p[1] - 0x80;
               if(idx < sizeof(latin_ext_b_c7) - 1)
               {  replacement = latin_ext_b_c7[idx];
               }
               else
               {  replacement = 0xB7;
               }
            }
            else
            {  replacement = 0xB7;
            }
         }
         else if(*p == 0xCC || (*p == 0xCD && p[1] <= 0xAF))
         {  /* Combining characters (U+0300-U+036F) - skip them */
            skip_char = TRUE;
         }
         else
         {  /* Other 2-byte sequences - try to map to Latin-1 if in range */
            if(utf8_char >= 0x80 && utf8_char <= 0xFF)
            {  replacement = (UBYTE)utf8_char;
            }
            else
            {  /* Character outside Latin-1 range - replace with bullet */
               replacement = 0xB7;
            }
         }
      }
      
      /* If we found a valid UTF-8 sequence, replace it */
      if(utf8_bytes > 0)
      {  /* Skip combining characters and soft hyphens */
         if(skip_char)
         {  /* Remove the UTF-8 bytes by shifting remaining bytes left */
            if(p + utf8_bytes <= end)
            {  memmove(p, p + utf8_bytes, end - (p + utf8_bytes));
            }
            ta->length -= utf8_bytes;
            end -= utf8_bytes;
            continue;
         }
         
         if(replacement_str)
         {  /* Multi-character replacement (e.g., "..." or "TM") */
            long replen = strlen(replacement_str);
            long pos = p - buf->buffer;
            if(Insertinbuffer(buf, replacement_str, replen, pos))
            {  Deleteinbuffer(buf, pos + replen, utf8_bytes);
               ta->length += (replen - utf8_bytes);
               end = buf->buffer + buf->length;
               p = buf->buffer + pos + replen;
            }
            else
            {  /* Buffer expansion failed - use single char fallback */
               *p = 0x3F;
               memmove(p + 1, p + utf8_bytes, end - (p + utf8_bytes));
               ta->length -= (utf8_bytes - 1);
               end -= (utf8_bytes - 1);
               p++;
            }
            continue;
         }
         else if(replacement > 0)
         {  /* Single character replacement */
            *p = replacement;
            /* Remove the extra UTF-8 bytes by shifting remaining bytes left */
            if(p + utf8_bytes <= end)
            {  memmove(p + 1, p + utf8_bytes, end - (p + utf8_bytes));
            }
            ta->length -= (utf8_bytes - 1);
            end -= (utf8_bytes - 1);
            n = replacement;
            p++;
            continue;
         }
      }
      n=*p;
      if(*p=='&')
      {  q=p;
         if(++q>=end) break;
         if(*q=='#')
         {  n=0;
            if(isdigit(q[1]))
            {  while(++q<end && isdigit(*q)) n=10*n+(*q-'0');
            }
            else if(toupper(q[1])=='X')
            {  q++;
               while(++q<end && isxdigit(*q))
               {  n=16*n+((*q<='9')?(*q-'0'):(toupper(*q)-'A'+10));
               }
            }
            /* Blindly replace the entity by the character value.
             * Validity check and translation is done below.
             * First remember the exact source in case we don't know the character. */
            if(q<end)
            {  if(*q!=';') q--;
               l=q-p+1;
               if(l>11) l=11;
               strncpy(ebuf,p,l);
               ebuf[l]='\0';
               memmove(p,q,end+1-q);
            }
            else q--;
            ta->length-=(q-p);
            end-=(q-p);
            *p=n;
         }
         else if(isattr && *q=='{')
         {  /* JavaScript expression */
            q++;
            for(r=q;r<end && *r!='}';r++);
            if(s=Dupstr(q,r-q))
            {  struct Jcontext *jc;
               struct Jvar *jv;
               UBYTE *result;
               doc->dflags|=DDF_NOSPARE;
               Runjavascript(doc->frame,s,NULL);
               jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
               if(jc && (jv=Jgetreturnvalue(jc)))
               {  result=Jtostring(jc,jv);
               }
               else
               {  result="";
               }
               r++;
               if(r<end && *r==';') r++;
               l=strlen(result);
               Deleteinbuffer(buf,p-buf->buffer,r-p);
               Insertinbuffer(buf,result,l,p-buf->buffer);
               ta->length=ta->length-(r-p)+l;
               end=end-(r-p)+l;
               p+=l-1;
               FREE(s);
            }
            n=*p; /* No replacement */
         }
         else
         {  UBYTE name[8];
            struct Chardes *cd;
            short i;
            for(i=0;
               q<end && i<7 && (isalnum(*q) || *q=='.' || *q=='-');
               q++,i++) name[i]=*q;
            name[i]='\0';
            if(cd=Findchar(name))
            {  /* Always convert all entities regardless of HTML mode or character code.
                * In strict mode, name must be terminated by semicolon or non-alphanumeric. */
               if(STREQUAL(cd->name,name)
               && (!strict || (q>=end || !isalnum(*q))))
               {  q=p+1+strlen(cd->name); /* +1 because of & */
                  if(q<end)
                  {  if(*q!=';') q--;
                     memmove(p,q,end+1-q);
                  }
                  else q--;
                  ta->length-=(q-p);
                  end-=(q-p);
                  n=cd->ch;
                  *p=cd->ch;
               }
            }
         }
      }
      /* Translate win '95 characters if not strict and no foreign character set.
       * (n) contains character (possibly Unicode) */
      r=NULL;
      if(!strict && !(doc->dflags&DDF_FOREIGN) && n>=128 && n<=159)
      {  switch(n)
         {  case 130:n=(UBYTE)',';break;
            case 131:n=(UBYTE)'f';break;
            case 132:n=(UBYTE)'"';break;
            case 133:r="...";break;
            case 134:n=(UBYTE)'+';break;
            case 135:n=(UBYTE)'+';break;
            case 136:n=(UBYTE)'^';break;
            case 137:r="�/..";break;
            case 138:n=(UBYTE)'S';break;   /* S hacek */
            case 139:n=(UBYTE)'{';break;
            case 140:r="OE";break;
            case 145:n=(UBYTE)'`';break;
            case 146:n=(UBYTE)'\'';break;
            case 147:n=(UBYTE)'"';break;
            case 148:n=(UBYTE)'"';break;
            case 149:n=(UBYTE)0x95;break;
            case 150:n=(UBYTE)'-';break;
            case 151:n=(UBYTE)'-';break;
            case 152:n=(UBYTE)'~';break;
            case 153:r="TM";break;
            case 154:n=(UBYTE)'s';break;   /* s hacek */
            case 155:n=(UBYTE)'}';break;
            case 156:r="oe";break;
            case 159:n=(UBYTE)'Y';break;   /* Y trema */
         }
      }
      else if(n>255)
      {  switch(n)
         {  case 338:r="OE";break;
            case 339:r="oe";break;
            case 352:n=(UBYTE)'S';break;
            case 353:n=(UBYTE)'s';break;
            case 376:n=(UBYTE)'Y';break;
            case 402:n=(UBYTE)'f';break;
            case 710:n=(UBYTE)0x2C6;break;
            case 732:n=(UBYTE)'~';break;
            case 8194:n=(UBYTE)' ';break;
            case 8195:n=(UBYTE)' ';break;
            case 8201:n=(UBYTE)' ';break;
            case 8204:n=(UBYTE)' ';break;
            case 8205:n=(UBYTE)' ';break;
            case 8211:n=(UBYTE)'-';break;
            case 8212:n=(UBYTE)'-';break;
            case 8216:n=(UBYTE)'`';break;
            case 8217:n=(UBYTE)'\'';break;
            case 8218:n=(UBYTE)'"';break;
            case 8220:n=(UBYTE)'"';break;
            case 8221:n=(UBYTE)'"';break;
            case 8222:n=(UBYTE)'"';break;
            case 8224:n=(UBYTE)'+';break;
            case 8225:n=(UBYTE)'+';break;
            case 8226:n=(UBYTE)0x95;break;
            case 8230:r="...";break;
            case 8240:r="%/..";break;
            case 8242:n=(UBYTE)'\'';break;
            case 8243:n=(UBYTE)'"';break;
            case 8249:n=(UBYTE)'<';break;
            case 8250:n=(UBYTE)'>';break;
            case 8254:n=(UBYTE)0xAF;break;
            case 8260:n=(UBYTE)'/';break;
            case 8482:r="TM";break;
            case 8592:n=(UBYTE)171;break;  /* Left arrow (larr) - approximate as « (laquo) */
            case 8594:n=(UBYTE)187;break;  /* Right arrow (rarr) - approximate as » (raquo) */
            case 8709:n=(UBYTE)0xD8;break;
            case 8722:n=(UBYTE)'-';break;
            case 8727:n=(UBYTE)'*';break;
            case 8764:n=(UBYTE)'~';break;
            case 8804:r="<=";break;
            case 8805:r=">=";break;
            case 8901:n=(UBYTE)0xB7;break;
            case 9001:n=(UBYTE)'<';break;
            case 9002:n=(UBYTE)'>';break;
            case 9674:n=(UBYTE)0x25CA;break;
            default:
               r=ebuf;
         }
      }
      if(r)
      {  l=strlen(r);
         Deleteinbuffer(buf,p-buf->buffer,1);
         Insertinbuffer(buf,r,l,p-buf->buffer);
         ta->length=ta->length+l-1;
         end=end+l-1;
         p=p+l-1;
      }
      /* replace invalid number by space if compatible */
      valid=(n>=32 && n<=126) || (n>=160 && n<=255) || (lf && n==10);
      if((valid || doc->htmlmode==HTML_COMPATIBLE) && !r)
      {  if(!valid) n=32;
         *p=n;
      }
      else if(n==0 && !strict)
      {  *p=' ';
      }
      else if(n==9)
      {  *p=' ';
      }
      p++;
   }
}

/* inspect text, find icon entities and split up */
static void Lookforicons(struct Document *doc,struct Tagattr *ta)
{  if((doc->pflags&(DPF_PREFORMAT|DPF_JSCRIPT))
   || doc->pmode==DPM_TITLE
   || doc->pmode==DPM_OPTION
   || doc->pmode==DPM_TEXTAREA
   || doc->htmlmode==HTML_STRICT)
   {  Translate(doc,&doc->args,ta,FALSE);
      Processhtml(doc,MARKUP_TEXT,ta);
   }
   else
   {  UBYTE name[32],*q;
      short i;
      UBYTE *icon;
      UBYTE *begin=doc->args.buffer+ta->valuepos,*p;
      UBYTE *end=begin+ta->length;
      p=begin;
      while(p<end)
      {  if(*p=='&')
         {  for(i=0,q=p+1;
                  q<end && i<31 && (isalnum(*q) || *q=='.' || *q=='-');
                  i++,q++)
               name[i]=*q;
            name[i]='\0';
            if(icon=Findicon(name))
            {  struct Tagattr tta={0};
               tta.attr=TAGATTR_TEXT;
               tta.valuepos=ta->valuepos;
               tta.length=p-begin;
               Translate(doc,&doc->args,&tta,FALSE);
               Processhtml(doc,MARKUP_TEXT,&tta);
               tta.attr=TAGATTR_SRC;
               tta.valuepos=(long)icon;
               Processhtml(doc,MARKUP_ICON,&tta);
               if(q<end && *q==';') q++;
               ta->valuepos+=q-begin;
               ta->length-=q-begin;
               begin=p=q;
            }
            else p++;
         }
         else p++;
      }
      Translate(doc,&doc->args,ta,FALSE);
      Processhtml(doc,MARKUP_TEXT,ta);
   }
}

/* End of text reached, If eof, notify HTML doc. */
static BOOL Eofandexit(struct Document *doc,BOOL eof)
{  if(eof)
   {  Processhtml(doc,MARKUP_EOF,attrs.first);
   }
   return TRUE;
}

/* Parse comment in html mode. Initial p points after initial "<!--"
 * Return new buffer pointer, or NULL when eof */

/* Strict HTML:  <!{--comment--wsp}> */
UBYTE *Parsecommentstrict(UBYTE *p,UBYTE *end,BOOL eof)
{  for(;;)
   {  while(p<end-1 && !(p[0]=='-' && p[1]=='-')) p++;   /* Skip to closing -- */
      p+=2;
      while(p<end-1 && *p!='>' && !(p[0]=='-' && p[1]=='-')) p++;
      if(p>=end-1) return NULL;
      if(*p=='>') break;
      p+=2;                                              /* skip next opening -- */
   }
   p++;  /* Skip closing > */
   return p;
}

/* Tolerant: Try strict first, but if wsp is something else than wsp,
 * or "---" is found, then redo using    <!--any> */
UBYTE *Parsecommenttolerant(UBYTE *p,UBYTE *end,BOOL eof)
{  UBYTE *savep=p;
   for(;;)
   {  while(p<end-1 && !(p[0]=='-' && p[1]=='-')) p++;   /* Skip to closing -- */
      p+=2;
      if(p<end && *p=='-')
      {  /* "---" */
         break;
      }
      while(p<end && isspace(*p)) p++;                   /* Skip whitespace */
      if(p<end && *p=='>')
      {  p++;
         return p;
      }
      else if(p>=end-1)
      {  if(!eof) return NULL;
         /* EOF found, retry */
         break;
      }
      else if(p[0]=='-' && p[1]=='-')
      {  /* Still a valid strict comment */
         p+=2;                                           /* Skip next opening -- */
         if(p<end && *p=='-')
         {  /* "---" */
            break;
         }
      }
      else
      {  /* Whitespace is no whitespace, retry */
         break;
      }
   }
   /* If loop was broken, retry */
   p=savep;
   while(p<end && *p!='>') p++;                          /* Skip to > */
   p++;
   return p;
}

/* Compatible:   <!--comment> */
UBYTE *Parsecommentcompatible(UBYTE *p,UBYTE *end,BOOL eof)
{  while(p<end && !(p[0]=='>')) p++;      /* Skip to > */
   if(p>=end) return NULL;
   p++;
   return p;
}

/* Parse XML declaration: <?xml version="1.0" encoding="..." standalone="yes"?>
 * Extracts encoding if present and stores it in document flags.
 * Returns pointer after ?> or NULL on error/eof */
static UBYTE *Parsexmldeclaration(struct Document *doc,UBYTE *p,UBYTE *end,BOOL eof)
{  UBYTE *attrstart,*attrend;
   UBYTE attrname[32],attrvalue[64];
   short i;
   UBYTE quote;
   
   /* Skip "xml" target name */
   while(p<end && isspace(*p)) p++;
   if(p>=end-3) return NULL;
   if(!STRNIEQUAL(p,"xml",3)) return NULL;  /* Not an XML declaration */
   p+=3;
   
   /* Parse attributes: version, encoding, standalone */
   while(p<end-1 && !(p[0]=='?' && p[1]=='>'))
   {  while(p<end && isspace(*p)) p++;
      if(p>=end-1) break;
      if(p[0]=='?' && p[1]=='>') break;
      
      /* Parse attribute name */
      i=0;
      while(p<end && i<31 && (isalnum(*p) || *p=='-' || *p==':'))
      {  attrname[i++]=*p++;
      }
      if(i==0) break;  /* Invalid attribute name */
      attrname[i]='\0';
      
      while(p<end && isspace(*p)) p++;
      if(p>=end || *p!='=') break;  /* Missing = */
      p++;
      while(p<end && isspace(*p)) p++;
      
      if(p>=end) break;
      if(*p!='"' && *p!='\'') break;  /* Missing quote */
      quote=*p++;
      
      /* Parse attribute value */
      attrstart=p;
      while(p<end && (*p!=quote || (p>attrstart && p[-1]=='\\')))
      {  p++;
      }
      if(p>=end) break;  /* Unclosed quote */
      attrend=p;
      p++;  /* Skip closing quote */
      
      /* Extract encoding if this is the encoding attribute */
      if(STRNIEQUAL(attrname,"encoding",8))
      {  i=0;
         while(attrstart<attrend && i<63)
         {  attrvalue[i++]=*attrstart++;
         }
         attrvalue[i]='\0';
         /* Store encoding - could be used to set DDF_FOREIGN flag */
         /* For now, we just parse it but don't use it yet */
      }
   }
   
   /* Find closing ?> */
   while(p<end-1 && !(p[0]=='?' && p[1]=='>')) p++;
   if(p>=end-1) return NULL;  /* Missing closing ?> */
   p+=2;  /* Skip ?> */
   return p;
}

/* Parse CDATA section: <![CDATA[...content...]]>
 * Extracts CDATA content and adds it as text to the document.
 * Normalizes newlines to spaces to match regular text handling.
 * Returns pointer after ]]> or NULL on error/eof */
static UBYTE *Parsecdata(struct Document *doc,UBYTE *p,UBYTE *end,BOOL eof)
{  UBYTE *cdata_start;
   UBYTE *cdata_end;
   UBYTE *q;
   struct Tagattr *ta;
   struct Tagattr *saved_attrs_first;
   struct Tagattr *saved_attrs_last;
   short saved_nextattr;
   long saved_args_length;
   
   /* Skip past "[CDATA[" */
   if(p>=end-7) return NULL;
   if(!STRNIEQUAL(p,"[CDATA[",7)) return NULL;  /* Not a CDATA section */
   p+=7;
   cdata_start=p;
   
   /* Find closing ]]> */
   while(p<end-2)
   {  if(p[0]==']' && p[1]==']' && p[2]=='>')
      {  cdata_end=p;
         p+=3;  /* Skip ]]> */
         
         /* Add CDATA content as text, normalizing newlines to spaces */
         if(cdata_end>cdata_start)
         {  /* Save current parsing state */
            saved_attrs_first=attrs.first;
            saved_attrs_last=attrs.last;
            saved_nextattr=nextattr;
            saved_args_length=doc->args.length;
            
            /* Create new attrs list for CDATA content */
            NEWLIST(&attrs);
            nextattr=0;
            doc->args.length=0;
            ta=Nextattr(doc);
            ta->attr=TAGATTR_TEXT;
            
            /* Copy CDATA content, converting newlines to spaces */
            q=cdata_start;
            while(q<cdata_end)
            {  if(*q=='\r')
               {  /* Convert \r\n or \r to space */
                  if(!Addtobuffer(&doc->args," ",1))
                  {  /* Restore state on error */
                     attrs.first=saved_attrs_first;
                     attrs.last=saved_attrs_last;
                     nextattr=saved_nextattr;
                     doc->args.length=saved_args_length;
                     return NULL;
                  }
                  ta->length++;
                  q++;
                  if(q<cdata_end && *q=='\n') q++;  /* Skip \n after \r */
               }
               else if(*q=='\n')
               {  /* Convert \n to space */
                  if(!Addtobuffer(&doc->args," ",1))
                  {  /* Restore state on error */
                     attrs.first=saved_attrs_first;
                     attrs.last=saved_attrs_last;
                     nextattr=saved_nextattr;
                     doc->args.length=saved_args_length;
                     return NULL;
                  }
                  ta->length++;
                  q++;
               }
               else
               {  /* Copy character as-is */
                  if(!Addtobuffer(&doc->args,q,1))
                  {  /* Restore state on error */
                     attrs.first=saved_attrs_first;
                     attrs.last=saved_attrs_last;
                     nextattr=saved_nextattr;
                     doc->args.length=saved_args_length;
                     return NULL;
                  }
                  ta->length++;
                  q++;
               }
            }
            
            if(!Addtobuffer(&doc->args,"",1))
            {  /* Restore state on error */
               attrs.first=saved_attrs_first;
               attrs.last=saved_attrs_last;
               nextattr=saved_nextattr;
               doc->args.length=saved_args_length;
               return NULL;
            }
            
            /* Process the text content */
            Processhtml(doc,MARKUP_TEXT,attrs.first);
            
            /* Restore previous parsing state */
            attrs.first=saved_attrs_first;
            attrs.last=saved_attrs_last;
            nextattr=saved_nextattr;
            doc->args.length=saved_args_length;
         }
         
         return p;
      }
      p++;
   }
   
   /* EOF reached before finding ]]> */
   if(!eof) return NULL;
   
   /* At EOF, add whatever we have as text, normalizing newlines */
   if(p>cdata_start)
   {  /* Save current parsing state */
      saved_attrs_first=attrs.first;
      saved_attrs_last=attrs.last;
      saved_nextattr=nextattr;
      saved_args_length=doc->args.length;
      
      /* Create new attrs list for CDATA content */
      NEWLIST(&attrs);
      nextattr=0;
      doc->args.length=0;
      ta=Nextattr(doc);
      ta->attr=TAGATTR_TEXT;
      
      /* Copy CDATA content, converting newlines to spaces */
      q=cdata_start;
      while(q<p)
      {  if(*q=='\r')
         {  /* Convert \r\n or \r to space */
            if(!Addtobuffer(&doc->args," ",1))
            {  /* Restore state on error */
               attrs.first=saved_attrs_first;
               attrs.last=saved_attrs_last;
               nextattr=saved_nextattr;
               doc->args.length=saved_args_length;
               return NULL;
            }
            ta->length++;
            q++;
            if(q<p && *q=='\n') q++;  /* Skip \n after \r */
         }
         else if(*q=='\n')
         {  /* Convert \n to space */
            if(!Addtobuffer(&doc->args," ",1))
            {  /* Restore state on error */
               attrs.first=saved_attrs_first;
               attrs.last=saved_attrs_last;
               nextattr=saved_nextattr;
               doc->args.length=saved_args_length;
               return NULL;
            }
            ta->length++;
            q++;
         }
         else
         {  /* Copy character as-is */
            if(!Addtobuffer(&doc->args,q,1))
            {  /* Restore state on error */
               attrs.first=saved_attrs_first;
               attrs.last=saved_attrs_last;
               nextattr=saved_nextattr;
               doc->args.length=saved_args_length;
               return NULL;
            }
            ta->length++;
            q++;
         }
      }
      
      if(!Addtobuffer(&doc->args,"",1))
      {  /* Restore state on error */
         attrs.first=saved_attrs_first;
         attrs.last=saved_attrs_last;
         nextattr=saved_nextattr;
         doc->args.length=saved_args_length;
         return NULL;
      }
      
      /* Process the text content */
      Processhtml(doc,MARKUP_TEXT,attrs.first);
      
      /* Restore previous parsing state */
      attrs.first=saved_attrs_first;
      attrs.last=saved_attrs_last;
      nextattr=saved_nextattr;
      doc->args.length=saved_args_length;
   }
   
   return p;
}

/* Parse DOCTYPE declaration: <!DOCTYPE root-element [PUBLIC "..." "..." | SYSTEM "..."] [...]>
 * Extracts root element name if present.
 * Returns pointer after > or NULL on error/eof */
static UBYTE *Parsedoctypedeclaration(struct Document *doc,UBYTE *p,UBYTE *end,BOOL eof)
{  UBYTE rootname[32];
   short i;
   UBYTE quote;
   
   /* Skip "DOCTYPE" keyword */
   while(p<end && isspace(*p)) p++;
   if(p>=end-7) return NULL;
   if(!STRNIEQUAL(p,"DOCTYPE",7)) return NULL;  /* Not a DOCTYPE */
   p+=7;
   
   while(p<end && isspace(*p)) p++;
   if(p>=end) return NULL;
   
   /* Parse root element name (first identifier after DOCTYPE) */
   i=0;
   while(p<end && i<31 && (isalnum(*p) || *p=='-' || *p==':'))
   {  rootname[i++]=*p++;
   }
   if(i>0)
   {  rootname[i]='\0';
      /* Root element name extracted - could be used for document type detection */
   }
   
   /* Skip rest of DOCTYPE until closing > */
   while(p<end && *p!='>')
   {  if(*p=='"' || *p=='\'')
      {  quote=*p++;
         while(p<end && *p!=quote) p++;
         if(p<end) p++;  /* Skip closing quote */
      }
      else p++;
   }
   
   if(p>=end) return NULL;  /* Missing closing > */
   p++;  /* Skip > */
   return p;
}

BOOL Parsehtml(struct Document *doc,struct Buffer *src,BOOL eof,long *srcpos)
{  UBYTE *p=src->buffer+(*srcpos);
   UBYTE *end=src->buffer+src->length;
   UBYTE *q;
   UBYTE buf[32];
   UBYTE quote;
   struct Tagdes *td;
   struct Attrdes *tattr;
   short i;
   struct Tagattr *ta;
   USHORT tagtype;
   BOOL thisisdata=FALSE;  /* looks like tag but is in fact data */
   BOOL removenl;          /* Remove newlines from URL values */
   BOOL skipnewline;       /* Current value of the DPF_SKIPNEWLINE flag */
   long oldsrcpos;
   BOOL tracetag;
   USHORT ttype;
   UBYTE *tname;
   UBYTE *loopstart;
   long loopsrcpos;
   UBYTE *attrloop_lastp;
   long attrloop_guard;
   /* trace removed */
   /* Skip leading nullbytes and whitespace at document start */
   if((*srcpos)==0)
   {  /* trace removed */
      while(p<end && !*p) p++;
      /* Skip all leading whitespace at document start (before first tag) */
      while(p<end && isspace(*p)) p++;
      *srcpos=p-src->buffer;
   }
   while(p<end && !(doc->pflags&DPF_SUSPEND))
   {  NEWLIST(&attrs);
      loopstart=p;
      loopsrcpos=*srcpos;
      nextattr=0;
      doc->args.length=0;
      skipnewline=BOOLVAL(doc->pflags&DPF_SKIPNEWLINE);
      if(p==end-1 && *p=='<' && !thisisdata) return Eofandexit(doc,eof);
      if(p<end-1 && *p=='<' && (isalpha(p[1]) || p[1]=='/' || p[1]=='!' || p[1]=='?') && !thisisdata)
      {  BOOL endtag=FALSE;
         /* If we don't yet have the full tag in the current buffer (no closing '>'),
          * stop parsing and wait for more data. This avoids pathological stalls when
          * the input buffer ends mid-tag. */
         if(!eof)
         {  UBYTE *gt=p;
            UBYTE q=0;
            while(gt<end)
            {  if(q)
               {  if(*gt==q) q=0;
               }
               else
               {  if(*gt=='\'' || *gt=='"') q=*gt;
                  else if(*gt=='>') break;
               }
               gt++;
            }
            if(gt>=end) return TRUE;
         }
         attrloop_lastp=NULL;
         attrloop_guard=0;
         if(++p>=end) return Eofandexit(doc,eof);
         if(*p=='?' && !(doc->pflags&(DPF_XMP|DPF_LISTING|DPF_JSCRIPT)))
         {  /* XML processing instruction: <?target ... ?> */
            UBYTE *newp;
            /* Check if this is an XML declaration (<?xml ... ?>) */
            if(p+1<end-3 && STRNIEQUAL(p+1,"xml",3))
            {  newp=Parsexmldeclaration(doc,p+1,end,eof);
               if(newp)
               {  p=newp;
                  continue;  /* Skip XML declaration, don't process it */
               }
            }
            /* Not XML declaration or parse failed - skip as generic PI */
            p++;
            while(p<end-1 && !(p[0]=='?' && p[1]=='>')) p++;
            if(p>=end-1) return Eofandexit(doc,eof);
            p+=2;  /* skip ?> */
            continue;  /* Skip this tag, don't process it */
         }
         if(*p=='!' && !(doc->pflags&(DPF_XMP|DPF_LISTING|DPF_JSCRIPT)))
         {  tagtype=0;
            p++;
            if(p>=end-1) return Eofandexit(doc,eof);
            if(p[0]=='-' && p[1]=='-')    /* <!-- */
            {  p+=2;                      /* skip opening -- */
               switch(doc->htmlmode)
               {  case HTML_STRICT:
                     p=Parsecommentstrict(p,end,eof);
                     break;
                  case HTML_TOLERANT:
                     p=Parsecommenttolerant(p,end,eof);
                     break;
                  default:
                     p=Parsecommentcompatible(p,end,eof);
                     break;
               }
               if(!p || p>=end) return Eofandexit(doc,eof);
            }
            else if(p[0]=='[')  /* Could be CDATA section: <![CDATA[ */
            {  UBYTE *newp;
               newp=Parsecdata(doc,p,end,eof);
               if(newp)
               {  p=newp;
                  continue;  /* CDATA processed, continue parsing */
               }
               /* Not CDATA or parse failed - skip as generic declaration */
               while(p<end && *p!='>') p++;  /* skip up to > */
               if(p>=end) return Eofandexit(doc,eof);
               p++;                          /* skip > */
            }
            else  /* No real comment - could be DOCTYPE or other declaration */
            {  UBYTE *newp;
               /* Check if this is a DOCTYPE declaration */
               if(p<end-7 && STRNIEQUAL(p,"DOCTYPE",7))
               {  newp=Parsedoctypedeclaration(doc,p,end,eof);
                  if(newp)
                  {  p=newp;
                     continue;  /* Skip DOCTYPE, don't process it */
                  }
               }
               /* Not DOCTYPE or parse failed - skip as generic declaration */
               while(p<end && *p!='>') p++;  /* skip up to > */
               if(p>=end) return Eofandexit(doc,eof);
               p++;                          /* skip > */
            }
         }
         else
         {  if(*p=='/')
            {  endtag=TRUE;
               if(++p>=end) return Eofandexit(doc,eof);
            }
            q=buf;
            i=0;
            while(p<end && Issgmlchar(*p) && i<31)
            {  *q++=*p++;
               i++;
            }
            if(p>=end) return Eofandexit(doc,eof);
            *q='\0';
            td=Findtag(buf);
            if(doc->pflags&DPF_XMP)
            {  if(!(endtag && td && td->type==MARKUP_XMP))
               {  thisisdata=TRUE;
                  p=src->buffer+(*srcpos);
                  continue;   /* try again */
               }
            }
            if(doc->pflags&DPF_LISTING)
            {  if(!(endtag && td && td->type==MARKUP_LISTING))
               {  thisisdata=TRUE;
                  p=src->buffer+(*srcpos);
                  continue;   /* try again */
               }
            }
            if(doc->pflags&DPF_JSCRIPT)
            {  if(!(endtag && td && td->type==MARKUP_SCRIPT))
               {  thisisdata=TRUE;
                  p=src->buffer+(*srcpos);
                  continue;   /* try again */
               }
            }
            for(;;)
            {  while(p<end && Isspace(*p)) p++;
               if(p>=end) return Eofandexit(doc,eof);
               if(*p=='>') break;
               if(doc->htmlmode!=HTML_STRICT && *p=='<')
               {  p--;  /* Don't skip over '<' yet */
                  break;
               }
               /* Guard against pathological/non-advancing attribute scans. */
               if(p==attrloop_lastp)
               {  attrloop_guard++;
                  if(attrloop_guard>20000)
                  {
                     /* Skip forward to end of tag to recover. */
                     while(p<end && *p!='>') p++;
                     if(p>=end) return Eofandexit(doc,eof);
                     break;
                  }
               }
               else
               {  attrloop_lastp=p;
                  attrloop_guard=0;
               }
               ta=Nextattr(doc);
               i=0;q=buf;
               if(!Issgmlchar(*p)) p++;   /* Skip invalid character to avoid endless loop */
               while(p<end && Issgmlchar(*p) && i<31)
               {  *q++=*p++;
                  i++;
               }
               if(p>=end) return Eofandexit(doc,eof);
               *q='\0';
               tattr=Findattr(buf);
               /* If <EMBED>, only allow valid attributes. Pass others as EMBEDPARAM pairs */
               if(td && td->type==MARKUP_EMBED)
               {  switch(tattr?tattr->type:0)
                  {  case TAGATTR_WIDTH:
                     case TAGATTR_HEIGHT:
                     case TAGATTR_NAME:
                     case TAGATTR_SRC:
                        ta->attr=tattr->type;
                        break;
                     default:
                        ta->attr=TAGATTR_EMBEDPARAMNAME;
                        if(!Addtobuffer(&doc->args,buf,strlen(buf)+1)) return FALSE;
                        ta=Nextattr(doc);
                        ta->attr=TAGATTR_EMBEDPARAMVALUE;
                  }
               }
               else if(tattr) ta->attr=tattr->type;
               while(p<end && Isspace(*p)) p++;
               if(p>=end) return Eofandexit(doc,eof);
               if(*p=='=')
               {  p++;
                  while(p<end && Isspace(*p)) p++;
                  if(p>=end) return Eofandexit(doc,eof);
                  if(*p=='"' || *p=='\'')
                  {  quote=*p;
                     if(++p>=end) return Eofandexit(doc,eof);
                     q=p;
                     removenl=FALSE;
                     if(doc->htmlmode!=HTML_STRICT)
                     {  switch(ta->attr)
                        {  case TAGATTR_ACTION:
                           case TAGATTR_BACKGROUND:
                           case TAGATTR_DATA:
                           case TAGATTR_HREF:
                           case TAGATTR_SRC:
                           case TAGATTR_USEMAP:
                              removenl=TRUE;
                              break;
                        }
                     }
                     while(q<end && *q!=quote)
                     {  if(doc->htmlmode==HTML_COMPATIBLE)
                        {  /* terminate quoted attribute on '>' */
                           if(*q=='>') break;
                           /* terminate URL on whitespace */
                           if(Isspace(*q)
                           && (ta->attr==TAGATTR_HREF 
                              || ta->attr==TAGATTR_SRC
                              || ta->attr==TAGATTR_ACTION)) break;
                        }
                        if(*q=='\r' || *q=='\n')
                        {  if(!Addtobuffer(&doc->args,p,q-p)) return FALSE;
                           ta->length+=(q-p);
                           if(*q=='\r')
                        {  if(q>=end-1)
                           {  if(!eof) return TRUE;
                              return Eofandexit(doc,eof);
                           }
                              if(q[1]=='\n') q++;
                           }
                           if(!Addtobuffer(&doc->args,removenl?"\n":" ",1)) return FALSE;
                           ta->length++;
                           p=++q;
                        }
                        else q++;
                     }
                     if(q>=end)
                     {  /* Attribute value continues in next input chunk. */
                        if(!eof) return TRUE;
                        return Eofandexit(doc,eof);
                     }
                     if(!Addtobuffer(&doc->args,p,q-p)) return FALSE;
                     ta->length+=(q-p);
                     p=q;if(*p==quote) p++;
                  }
                  else
                  {  q=p;
                     while(q<end && !Isspace(*q) && *q!='>') q++;
                     if(q>=end)
                     {  /* Unquoted attribute value continues in next chunk. */
                        if(!eof) return TRUE;
                     }
                     if(!Addtobuffer(&doc->args,p,q-p)) return FALSE;
                     ta->length+=(q-p);
                     p=q;
                  }
                  if(!Addtobuffer(&doc->args,"",1)) return FALSE;
                  if(removenl) Removenl(&doc->args,ta);
                  Translate(doc,&doc->args,ta,TRUE);
               }
               else
               {  /* Add nullbyte for empty attribute */
                  if(!Addtobuffer(&doc->args,"",1)) return FALSE;
               }
            }
            p++; /* skip over '>' */
            
            /* Skip newlines after opening tag */
            if(td && td->container && !endtag && doc->htmlmode!=HTML_COMPATIBLE)
            {  skipnewline=TRUE;
            }
            if(td) tagtype=td->type;
            else tagtype=MARKUP_UNKNOWN;
            if(endtag) tagtype|=MARKUP_END;
         }
      }
      else
      {  thisisdata=FALSE;
         if(!Addtobuffer(&doc->args,
            doc->text.buffer+doc->text.length-1,1)) return FALSE;
         ta=Nextattr(doc);
         ta->attr=TAGATTR_TEXT;
         tagtype=MARKUP_TEXT;
         while(p<end)
         {  if(Isspace(*p))
            {  if((doc->pflags&(DPF_PREFORMAT|DPF_JSCRIPT))
               && doc->pmode!=DPM_OPTION && doc->pmode!=DPM_TEXTAREA)
               {  if(doc->pflags&DPF_PREFORMAT)
                  {   }

                  if(*p=='\r' || *p=='\n')
                  {  if(!skipnewline)
                     {  if(doc->pflags&DPF_PREFORMAT)
                        ta=Nextattr(doc);
                        ta->attr=TAGATTR_BR;
                        if(*p=='\r')
                        {  if(++p>=end) return Eofandexit(doc,eof);
                           if(*p=='\n') p++;
                        }
                        else p++;
                        doc->charcount=0;
                        if(doc->pflags&DPF_PREFORMAT)
                        break; /* exit text loop and process */
                     }
                  }
                  else if(*p=='\t')
                  {  /* Add nbsp to fill up to next multiple of 8 */
                     i=8-(doc->charcount%8);
                     if(doc->pflags&DPF_PREFORMAT)
                     if(!Addtobuffer(&doc->args,"\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0",i))
                        return FALSE;
                     ta->length+=i;
                     doc->charcount+=i;
                     skipnewline=FALSE;
                  }
                  else
                  {  /* In preformat mode, preserve all spaces as non-breaking spaces */
                     /* This ensures column alignment is maintained in <pre> blocks */
                     if(doc->pflags&DPF_PREFORMAT)
                     if(!Addtobuffer(&doc->args,"\xa0",1)) return FALSE;
                     ta->length++;
                     doc->charcount++;
                     skipnewline=FALSE;
                  }
               }
               else if(doc->pmode==DPM_TEXTAREA)
               {  if(*p=='\r')
                  {  if(p>=end+1) return Eofandexit(doc,eof);
                     if(p[1]=='\n') p++;
                     if(!skipnewline)
                     {  if(!Addtobuffer(&doc->args,"\n",1)) return FALSE;
                        ta->length++;
                     }
                  }
                  else if(*p=='\n')
                  {  if(!skipnewline)
                     {  if(!Addtobuffer(&doc->args,"\n",1)) return FALSE;
                        ta->length++;
                     }
                  }
                  else
                  {  if(!Addtobuffer(&doc->args," ",1)) return FALSE;
                     ta->length++;
                     skipnewline=FALSE;
                  }
               }
               else
               {  /* HTML5 spec: Preserve whitespace after line break or block boundary */
                  /* Check if we're preserving leading whitespace after a break */
                  if(doc->htmlmode!=HTML_STRICT && (doc->pflags&DPF_AFTERBREAK))
                  {  /* HTML5 spec: Preserve ALL leading whitespace after break - convert spaces/tabs to non-breaking spaces */
                     /* This handles spaces and tabs that occur after a <br> tag or block boundary */
                     /* Only preserve if this is truly leading whitespace - check if ta->length is 0 or 1 (sentinel only) */
                     /* or if the last character we added was whitespace/nbsp (continuing a whitespace sequence) */
                     BOOL isLeading = (ta->length <= 1) || 
                                      (doc->args.length > 0 && 
                                       (doc->args.buffer[doc->args.length-1] == ' ' || 
                                        doc->args.buffer[doc->args.length-1] == '\xa0'));
                     
                     if(isLeading && (*p==' ' || *p=='\t'))
                     {  /* Preserve this whitespace as non-breaking space */
                        if(*p==' ')
                        {  if(!Addtobuffer(&doc->args,"\xa0",1)) return FALSE;
                           ta->length++;
                        }
                        else
                        {  /* Tab: add nbsp to fill up to next multiple of 8 */
                           i=8-(doc->charcount%8);
                           if(!Addtobuffer(&doc->args,"\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0",i))
                              return FALSE;
                           ta->length+=i;
                           doc->charcount+=i;
                        }
                        skipnewline=FALSE;
                        /* Keep flag set so we continue preserving whitespace until non-whitespace is encountered */
                     }
                     else
                     {  /* Non-whitespace or not leading - clear flag and handle normally */
                        doc->pflags&=~DPF_AFTERBREAK;
                        /* Fall through to normal whitespace handling */
                        if(doc->args.buffer[doc->args.length-1]!=' ')
                        {  if(!skipnewline || (*p!='\n' && *p!='\r'))
                           {  if(!Addtobuffer(&doc->args," ",1)) return FALSE;
                              ta->length++;
                              skipnewline=FALSE;
                           }
                        }
                     }
                  }
                  else
                  {  /* Normal whitespace collapsing */
                     if(doc->args.buffer[doc->args.length-1]!=' ')
                        {  if(!skipnewline || (*p!='\n' && *p!='\r'))
                           {  if(!Addtobuffer(&doc->args," ",1)) return FALSE;
                              ta->length++;
                              skipnewline=FALSE;
                           }
                        }
                     /* Clear flag when we process whitespace normally */
                     doc->pflags&=~DPF_AFTERBREAK;
                  }
               }
            }
            else
            {  if(!Addtobuffer(&doc->args,p,1)) return FALSE;
               ta->length++;
               if(doc->pflags&DPF_PREFORMAT) doc->charcount++;
               skipnewline=FALSE;
               /* Clear flag when non-whitespace content is encountered */
               doc->pflags&=~DPF_AFTERBREAK;
            }
            p++;
            if(p<end && *p=='<') break;
         }
         if(p>=end && !eof) return Eofandexit(doc,eof);
         if(!Addtobuffer(&doc->args,"",1)) return FALSE;
      }
      /* Store this flag only here in case it was reset but text not yet processed. */
      SETFLAG(doc->pflags,DPF_SKIPNEWLINE,skipnewline);
      oldsrcpos=*srcpos;
      (*srcpos)=p-src->buffer;   /* Before processhtml bcz buffer size calculation uses it */
      if(tagtype==MARKUP_TEXT && !(doc->pflags&DPF_JSCRIPT))
      {  Lookforicons(doc,attrs.first);
      }
      else
      {  /* trace removed */
         Processhtml(doc,tagtype,attrs.first);
         if(doc->pflags&DPF_SUSPEND)
         {  /* Resume processing later with this same tag */
            (*srcpos)=oldsrcpos;
            /* Don't pass an EOF to the HTML engine */
            return TRUE;
         }
      }
      /* Guard against infinite loops: if we didn't advance, force progress. */
      if(! (doc->pflags&DPF_SUSPEND) && p==loopstart)
      {  p++;
         (*srcpos)=p-src->buffer;
      }
   }
   return Eofandexit(doc,eof);
}

BOOL Parseplain(struct Document *doc,struct Buffer *src,BOOL eof,long *srcpos)
{  UBYTE *p=src->buffer+(*srcpos);
   UBYTE *end=src->buffer+src->length;
   UBYTE ch;
   struct Tagattr *ta;
   short i;
   if((*srcpos)==0)
   {  while(p<end && !*p) p++;
      *srcpos=p-src->buffer;
      NEWLIST(&attrs);
      Nextattr(doc);
      Processhtml(doc,MARKUP_PRE,attrs.first);
   }
   while(p<end)
   {  NEWLIST(&attrs);
      nextattr=0;
      doc->args.length=0;
      ta=Nextattr(doc);
      ta->attr=TAGATTR_TEXT;
      while(p<end && *p!='\r' && *p!='\n')
      {  if(*p=='\t')
         {  i=8-(doc->charcount%8);
            if(!Addtobuffer(&doc->args,"\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0",i))
               return FALSE;
            ta->length+=i;
            doc->charcount+=i;
         }
         else
         {  if(Isspace(*p)) ch=(UBYTE)'\xa0';
            else ch=*p;
            if(!Addtobuffer(&doc->args,&ch,1)) return FALSE;
            ta->length++;
            doc->charcount++;
         }
         p++;
      }
      if(p>=end && !eof) break;
      ta=Nextattr(doc);
      ta->attr=TAGATTR_BR;
      if(p<end && *p=='\r')
      {  if(p+1<end)
         {  if(p[1]=='\n') p++;
         }
         else if(!eof) break;
      }
      p++;
      doc->charcount=0;
      (*srcpos)=p-src->buffer;
      Processhtml(doc,MARKUP_TEXT,attrs.first);
   }
   return TRUE;
}

