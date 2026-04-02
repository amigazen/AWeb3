/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2025 amigazen project
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

/* css.h - AWeb CSS parsing and application */

#ifndef AWEB_CSS_H
#define AWEB_CSS_H

#include "aweb.h"
#include "docprivate.h"

/* CSS selector types */
#define CSS_SEL_ELEMENT    0x0001
#define CSS_SEL_CLASS      0x0002
#define CSS_SEL_ID         0x0004
#define CSS_SEL_PSEUDO     0x0008
#define CSS_SEL_PSEUDOEL   0x0010  /* Pseudo-element (::before, ::after, etc.) */
#define CSS_SEL_ATTRIBUTE  0x0020  /* Attribute selector [attr] */
#define CSS_SEL_ROOT       0x0040  /* :root selector */

/* CSS selector combinators */
#define CSS_COMB_NONE        0
#define CSS_COMB_DESCENDANT  1  /* space: "div p" */
#define CSS_COMB_CHILD       2  /* >: "div > p" */

/* CSS attribute selector operators */
#define CSS_ATTR_NONE      0  /* [attr] */
#define CSS_ATTR_EQUAL     1  /* [attr=value] */
#define CSS_ATTR_CONTAINS  2  /* [attr*=value] */
#define CSS_ATTR_STARTS    3  /* [attr^=value] */
#define CSS_ATTR_ENDS      4  /* [attr$=value] */
#define CSS_ATTR_WORD      5  /* [attr~=value] (word match) */

/* CSS attribute selector structure */
struct CSSAttribute
{  UBYTE *name;              /* Attribute name */
   UBYTE *value;             /* Attribute value (for =, *=, ^=, $=, ~=) */
   UWORD operator;           /* CSS_ATTR_* operator */
};

/* CSS selector structure */
struct CSSSelector
{  struct MinNode node;
   USHORT type;              /* Selector type flags */
   UBYTE *name;              /* Element name (NULL = any) */
   UBYTE *class;             /* Class name */
   UBYTE *id;                /* ID name */
   UBYTE *pseudo;            /* Pseudo-class name (e.g., "link", "visited", "hover") */
   UBYTE *pseudoElement;     /* Pseudo-element name (e.g., "before", "after", "selection") */
   struct CSSAttribute *attr; /* Attribute selector (NULL if none) */
   USHORT specificity;      /* Selector specificity for cascade */
   struct CSSSelector *parent; /* Parent selector for descendant/child selectors */
   UWORD combinator;         /* CSS_COMB_NONE, CSS_COMB_DESCENDANT, CSS_COMB_CHILD */
};

/* CSS property structure */
struct CSSProperty
{  struct MinNode node;
   UBYTE *name;              /* Property name */
   UBYTE *value;             /* Property value */
};

/* CSS rule structure */
struct CSSRule
{  struct MinNode node;
   struct MinList selectors; /* List of CSSSelector */
   struct MinList properties; /* List of CSSProperty */
};

/* CSS stylesheet structure */
struct CSSStylesheet
{  struct MinList rules;     /* List of CSSRule */
   void *pool;               /* Memory pool */
};

/* Function prototypes */
void ParseCSSStylesheet(struct Document *doc,UBYTE *css);
void ApplyCSSToElement(struct Document *doc,void *element);
void FreeCSSStylesheet(struct Document *doc);
void ApplyInlineCSS(struct Document *doc,void *element,UBYTE *style);
void ApplyInlineCSSToBody(struct Document *doc,void *body,UBYTE *style,UBYTE *tagname);
void ApplyInlineCSSToLink(struct Document *doc,void *link,void *body,UBYTE *style);
struct Colorinfo *ExtractBackgroundColorFromStyle(struct Document *doc,UBYTE *style);
void ApplyCSSToTableCell(struct Document *doc,void *table,UBYTE *style);
void ApplyCSSToImage(struct Document *doc,void *copy,UBYTE *style);
void ApplyCSSToTable(struct Document *doc,void *table,UBYTE *style);
ULONG ParseHexColor(UBYTE *pcolor);
void ApplyCSSToLink(struct Document *doc,void *link,void *body);
void ApplyCSSToLinkColors(struct Document *doc);
void MergeCSSStylesheet(struct Document *doc,UBYTE *css);
void ApplyCSSToBody(struct Document *doc,void *body,UBYTE *class,UBYTE *id,UBYTE *tagname);
void SkipWhitespace(UBYTE **p);
long ParseCSSLengthValue(UBYTE *value,struct Number *num);
struct Colorinfo *ExtractBackgroundColorFromRules(struct Document *doc,UBYTE *class,UBYTE *id,UBYTE *tagname);
void ApplyCSSToTableCellFromRules(struct Document *doc,void *table,UBYTE *class,UBYTE *id,UBYTE *tagname);
void ApplyCSSToTableFromRules(struct Document *doc,void *table,UBYTE *class,UBYTE *id);
void ReapplyCSSToAllElements(struct Document *doc);
void MarkDocCssDirty(struct Document *doc);
void ApplyDocCssIfReady(struct Document *doc);

#endif /* AWEB_CSS_H */

