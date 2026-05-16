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

/* html.c - AWeb document html engine */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include "aweb.h"
#include "source.h"
#include "html.h"
#include "colours.h"
#include "docprivate.h"
#include "body.h"
#include "frameset.h"
#include "text.h"
#include "break.h"
#include "ruler.h"
#include "link.h"
#include "frame.h"
#include "copy.h"
#include "css.h"
#include "bullet.h"
#include "table.h"
#include "map.h"
#include "area.h"
#include "form.h"
#include "field.h"
#include "button.h"
#include "checkbox.h"
#include "radio.h"
#include "input.h"
#include "select.h"
#include "textarea.h"
#include "filefield.h"
#include "url.h"
#include "css.h"
#include "window.h"
#include "application.h"
#include "jslib.h"
#include "frprivate.h"
#include <stdio.h>
#include <stdarg.h>

/* COLOR macro - extract pen number from Colorinfo */
#define COLOR(ci) ((ci)?((ci)->pen):(-1))

/* External debug flag */
extern BOOL httpdebug;

#define ATTR(doc,ta)          ((doc)->args.buffer+(ta)->valuepos)
#define ATTREQUAL(doc,ta,v)   STRIEQUAL(ATTR(doc,ta),v)
#define CONDTAG(tag,v)        (((v)>=0)?(tag):TAG_IGNORE),(v)

#define Wantbreak(doc,n) ((doc->wantbreak<(n))?doc->wantbreak=(n):0)

#define STRICT (doc->htmlmode==HTML_STRICT)

static struct Tagattr dummyta={0};

#define DINGBATPATH  "file:///AWeb:Images/"

/*---------------------------------------------------------------------*/

/* Add text to text buffer. If buffer is too small, and we are somewhere
   in the source buffer, compute expected text buffer size as:
      (source size) * (text size) / (source position)
   assuming that the ratio source/text will be constant. */
static BOOL Addtotextbuf(struct Document *doc,UBYTE *text,long len)
{  long size;
   if(doc->text.length+len>doc->text.size)
   {  struct Buffer *src=&doc->source->buf;
      if(doc->text.size && doc->srcpos)
      {  size=((doc->text.size<<8)/doc->srcpos)*(src->size>>8);
      }
      else
      {  size=src->size/2;
      }
      if(!Expandbuffer(&doc->text,size-doc->text.length)) return FALSE;
   }
   return Addtobuffer(&doc->text,text,len);
}

/* Make sure that the last position in the text buffer is a space */
static BOOL Ensuresp(struct Document *doc)
{  return (BOOL)(doc->text.buffer[doc->text.length-1]==' '
      || Addtotextbuf(doc," ",1));
}

/* Make sure that the last position in the text buffer is not a space */
static BOOL Ensurenosp(struct Document *doc)
{  return (BOOL)(doc->text.buffer[doc->text.length-1]!=' '
      || Addtotextbuf(doc,".",1));
}

/* Make sure that document has a child, create body if it is still empty */
static BOOL Ensurebody(struct Document *doc)
{  if(doc->doctype==DOCTP_NONE)
   {  if(!(doc->body=Anewobject(AOTP_BODY,
         AOBJ_Pool,doc->pool,
         AOBJ_Frame,doc->frame,
         AOBJ_Cframe,doc->frame,
         AOBJ_Window,doc->win,
         AOBJ_Layoutparent,doc,
         AOBJ_Nobackground,BOOLVAL(doc->dflags&DDF_NOBACKGROUND),
         AOBDY_Leftmargin,doc->hmargin,
         AOBDY_Topmargin,doc->vmargin,
         TAG_END)))
      {  return FALSE;
      }
      doc->doctype=DOCTP_BODY;
      /* Hardcoded minimal Japanese support: prefer JKFF font on Shift_JIS pages.
       * UTF-8 pages: prefer Unicode-capable TTF families via ttengine (see docprivate.h). */
      if(doc->charset==DOCCHARSET_SHIFT_JIS)
      {  Asetattrs(doc->body,
            AOBDY_Fontface,(UBYTE *)"JKFF",
            TAG_END);
      }
      else if(doc->charset==DOCCHARSET_UTF8)
      {  Asetattrs(doc->body,
            AOBDY_Fontface,(UBYTE *)AWEB_UTF8_FONTFACE,
            TAG_END);
      }
      /* If stylesheet was merged before BODY existed, apply deterministically now. */
      ApplyDocCssIfReady(doc);
   }
   return TRUE;
}

/* Return the current body (could be button or table member) */
static void *Docbody(struct Document *doc)
{  void *body=NULL;
   if(doc->button) body=(void *)Agetattr(doc->button,AOBUT_Body);
   if(!body && !ISEMPTY(&doc->tables))
   {  body=(void *)Agetattr(doc->tables.first->table,AOTAB_Body);
   }
   if(!body) body=doc->body;
   return body;
}

/* Return the current body (could be button or table member) but don't create a new one */
static void *Docbodync(struct Document *doc)
{  void *body=NULL;
   if(doc->button) body=(void *)Agetattr(doc->button,AOBUT_Body);
   if(!body && !ISEMPTY(&doc->tables))
   {  body=(void *)Agetattr(doc->tables.first->table,AOTAB_Bodync);
   }
   /* MARQUEE disabled: was Docbodync target for nested marquee body
   if(!body && (doc->pflags&DPF_MARQUEE) && doc->marqueebody)
   {  body=doc->marqueebody;
   }
   */
   if(!body) body=doc->body;
   return body;
}

/* Create as much line break elements as needed to fulfill (wantbreak) */
static BOOL Solvebreaks(struct Document *doc)
{  void *br;
   for(doc->wantbreak-=doc->gotbreak;doc->wantbreak>0;doc->wantbreak--)
   {  if(!(br=Anewobject(AOTP_BREAK,
         AOBJ_Pool,doc->pool,
         AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
         TAG_END))) return FALSE;
      Aaddchild(Docbody(doc),br,0);
      doc->gotbreak++;
   }
   doc->pflags&=~DPF_BULLET;
   return TRUE;
}

/* Minimal Body structure to access contents.first (full definition in body.c) */
/* LIST(Element) expands to: struct { struct Element *first; struct Element *tail; struct Element *last; } */
struct BodyMinimal
{  struct Aobject object;
   long aox,aoy,aow,aoh;
   void *frame;
   void *cframe;
   void *pool;
   void *parent;
   void *tcell;
   struct
   {  struct Element *first;
      struct Element *tail;
      struct Element *last;
   } contents;  /* LIST(Element) */
};

/* Add element to document contents. If no contents yet, create a body. */
static BOOL Addelement(struct Document *doc,void *elt)
{  void *body;
   struct Aobject *ao;
   short objtype;
   UBYTE *tagname;
   UBYTE *class;
   UBYTE *id;
   
   if(!Ensurebody(doc))
   {  Adisposeobject(elt);
      return FALSE;
   }
   if(doc->doctype==DOCTP_BODY)
   {  if(Agetattr(elt,AOELT_Visible))
      {  Solvebreaks(doc);
         doc->gotbreak=0;
         doc->wantbreak=0;
         body=Docbody(doc);
      }
      else
      {  body=Docbodync(doc);
      }
      if(body)
      {  Aaddchild(body,elt,0);
         /* Apply CSS to this element if stylesheet exists */
         if(doc->cssstylesheet && elt)
         {  extern BOOL httpdebug;
            ao = (struct Aobject *)elt;
            objtype = ao->objecttype;
            
            /* Apply CSS based on element type */
            if(objtype == AOTP_BODY)
            {  /* Body element - use ApplyCSSToBody */
               tagname = (UBYTE *)Agetattr(elt, AOBDY_TagName);
               class = (UBYTE *)Agetattr(elt, AOBDY_Class);
               id = (UBYTE *)Agetattr(elt, AOBDY_Id);
               if(httpdebug)
               {  AwebLog("css", "Addelement: Applying CSS to BODY element, tagname=%s, class=%s, id=%s",
                        tagname ? (char *)tagname : "NULL",
                        class ? (char *)class : "NULL",
                        id ? (char *)id : "NULL");
               }
               ApplyCSSToBody(doc, elt, class, id, tagname);
            }
            else
            {  /* Regular element - use ApplyCSSToElement */
               if(httpdebug)
               {  AwebLog("css", "Addelement: Applying CSS to element, type=%d, stylesheet=%p",
                        objtype, doc->cssstylesheet);
               }
               ApplyCSSToElement(doc, elt);
            }
         }
      }
      else
      {  Adisposeobject(elt);
         return FALSE;
      }
   }
   else if(doc->doctype==DOCTP_FRAMESET && doc->framesets.first->next)
   {  Aaddchild(doc->framesets.first->frameset,elt,0);
   }
   else
   {  Adisposeobject(elt);
      return FALSE;
   }
   return TRUE;
}


/* Forward declaration */
static long Getnumber(struct Number *num,UBYTE *p);

/* Match a class name against an element's class attribute
 * Class attribute may contain multiple space-separated class names
 * Returns TRUE if selector class matches any class in element's class attribute
 * Uses proper word-boundary matching to avoid partial matches
 */
static BOOL MatchClassAttribute(UBYTE *elementClass, UBYTE *selectorClass)
{  UBYTE *p;
   UBYTE *start;
   size_t selectorLen;
   size_t wordLen;
   
   if(!elementClass || !selectorClass || !*selectorClass)
   {  return FALSE;
   }
   
   selectorLen = strlen((char *)selectorClass);
   p = elementClass;
   
   /* Skip leading whitespace */
   while(*p && isspace(*p)) p++;
   
   while(*p)
   {  start = p;
      
      /* Find end of current word */
      while(*p && !isspace(*p)) p++;
      wordLen = p - start;
      
      /* Check if this word matches selector class (case-insensitive) */
      if(wordLen == selectorLen)
      {  if(Strnicmp((char *)start, (char *)selectorClass, selectorLen) == 0)
         {  return TRUE;
         }
      }
      
      /* Skip whitespace to next word */
      while(*p && isspace(*p)) p++;
   }
   
   return FALSE;
}

/* Check if a DIV element should be displayed inline based on CSS rules or inline styles */
static BOOL IsDivInline(struct Document *doc,UBYTE *class,UBYTE *id,UBYTE *styleAttr)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   struct CSSStylesheet *sheet;
   UBYTE *classPtr;
   UBYTE *p;
   BOOL matches;
   
   /* First check inline style - look for "display" followed by "inline" or "float: left" */
   if(styleAttr)
   {  /* Check for display: inline */
      p = (UBYTE *)strstr((char *)styleAttr,"display");
      if(p)
      {  /* Skip "display" and whitespace/colon */
         p += 7; /* length of "display" */
         while(*p && (isspace(*p) || *p == ':')) p++;
         /* Check if next word is "inline" */
         if(Strnicmp((char *)p,"inline",6) == 0)
         {  /* Make sure it's a complete word */
            if(p[6] == '\0' || p[6] == ';' || isspace(p[6]))
            {  return TRUE;
            }
         }
      }
      /* Check for float: left */
      p = (UBYTE *)strstr((char *)styleAttr,"float");
      if(p)
      {  /* Skip "float" and whitespace/colon */
         p += 5; /* length of "float" */
         while(*p && (isspace(*p) || *p == ':')) p++;
         /* Check if next word is "left" */
         if(Strnicmp((char *)p,"left",4) == 0)
         {  /* Make sure it's a complete word */
            if(p[4] == '\0' || p[4] == ';' || isspace(p[4]))
            {  /* debug_printf("IsDivInline: Found float:left in inline style - class=%s id=%s\n",
                          class ? (char *)class : "NULL",
                          id ? (char *)id : "NULL"); */
               return TRUE;
            }
         }
      }
   }
   
   /* Then check CSS stylesheet */
   if(!doc || !doc->cssstylesheet)
   {  /* debug_printf("IsDivInline: No stylesheet - class=%s id=%s\n",
                   class ? (char *)class : "NULL",
                   id ? (char *)id : "NULL"); */
      return FALSE;
   }
   
   sheet = (struct CSSStylesheet *)doc->cssstylesheet;
   
   /* Optimize: Only check rules that could possibly match */
   /* Skip early if DIV has no class/ID and selector requires one */
   
   for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         (struct MinNode *)sel->node.mln_Succ;
         sel = (struct CSSSelector *)sel->node.mln_Succ)
      {  /* Early skip: if selector requires class but DIV has no class, skip */
         if((sel->type & CSS_SEL_CLASS) && sel->class && !class)
         {  continue; /* Skip this selector */
         }
         
         /* Early skip: if selector requires ID but DIV has no ID, skip */
         if((sel->type & CSS_SEL_ID) && sel->id && !id)
         {  continue; /* Skip this selector */
         }
         
         matches = TRUE;
         
         /* Match element name - must be "div" or match any element (or no element specified) */
         if(sel->type & CSS_SEL_ELEMENT && sel->name)
         {  if(Stricmp((char *)sel->name,"div") != 0)
            {  matches = FALSE;
            }
         }
         /* If no element name specified, it matches any element (including div) */
         
         /* Match class - use proper word-boundary matching */
         if(matches && sel->type & CSS_SEL_CLASS && sel->class)
         {  if(!class)
            {  matches = FALSE;
            }
            else
            {  UBYTE *p;
               UBYTE *start;
               size_t selectorLen;
               size_t wordLen;
               BOOL classMatch = FALSE;
               
               selectorLen = strlen((char *)sel->class);
               p = class;
               
               /* Skip leading whitespace */
               while(*p && isspace(*p)) p++;
               
               /* Check each word in class attribute */
               while(*p && !classMatch)
               {  start = p;
                  
                  /* Find end of current word */
                  while(*p && !isspace(*p)) p++;
                  wordLen = p - start;
                  
                  /* Check if this word matches selector class (case-insensitive) */
                  if(wordLen == selectorLen)
                  {  if(Strnicmp((char *)start, (char *)sel->class, selectorLen) == 0)
                     {  classMatch = TRUE;
                     }
                  }
                  
                  /* Skip whitespace to next word */
                  while(*p && isspace(*p)) p++;
               }
               
               if(!classMatch)
               {  matches = FALSE;
               }
            }
         }
         
         /* Match ID */
         if(matches && sel->type & CSS_SEL_ID && sel->id)
         {  if(!id || Stricmp((char *)sel->id,(char *)id) != 0)
            {  matches = FALSE;
            }
         }
         
         /* If selector matches, check for display: inline or float: left */
         if(matches)
         {  /* debug_printf("IsDivInline: Selector matched! class=%s id=%s, checking for display:inline or float:left\n",
                        class ? (char *)class : "NULL",
                        id ? (char *)id : "NULL"); */
            for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
               (struct MinNode *)prop->node.mln_Succ;
               prop = (struct CSSProperty *)prop->node.mln_Succ)
            {  if(prop->name && prop->value)
               {  if(Stricmp((char *)prop->name,"display") == 0)
                  {  if(Stricmp((char *)prop->value,"inline") == 0 ||
                        Stricmp((char *)prop->value,"inline-block") == 0)
                     {  /* debug_printf("IsDivInline: Found display:inline or inline-block! class=%s id=%s, Returning TRUE\n",
                                  class ? (char *)class : "NULL",
                                  id ? (char *)id : "NULL"); */
                        return TRUE;
                     }
                  }
                  else if(Stricmp((char *)prop->name,"float") == 0)
                  {  UBYTE *floatValue;
                     floatValue = prop->value;
                     /* Skip whitespace */
                     while(*floatValue && isspace(*floatValue)) floatValue++;
                     if(Stricmp((char *)floatValue,"left") == 0)
                     {  /* debug_printf("IsDivInline: Found float:left! class=%s id=%s, Returning TRUE\n",
                                  class ? (char *)class : "NULL",
                                  id ? (char *)id : "NULL"); */
                        return TRUE;
                     }
                  }
               }
            }
         }
      }
   }
   
   return FALSE;
}

static BOOL CssSelCompMatchesDivAncestor(struct CSSSelector *sel, struct Divancestor *da)
{  if(!sel || !da) return FALSE;
   if(sel->type & CSS_SEL_ELEMENT && sel->name)
   {  if(!da->tagname || Stricmp((char *)sel->name,(char *)da->tagname) != 0)
      {  return FALSE;
      }
   }
   if(sel->type & CSS_SEL_ID && sel->id)
   {  if(!da->id || Stricmp((char *)sel->id,(char *)da->id) != 0)
      {  return FALSE;
      }
   }
   if(sel->type & CSS_SEL_CLASS && sel->class)
   {  if(!MatchClassAttribute(da->class, sel->class))
      {  return FALSE;
      }
   }
   return TRUE;
}

static BOOL CssSelectorMatchesDivParseStack(struct Document *doc, struct CSSSelector *leaf,
   UBYTE *tagname, UBYTE *class, UBYTE *id)
{  struct Divancestor cur;
   short curi;
   short ai;
   struct CSSSelector *sel;
   struct CSSSelector *par;
   if(!doc || !leaf) return FALSE;
   cur.tagname = tagname;
   cur.class = class;
   cur.id = id;
   if(!CssSelCompMatchesDivAncestor(leaf, &cur)) return FALSE;
   sel = leaf;
   curi = doc->divancsp - 1;
   while(sel && sel->parent)
   {  par = sel->parent;
      if(sel->combinator == CSS_COMB_CHILD)
      {  if(curi <= 0) return FALSE;
         curi--;
         if(!CssSelCompMatchesDivAncestor(par, &doc->divanc[curi])) return FALSE;
      }
      else if(sel->combinator == CSS_COMB_DESCENDANT)
      {  ai = curi - 1;
         for(; ai >= 0; ai--)
         {  if(CssSelCompMatchesDivAncestor(par, &doc->divanc[ai]))
            {  curi = ai;
               break;
            }
         }
         if(ai < 0) return FALSE;
      }
      else
      {  return FALSE;
      }
      sel = par;
   }
   return TRUE;
}

static BOOL CssSelectorChainHasPseudo(struct CSSSelector *sel)
{  while(sel)
   {  if((sel->type & CSS_SEL_PSEUDO) && sel->pseudo)
      {  return TRUE;
      }
      sel = sel->parent;
   }
   return FALSE;
}

/* Apply CSS to a Body object based on class/ID attributes */
void ApplyCSSToBody(struct Document *doc,void *body,UBYTE *class,UBYTE *id,UBYTE *tagname)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   struct CSSStylesheet *sheet;
   UBYTE *classPtr;
   short align;
   struct Number num;
   long posValue;
   BOOL isAbsolute;
   UBYTE *topValue;
   UBYTE *leftValue;
   UBYTE *marginRightValue;
   BOOL matches;
   UBYTE *fontFace;
   short fontSize;
   BOOL isRelative;
   extern BOOL httpdebug;
   
   if(!doc || !body || !doc->cssstylesheet)
   {  if(httpdebug)
      {  AwebLog("css", "ApplyCSSToBody: Skipped - doc=%p body=%p stylesheet=%p",
                doc, body, (doc ? doc->cssstylesheet : NULL));
      }
      return;
   }
   
   /* IMPORTANT: Do not fall back to AOBDY_Class/AOBDY_Id when callers pass NULL.
    * Many tag handlers only set these attributes when present, so the Body can
    * retain stale class/id values from prior elements (e.g. SPAN), which would
    * make class/id selectors match unrelated later content and cause "style bleed". */
   if(!tagname)
   {  tagname = (UBYTE *)Agetattr(body, AOBDY_TagName);
   }
   
   if(httpdebug)
   {  AwebLog("css", "ApplyCSSToBody: Called - tagname=%s, class=%s, id=%s, body=%p",
             (tagname ? (char *)tagname : "NULL"),
             (class ? (char *)class : "NULL"),
             (id ? (char *)id : "NULL"),
             body);
   }
   
   isAbsolute = FALSE;
   topValue = NULL;
   leftValue = NULL;
   marginRightValue = NULL;
   
   sheet = (struct CSSStylesheet *)doc->cssstylesheet;
   
   /* Single pass: Match selectors and apply properties.
    * Position-related properties (top, left, margin-right) are collected
    * during this pass and applied after the loop, since they depend on
    * whether position:absolute was set (which may be in a different rule). */
   for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         (struct MinNode *)sel->node.mln_Succ;
         sel = (struct CSSSelector *)sel->node.mln_Succ)
      {  /* Descendant/child/:hover-on-ancestor selectors require the full matcher */
         if(tagname && Stricmp((char *)tagname,"DIV") == 0 && sel->parent && doc->divancsp > 0
            && !CssSelectorChainHasPseudo(sel))
         {  matches = CssSelectorMatchesDivParseStack(doc, sel, tagname, class, id);
         }
         else
         {  /* Pass resolved tag/class/id: many AOTP_BODY objects never set AOBDY_TagName
            * (e.g. document root, table cells) while callers supply the real tag here. */
            matches = CssSelectorMatchesLayoutObject(doc, body, sel, tagname, class, id);
         }
         if(!matches)
         {  continue;
         }
         
         /* If selector matches, apply properties */
         if(matches)
         {  extern BOOL httpdebug;
            if(httpdebug)
            {  AwebLog("css", "ApplyCSSToBody: Selector matched - element=%s, class=%s, id=%s, tagname=%s",
                     (sel->name ? (char *)sel->name : "any"),
                     (sel->class ? (char *)sel->class : "none"),
                     (sel->id ? (char *)sel->id : "none"),
                     (tagname ? (char *)tagname : "NULL"));
            }
            for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
               (struct MinNode *)prop->node.mln_Succ;
               prop = (struct CSSProperty *)prop->node.mln_Succ)
            {  if(prop->name && prop->value && prop->value[0] != '\0')
               {  extern BOOL httpdebug;
                  if(httpdebug)
                  {  AwebLog("css", "ApplyCSSToBody: Applying property %s = %s to tagname=%s",
                           prop->name ? (char *)prop->name : "NULL",
                           prop->value ? (char *)prop->value : "NULL",
                           tagname ? (char *)tagname : "NULL");
                  }
                  /* Apply text-align to body alignment */
                  if(Stricmp((char *)prop->name,"text-align") == 0)
                  {  if(Stricmp((char *)prop->value,"center") == 0)
                     {  align = HALIGN_CENTER;
                     }
                     else if(Stricmp((char *)prop->value,"left") == 0)
                     {  align = HALIGN_LEFT;
                     }
                     else if(Stricmp((char *)prop->value,"right") == 0)
                     {  align = HALIGN_RIGHT;
                     }
                     else
                     {  continue;
                     }
                     /* Apply to body's divalign (for DIV) or align (for P) */
                     if(tagname && Stricmp((char *)tagname,"DIV") == 0)
                     {  Asetattrs(body,AOBDY_Divalign,align,TAG_END);
                     }
                     else if(tagname && Stricmp((char *)tagname,"P") == 0)
                     {  Asetattrs(body,AOBDY_Align,align,TAG_END);
                     }
                     /* For table cells (TD/TH), text-align is handled by ApplyCSSToTableCellFromRules */
                     /* which applies it to the table object, not the body */
                  }
                  /* Apply font-family - handle comma-separated list */
                  else if(Stricmp((char *)prop->name,"font-family") == 0)
                  {  UBYTE *fontValue;
                     UBYTE *p;
                     UBYTE *start;
                     UBYTE *end;
                     long len;
                     BOOL found = FALSE;
                     
                     fontFace = NULL;
                     fontValue = prop->value;
                     if(httpdebug)
                     {  AwebLog("css", "font-family: raw='%s' tagname=%s body=%p",
                           fontValue ? (char *)fontValue : "(null)",
                           tagname ? (char *)tagname : "(null)", body);
                     }
                     p = fontValue;
                     
                     /* Try each font in the comma-separated list */
                     while(*p && !found)
                     {  /* Skip leading whitespace */
                        while(*p && isspace(*p)) p++;
                        if(!*p) break;
                        
                        start = p;
                        
                        /* Check if font name is quoted */
                        if(*p == '"' || *p == '\'')
                        {  UBYTE quote = *p;
                           start = p + 1;
                           /* Find closing quote */
                           end = (UBYTE *)strchr((char *)start,quote);
                           if(end)
                           {  len = end - start;
                              p = end + 1;
                           }
                           else
                           {  /* No closing quote, use rest of string */
                              len = strlen((char *)start);
                              p = start + len;
                           }
                        }
                        else
                        {  /* Find comma or end */
                           end = (UBYTE *)strchr((char *)start,',');
                           if(end)
                           {  len = end - start;
                              p = end + 1;
                           }
                           else
                           {  len = strlen((char *)start);
                              p = start + len;
                           }
                        }
                        
                        /* Trim trailing whitespace */
                        while(len > 0 && isspace(start[len - 1])) len--;
                        
                        /* Skip comma and whitespace */
                        while(*p && (isspace(*p) || *p == ',')) p++;
                     }
                     
                     /* Apply the full font list so Matchfont can handle fallbacks properly */
                     /* Strip quotes from font names before passing to Matchfont */
                     if(fontValue && *fontValue)
                     {  UBYTE *p;
                        UBYTE *q;
                        long len;
                        BOOL inQuotes;
                        UBYTE quote;
                        
                        /* Calculate length needed (without quotes) */
                        len = 0;
                        p = fontValue;
                        inQuotes = FALSE;
                        quote = 0;
                        while(*p)
                        {  if((*p == '"' || *p == '\'') && !inQuotes)
                           {  quote = *p;
                              inQuotes = TRUE;
                              p++;
                           }
                           else if(inQuotes && *p == quote)
                           {  inQuotes = FALSE;
                              quote = 0;
                              p++;
                           }
                           else
                           {  len++;
                              p++;
                           }
                        }
                        
                        fontFace = ALLOCTYPE(UBYTE,len + 1,0);
                        if(fontFace)
                        {  /* Copy font value, stripping quotes */
                           p = fontValue;
                           q = fontFace;
                           inQuotes = FALSE;
                           quote = 0;
                           while(*p)
                           {  if((*p == '"' || *p == '\'') && !inQuotes)
                              {  quote = *p;
                                 inQuotes = TRUE;
                                 p++;
                              }
                              else if(inQuotes && *p == quote)
                              {  inQuotes = FALSE;
                                 quote = 0;
                                 p++;
                              }
                              else
                              {  *q++ = *p++;
                              }
                           }
                           *q = '\0';
                           
                           if(httpdebug)
                           {  AwebLog("css", "font-family: AOBDY_Fontface='%s' tagname=%s body=%p",
                                 (char *)fontFace,
                                 tagname ? (char *)tagname : "(null)", body);
                           }
                           /* Apply font face to body - Matchfont will handle the comma-separated list and generic families */
                           Asetattrs(body,AOBDY_Fontface,fontFace,TAG_END);
                           FREE(fontFace);
                        }
                        else
                        {  if(httpdebug)
                           {  AwebLog("css", "font-family: alloc failed for raw='%s' tagname=%s",
                                 fontValue ? (char *)fontValue : "(null)",
                                 tagname ? (char *)tagname : "(null)");
                           }
                        }
                     }
                     else
                     {  if(httpdebug)
                        {  AwebLog("css", "font-family: empty value tagname=%s",
                              tagname ? (char *)tagname : "(null)");
                        }
                     }
                  }
                  /* Apply font-size */
                  else if(Stricmp((char *)prop->name,"font-size") == 0)
                  {  fontSize = 0;
                     isRelative = FALSE;
                     
                     /* Map CSS font-size keywords to AWeb sizes (1-7) */
                     if(Stricmp((char *)prop->value,"xx-small") == 0)
                     {  fontSize = 1; /* Smallest */
                     }
                     else if(Stricmp((char *)prop->value,"x-small") == 0)
                     {  fontSize = 2;
                     }
                     else if(Stricmp((char *)prop->value,"small") == 0)
                     {  fontSize = 3;
                     }
                     else if(Stricmp((char *)prop->value,"medium") == 0)
                     {  fontSize = 4; /* Default */
                     }
                     else if(Stricmp((char *)prop->value,"large") == 0)
                     {  fontSize = 5;
                     }
                     else if(Stricmp((char *)prop->value,"x-large") == 0)
                     {  fontSize = 6;
                     }
                     else if(Stricmp((char *)prop->value,"xx-large") == 0)
                     {  fontSize = 7; /* Largest */
                     }
                     else if(Stricmp((char *)prop->value,"smaller") == 0)
                     {  fontSize = -1; /* Relative: one size smaller */
                        isRelative = TRUE;
                     }
                     else if(Stricmp((char *)prop->value,"larger") == 0)
                     {  fontSize = 1; /* Relative: one size larger */
                        isRelative = TRUE;
                     }
                     else
                     {  /* Try to parse as pixel/length value (e.g., "14px", "1.2em") */
                        long sizeValue;
                        sizeValue = ParseCSSLengthValue(prop->value,&num);
                        if(num.type == NUMBER_NUMBER && sizeValue > 0)
                        {  /* Map pixel values to AWeb sizes (1-7)
                             * Approximate mapping: <10px=1, 10-12px=2, 13-14px=3, 15-16px=4, 17-18px=5, 19-22px=6, >22px=7 */
                           if(sizeValue < 10)
                           {  fontSize = 1;
                           }
                           else if(sizeValue <= 12)
                           {  fontSize = 2;
                           }
                           else if(sizeValue <= 14)
                           {  fontSize = 3;
                           }
                           else if(sizeValue <= 16)
                           {  fontSize = 4;
                           }
                           else if(sizeValue <= 18)
                           {  fontSize = 5;
                           }
                           else if(sizeValue <= 22)
                           {  fontSize = 6;
                           }
                           else
                           {  fontSize = 7;
                           }
                        }
                        /* Note: em, ex, % values would need parent font size context - not implemented yet */
                     }
                     
                     if(fontSize != 0)
                     {  /* Apply font size to body */
                        if(isRelative)
                        {  Asetattrs(body,AOBDY_Fontsizerel,fontSize,TAG_END);
                        }
                        else
                        {  Asetattrs(body,AOBDY_Fontsize,fontSize,TAG_END);
                        }
                     }
                  }
                  /* CSS2: position property */
                  else if(Stricmp((char *)prop->name,"position") == 0)
                  {  if(Stricmp((char *)prop->value,"absolute") == 0)
                     {  isAbsolute = TRUE;
                        /* Note: Full absolute positioning requires layout engine changes */
                     }
                     else if(Stricmp((char *)prop->value,"relative") == 0)
                     {  /* Relative positioning - not yet implemented */
                     }
                     else if(Stricmp((char *)prop->value,"static") == 0)
                     {  /* Static positioning (default) */
                     }
                     else if(Stricmp((char *)prop->value,"fixed") == 0)
                     {  /* Fixed positioning - not yet implemented */
                     }
                  }
                  /* CSS2: top property - save for deferred processing in case
                   * position:absolute appears in a later rule */
                  else if(Stricmp((char *)prop->name,"top") == 0)
                  {  topValue = prop->value;
                     posValue = ParseCSSLengthValue(prop->value,&num);
                     if(isAbsolute && num.type == NUMBER_PERCENT)
                     {  /* Percentage-based top positioning */
                        /* Convert percentage (0-100) to 0-10000 scale */
                        long percentValue = num.n * 100;
                        if(percentValue < 0) percentValue = 0;
                        if(percentValue > 10000) percentValue = 10000;
                        Asetattrs(body,AOBDY_TopPercent,percentValue,TAG_END);
                        topValue = NULL; /* Already applied */
                     }
                     else if(isAbsolute && (num.type == NUMBER_NUMBER || num.type == NUMBER_SIGNED))
                     {  /* Pixel-based top positioning (can be negative) */
                        Asetattrs(body,AOBJ_Top,posValue,TAG_END);
                        topValue = NULL; /* Already applied */
                     }
                  }
                  /* CSS2: left property - save for deferred processing in case
                   * position:absolute appears in a later rule */
                  else if(Stricmp((char *)prop->name,"left") == 0)
                  {  leftValue = prop->value;
                     posValue = ParseCSSLengthValue(prop->value,&num);
                     if(isAbsolute && num.type == NUMBER_PERCENT)
                     {  /* Percentage-based left positioning */
                        /* Convert percentage (0-100) to 0-10000 scale */
                        long percentValue = num.n * 100;
                        if(percentValue < 0) percentValue = 0;
                        if(percentValue > 10000) percentValue = 10000;
                        Asetattrs(body,AOBDY_LeftPercent,percentValue,TAG_END);
                        leftValue = NULL; /* Already applied */
                     }
                     else if(isAbsolute && (num.type == NUMBER_NUMBER || num.type == NUMBER_SIGNED))
                     {  /* Pixel-based left positioning (can be negative) */
                        Asetattrs(body,AOBJ_Left,posValue,TAG_END);
                        leftValue = NULL; /* Already applied */
                     }
                  }
                  /* CSS1: margin-right property - save for deferred processing */
                  else if(Stricmp((char *)prop->name,"margin-right") == 0)
                  {  marginRightValue = prop->value;
                     posValue = ParseCSSLengthValue(prop->value,&num);
                     if(num.type == NUMBER_PERCENT)
                     {  /* Percentage-based margin */
                        /* Convert percentage (0-100) to 0-10000 scale */
                        long percentValue = num.n * 100;
                        if(percentValue < -10000) percentValue = -10000;
                        if(percentValue > 10000) percentValue = 10000;
                        Asetattrs(body,AOBDY_MarginRight,percentValue,TAG_END);
                     }
                     else if(num.type == NUMBER_NUMBER || num.type == NUMBER_SIGNED)
                     {  /* Pixel-based margin (can be negative) */
                        Asetattrs(body,AOBDY_MarginRight,posValue,TAG_END);
                     }
                  }
                  /* CSS1: margin-left, margin-top, margin-bottom */
                  else if(Stricmp((char *)prop->name,"margin-left") == 0 ||
                          Stricmp((char *)prop->name,"margin-top") == 0 ||
                          Stricmp((char *)prop->name,"margin-bottom") == 0)
                  {  posValue = ParseCSSLengthValue(prop->value,&num);
                     if(num.type == NUMBER_NUMBER)
                     {  /* Pixel-based margin */
                        if(Stricmp((char *)prop->name,"margin-left") == 0)
                        {  /* Apply to left margin */
                           Asetattrs(body,AOBDY_Leftmargin,posValue,TAG_END);
                        }
                        else if(Stricmp((char *)prop->name,"margin-top") == 0)
                        {  /* Apply to top margin */
                           Asetattrs(body,AOBDY_Topmargin,posValue,TAG_END);
                        }
                        else if(Stricmp((char *)prop->name,"margin-bottom") == 0)
                        {  Asetattrs(body,AOBDY_MarginBottom,posValue,TAG_END);
                        }
                     }
                  }
                  /* Apply color */
                  else if(Stricmp((char *)prop->name,"color") == 0)
                  {  /* Don't apply color to body font color for anchor tags.
                       * Link colors are handled at the document level via ApplyCSSToLinkColors.
                       * Setting color on the body would incorrectly affect all body text, not just the link. */
                     if(!tagname || Stricmp((char *)tagname,"A") != 0)
                     {  ULONG colorrgb;
                        struct Colorinfo *ci;
                        /* Use Gethexcolor to support both hex colors (#rrggbb) and color names (brown, black, etc.) */
                        Gethexcolor(doc,prop->value,&colorrgb);
                        if(colorrgb != (ULONG)~0)
                        {  ci = Finddoccolor(doc,colorrgb);
                           if(ci)
                           {  Asetattrs(body,AOBDY_Fontcolor,ci,TAG_END);
                           }
                        }
                     }
                  }
                  /* Apply background-color */
                  else if(Stricmp((char *)prop->name,"background-color") == 0)
                  {  UBYTE *pval = prop->value;
                     BOOL isTransparent = FALSE;
                     
                     /* Skip whitespace */
                     while(*pval && isspace(*pval)) pval++;
                     
                     /* Check if value is "transparent" */
                     if(Stricmp((char *)pval, "transparent") == 0)
                     {  isTransparent = TRUE;
                     }
                     
                     if(isTransparent)
                     {  /* Set bgcolor to -1 to indicate transparent (no background color) */
                        Asetattrs(body, AOBDY_Bgcolor, -1, TAG_END);
                     }
                     else
                     {  ULONG colorrgb;
                        struct Colorinfo *ci;
                        /* Use Gethexcolor to support both hex colors (#rrggbb) and color names (brown, black, etc.) */
                        Gethexcolor(doc,prop->value,&colorrgb);
                        if(colorrgb != (ULONG)~0)
                        {  ci = Finddoccolor(doc,colorrgb);
                           if(ci)
                           {  Asetattrs(body,AOBDY_Bgcolor,COLOR(ci),TAG_END);
                           }
                        }
                     }
                  }
                  /* Apply background-image (url(...) from stylesheet) */
                  else if(Stricmp((char *)prop->name,"background-image") == 0)
                  {  UBYTE *urlValue;
                     UBYTE *url;
                     void *bgimg;
                     urlValue = prop->value;
                     while(*urlValue && isspace(*urlValue)) urlValue++;
                     if(Strnicmp((char *)urlValue,"url(",4) == 0)
                     {  UBYTE *start;
                        UBYTE *end;
                        long len;
                        start = urlValue + 4;
                        while(*start && isspace(*start)) start++;
                        end = (UBYTE *)strchr((char *)start,')');
                        if(end && end > start)
                        {  if((*start == '"' || *start == '\'') && end > start + 1)
                           {  start++;
                              if(*(end - 1) == '"' || *(end - 1) == '\'')
                              {  end--;
                              }
                           }
                           len = end - start;
                           if(len > 0)
                           {  url = ALLOCTYPE(UBYTE,len + 1,0);
                              if(url)
                              {  memmove(url,start,len);
                                 url[len] = '\0';
                                 bgimg = Backgroundimg(doc,url);
                                 if(bgimg)
                                 {  Asetattrs(body,AOBDY_Bgimage,bgimg,TAG_END);
                                 }
                                 FREE(url);
                              }
                           }
                        }
                     }
                  }
                  /* Apply background-repeat (repeat, repeat-y, etc.) */
                  else if(Stricmp((char *)prop->name,"background-repeat") == 0)
                  {  UBYTE *repeatStr;
                     repeatStr = Dupstr(prop->value, -1);
                     if(repeatStr)
                     {  if(Stricmp((char *)repeatStr, "repeat") == 0 ||
                           Stricmp((char *)repeatStr, "no-repeat") == 0 ||
                           Stricmp((char *)repeatStr, "repeat-x") == 0 ||
                           Stricmp((char *)repeatStr, "repeat-y") == 0)
                        {  Asetattrs(body, AOBDY_BackgroundRepeat, repeatStr, TAG_END);
                        }
                        else
                        {  FREE(repeatStr);
                        }
                     }
                  }
                  /* Apply text-transform */
                  else if(Stricmp((char *)prop->name,"text-transform") == 0)
                  {  UBYTE *transformStr;
                     transformStr = Dupstrp(prop->value, -1, doc->pool);
                     if(transformStr)
                     {  Asetattrs(body, AOBDY_TextTransform, transformStr, TAG_END);
                        /* Also set document-level text-transform for compatibility */
                        if(Stricmp((char *)transformStr, "uppercase") == 0)
                        {  doc->texttransform = 1;
                        }
                        else if(Stricmp((char *)transformStr, "lowercase") == 0)
                        {  doc->texttransform = 2;
                        }
                        else if(Stricmp((char *)transformStr, "capitalize") == 0)
                        {  doc->texttransform = 3;
                        }
                        else if(Stricmp((char *)transformStr, "none") == 0)
                        {  doc->texttransform = 0;
                        }
                     }
                  }
                  /* Apply margin shorthand */
                  else if(Stricmp((char *)prop->name,"margin") == 0)
                  {  UBYTE *marginP;
                     UBYTE *tokenStart;
                     UBYTE *tokenEnd;
                     long tokenLen;
                     UBYTE *tokenBuf;
                     UBYTE *marginTokens[4];
                     long marginCount;
                     long marginTop;
                     long marginRight;
                     long marginBottom;
                     long marginLeft;
                     long j;
                     struct Number marginNum;
                     marginP = prop->value;
                     marginCount = 0;
                     marginTop = marginRight = marginBottom = marginLeft = 0;
                     for(j = 0; j < 4; j++) marginTokens[j] = NULL;
                     
                     /* Parse margin values - can be 1, 2, 3, or 4 values */
                     for(j = 0; j < 4 && marginP && *marginP; j++)
                     {  while(*marginP && isspace(*marginP)) marginP++;
                        if(!*marginP) break;
                        tokenStart = marginP;
                        while(*marginP && !isspace(*marginP)) marginP++;
                        tokenEnd = marginP;
                        tokenLen = tokenEnd - tokenStart;
                        if(tokenLen > 0)
                        {  tokenBuf = ALLOCTYPE(UBYTE,tokenLen + 1,0);
                           if(tokenBuf)
                           {  memmove(tokenBuf,tokenStart,tokenLen);
                              tokenBuf[tokenLen] = '\0';
                              marginTokens[marginCount] = tokenBuf;
                              marginCount++;
                           }
                        }
                     }
                     
                     /* Apply margin values based on count (CSS1 shorthand) */
                     if(marginCount >= 1)
                     {  marginTop = ParseCSSLengthValue(marginTokens[0],&marginNum);
                        marginRight = marginTop;
                        marginBottom = marginTop;
                        marginLeft = marginTop;
                        if(marginCount == 2)
                        {  marginBottom = marginTop;
                           marginRight = ParseCSSLengthValue(marginTokens[1],&marginNum);
                           marginLeft = marginRight;
                        }
                        else if(marginCount == 3)
                        {  marginRight = ParseCSSLengthValue(marginTokens[1],&marginNum);
                           marginLeft = marginRight;
                           marginBottom = ParseCSSLengthValue(marginTokens[2],&marginNum);
                        }
                        else if(marginCount == 4)
                        {  marginRight = ParseCSSLengthValue(marginTokens[1],&marginNum);
                           marginBottom = ParseCSSLengthValue(marginTokens[2],&marginNum);
                           marginLeft = ParseCSSLengthValue(marginTokens[3],&marginNum);
                        }
                        marginTop = ParseCSSLengthValue(marginTokens[0],&marginNum);
                        if(marginTop >= 0 && (marginNum.type == NUMBER_NUMBER || marginNum.type == NUMBER_SIGNED))
                        {  Asetattrs(body,AOBDY_Topmargin,marginTop,TAG_END);
                        }
                        if(marginCount >= 2)
                        {  marginRight = ParseCSSLengthValue(marginTokens[1],&marginNum);
                        }
                        if(marginRight >= 0 && (marginNum.type == NUMBER_NUMBER || marginNum.type == NUMBER_SIGNED))
                        {  Asetattrs(body,AOBDY_MarginRight,marginRight,TAG_END);
                        }
                        if(marginCount >= 3)
                        {  marginBottom = ParseCSSLengthValue(marginTokens[2],&marginNum);
                        }
                        if(marginBottom >= 0 && (marginNum.type == NUMBER_NUMBER || marginNum.type == NUMBER_SIGNED))
                        {  Asetattrs(body,AOBDY_MarginBottom,marginBottom,TAG_END);
                        }
                        if(marginCount >= 4)
                        {  marginLeft = ParseCSSLengthValue(marginTokens[3],&marginNum);
                        }
                        else if(marginCount == 3)
                        {  marginLeft = ParseCSSLengthValue(marginTokens[1],&marginNum);
                        }
                        else if(marginCount == 2)
                        {  marginLeft = marginRight;
                        }
                        if(marginLeft >= 0 && (marginNum.type == NUMBER_NUMBER || marginNum.type == NUMBER_SIGNED))
                        {  Asetattrs(body,AOBDY_Leftmargin,marginLeft,TAG_END);
                        }
                     }
                     
                     /* Free temporary token buffers */
                     for(j = 0; j < marginCount; j++)
                     {  if(marginTokens[j]) FREE(marginTokens[j]);
                     }
                  }
                  /* Apply line-height */
                  else if(Stricmp((char *)prop->name,"line-height") == 0)
                  {  float lineHeightValue;
                     UBYTE *lineHeightStr;
                     lineHeightStr = prop->value;
                     while(*lineHeightStr && isspace(*lineHeightStr)) lineHeightStr++;
                     if(sscanf((char *)lineHeightStr,"%f",&lineHeightValue) == 1)
                     {  doc->lineheight = lineHeightValue;
                     }
                  }
                  /* Apply font-weight */
                  else if(Stricmp((char *)prop->name,"font-weight") == 0)
                  {  if(Stricmp((char *)prop->value,"bold") == 0 || Stricmp((char *)prop->value,"700") == 0)
                     {  Asetattrs(body,AOBDY_Sethardstyle,FSF_BOLD,TAG_END);
                     }
                     else if(Stricmp((char *)prop->value,"normal") == 0 || Stricmp((char *)prop->value,"400") == 0)
                     {  Asetattrs(body,AOBDY_Unsethardstyle,FSF_BOLD,TAG_END);
                     }
                  }
                  /* Apply font-style */
                  else if(Stricmp((char *)prop->name,"font-style") == 0)
                  {  if(Stricmp((char *)prop->value,"italic") == 0)
                     {  Asetattrs(body,AOBDY_Sethardstyle,FSF_ITALIC,TAG_END);
                     }
                     else if(Stricmp((char *)prop->value,"normal") == 0)
                     {  Asetattrs(body,AOBDY_Unsethardstyle,FSF_ITALIC,TAG_END);
                     }
                  }
                  /* Apply text-decoration */
                  else if(Stricmp((char *)prop->name,"text-decoration") == 0)
                  {  if(Stricmp((char *)prop->value,"line-through") == 0)
                     {  Asetattrs(body,AOBDY_Sethardstyle,FSF_STRIKE,TAG_END);
                     }
                     else if(Stricmp((char *)prop->value,"none") == 0)
                     {  Asetattrs(body,AOBDY_Unsethardstyle,FSF_STRIKE,TAG_END);
                     }
                  }
                  /* Apply width (pixel lengths only; % and table cells use other paths) */
                  else if(Stricmp((char *)prop->name,"width") == 0)
                  {  long widthValue;
                     void *table;
                     ULONG cwtag;
                     BOOL skipWidth;
                     skipWidth = FALSE;
                     if(tagname)
                     {  if(Stricmp((char *)tagname,"TD") == 0 ||
                           Stricmp((char *)tagname,"TH") == 0)
                        {  skipWidth = TRUE;
                           widthValue = ParseCSSLengthValue(prop->value,&num);
                           if(widthValue >= 0 && num.type != NUMBER_NONE)
                           {  table = (void *)Agetattr(body,AOBJ_Layoutparent);
                              if(table)
                              {  if(num.type == NUMBER_PERCENT)
                                 {  cwtag = AOTAB_Percentwidth;
                                 }
                                 else if(num.type == NUMBER_RELATIVE)
                                 {  cwtag = AOTAB_Relwidth;
                                 }
                                 else
                                 {  cwtag = AOTAB_Pixelwidth;
                                 }
                                 TableApplyCellAttrs(table,body,cwtag,widthValue);
                              }
                           }
                        }
                        else if(Stricmp((char *)tagname,"TABLE") == 0)
                        {  skipWidth = TRUE;
                        }
                     }
                     if(!skipWidth)
                     {  widthValue = ParseCSSLengthValue(prop->value,&num);
                        /* Do not map width:50% to AOBJ_Width=50 (pixels) */
                        if(widthValue >= 0 && num.type == NUMBER_NUMBER)
                        {  Asetattrs(body,AOBJ_Width,widthValue,TAG_END);
                        }
                     }
                  }
                  /* Apply height (pixel lengths only; table cells use table attrs) */
                  else if(Stricmp((char *)prop->name,"height") == 0)
                  {  long heightValue;
                     void *table;
                     ULONG chtag;
                     BOOL skipHeight;
                     skipHeight = FALSE;
                     if(tagname)
                     {  if(Stricmp((char *)tagname,"TD") == 0 ||
                           Stricmp((char *)tagname,"TH") == 0)
                        {  skipHeight = TRUE;
                           heightValue = ParseCSSLengthValue(prop->value,&num);
                           if(heightValue >= 0 && num.type != NUMBER_NONE)
                           {  table = (void *)Agetattr(body,AOBJ_Layoutparent);
                              if(table)
                              {  if(num.type == NUMBER_PERCENT)
                                 {  chtag = AOTAB_Percentheight;
                                 }
                                 else
                                 {  chtag = AOTAB_Pixelheight;
                                 }
                                 TableApplyCellAttrs(table,body,chtag,heightValue);
                              }
                           }
                        }
                        else if(Stricmp((char *)tagname,"TABLE") == 0)
                        {  skipHeight = TRUE;
                        }
                     }
                     if(!skipHeight)
                     {  heightValue = ParseCSSLengthValue(prop->value,&num);
                        if(heightValue >= 0 && num.type == NUMBER_NUMBER)
                        {  Asetattrs(body,AOBJ_Height,heightValue,TAG_END);
                        }
                     }
                  }
                  /* Apply position */
                  else if(Stricmp((char *)prop->name,"position") == 0)
                  {  UBYTE *posStr;
                     posStr = Dupstrp(prop->value, -1, doc->pool);
                     if(posStr)
                     {  Asetattrs(body, AOBDY_Position, posStr, TAG_END);
                     }
                  }
                  /* Apply top */
                  else if(Stricmp((char *)prop->name,"top") == 0)
                  {  long topValue;
                     topValue = ParseCSSLengthValue(prop->value,&num);
                     if(topValue >= 0 && num.type == NUMBER_NUMBER)
                     {  Asetattrs(body,AOBJ_Top,topValue,TAG_END);
                     }
                  }
                  /* Apply left */
                  else if(Stricmp((char *)prop->name,"left") == 0)
                  {  long leftValue;
                     leftValue = ParseCSSLengthValue(prop->value,&num);
                     if(leftValue >= 0 && num.type == NUMBER_NUMBER)
                     {  Asetattrs(body,AOBJ_Left,leftValue,TAG_END);
                     }
                  }
                  /* Apply right */
                  else if(Stricmp((char *)prop->name,"right") == 0)
                  {  long rightValue;
                     rightValue = ParseCSSLengthValue(prop->value, &num);
                     if(rightValue >= 0 && num.type == NUMBER_NUMBER)
                     {  Asetattrs(body, AOBDY_Right, rightValue, TAG_END);
                     }
                  }
                  /* Apply bottom */
                  else if(Stricmp((char *)prop->name,"bottom") == 0)
                  {  long bottomValue;
                     bottomValue = ParseCSSLengthValue(prop->value, &num);
                     if(bottomValue >= 0 && num.type == NUMBER_NUMBER)
                     {  Asetattrs(body, AOBDY_Bottom, bottomValue, TAG_END);
                     }
                  }
                  /* Apply z-index */
                  else if(Stricmp((char *)prop->name,"z-index") == 0)
                  {  long zIndexValue;
                     UBYTE *zval;
                     UBYTE *zvalCopy;
                     zval = prop->value;
                     if(zval && zval[0] != '\0')
                     {  zvalCopy = Dupstr(zval, -1);
                        if(zvalCopy)
                        {  SkipWhitespace(&zvalCopy);
                           if(Stricmp((char *)zvalCopy,"auto") == 0)
                           {  zIndexValue = 0;
                           }
                           else
                           {  zIndexValue = strtol((char *)zvalCopy, NULL, 10);
                           }
                           FREE(zvalCopy);
                           Asetattrs(body, AOBDY_ZIndex, zIndexValue, TAG_END);
                        }
                     }
                  }
                  /* Apply display property */
                  else if(Stricmp((char *)prop->name,"display") == 0)
                  {  UBYTE *displayValue;
                     UBYTE *end;
                     UBYTE *dispStr;
                     long dispLen;
                     displayValue = prop->value;
                     while(*displayValue && isspace(*displayValue)) displayValue++;
                     end = displayValue;
                     while(*end) end++;
                     while(end > displayValue && (isspace(*(end - 1)) || *(end - 1) == ';'))
                     {  end--;
                     }
                     dispLen = end - displayValue;
                     dispStr = NULL;
                     if(dispLen > 0)
                     {  dispStr = Dupstr(displayValue, dispLen);
                     }
                     if(dispStr)
                     {  Asetattrs(body, AOBDY_Display, dispStr, TAG_END);
                        if(httpdebug)
                        {  AwebLog("css", "ApplyCSSToBody display=%s body=%p tag=%s class=%s id=%s",
                                  (char *)dispStr,
                                  body,
                                  tagname ? (char *)tagname : "NULL",
                                  class ? (char *)class : "NULL",
                                  id ? (char *)id : "NULL");
                        }
                     }
                  }
                  /* Apply vertical-align */
                  else if(Stricmp((char *)prop->name,"vertical-align") == 0)
                  {  short valign;
                     UBYTE *valignValue;
                     valign = -1;
                     valignValue = Dupstr(prop->value, -1);
                     if(valignValue)
                     {  if(Stricmp((char *)valignValue,"top") == 0)
                        {  valign = VALIGN_TOP;
                        }
                        else if(Stricmp((char *)valignValue,"middle") == 0)
                        {  valign = VALIGN_MIDDLE;
                        }
                        else if(Stricmp((char *)valignValue,"bottom") == 0)
                        {  valign = VALIGN_BOTTOM;
                        }
                        else if(Stricmp((char *)valignValue,"baseline") == 0)
                        {  valign = VALIGN_BASELINE;
                        }
                        FREE(valignValue);
                        if(valign >= 0)
                        {  Asetattrs(body, AOBDY_VerticalAlign, valign, TAG_END);
                        }
                     }
                  }
                  /* Apply clear */
                  else if(Stricmp((char *)prop->name,"clear") == 0)
                  {  UBYTE *clearStr;
                     clearStr = Dupstrp(prop->value, -1, doc->pool);
                     if(clearStr)
                     {  Asetattrs(body, AOBDY_Clear, clearStr, TAG_END);
                     }
                  }
                  /* Apply overflow */
                  else if(Stricmp((char *)prop->name,"overflow") == 0)
                  {  UBYTE *overflowStr;
                     overflowStr = Dupstrp(prop->value, -1, doc->pool);
                     if(overflowStr)
                     {  Asetattrs(body, AOBDY_Overflow, overflowStr, TAG_END);
                     }
                  }
                  /* Apply list-style */
                  else if(Stricmp((char *)prop->name,"list-style") == 0)
                  {  UBYTE *listStyleStr;
                     listStyleStr = Dupstrp(prop->value, -1, doc->pool);
                     if(listStyleStr)
                     {  Asetattrs(body, AOBDY_ListStyle, listStyleStr, TAG_END);
                     }
                  }
                  /* Apply min-width */
                  else if(Stricmp((char *)prop->name,"min-width") == 0)
                  {  long minWidthValue;
                     minWidthValue = ParseCSSLengthValue(prop->value, &num);
                     if(minWidthValue >= 0 && num.type == NUMBER_NUMBER)
                     {  Asetattrs(body, AOBDY_MinWidth, minWidthValue, TAG_END);
                     }
                  }
                  /* Apply max-width */
                  else if(Stricmp((char *)prop->name,"max-width") == 0)
                  {  long maxWidthValue;
                     maxWidthValue = ParseCSSLengthValue(prop->value, &num);
                     if(maxWidthValue >= 0 && num.type == NUMBER_NUMBER)
                     {  Asetattrs(body, AOBDY_MaxWidth, maxWidthValue, TAG_END);
                     }
                  }
                  /* Apply min-height */
                  else if(Stricmp((char *)prop->name,"min-height") == 0)
                  {  long minHeightValue;
                     minHeightValue = ParseCSSLengthValue(prop->value, &num);
                     if(minHeightValue >= 0 && num.type == NUMBER_NUMBER)
                     {  Asetattrs(body, AOBDY_MinHeight, minHeightValue, TAG_END);
                     }
                  }
                  /* Apply max-height */
                  else if(Stricmp((char *)prop->name,"max-height") == 0)
                  {  long maxHeightValue;
                     maxHeightValue = ParseCSSLengthValue(prop->value, &num);
                     if(maxHeightValue >= 0 && num.type == NUMBER_NUMBER)
                     {  Asetattrs(body, AOBDY_MaxHeight, maxHeightValue, TAG_END);
                     }
                  }
                  /* Apply cursor */
                  else if(Stricmp((char *)prop->name,"cursor") == 0)
                  {  UBYTE *cursorStr;
                     cursorStr = Dupstrp(prop->value, -1, doc->pool);
                     if(cursorStr)
                     {  Asetattrs(body, AOBDY_Cursor, cursorStr, TAG_END);
                     }
                  }
                  /* Apply white-space */
                  else if(Stricmp((char *)prop->name,"white-space") == 0)
                  {  UBYTE *whitespaceStr;
                     whitespaceStr = Dupstrp(prop->value, -1, doc->pool);
                     if(whitespaceStr)
                     {  Asetattrs(body, AOBDY_WhiteSpace, whitespaceStr, TAG_END);
                        /* Also apply to AOBDY_Nobr for nowrap */
                        if(Stricmp((char *)whitespaceStr, "nowrap") == 0)
                        {  Asetattrs(body, AOBDY_Nobr, TRUE, TAG_END);
                        }
                        else if(Stricmp((char *)whitespaceStr, "pre") == 0)
                        {  /* "pre" requires STYLE_PRE to actually preserve whitespace */
                           Asetattrs(body, AOBDY_Style, STYLE_PRE, TAG_END);
                           Asetattrs(body, AOBDY_Nobr, FALSE, TAG_END);
                        }
                        else if(Stricmp((char *)whitespaceStr, "normal") == 0 || 
                                Stricmp((char *)whitespaceStr, "pre-wrap") == 0 || 
                                Stricmp((char *)whitespaceStr, "pre-line") == 0)
                        {  Asetattrs(body, AOBDY_Nobr, FALSE, TAG_END);
                        }
                     }
                  }
                  /* Apply padding shorthand */
                  else if(Stricmp((char *)prop->name,"padding") == 0)
                  {  UBYTE *paddingP;
                     UBYTE *tokenStart;
                     UBYTE *tokenEnd;
                     long tokenLen;
                     UBYTE *tokenBuf;
                     UBYTE *paddingTokens[4];
                     long paddingCount;
                     long paddingTop;
                     long paddingRight;
                     long paddingBottom;
                     long paddingLeft;
                     long j;
                     paddingP = prop->value;
                     paddingCount = 0;
                     paddingTop = paddingRight = paddingBottom = paddingLeft = 0;
                     for(j = 0; j < 4; j++) paddingTokens[j] = NULL;
                     
                     /* Parse padding values - can be 1, 2, 3, or 4 values */
                     for(j = 0; j < 4 && paddingP && *paddingP; j++)
                     {  while(*paddingP && isspace(*paddingP)) paddingP++;
                        if(!*paddingP) break;
                        tokenStart = paddingP;
                        while(*paddingP && !isspace(*paddingP)) paddingP++;
                        tokenEnd = paddingP;
                        tokenLen = tokenEnd - tokenStart;
                        if(tokenLen > 0)
                        {  tokenBuf = ALLOCTYPE(UBYTE,tokenLen + 1,0);
                           if(tokenBuf)
                           {  memmove(tokenBuf,tokenStart,tokenLen);
                              tokenBuf[tokenLen] = '\0';
                              paddingTokens[paddingCount] = tokenBuf;
                              paddingCount++;
                           }
                        }
                     }
                     
                     /* Apply padding values based on count */
                     if(paddingCount >= 1)
                     {  paddingTop = ParseCSSLengthValue(paddingTokens[0],&num);
                        if(paddingCount == 1)
                        {  paddingRight = paddingBottom = paddingLeft = paddingTop;
                        }
                        else if(paddingCount == 2)
                        {  paddingBottom = paddingTop;
                           paddingRight = ParseCSSLengthValue(paddingTokens[1],&num);
                           paddingLeft = paddingRight;
                        }
                        else if(paddingCount == 3)
                        {  paddingRight = ParseCSSLengthValue(paddingTokens[1],&num);
                           paddingLeft = paddingRight;
                           paddingBottom = ParseCSSLengthValue(paddingTokens[2],&num);
                        }
                        else if(paddingCount == 4)
                        {  paddingRight = ParseCSSLengthValue(paddingTokens[1],&num);
                           paddingBottom = ParseCSSLengthValue(paddingTokens[2],&num);
                           paddingLeft = ParseCSSLengthValue(paddingTokens[3],&num);
                        }
                        
                        /* CSS padding on TD/TH replaces HTML cellpadding (Leftmargin) */
                        if(tagname &&
                           (Stricmp((char *)tagname,"TD") == 0 ||
                            Stricmp((char *)tagname,"TH") == 0))
                        {  Asetattrs(body,AOBDY_Leftmargin,0,TAG_END);
                           Asetattrs(body,AOBDY_Topmargin,0,TAG_END);
                        }
                        /* Apply padding values */
                        if(paddingTop >= 0 && num.type == NUMBER_NUMBER) Asetattrs(body,AOBDY_PaddingTop,paddingTop,TAG_END);
                        if(paddingRight >= 0 && num.type == NUMBER_NUMBER) Asetattrs(body,AOBDY_PaddingRight,paddingRight,TAG_END);
                        if(paddingBottom >= 0 && num.type == NUMBER_NUMBER) Asetattrs(body,AOBDY_PaddingBottom,paddingBottom,TAG_END);
                        if(paddingLeft >= 0 && num.type == NUMBER_NUMBER) Asetattrs(body,AOBDY_PaddingLeft,paddingLeft,TAG_END);
                     }
                     
                     /* Free temporary token buffers */
                     for(j = 0; j < paddingCount; j++)
                     {  if(paddingTokens[j]) FREE(paddingTokens[j]);
                     }
                  }
                  /* Apply border shorthand */
                  else if(Stricmp((char *)prop->name,"border") == 0)
                  {  UBYTE *pval;
                     UBYTE *token;
                     long borderWidth;
                     ULONG borderColor;
                     struct Colorinfo *ci;
                     
                     pval = prop->value;
                     borderWidth = -1;
                     borderColor = ~0;
                     
                     /* Parse tokens separated by spaces */
                     while(*pval)
                     {  SkipWhitespace(&pval);
                        if(!*pval) break;
                        
                        token = pval;
                        while(*pval && !isspace(*pval)) pval++;
                        
                        /* Check if it's a width */
                        if(isdigit(*token) || *token == '+' || *token == '-')
                        {  borderWidth = ParseCSSLengthValue(token, &num);
                           if(borderWidth >= 0 && num.type == NUMBER_NUMBER)
                           {  Asetattrs(body, AOBDY_BorderWidth, borderWidth, TAG_END);
                           }
                        }
                        /* Check if it's a color */
                        else if(*token == '#')
                        {  borderColor = ParseHexColor(token);
                           if(borderColor != ~0)
                           {  ci = Finddoccolor(doc, borderColor);
                              if(ci)
                              {  Asetattrs(body, AOBDY_BorderColor, ci, TAG_END);
                              }
                           }
                        }
                        /* Otherwise it's probably a style */
                        else
                        {  UBYTE *styleStr;
                           long len;
                           len = pval - token;
                           styleStr = ALLOCTYPE(UBYTE, len + 1, 0);
                           if(styleStr)
                           {  memmove(styleStr, token, len);
                              styleStr[len] = '\0';
                              Asetattrs(body, AOBDY_BorderStyle, styleStr, TAG_END);
                           }
                        }
                     }
                  }
                  /* Apply border-color */
                  else if(Stricmp((char *)prop->name,"border-color") == 0)
                  {  ULONG colorrgb;
                     struct Colorinfo *ci;
                     colorrgb = ParseHexColor(prop->value);
                     if(colorrgb != ~0)
                     {  ci = Finddoccolor(doc, colorrgb);
                        if(ci)
                        {  Asetattrs(body, AOBDY_BorderColor, ci, TAG_END);
                        }
                     }
                  }
                  /* Apply border-style */
                  else if(Stricmp((char *)prop->name,"border-style") == 0)
                  {  UBYTE *styleStr;
                     styleStr = Dupstrp(prop->value, -1, doc->pool);
                     if(styleStr)
                     {  Asetattrs(body, AOBDY_BorderStyle, styleStr, TAG_END);
                     }
                  }
                  /* Apply grid-gap (row-gap column-gap) */
                  else if(Stricmp((char *)prop->name,"grid-gap") == 0)
                  {  UBYTE *gapP;
                     UBYTE *tokenStart;
                     long tokenLen;
                     long rowGap;
                     long colGap;
                     struct Number num;
                     
                     gapP = prop->value;
                     rowGap = -1;
                     colGap = -1;
                     
                     /* Parse first value (row gap) */
                     SkipWhitespace(&gapP);
                     tokenStart = gapP;
                     while(*gapP && !isspace(*gapP)) gapP++;
                     tokenLen = gapP - tokenStart;
                     if(tokenLen > 0)
                     {  UBYTE *tokenBuf;
                        tokenBuf = ALLOCTYPE(UBYTE,tokenLen + 1,0);
                        if(tokenBuf)
                        {  memmove(tokenBuf,tokenStart,tokenLen);
                           tokenBuf[tokenLen] = '\0';
                           rowGap = ParseCSSLengthValue(tokenBuf,&num);
                           FREE(tokenBuf);
                        }
                     }
                     
                     /* Parse second value (column gap) if present */
                     SkipWhitespace(&gapP);
                     if(*gapP)
                     {  tokenStart = gapP;
                        while(*gapP && !isspace(*gapP)) gapP++;
                        tokenLen = gapP - tokenStart;
                        if(tokenLen > 0)
                        {  UBYTE *tokenBuf;
                           tokenBuf = ALLOCTYPE(UBYTE,tokenLen + 1,0);
                           if(tokenBuf)
                           {  memmove(tokenBuf,tokenStart,tokenLen);
                              tokenBuf[tokenLen] = '\0';
                              colGap = ParseCSSLengthValue(tokenBuf,&num);
                              FREE(tokenBuf);
                           }
                        }
                     }
                     else
                     {  /* If only one value, use it for both */
                        colGap = rowGap;
                     }
                     
                     /* Apply gaps as margins for grid layout */
                     if(rowGap >= 0)
                     {  /* Apply row gap as top margin for child elements */
                        /* This is a simplified grid implementation */
                        /* debug_printf("CSS: grid-gap applied - row=%ld col=%ld\n",rowGap,colGap); */
                     }
                     if(colGap >= 0)
                     {  /* Store column gap for use by child elements with grid-column-start */
                        doc->gridcolgap = colGap;
                        /* debug_printf("CSS: grid-gap column gap=%ld stored\n",colGap); */
                     }
                  }
                  /* Apply grid-template-columns (not fully implemented, but parse it) */
                  else if(Stricmp((char *)prop->name,"grid-template-columns") == 0)
                  {  /* Parse grid template - for now just log it */
                     /* debug_printf("CSS: grid-template-columns='%s' (parsed but not fully implemented)\n",
                                  prop->value ? (char *)prop->value : "NULL"); */
                  }
                  /* Apply grid-column-start (for grid layout positioning) */
                  else if(Stricmp((char *)prop->name,"grid-column-start") == 0)
                  {  long gridColStart;
                     long leftMargin;
                     UBYTE *p;
                     
                     /* Parse grid-column-start value - can be a number (e.g., "2") or a length */
                     p = prop->value;
                     SkipWhitespace(&p);
                     if(isdigit(*p))
                     {  /* Parse as integer */
                        gridColStart = strtol((char *)p,NULL,10);
                     }
                     else
                     {  /* Try parsing as length value */
                        struct Number num;
                        gridColStart = ParseCSSLengthValue(prop->value,&num);
                        /* If it's a length, convert to column number (approximate) */
                        if(gridColStart > 0)
                        {  /* Assume each column is at least 100px wide */
                           gridColStart = (gridColStart / 100) + 1;
                        }
                     }
                     
                     if(gridColStart >= 2)
                     {  /* For grid-column-start >= 2, apply left margin to push to second column */
                        /* Use column gap from parent dl element if available */
                        if(doc->gridcolgap > 0)
                        {  leftMargin = doc->gridcolgap;
                        }
                        else
                        {  /* Default column gap if not specified */
                           leftMargin = 16;
                        }
                        Asetattrs(body,AOBDY_Leftmargin,leftMargin,TAG_END);
                        /* debug_printf("CSS: grid-column-start=%ld applied, left margin=%ld (colgap=%ld)\n",
                                     gridColStart,leftMargin,doc->gridcolgap); */
                     }
                  }
                  /* Apply grid-column-end (for grid layout positioning) */
                  else if(Stricmp((char *)prop->name,"grid-column-end") == 0)
                  {  /* Parse but not fully implemented */
                     /* debug_printf("CSS: grid-column-end='%s' (parsed but not fully implemented)\n",
                                  prop->value ? (char *)prop->value : "NULL"); */
                  }
                  /* Apply grid-row-start (for grid layout positioning) */
                  else if(Stricmp((char *)prop->name,"grid-row-start") == 0)
                  {  /* Parse but not fully implemented */
                     /* debug_printf("CSS: grid-row-start='%s' (parsed but not fully implemented)\n",
                                  prop->value ? (char *)prop->value : "NULL"); */
                  }
                  /* Apply grid-row-end (for grid layout positioning) */
                  else if(Stricmp((char *)prop->name,"grid-row-end") == 0)
                  {  /* Parse but not fully implemented */
                     /* debug_printf("CSS: grid-row-end='%s' (parsed but not fully implemented)\n",
                                  prop->value ? (char *)prop->value : "NULL"); */
                  }
                  /* Apply transform */
                  else if(Stricmp((char *)prop->name,"transform") == 0)
                  {  UBYTE *transformStr;
                     UBYTE *pval;
                     
                     pval = prop->value;
                     SkipWhitespace(&pval);
                     
                     /* Store transform value as-is - will be parsed during layout */
                     /* Supported formats: translate(x, y), translateX(x), translateY(y) */
                     transformStr = Dupstrp(pval, -1, doc->pool);
                     if(transformStr)
                     {  Asetattrs(body, AOBDY_Transform, transformStr, TAG_END);
                     }
                  }
               }
            }
         }
      }
   }
   
   /* Apply position properties after all other properties are processed */
   if(isAbsolute)
   {  /* Process top and left with percentage support */
      if(topValue)
      {  posValue = ParseCSSLengthValue(topValue,&num);
         if(num.type == NUMBER_PERCENT)
         {  /* Percentage-based top: requires containing block height */
            /* Note: This requires layout engine integration */
         }
         else if(num.type == NUMBER_NUMBER)
         {  Asetattrs(body,AOBJ_Top,posValue,TAG_END);
         }
      }
      if(leftValue)
      {  posValue = ParseCSSLengthValue(leftValue,&num);
         if(num.type == NUMBER_PERCENT)
         {  /* Percentage-based left: requires containing block width */
            /* Note: This requires layout engine integration */
         }
         else if(num.type == NUMBER_NUMBER)
         {  Asetattrs(body,AOBJ_Left,posValue,TAG_END);
         }
      }
      if(marginRightValue)
      {  posValue = ParseCSSLengthValue(marginRightValue,&num);
         /* Note: margin-right with percentage/negative values requires layout calculation */
      }
   }
}

/* Check for ID attribute. Scan ta list both ways since this function can be
 * called before or after the list was scanned in the tag function itself */
static void Checkid(struct Document *doc,struct Tagattr *tap)
{  struct Tagattr *ta;
   UBYTE *name=NULL;
   if(doc->doctype==DOCTP_BODY && tap)
   {  for(ta=tap->prev;ta && ta->prev && !name;ta=ta->prev)
      {  if(ta->attr==TAGATTR_ID) name=ATTR(doc,ta);
      }
      for(ta=tap;ta && ta->next && !name;ta=ta->next)
      {  if(ta->attr==TAGATTR_ID) name=ATTR(doc,ta);
      }
      if(name)
      {  void *elt;
         struct Fragment *frag;
         Solvebreaks(doc);
         if(elt=Anewobject(AOTP_NAME,
            AOBJ_Pool,doc->pool,
            TAG_END))
         {  Addelement(doc,elt);
            if(frag=PALLOCSTRUCT(Fragment,1,MEMF_PUBLIC,doc->pool))
            {  frag->name=Dupstrp(name,-1,doc->pool);
               frag->elt=elt;
               ADDTAIL(&doc->fragments,frag);
            }
         }
      }
   }
}

/* Remember a new nested table */
static BOOL Pushtable(struct Document *doc,void *table)
{  struct Tableref *tref;
   if(!(tref=PALLOCSTRUCT(Tableref,1,MEMF_CLEAR,doc->pool))) return FALSE;
   tref->table=table;
   ADDHEAD(&doc->tables,tref);
   return TRUE;
}

/* Forget inner nested table */
static void Poptable(struct Document *doc)
{  struct Tableref *tref;
   if(tref=REMHEAD(&doc->tables)) FREE(tref);
}

/* Remember a new nested frameset */
static BOOL Pushframeset(struct Document *doc,void *frameset)
{  struct Framesetref *fref;
   if(!(fref=PALLOCSTRUCT(Framesetref,1,MEMF_CLEAR,doc->pool))) return FALSE;
   fref->frameset=frameset;
   ADDHEAD(&doc->framesets,fref);
   return TRUE;
}

/* Forget inner nested frameset but leave at least one. */
static void Popframeset(struct Document *doc)
{  struct Framesetref *fref=doc->framesets.first;
   if(fref->next && fref->next->next)
   {  Asetattrs(fref->frameset,AOFRS_Endframeset,TRUE,TAG_END);
      REMOVE(fref);
      FREE(fref);
   }
   else
   {  doc->pflags|=DPF_FRAMESETEND;
   }
}

/* Scan (p) for numeric value, possibly signed or percentage or relative. */
static long Getnumber(struct Number *num,UBYTE *p)
{  char c='\0';
   short m;
   BOOL sign=FALSE;
   num->n=0;
   num->type=NUMBER_NONE;
   while(isspace(*p)) p++;
   sign=(*p=='+' || *p=='-');
   if(*p=='*')
   {  num->n=1;
      c='*';
      m=1;
   }
   else
   {  m=sscanf(p," %ld%c",&num->n,&c);
   }
   if(m)
   {  if(c=='%') num->type=NUMBER_PERCENT;
      else if(c=='*') num->type=NUMBER_RELATIVE;
      else if(sign) num->type=NUMBER_SIGNED;
      else num->type=NUMBER_NUMBER;
   }
   if(num->type!=NUMBER_SIGNED && num->n<0) num->n=0;
   return num->n;
}

/* Scan (p) for positive (>=0) numeric value */
static long Getposnumber(UBYTE *p)
{  struct Number num;
   Getnumber(&num,p);
   if(num.type==NUMBER_NUMBER) return num.n;
   else return -1;
}

/* Scan (p) for hexadecimal number and put result in (ptr).
 * Parse things like ff ff ff or ff,0,ff or even ff,fff,ff right.
 * Convert a 3-digit number to 6 digits: abc -> aabbcc. */
static void Scanhexnumber(UBYTE *p,ULONG *ptr,BOOL strict)
{  long n=0;
   long part[3]={0,0,0};
   short i,l=0;
   UBYTE c;
   for(i=0;i<3;i++)
   {  while(*p && (isspace(*p) || ispunct(*p))) p++;  /* skip separators */
      if(!*p) break;
      c=*p;
      if(!strict && (c=='o' || c=='O')) c='0';
      if(!isxdigit(c)) return;
      while(isxdigit(c))               /* get partial number */
      {  part[i]=part[i]*0x10+(c<='9'?(c-'0'):(toupper(c)-'A'+10));
         c=*++p;
         if(!strict && (c=='o' || c=='O')) c='0';
         l++;
      }
   }
   if(i<3)
   {  /* one part, first part is number */
      if(l==3)
      {  /* 3-digit number, convert to 6 digits */
         n=((part[0]&0xf00)<<8)|((part[0]&0x0f0)<<4)|(part[0]&0x00f);
         n=n|(n<<4);
      }
      else n=part[0];
   }
   else n=((part[0]&0xff)<<16)|((part[1]&0xff)<<8)|(part[2]&0xff);
   *ptr=n;
}

/* Scan (p) for a color name or a hexadecimal color number, 
 * and return the resulting RRGGBB value in (ptr) */
void Gethexcolor(struct Document *doc,UBYTE *p,ULONG *ptr)
{  BOOL gotone=FALSE;
   if(*p=='#') Scanhexnumber(p+1,ptr,STRICT);
   else
   {  short a=0,b=NR_COLORNAMES,m;
      long c;
      UBYTE buf[32];
      UBYTE *q;
      for(q=p,m=0;*q && m<31;q++)
      {  if(!isspace(*q)) buf[m++]=*q;
      }
      buf[m]='\0';
      while(a<=b)
      {  m=(a+b)/2;
         c=Stricmp(colornames[m].name,buf);
         if(c==0)
         {  if(!STRICT || colornames[m].strict)
            {  *ptr=colornames[m].color;
               gotone=TRUE;
            }
            break;
         }
         if(c<0) a=m+1;
         else b=m-1;
      }
      if(!gotone && !STRICT && *p) Scanhexnumber(p,ptr,STRICT);
   }
}

/* Scan (p) for a color designator, find a matching Colorinfo and store it
 * in (cip) */
BOOL Setbodycolor(struct Document *doc,struct Colorinfo **cip,UBYTE *p)
{  ULONG color=(ULONG)~0;
   Gethexcolor(doc,p,&color);
   if(color!=(ULONG)~0)
   {  if(!(*cip=Finddoccolor(doc,color))) return FALSE;
   }
   return TRUE;
}

/* Get horizontal alignment value */
static short Gethalign(UBYTE *p)
{  short align=-1;
   if(STRIEQUAL(p,"LEFT")) align=HALIGN_LEFT;
   else if(STRIEQUAL(p,"CENTER")) align=HALIGN_CENTER;
   else if(STRIEQUAL(p,"RIGHT")) align=HALIGN_RIGHT;
   return align;
}

/* Get vertictal alignment value */
static short Getvalign(UBYTE *p,BOOL strict)
{  short align=-1;
   if(STRIEQUAL(p,"TOP")) align=VALIGN_TOP;
   else if(STRIEQUAL(p,"MIDDLE")) align=VALIGN_MIDDLE;
   else if(STRIEQUAL(p,"BOTTOM")) align=VALIGN_BOTTOM;
   else if(!strict)
   {  if(STRIEQUAL(p,"TEXTTOP")) align=VALIGN_TOP;
      else if(STRIEQUAL(p,"ABSMIDDLE")) align=VALIGN_MIDDLE;
      else if(STRIEQUAL(p,"BASELINE")) align=VALIGN_BOTTOM;
      else if(STRIEQUAL(p,"ABSBOTTOM")) align=VALIGN_BOTTOM;
   }
   return align;
}

/* Get floating alignment value */
static short Getflalign(UBYTE *p)
{  short align=-1;
   if(STRIEQUAL(p,"LEFT")) align=HALIGN_FLOATLEFT;
   else if(STRIEQUAL(p,"RIGHT")) align=HALIGN_FLOATRIGHT;
   return align;
}

/* Get a boolean value */
static long Getbool(UBYTE *p,long defval)
{  switch(toupper(*p))
   {  case 'Y':
      case 'T':
      case '1':
         defval=TRUE;
         break;
      case 'N':
      case 'F':
      case '0':
         defval=FALSE;
         break;
   }
   return defval;
}

/* Convert (n) into roman number in (buf). Returns number of characters */
static long Makeroman(UBYTE *buf,long n,BOOL lower)
{  char *p=buf;
   char letter[]={ 'M','D','C','L','X','V','I' };
   long m=1000,i=0;
   n%=10000;
   while(n)
   {  if(i>0)
      {  if(n>=9*m)
         {  *p++=letter[i];
            *p++=letter[i-2];
            n-=9*m;
         }
         else
         {  if(n>=4*m && n<5*m)
            {  *p++=letter[i];
               n+=m;
            }
            if(n>=5*m)
            {  *p++=letter[i-1];
               n-=5*m;
            }
         }
      }
      while(n>=m)
      {  *p++=letter[i];
         n-=m;
      }
      i+=2;
      m/=10;
   }
   *p++=' ';
   *p='\0';
   if(lower)
   {  for(p=buf;*p;p++) *p=tolower(*p);
   }
   return p-buf;
}

/* Find the map descriptor with this name, or create a new one */
static void *Findmap(struct Document *doc,UBYTE *name)
{  struct Aobject *map;
   for(map=doc->maps.first;map->next;map=map->next)
   {  if(STRIEQUAL((UBYTE *)Agetattr(map,AOMAP_Name),name))
      {  return map;
      }
   }
   map=Anewobject(AOTP_MAP,
      AOBJ_Pool,doc->pool,
      AOMAP_Name,name,
      TAG_END);
   if(map) ADDTAIL(&doc->maps,map);
   return map;
}

/* Create the map descriptor for this external map definition */
static void *Externalmap(struct Document *doc,UBYTE *urlname)
{  void *url,*copy,*referer;
   void *map=NULL;
   UBYTE *mapname=Fragmentpart(urlname);
   if(mapname && *mapname)
   {  if(url=Findurl(doc->base,urlname,0))
      {  referer=(void *)Agetattr(doc->source->source,AOSRC_Url);
         if(url==referer)
         {  map=Findmap(doc,mapname);
         }
         else
         {  copy=Anewobject(AOTP_COPY,
               AOBJ_Pool,doc->pool,
               AOCPY_Url,url,
               AOCPY_Referer,referer,
               AOCPY_Defaulttype,"text/html",
               AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
               AOCPY_Mapdocument,TRUE,
               TAG_END);
            if(copy)
            {  map=Anewobject(AOTP_MAP,
                  AOBJ_Pool,doc->pool,
                  AOBJ_Window,doc->win,
                  AOMAP_Name,"",
                  AOMAP_Extcopy,copy,
                  AOMAP_Extname,mapname,
                  TAG_END);
               if(map)
               {  ADDTAIL(&doc->maps,map);
               }
               else
               {  Adisposeobject(copy);
               }
            }
         }
      }
   }
   return map;
}

/* Create the background image with this url */
void *Backgroundimg(struct Document *doc,UBYTE *urlname)
{  void *url,*referer,*elt=NULL;
   struct Bgimage *bgi;
   if(!(url=Findurl(doc->base,urlname,0))) return NULL;
   for(bgi=doc->bgimages.first;bgi->next;bgi=bgi->next)
   {  if(bgi->url==url) return bgi->copy;
   }
   referer=(void *)Agetattr(doc->source->source,AOSRC_Url);
   if(url==referer) return NULL; /* Catch <BODY BACKGROUND=""> */
   elt=Anewobject(AOTP_COPY,
      AOBJ_Pool,doc->pool,
      AOBJ_Layoutparent,doc->body,
      AOBJ_Frame,doc->frame,
      AOBJ_Cframe,doc->frame,
      AOBJ_Window,doc->win,
      AOCPY_Url,url,
      AOCPY_Background,TRUE,
      AOCPY_Referer,referer,
      AOCPY_Defaulttype,"image/x-unknown",
      AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
      TAG_END);
   if(bgi=ALLOCSTRUCT(Bgimage,1,MEMF_CLEAR))
   {  bgi->url=url;
      bgi->copy=elt;
      NEWLIST(&bgi->bgusers);
      ADDTAIL(&doc->bgimages,bgi);
   }
   return elt;
}

/* Add an infotext */
static void Addinfotext(struct Document *doc,UBYTE *name,UBYTE *content,void *link)
{  struct Infotext *it;
   long len=0;
   if(name) len+=strlen(name)+2;
   if(content) len+=strlen(content);
   if(it=ALLOCSTRUCT(Infotext,1,0))
   {  if(it->text=(UBYTE *)Pallocmem(len+1,MEMF_PUBLIC|MEMF_CLEAR,doc->pool))
      {  if(name)
         {  strcpy(it->text,name);
            strcat(it->text,": ");
         }
         if(content)
         {  strcat(it->text,content);
         }
         it->link=link;
         ADDTAIL(&doc->infotexts,it);
      }
      else
      {  FREE(it);
      }
   }
}

/* Only process certain tagtypes for a map definition document */
static USHORT Mapdoctag(USHORT tagtype)
{  switch(tagtype&MARKUP_MASK)
   {  case MARKUP_MAP:
      case MARKUP_AREA:
      case MARKUP_STYLE:
      case MARKUP_SCRIPT:
         break;
      default:
         tagtype=0;
   }
   return tagtype;
}

/*--- header contents -------------------------------------------------*/

/*** <TITLE> ***/
static BOOL Dotitle(struct Document *doc)
{  if(!(Ensuresp(doc))) return FALSE;
   doc->titlepos=doc->text.length;
   doc->pmode=DPM_TITLE;
   return TRUE;
}

/*** text within title ***/
static BOOL Dotitleadd(struct Document *doc,struct Tagattr *ta)
{  return Addtotextbuf(doc,ATTR(doc,ta),ta->length);
}

/*** </TITLE> ***/
static BOOL Dotitleend(struct Document *doc)
{  UBYTE *p;
   UBYTE *end;
   doc->pmode=DPM_BODY;
   if(!Addtotextbuf(doc,"\0 ",2)) return FALSE;
   /* Use length-based iteration instead of null-terminator to prevent
    * reading past buffer bounds. The buffer should be null-terminated,
    * but we use length as the authoritative source to be safe. */
   end=doc->text.buffer+doc->text.length;
   for(p=doc->text.buffer+doc->titlepos;p<end && *p;p++)
   {  if(!isspace(*p))
      {  doc->dflags|=DDF_TITLEVALID;
         break;
      }
   }
   return TRUE;
}

/* Brief load-phase message in the status bar (URL clipped to fit STATUSBUFSIZE). */
static void Docsetloadstatus(struct Document *doc,UBYTE *msg,UBYTE *url)
{  UBYTE buf[STATUSBUFSIZE];
   UBYTE clip[100];
   long ulen;
   long take;
   if(!doc || !doc->win || !msg) return;
   clip[0]='\0';
   if(url && *url)
   {  ulen=(long)strlen((char *)url);
      take=ulen;
      if(take>99) take=99;
      strncpy((char *)clip,(char *)url+(ulen-take),(int)take+1);
      clip[take]='\0';
      sprintf((char *)buf,"%s %s",msg,clip);
   }
   else strncpy((char *)buf,(char *)msg,sizeof(buf)-1);
   buf[sizeof(buf)-1]='\0';
   Asetattrs(doc->win,AOWIN_Status,buf,TAG_END);
}

/*** <BASE> ***/
static BOOL Dobase(struct Document *doc,struct Tagattr *ta)
{  UBYTE *base=NULL,*target=NULL;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_HREF:
            if(STRICT) base=Dupstr(ATTR(doc,ta),-1);
            else base=Makeabsurl(doc->base,ATTR(doc,ta));
            if(base)
            {  if(doc->base) FREE(doc->base);
               doc->base=base;
            }
            break;
         case TAGATTR_TARGET:
            if(target=Dupstrp(ATTR(doc,ta),-1,doc->pool))
            {  if(doc->target) FREE(doc->target);
               doc->target=target;
            }
            break;
      }
   }
   return TRUE;
}

/*------------------------------------------------------------------------*/
/* Same-site domain matching (subdomains allowed)                          */
/*------------------------------------------------------------------------*/

/* Return TRUE if 'host' ends with 'suffix' and the match is on a label boundary:
 * either host==suffix or host ends with "." + suffix. Case-insensitive. */
static BOOL HostHasSuffix(UBYTE *host, long hostlen, UBYTE *suffix, long suffixlen)
{  if(!host || !suffix || hostlen <= 0 || suffixlen <= 0) return FALSE;
   if(hostlen < suffixlen) return FALSE;
   if(hostlen == suffixlen)
   {  return BOOLVAL(Strnicmp((char *)host, (char *)suffix, (ULONG)suffixlen) == 0);
   }
   /* hostlen > suffixlen */
   if(host[hostlen - suffixlen - 1] != '.') return FALSE;
   return BOOLVAL(Strnicmp((char *)(host + (hostlen - suffixlen)), (char *)suffix, (ULONG)suffixlen) == 0);
}

/* Strip optional ":port" from a host string (in-place length). */
static void StripHostPort(UBYTE *host, long *plen)
{  long i;
   if(!host || !plen) return;
   for(i = 0; i < *plen; i++)
   {  if(host[i] == ':')
      {  *plen = i;
         return;
      }
   }
}

/* Determine an approximate "site base domain" (e.g. amigazen.com) from a host.
 * This is a heuristic (no PSL) but good enough for our HTML4-era policy: allow
 * subdomains of the same site, reject other sites. */
static void SiteBaseDomain(UBYTE *host, long hostlen, UBYTE **out, long *outlen)
{  long i;
   long dots;
   long start;
   /* Common multi-label public suffixes where base domain needs 3 labels. */
   static const char *suffixes3[] =
   {  "co.uk","org.uk","ac.uk","gov.uk",
      "com.au","net.au","org.au",
      "co.jp","ne.jp",
      NULL
   };
   const char **s;
   long sufLen;
   if(!out || !outlen)
   {  return;
   }
   *out = host;
   *outlen = hostlen;
   if(!host || hostlen <= 0)
   {  return;
   }
   /* Basic normalization: ignore port. */
   StripHostPort(host, &hostlen);
   *out = host;
   *outlen = hostlen;
   if(hostlen <= 0)
   {  return;
   }
   /* If host is an IPv4-ish literal, just use whole host. */
   for(i = 0; i < hostlen; i++)
   {  if(!(isdigit((unsigned char)host[i]) || host[i] == '.'))
      {  break;
      }
   }
   if(i == hostlen)
   {  return;
   }
   /* If host matches a known 3-label suffix, return last 3 labels. */
   for(s = suffixes3; *s; s++)
   {  sufLen = (long)strlen(*s);
      if(HostHasSuffix(host, hostlen, (UBYTE *)*s, sufLen))
      {  /* Return last 3 labels: find 2 dots before suffix start (or less). */
         start = hostlen - sufLen;
         if(start > 0 && host[start - 1] == '.') start--;
         dots = 0;
         for(i = start - 1; i >= 0; i--)
         {  if(host[i] == '.')
            {  dots++;
               if(dots == 2)
               {  *out = host + i + 1;
                  *outlen = hostlen - (i + 1);
                  return;
               }
            }
         }
         /* Not enough dots: fall back to whole host */
         return;
      }
   }
   /* Default: last 2 labels (everything after the second-to-last dot). */
   dots = 0;
   for(i = hostlen - 1; i >= 0; i--)
   {  if(host[i] == '.')
      {  dots++;
         if(dots == 2)
         {  *out = host + i + 1;
            *outlen = hostlen - (i + 1);
            return;
         }
      }
   }
}

/* True if two hosts are in the same "site" (same base domain), allowing subdomains. */
static BOOL SameSiteHost(UBYTE *hostA, long lenA, UBYTE *hostB, long lenB)
{  UBYTE *baseA;
   UBYTE *baseB;
   long baseAlen;
   long baseBlen;
   if(!hostA || !hostB || lenA <= 0 || lenB <= 0) return FALSE;
   StripHostPort(hostA, &lenA);
   StripHostPort(hostB, &lenB);
   if(lenA <= 0 || lenB <= 0) return FALSE;
   /* Exact host match */
   if(lenA == lenB && Strnicmp((char *)hostA, (char *)hostB, (ULONG)lenA) == 0)
   {  return TRUE;
   }
   /* Subdomain match on full host: allow foo.example.com <-> example.com. */
   if(HostHasSuffix(hostA, lenA, hostB, lenB) || HostHasSuffix(hostB, lenB, hostA, lenA))
   {  return TRUE;
   }
   /* Base domain match */
   SiteBaseDomain(hostA, lenA, &baseA, &baseAlen);
   SiteBaseDomain(hostB, lenB, &baseB, &baseBlen);
   if(baseAlen == baseBlen && baseAlen > 0
      && Strnicmp((char *)baseA, (char *)baseB, (ULONG)baseAlen) == 0)
   {  return TRUE;
   }
   return FALSE;
}

/*** <LINK> ***/
static BOOL Dolink(struct Document *doc,struct Tagattr *ta)
{  UBYTE *rel=NULL,*title=NULL,*href;
   void *url=NULL;
   UBYTE *extcss;
   BOOL thirdpartycss;
   UBYTE *csshost;
   long csshostlen;
   UBYTE *docdomain;
   long docdomainlen;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_REL:
            rel=ATTR(doc,ta);
            break;
         case TAGATTR_HREF:
            href=ATTR(doc,ta);
            url=Findurl(doc->base,href,0);
            break;
         case TAGATTR_TITLE:
            title=ATTR(doc,ta);
            break;
      }
   }
   /* Check for external stylesheet */
   if(url && rel && STRIEQUAL(rel,"stylesheet"))
   {  extern BOOL httpdebug;
      UBYTE *urlstr;
      BOOL isReload;
      urlstr = (UBYTE *)Agetattr(url,AOURL_Url);
      isReload = (doc->pflags&DPF_RELOADVERIFY) && !(doc->pflags&DPF_NORLDOCEXT);
      if(httpdebug)
      {  AwebLog("css", "Dolink: Found stylesheet link, href=%s, reload=%d, existing stylesheet=%p",
                urlstr ? (char *)urlstr : "NULL", isReload ? 1 : 0, doc->cssstylesheet);
      }
      /* Block third-party CSS. AWeb targets an HTML4-era web where same-site CSS is typical;
       * modern third-party CSS (e.g. Google Fonts) is mostly @font-face and can be expensive
       * to fetch/parse while adding no value to our renderer. */
      thirdpartycss = FALSE;
      docdomain = doc->jdomain;
      csshost = NULL;
      csshostlen = 0;
      if(docdomain && *docdomain)
      {  docdomainlen = (long)strlen((char *)docdomain);
         Getjspart(url, UJP_HOST, &csshost, &csshostlen);
         if(csshost && csshostlen > 0)
         {  if(!SameSiteHost(docdomain, docdomainlen, csshost, csshostlen))
            {  thirdpartycss = TRUE;
            }
         }
      }
      if(thirdpartycss)
      {  if(httpdebug)
         {  AwebLog("css", "Dolink: Blocking third-party stylesheet: dochost=%s, srchost=%.*s, url=%s",
                   docdomain ? (char *)docdomain : "NULL",
                   (int)csshostlen, csshost ? (char *)csshost : "",
                   urlstr ? (char *)urlstr : "NULL");
         }
         return TRUE;
      }
      /* Try to load external CSS */
      extcss = Finddocext(doc,url,isReload);
      if(extcss)
      {  if(extcss == (UBYTE *)~0)
         {  extern BOOL httpdebug;
            if(httpdebug)
            {  AwebLog("css", "Dolink: External CSS load error, skipping");
            }
            /* External CSS is in error, skip it */
         }
         else
         {  extern BOOL httpdebug;
            UBYTE *contenttype;
            BOOL contentIsCss;
            BOOL payloadIsCss;
            contenttype = NULL;
            contentIsCss = TRUE;
            payloadIsCss = TRUE;
            /* Check if this CSS was already merged via AODOC_Docextready.
             * If DPF_NORLDOCEXT is set AND stylesheet exists, it means the CSS was already processed.
             * If stylesheet is NULL, the flag might be from a previous document, so merge anyway. */
            if((doc->pflags&DPF_NORLDOCEXT) && doc->cssstylesheet)
            {  if(httpdebug)
               {  AwebLog("css", "Dolink: CSS already merged via AODOC_Docextready, skipping duplicate merge");
               }
               /* CSS was already merged, just apply to body if needed */
               if(doc->body && doc->cssstylesheet)
               {  if(httpdebug)
                  {  AwebLog("css", "Dolink: Re-applying CSS to all elements (already merged), body=%p, stylesheet=%p, frame=%p",
                           doc->body, doc->cssstylesheet, doc->frame);
                  }
                  /* Reapply CSS to all existing elements to ensure deterministic application */
                  ApplyDocCssIfReady(doc);
                  if(doc->win && doc->frame)
                  {  Registerdoccolors(doc);
                  }
               }
            }
            else
            {  /* CRITICAL: Don't call strlen on extcss - it's a pointer to a shared buffer
                 * that might not be properly null-terminated or could be reallocated.
                 * MergeCSSStylesheet will copy it safely. */
               /* If server says this is not CSS, don't attempt to parse as CSS even if the URL ends in .css.
                * This prevents intermittent "CSS not applied" when an error page (HTML) is returned. */
               if(url)
               {  contenttype = (UBYTE *)Agetattr(url,AOURL_Contenttype);
                  if(contenttype && strnicmp((char *)contenttype,"text/css",8) != 0)
                  {  contentIsCss = FALSE;
                  }
               }
               /* Some legacy stylesheets are wrapped in HTML comments ("<!-- ... -->").
                * Only reject actual HTML documents here. */
               if(extcss && extcss[0] == '<')
               {  if(STRNIEQUAL(extcss,"<!--",4))
                  {  payloadIsCss = TRUE;
                  }
                  else if(STRNIEQUAL(extcss,"<!DOCTYPE",9)
                  || STRNIEQUAL(extcss,"<HTML",5)
                  || STRNIEQUAL(extcss,"<?XML",5))
                  {  payloadIsCss = FALSE;
                  }
               }
               if(httpdebug)
               {  /* extcss points to a shared/network buffer; it may be chunked or not NUL-terminated.
                   * Never call strlen()/Dupstr(-1) here: scanning past the buffer can stall the UI
                   * and/or corrupt memory. */
                  AwebLog("css", "Dolink: External CSS loaded, calling MergeCSSStylesheet");
               }
               if(!contentIsCss || !payloadIsCss)
               {  if(httpdebug)
                  {  AwebLog("css", "Dolink: Stylesheet content is not CSS (content-type=%s), skipping merge",
                           contenttype ? (char *)contenttype : "NULL");
                  }
               }
               else
               {
               /* On reload, free existing stylesheet first to start fresh */
               if((doc->pflags & DPF_RELOADVERIFY) && doc->cssstylesheet)
               {  if(httpdebug)
                  {  AwebLog("css", "Dolink: Reload detected, freeing existing stylesheet before merge");
                  }
                  FreeCSSStylesheet(doc);
               }
               /* Merge external CSS with existing stylesheet */
               if(httpdebug)
               {  AwebLog("css", "Dolink: Merging CSS into stylesheet (existing=%p)", doc->cssstylesheet);
               }
               MergeCSSStylesheet(doc,extcss);
               /* Prevent duplicate merge if AODOC_Docextready is called later */
               doc->pflags |= DPF_NORLDOCEXT;
               /* Apply link colors from CSS (a:link, a:visited) */
               ApplyCSSToLinkColors(doc);
               /* Always re-apply CSS to body when external CSS loads */
               if(doc->body && doc->cssstylesheet)
               {  if(httpdebug)
                  {  AwebLog("css", "Dolink: Re-applying CSS to all elements after external CSS load, body=%p, stylesheet=%p, frame=%p",
                            doc->body, doc->cssstylesheet, doc->frame);
                  }
                  ApplyDocCssIfReady(doc);
                  /* Re-register colors to ensure link colors are updated */
                  if(doc->win && doc->frame)
                  {  Registerdoccolors(doc);
                  }
               }
               else if(httpdebug)
               {  AwebLog("css", "Dolink: Cannot apply CSS - body=%p, stylesheet=%p, frame=%p", doc->body, doc->cssstylesheet, doc->frame);
               }
               }
            }
         }
      }
      else
      {  extern BOOL httpdebug;
         if(httpdebug)
         {  AwebLog("css", "Dolink: External CSS not yet available, suspending parsing");
         }
         Docsetloadstatus(doc,(UBYTE *)"Fetching CSS",urlstr);
         /* External CSS not yet available, suspend parsing. Only then defer
          * AOBJ_Changedchild from Srcupdatedocument: setting DPF_EXTCSSEXPECT before
          * Finddocext could suppress refresh across sync/re-entrant completion paths
          * and stall loading (e.g. nested AODOC_Docextready while buffer is still parsing). */
         doc->pflags|=DPF_SUSPEND|DPF_EXTCSSEXPECT;
      }
   }
   if(url)
   {  Addinfotext(doc,title?NULL:rel,title?title:(UBYTE *)Agetattr(url,AOURL_Url),url);
      Asetattrs(doc->copy,AOCPY_Infochanged,TRUE,TAG_END);
   }
   return TRUE;
}

/*** <META> ***/
static BOOL Dometa(struct Document *doc,struct Tagattr *ta)
{  UBYTE *httpequiv=NULL,*content=NULL,*name=NULL,*charset=NULL;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_HTTP_EQUIV:
            httpequiv=ATTR(doc,ta);
            break;
         case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_CONTENT:
            content=ATTR(doc,ta);
            break;
         case TAGATTR_CHARSET:
            charset=ATTR(doc,ta);
            break;
      }
   }
   if((httpequiv && content && STRIEQUAL(httpequiv,"REFRESH"))
   || (name && content && STRIEQUAL(name,"REFRESH")))
   {  if(doc->clientpull) FREE(doc->clientpull);
      doc->clientpull=Dupstrp(content,-1,doc->pool);
      Asetattrs(doc->copy,AOURL_Clientpull,content,TAG_END);
   }
   if(httpequiv && content && STRIEQUAL(httpequiv,"CONTENT-SCRIPT-TYPE"))
   {  SETFLAG(doc->pflags,DPF_SCRIPTJS,STRIEQUAL(content,"TEXT/JAVASCRIPT"));
   }
   /* Charset detection:
    * - HTML5: <meta charset="...">
    * - HTML4: <meta http-equiv="Content-Type" content="text/html; charset=...">
    */
   if(charset && *charset)
   {  UBYTE *p;
      p=charset;
      while(*p && isspace(*p)) p++;
      if(*p)
      {  if(!STRIEQUAL(p,"ISO-8859-1"))
         {  doc->dflags|=DDF_FOREIGN;
         }
         if(STRIEQUAL(p,"SHIFT_JIS") || STRIEQUAL(p,"SHIFT-JIS") || STRIEQUAL(p,"SHIFTJIS")
         || STRIEQUAL(p,"SJIS") || STRIEQUAL(p,"X-SJIS") || STRIEQUAL(p,"MS_KANJI") || STRIEQUAL(p,"CSSHIFTJIS"))
         {  doc->charset=DOCCHARSET_SHIFT_JIS;
            doc->japanesemode=1;
         }
         else if(STRIEQUAL(p,"UTF-8") || STRIEQUAL(p,"UTF8"))
         {  doc->charset=DOCCHARSET_UTF8;
            doc->japanesemode=0;
         }
         if(doc->body
         && !(doc->cssstylesheet && doc->cssappliedserial == doc->cssserial))
         {  
            if(doc->charset==DOCCHARSET_SHIFT_JIS)
            {  Asetattrs(doc->body,AOBDY_Fontface,(UBYTE *)"JKFF",TAG_END);
            }
            else if(doc->charset==DOCCHARSET_UTF8)
            {  Asetattrs(doc->body,AOBDY_Fontface,(UBYTE *)AWEB_UTF8_FONTFACE,TAG_END);
            }
         }
      }
   }
   else if(httpequiv && content && STRIEQUAL(httpequiv,"CONTENT-TYPE"))
   {  UBYTE *p,*q;
      p=NULL;
      for(q=content;*q && *q!=';';q++);
      for(;*q && !STRNIEQUAL(q,"CHARSET=",8);q++);
      if(*q)
      {  p=q+8;
         while(*p && isspace(*p)) p++;
         if(*p=='"')
         {  p++;
            for(q=p;*q && *q!='"';q++);
            *q='\0';
         }
         else
         {  for(q=p;*q && !isspace(*q);q++);
            *q='\0';
         }
         if(*p && !STRIEQUAL(p,"ISO-8859-1")) doc->dflags|=DDF_FOREIGN;
         if(STRIEQUAL(p,"SHIFT_JIS") || STRIEQUAL(p,"SHIFT-JIS") || STRIEQUAL(p,"SHIFTJIS")
         || STRIEQUAL(p,"SJIS") || STRIEQUAL(p,"X-SJIS") || STRIEQUAL(p,"MS_KANJI") || STRIEQUAL(p,"CSSHIFTJIS"))
         {  doc->charset=DOCCHARSET_SHIFT_JIS;
            doc->japanesemode=1;
         }
         else if(STRIEQUAL(p,"UTF-8") || STRIEQUAL(p,"UTF8"))
         {  doc->charset=DOCCHARSET_UTF8;
            doc->japanesemode=0;
         }
         if(doc->body
         && !(doc->cssstylesheet && doc->cssappliedserial == doc->cssserial))
         {  if(doc->charset==DOCCHARSET_SHIFT_JIS)
            {  Asetattrs(doc->body,AOBDY_Fontface,(UBYTE *)"JKFF",TAG_END);
            }
            else if(doc->charset==DOCCHARSET_UTF8)
            {  Asetattrs(doc->body,AOBDY_Fontface,(UBYTE *)AWEB_UTF8_FONTFACE,TAG_END);
            }
         }
      }
   }
   /* Parse viewport meta tag: <meta name="viewport" content="width=device-width, initial-scale=1.0"> */
   if(name && content && STRIEQUAL(name,"VIEWPORT"))
   {  UBYTE *content_copy,*p,*key_start,*key_end,*value_start,*value_end;
      long viewport_width=0;
      /* Make a copy of content for parsing to avoid modifying original */
      content_copy=Dupstr(content,-1);
      if(content_copy)
      {  p=content_copy;
         while(p && *p)
         {  /* Skip whitespace and commas */
            while(*p && (isspace(*p) || *p==',')) p++;
            if(!*p) break;
            /* Find key */
            key_start=p;
            while(*p && *p!='=' && !isspace(*p) && *p!=',') p++;
            if(*p!='=') break;
            key_end=p;
            /* Trim trailing whitespace from key */
            while(key_end>key_start && isspace(key_end[-1])) key_end--;
            *key_end='\0';
            p++;
            /* Skip whitespace after = */
            while(*p && isspace(*p)) p++;
            if(!*p) break;
            /* Find value */
            value_start=p;
            if(*p=='"')
            {  p++;
               value_start=p;
               while(*p && *p!='"') p++;
               value_end=p;
               if(*p) p++;
            }
            else
            {  while(*p && *p!=',' && !isspace(*p)) p++;
               value_end=p;
            }
            *value_end='\0';
            /* Parse width value */
            if(STRIEQUAL(key_start,"WIDTH"))
            {  if(STRIEQUAL(value_start,"DEVICE-WIDTH"))
               {  /* device-width means use window inner width (default), so 0 */
                  viewport_width=0;
               }
               else
               {  struct Number num;
                  /* Try to parse as number */
                  Getnumber(&num,value_start);
                  if(num.type==NUMBER_NUMBER && num.n>0)
                  {  viewport_width=num.n;
                  }
               }
            }
            /* Skip to next key=value pair */
            while(*p && (isspace(*p) || *p==',')) p++;
         }
         FREE(content_copy);
         /* Store viewport width in document */
         if(viewport_width>=0)
         {  doc->viewportwidth=viewport_width;
            /* Pass to copy driver so frame can use it */
            Asetattrs(doc->copy,AOCDV_Viewportwidth,viewport_width,TAG_END);
         }
      }
   }
   if(httpequiv) name=httpequiv;
   if(name && content)
   {  Addinfotext(doc,name,content,NULL);
      Asetattrs(doc->copy,AOCPY_Infochanged,TRUE,TAG_END);
   }
   return TRUE;
}

/*--- JavaScript ------------------------------------------------------*/

/*** <SCRIPT> ***/
static BOOL Doscript(struct Document *doc,struct Tagattr *ta)
{  UBYTE *language;
   UBYTE *src=NULL;
   BOOL isjs=FALSE;
   BOOL thirdparty=FALSE;
   UBYTE *scrstart;
   long scrlength;
   UBYTE *docdomain;
   long docdomainlen;
   extern BOOL httpdebug;
   if(doc->pflags&DPF_SCRIPTJS) language="JavaScript";
   else language="";
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_LANGUAGE:
            language=ATTR(doc,ta);
            break;
         case TAGATTR_TYPE:
            if(STRIEQUAL(ATTR(doc,ta),"text/javascript")) language="JavaScript";
            else language="unknown";
            break;
         case TAGATTR_SRC:
            src=ATTR(doc,ta);
            break;
      }
   }
   if(prefs.browser.dojs)
   {  if(STRNIEQUAL(language,"LiveScript",10)) isjs=TRUE;
      else if(STRNIEQUAL(language,"JavaScript",10))
      {  if(prefs.browser.dojs==DOJS_ALL) isjs=TRUE;
         else if(language[10]=='1')
         {  if(language[11]=='.')
            {  if(language[12]=='0' || language[12]=='1') isjs=TRUE;
            }
         }
         else if(!language[10]) isjs=TRUE;
      }
   }
   if(isjs)      
   {  if(src)
      {  void *url;
         UBYTE *extsrc;
         url=Findurl(doc->base,src,0);
         /* Third-party JavaScript is a common crash/compat risk on modern sites.
          * Since AWeb targets an HTML4/JS1.x era, block executing external scripts
          * that come from a different host than the document itself. Inline scripts
          * (no SRC) and same-host scripts are still allowed. */
         thirdparty=FALSE;
         docdomain=doc->jdomain;
         if(url && docdomain && *docdomain)
         {  docdomainlen=(long)strlen((char *)docdomain);
            scrstart=NULL;
            scrlength=0;
            Getjspart(url,UJP_HOST,&scrstart,&scrlength);
            if(scrstart && scrlength>0)
            {  if(!SameSiteHost(docdomain, docdomainlen, scrstart, scrlength))
               {  thirdparty=TRUE;
               }
            }
         }
         if(thirdparty)
         {  if(httpdebug)
            {  UBYTE *absjs;
               absjs=(UBYTE *)Agetattr(url,AOURL_Url);
               printf("[js] blocked third-party script dochost=%s srchost=%.*s url=%s\n",
                      docdomain ? (char *)docdomain : "NULL",
                      (int)scrlength, scrstart ? (char *)scrstart : "",
                      absjs ? (char *)absjs : "NULL");
            }
            doc->pmode=DPM_SCRIPT;
            return TRUE;
         }
         if(extsrc=Finddocext(doc,url,
            (doc->pflags&DPF_RELOADVERIFY) && !(doc->pflags&DPF_NORLDOCEXT)))
         {  if(extsrc==(UBYTE *)~0)
            {  /* External source is in error, use element contents. */
               Freebuffer(&doc->jsrc);
               doc->pmode=DPM_BODY;
               doc->pflags|=DPF_JSCRIPT;
               doc->dflags|=DDF_NOSPARE;
               doc->jsrcline=Docslinenrfrompos(doc->source,doc->srcpos);
            }
            else
            {  /* Add external source to jsource buffer, skip element contents, and
                * execute the script when </SCRIPT> tag is encountered. */
               Addtobuffer(&doc->jsrc,extsrc,-1);
               doc->pmode=DPM_SCRIPT;
               doc->pflags|=DPF_JSCRIPT;
               doc->pflags&=~DPF_NORLDOCEXT;
               doc->jsrcline=-1; /* 1 will be added when passed to JS library */
            }
         }
         else
         {  UBYTE *absjs;
            /* External source not yet available. */
            absjs=(UBYTE *)Agetattr(url,AOURL_Url);
            Docsetloadstatus(doc,(UBYTE *)"Fetching JavaScript",absjs);
            doc->pflags|=DPF_SUSPEND|DPF_NORLDOCEXT;
            doc->jsrcline=Docslinenrfrompos(doc->source,doc->srcpos);
         }
      }
      else
      {  Freebuffer(&doc->jsrc);
         doc->pflags|=DPF_JSCRIPT;
         doc->dflags|=DDF_NOSPARE;
         doc->jsrcline=Docslinenrfrompos(doc->source,doc->srcpos);
      }
   }
   else
   {  doc->pmode=DPM_SCRIPT;
   }
   return TRUE;
}

/*** JavaScript source ***/
static BOOL Dojsource(struct Document *doc,struct Tagattr *ta)
{  if(ta->length>0)
   if(!Addtobuffer(&doc->jsrc,ATTR(doc,ta),ta->length)) return FALSE;
   ta=ta->next;
   if(ta && ta->next && ta->attr==TAGATTR_BR)
   {  if(!Addtobuffer(&doc->jsrc,"\n",1)) return FALSE;
   }
   return TRUE;
}

/*** CSS source ***/
static BOOL Docsssource(struct Document *doc,struct Tagattr *ta)
{  if(ta->length>0)
   if(!Addtobuffer(&doc->csssrc,ATTR(doc,ta),ta->length)) return FALSE;
   ta=ta->next;
   if(ta && ta->next && ta->attr==TAGATTR_BR)
   {  if(!Addtobuffer(&doc->csssrc,"\n",1)) return FALSE;
   }
   return TRUE;
}

/*** </STYLE> - Process CSS ***/
static BOOL Docssend(struct Document *doc)
{  UBYTE *css;
   extern BOOL httpdebug;
   if(doc->csssrc.length>0)
   {  if(!Addtobuffer(&doc->csssrc,"",1)) return FALSE; /* null terminate */
      css=doc->csssrc.buffer;
      if(css)
      {  BOOL hadExistingSheet = (doc->cssstylesheet != NULL);
         if(httpdebug)
         {  AwebLog("css", "Docssend: Processing inline CSS, length=%ld bytes, existing stylesheet=%p",
                   doc->csssrc.length, doc->cssstylesheet);
         }
         /* Parse and apply CSS stylesheet */
         /* Pass known text length (excluding the added NUL) so ParseCSS never scans past buffer. */
         ParseCSSStylesheet(doc,css,(long)doc->csssrc.length-1);
         if(httpdebug)
         {  AwebLog("css", "Docssend: CSS parsed, stylesheet=%p, body=%p",
                   doc->cssstylesheet, doc->body);
         }
         /* Apply link colors from CSS (a:link, a:visited) */
         ApplyCSSToLinkColors(doc);
         /* Apply CSS to body if it already exists, or if we merged with existing sheet */
         if(doc->body && doc->cssstylesheet)
         {  if(httpdebug)
            {  AwebLog("css", "Docssend: Applying CSS to all elements");
            }
            ApplyDocCssIfReady(doc);
            /* Re-register colors if we merged (to ensure link colors are updated) */
            if(hadExistingSheet && doc->win && doc->frame)
            {  Registerdoccolors(doc);
            }
         }
         else if(httpdebug)
         {  AwebLog("css", "Docssend: Cannot apply CSS - body=%p, stylesheet=%p", doc->body, doc->cssstylesheet);
         }
      }
      Freebuffer(&doc->csssrc);
   }
   return TRUE;
}

/*** </SCRIPT> ***/
/* Only called while processing JavaScript, not other scripts */
static BOOL Doscriptend(struct Document *doc)
{  long joutpos;
   struct Buffer jout;
   UBYTE *src;
   BOOL jrun,jparse;
   if(doc->pflags&DPF_JSCRIPT)
   {  Addtobuffer(&doc->jsrc,"\0",1);
      src=Dupstr(doc->jsrc.buffer,-1);
      Freebuffer(&doc->jsrc);
      doc->pflags&=~DPF_JSCRIPT;

      /* Make stacked copy of current JS output, since the script may
       * document.write("<SCRIPT>") etc... */
      jout=doc->jout;
      joutpos=doc->joutpos;
      memset(&doc->jout,0,sizeof(doc->jout));
      doc->joutpos=0;
      jrun=BOOLVAL(doc->pflags&DPF_JRUN);
      doc->pflags|=DPF_JRUN;
      Docjexecute(doc,src);
      if(!jrun) doc->pflags&=~DPF_JRUN;

      /* Parse the last bit */
      jparse=BOOLVAL(doc->pflags&DPF_JPARSE);
      doc->pflags|=DPF_JPARSE;
      Parsehtml(doc,&doc->jout,TRUE,&doc->joutpos);
      if(!jparse) doc->pflags&=~DPF_JPARSE;
      if(src) FREE(src);

      /* Restore JS output */
      Freebuffer(&doc->jout);
      doc->jout=jout;
      doc->joutpos=joutpos;
      
      doc->pflags|=DPF_DIDSCRIPT;
   }
   return TRUE;
}

/*** <NOSCRIPT> ***/
static BOOL Donoscript(struct Document *doc,struct Tagattr *ta)
{  if(doc->pflags&DPF_DIDSCRIPT)
   {  doc->pmode=DPM_NOSCRIPT;
   }
   return TRUE;
}

/*** </NOSCRIPT> ***/
static BOOL Donoscriptend(struct Document *doc)
{  if(doc->doctype==DOCTP_FRAMESET) doc->pmode=DPM_FRAMESET;
   else doc->pmode=DPM_BODY;
   doc->pflags&=~DPF_DIDSCRIPT;
   return TRUE;
}

/*--- body ------------------------------------------------------------*/

/*** <BODY> ***/
static BOOL Dobody(struct Document *doc,struct Tagattr *ta)
{  BOOL gotbg=FALSE,gotcolor=FALSE;
   if(STRICT && doc->doctype!=DOCTP_NONE) return TRUE;
   if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype!=DOCTP_BODY) return TRUE;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_BGCOLOR:
            if(!Setbodycolor(doc,&doc->bgcolor,ATTR(doc,ta))) return FALSE;
            gotbg=TRUE;
            break;
         case TAGATTR_TEXT:
            if(!Setbodycolor(doc,&doc->textcolor,ATTR(doc,ta))) return FALSE;
            gotcolor=TRUE;
            break;
         case TAGATTR_LINK:
            if(!Setbodycolor(doc,&doc->linkcolor,ATTR(doc,ta))) return FALSE;
            gotcolor=TRUE;
            break;
         case TAGATTR_VLINK:
            if(!Setbodycolor(doc,&doc->vlinkcolor,ATTR(doc,ta))) return FALSE;
            gotcolor=TRUE;
            break;
         case TAGATTR_ALINK:
            if(!Setbodycolor(doc,&doc->alinkcolor,ATTR(doc,ta))) return FALSE;
            gotcolor=TRUE;
            break;
         case TAGATTR_BACKGROUND:
            doc->bgimage=Backgroundimg(doc,ATTR(doc,ta));
            break;
         case TAGATTR_LEFTMARGIN:
            if(!STRICT)
            {  doc->hmargin=Getposnumber(ATTR(doc,ta));
               doc->dflags|=DDF_HMARGINSET;
               Asetattrs(doc->body,
                  AOBDY_Leftmargin,doc->hmargin,
                  TAG_END);
            }
            break;
         case TAGATTR_TOPMARGIN:
            if(!STRICT)
            {  doc->vmargin=Getposnumber(ATTR(doc,ta));
               doc->dflags|=DDF_VMARGINSET;
               Asetattrs(doc->body,
                  AOBDY_Topmargin,doc->vmargin,
                  TAG_END);
            }
            break;
         case TAGATTR_ONLOAD:
            if(doc->onload) FREE(doc->onload);
            doc->onload=Dupstrp(ATTR(doc,ta),-1,doc->pool);
            if(doc->jobject && doc->frame)
            {  /* Jobject already exists so we must add our handler to
                 * it belatedly.
                 * Usually means there was <script> in the head */
               struct Jcontext *jc;
               struct Frame *fr;
               struct Jobject *parent;
               jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
               if(jc)
               {  fr=(struct Frame *)doc->frame;
                  parent=fr->jobject;
                  if(parent)
                  {  Jaddeventhandler(jc,parent,"onload",doc->onload);
                  }
               }
            }
            break;
         case TAGATTR_ONUNLOAD:
            if(doc->onunload) FREE(doc->onunload);
            doc->onunload=Dupstrp(ATTR(doc,ta),-1,doc->pool);
            if(doc->jobject && doc->frame)
            {  /* Jobject already exists so we must add our handler to
                 * it belatedly.
                 * Usually means there was <script> in the head */
               struct Jcontext *jc;
               struct Frame *fr;
               struct Jobject *parent;
               jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
               if(jc)
               {  fr=(struct Frame *)doc->frame;
                  parent=fr->jobject;
                  if(parent)
                  {  Jaddeventhandler(jc,parent,"onunload",doc->onunload);
                  }
               }
            }
            break;
         case TAGATTR_ONFOCUS:
            if(doc->onfocus) FREE(doc->onfocus);
            doc->onfocus=Dupstrp(ATTR(doc,ta),-1,doc->pool);
            if(doc->frame) Asetattrs(doc->frame,AOFRM_Onfocus,doc->onfocus,TAG_END);
            break;
         case TAGATTR_ONBLUR:
            if(doc->onblur) FREE(doc->onblur);
            doc->onblur=Dupstrp(ATTR(doc,ta),-1,doc->pool);
            if(doc->frame) Asetattrs(doc->frame,AOFRM_Onblur,doc->onblur,TAG_END);
            break;
      }
   }
   /* Apply CSS from stylesheet to body element */
   /* Note: Child elements get CSS applied when they're added via Addelement() */
   if(doc->body)
   {  extern BOOL httpdebug;
      if(httpdebug)
      {  AwebLog("render", "Dobody: Applying CSS to body, stylesheet=%p, body=%p",
                doc->cssstylesheet, doc->body);
      }
      if(doc->cssstylesheet) ApplyCSSToBody(doc,doc->body,NULL,NULL,"BODY");
      /* If CSS exists, always reapply CSS to all existing elements */
      /* This ensures CSS is applied to elements created before CSS loaded (like PRE) */
      /* (New children will get CSS applied via Addelement) */
      if(doc->cssstylesheet)
      {  if(httpdebug)
         {  AwebLog("render", "Dobody: Reapplying CSS to all elements (stylesheet=%p)", doc->cssstylesheet);
         }
         ApplyDocCssIfReady(doc);
      }
   }
   if(gotcolor && !gotbg)
   {  /* Set bg to white if no bg is defined but other colors are. */
      Setbodycolor(doc,&doc->bgcolor,"#ffffff");
   }
   /* Register colours with our frame - this ensures CSS link colors are applied */
   if(doc->win && doc->frame)
   {  extern BOOL httpdebug;
      Registerdoccolors(doc);
      if(httpdebug)
      {  AwebLog("render", "Dobody: Registered document colors (linkcolor=%p vlinkcolor=%p)",
                doc->linkcolor, doc->vlinkcolor);
      }
   }
   Checkid(doc,ta);
   return TRUE;
}

/*--- text, line breaks -----------------------------------------------*/

/*** <BLINK> ***/
static BOOL Doblink(struct Document *doc,struct Tagattr *ta)
{  if(!STRICT)
   {  doc->pflags|=DPF_BLINK;
   }
   Checkid(doc,ta);
   return TRUE;
}

/*** </BLINK> ***/
static BOOL Doblinkend(struct Document *doc)
{  if(!STRICT)
   {  doc->pflags&=~DPF_BLINK;
   }
   return TRUE;
}

/*** <MARQUEE> ***/
#if 0
static BOOL Domarquee(struct Document *doc,struct Tagattr *ta)
{  void *body;
   long width=-1,height=-1;
   long scrollamount=6,scrolldelay=85,loop=-1;
   UBYTE *direction=NULL,*behavior=NULL;
   UBYTE *p;
   struct Number num;
   ULONG wtag=TAG_IGNORE,htag=TAG_IGNORE;
   
   if(STRICT) return TRUE;  /* Not in strict HTML */
   
   /* Parse marquee attributes */
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_WIDTH:
            width=Getnumber(&num,ATTR(doc,ta));
            wtag=AOBDY_MaxWidth;
            if(width<=0) width=100;
            else if(num.type!=NUMBER_PERCENT)
            {  wtag=AOBDY_MaxWidth;  /* Use maxwidth for pixel width */
            }
            break;
         case TAGATTR_HEIGHT:
            height=Getnumber(&num,ATTR(doc,ta));
            htag=AOBDY_MaxHeight;
            if(height<=0) height=100;
            else if(num.type!=NUMBER_PERCENT)
            {  htag=AOBDY_MaxHeight;  /* Use maxheight for pixel height */
            }
            break;
         case TAGATTR_DIRECTION:
            p=ATTR(doc,ta);
            if(p)
            {  direction=Dupstrp(p,-1,doc->pool);
               /* Normalize to lowercase */
               for(p=direction;*p;p++)
               {  if(*p>='A' && *p<='Z') *p=*p+('a'-'A');
               }
            }
            break;
         case TAGATTR_BEHAVIOR:
            p=ATTR(doc,ta);
            if(p)
            {  behavior=Dupstrp(p,-1,doc->pool);
               /* Normalize to lowercase */
               for(p=behavior;*p;p++)
               {  if(*p>='A' && *p<='Z') *p=*p+('a'-'A');
               }
            }
            break;
         case TAGATTR_SCROLLAMOUNT:
            scrollamount=Getnumber(&num,ATTR(doc,ta));
            if(scrollamount<=0) scrollamount=6;
            break;
         case TAGATTR_SCROLLDELAY:
            scrolldelay=Getnumber(&num,ATTR(doc,ta));
            if(scrolldelay<=0) scrolldelay=85;
            break;
         case TAGATTR_LOOP:
            loop=Getnumber(&num,ATTR(doc,ta));
            if(loop<=0) loop=-1;  /* -1 means infinite */
            break;
      }
   }
   
   /* Default direction is left if not specified */
   if(!direction) direction=Dupstrp((UBYTE *)"left",-1,doc->pool);
   /* Default behavior is scroll if not specified */
   if(!behavior) behavior=Dupstrp((UBYTE *)"scroll",-1,doc->pool);
   
   /* Create NEW body element for marquee container */
   Wantbreak(doc,1);
   if(!Ensurebody(doc)) return FALSE;
   
   /* Get parent body to inherit frame/window settings */
   body=Docbodync(doc);
   if(!body) return FALSE;
   
   /* Create new body element for marquee container */
   doc->marqueebody=Anewobject(AOTP_BODY,
      AOBJ_Pool,doc->pool,
      AOBJ_Frame,Agetattr(body,AOBJ_Frame),
      AOBJ_Cframe,Agetattr(body,AOBJ_Cframe),
      AOBJ_Window,Agetattr(body,AOBJ_Window),
      AOBJ_Layoutparent,body,
      AOBDY_TagName,Dupstrp((UBYTE *)"MARQUEE",-1,doc->pool),
      AOBDY_Overflow,Dupstrp((UBYTE *)"hidden",-1,doc->pool),
      CONDTAG(wtag,width),
      CONDTAG(htag,height),
      AOBDY_MarqueeDirection,direction,
      AOBDY_MarqueeBehavior,behavior,
      AOBDY_MarqueeScrollAmount,scrollamount,
      AOBDY_MarqueeScrollDelay,scrolldelay,
      AOBDY_MarqueeLoop,loop,
      AOBDY_MarqueeScrollX,0,
      AOBDY_MarqueeScrollY,0,
      TAG_END);
   
   if(!doc->marqueebody) return FALSE;
   
   /* Temporarily clear DPF_MARQUEE flag so Addelement adds to parent body, not marquee body */
   doc->pflags&=~DPF_MARQUEE;
   /* Add marquee body to document as a child of current body */
   if(!Addelement(doc,doc->marqueebody))
   {  doc->marqueebody=NULL;
      return FALSE;
   }
   /* Restore DPF_MARQUEE flag so subsequent content goes into marquee body */
   doc->pflags|=DPF_MARQUEE;
   Checkid(doc,ta);
   return TRUE;
}

/*** </MARQUEE> ***/
static BOOL Domarqueeend(struct Document *doc)
{  if(!STRICT)
   {  doc->pflags&=~DPF_MARQUEE;
      /* Marquee body already has all data stored via AOBDY_ attributes */
      /* It will be registered for animation when it becomes visible */
      /* This happens in body.c when AOBJ_Cframe is set */
      doc->marqueebody=NULL;  /* Clear reference */
   }
   return TRUE;
}
#endif

/*** <BR> ***/
static BOOL Dobr(struct Document *doc,struct Tagattr *ta)
{  void *elt;
   BOOL clrleft=FALSE,clrright=FALSE;
   short gotbreak;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_CLEAR:
            if(STRIEQUAL(ATTR(doc,ta),"LEFT")) clrleft=TRUE;
            else if(STRIEQUAL(ATTR(doc,ta),"RIGHT")) clrright=TRUE;
            else if(STRIEQUAL(ATTR(doc,ta),"ALL")) clrleft=clrright=TRUE;
            break;
      }
   }
   if(!(elt=Anewobject(AOTP_BREAK,
      AOBJ_Pool,doc->pool,
      AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
      AOBRK_Clearleft,clrleft,
      AOBRK_Clearright,clrright,
      TAG_END))) return FALSE;
   if(doc->wantbreak>0) doc->wantbreak--;
   gotbreak=doc->gotbreak+doc->wantbreak;
   if(!Addelement(doc,elt)) return FALSE;
   doc->gotbreak=gotbreak+1;
   if(!Ensuresp(doc)) return FALSE;
   Checkid(doc,ta);
   return TRUE;
}

/*** <CENTER> ***/
static BOOL Docenter(struct Document *doc,struct Tagattr *ta)
{  void *body;
   UBYTE *class = NULL;
   UBYTE *id = NULL;
   struct Tagattr *tap;
   tap = ta;  /* Save original ta pointer for Checkid */
   Wantbreak(doc,1);
   if(!Ensurebody(doc)) return FALSE;
   body = Docbodync(doc);
   /* Parse class and id attributes */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_CLASS:
            class = ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id = ATTR(doc,ta);
            break;
      }
   }
   Asetattrs(body,AOBDY_Divalign,HALIGN_CENTER,TAG_END);
   /* Store class/id/tagname on body for CSS matching */
   if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
   if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
   Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"CENTER",-1,doc->pool),TAG_END);
   /* Apply CSS to CENTER element based on its class/id/tagname */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"CENTER");
   Checkid(doc,tap);  /* Use original ta pointer */
   return TRUE;
}
/*** <DIV> ***/
static BOOL Dodiv(struct Document *doc,struct Tagattr *ta)
{  short align=-1;
   void *body;
   UBYTE *styleAttr=NULL;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   struct Tagattr *tap;  /* Save original ta pointer for Checkid */
   BOOL isInline;
   short sp;
   extern BOOL httpdebug;

   /* Save original ta pointer (sentinel) for Checkid */
   tap = ta;

   /* Get attributes first to check for display: inline */
   /* ta is the sentinel (list head), ta->next is the first real attribute */
   /* Use exact same pattern as Dopara */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_ALIGN:
            align=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            if(httpdebug && class)
            {  printf("[DIV] Dodiv: Found class='%s' in attributes\n", (char *)class);
            }
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
      }
   }
   if(httpdebug)
   {  printf("[DIV] Dodiv: After parsing attributes - class=%s, id=%s\n",
             (class ? (char *)class : "NULL"),
             (id ? (char *)id : "NULL"));
   }

   /* Push DIV onto parse ancestor stack for CSS descendant/child selectors */
   if(doc->divancsp < DIV_ANCESTOR_STACK_MAX)
   {  sp = doc->divancsp;
      doc->divanc[sp].tagname = Dupstrp((UBYTE *)"DIV",-1,doc->pool);
      doc->divanc[sp].class = class ? Dupstrp(class,-1,doc->pool) : NULL;
      doc->divanc[sp].id = id ? Dupstrp(id,-1,doc->pool) : NULL;
      doc->divancsp = sp + 1;
   }

   /* Check if this DIV should be inline */
   /* Only check if we have a class, ID, or inline style - otherwise skip expensive CSS check */
   if(class || id || styleAttr)
   {  isInline = IsDivInline(doc,class,id,styleAttr);

   }
   else
   {  isInline = FALSE;
   }
   doc->currentdivinline = isInline;

   /* Only add line break if not inline */
   if(!isInline)
   {  Wantbreak(doc,1);
   }

   if(!Ensurebody(doc)) return FALSE;
   body = Docbodync(doc);
   Asetattrs(body,AOBDY_Divalign,align,TAG_END);
   /* Store class/id/tagname on body for CSS matching */
   if(class)
   {  Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
      if(httpdebug)
      {  printf("[DIV] Dodiv: Set class='%s' on body=%p\n", (char *)class, body);
      }
   }
   else
   {  Asetattrs(body,AOBDY_Class,NULL,TAG_END);
   }
   if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
   else Asetattrs(body,AOBDY_Id,NULL,TAG_END);
   Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"DIV",-1,doc->pool),TAG_END);
   if(httpdebug)
   {  printf("[DIV] Dodiv: Set tagname='DIV' on body=%p, class=%s\n", body, (class ? (char *)class : "NULL"));
   }
   Checkid(doc,tap);  /* Use original sentinel pointer */
   /* Apply CSS to body based on class/ID */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"DIV");
   /* Extract background-color from external stylesheet for text elements */
   {  struct Colorinfo *cssBgcolor;
      cssBgcolor = ExtractBackgroundColorFromRules(doc,class,id,"DIV");
      doc->parabgcolor = cssBgcolor;
   }
   /* Apply inline CSS if present */
   if(styleAttr)
   {  struct Colorinfo *cssBgcolor;
      ApplyInlineCSSToBody(doc,body,styleAttr,(UBYTE *)"DIV");
      /* Extract background-color for text elements in this div (inline overrides external) */
      cssBgcolor = ExtractBackgroundColorFromStyle(doc,styleAttr);
      if(cssBgcolor) doc->parabgcolor = cssBgcolor;
   }
   return TRUE;
}

/*** </DIV> ***/
static BOOL Dodivend(struct Document *doc)
{  void *body;
   short sp;
   /* Only add line break if DIV was not inline */
   if(!doc->currentdivinline)
{  Wantbreak(doc,1);
   }
   doc->currentdivinline = FALSE; /* Reset flag */
   body=Docbodync(doc);
   Asetattrs(body,AOBDY_Divalign,-1,TAG_END);
   /* Pop DIV parse ancestor stack */
   if(doc->divancsp > 0)
   {  sp = doc->divancsp - 1;
      if(doc->divanc[sp].tagname) FREE(doc->divanc[sp].tagname);
      if(doc->divanc[sp].class) FREE(doc->divanc[sp].class);
      if(doc->divanc[sp].id) FREE(doc->divanc[sp].id);
      doc->divanc[sp].tagname = NULL;
      doc->divanc[sp].class = NULL;
      doc->divanc[sp].id = NULL;
      doc->divancsp = sp;
   }
   /* Clear paragraph background color and text transform */
   doc->parabgcolor = NULL;
   doc->texttransform = 0;
   if(!Ensuresp(doc)) return FALSE;
   return TRUE;
}

/*** <NOBR> ***/
static BOOL Donobr(struct Document *doc,struct Tagattr *ta)
{  if(!STRICT)
   {  if(!Ensurebody(doc)) return FALSE;
      Asetattrs(Docbody(doc),AOBDY_Nobr,TRUE,TAG_END);
      Checkid(doc,ta);
   }
   return TRUE;
}

/*** </NOBR> ***/
static BOOL Donobrend(struct Document *doc)
{  if(!STRICT)
   {  Asetattrs(Docbody(doc),AOBDY_Nobr,FALSE,TAG_END);
   }
   return TRUE;
}

/*** <P> ***/
static BOOL Dopara(struct Document *doc,struct Tagattr *ta)
{  short align=-1;
   void *body;
   UBYTE *styleAttr=NULL;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   struct Tagattr *sentinel;
   Wantbreak(doc,2);
   /* Save ta for Checkid - ta is the sentinel node */
   sentinel = ta;
   /* Start from ta->next (first real attribute), skip sentinel */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_ALIGN:
            align=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
      }
   }
   if(!Ensurebody(doc)) return FALSE;
   body = Docbodync(doc);
   Asetattrs(body,AOBDY_Align,align,TAG_END);
   /* Store class/id/tagname on body for CSS matching */
   if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
   if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
   Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"P",-1,doc->pool),TAG_END);
   if(!Ensuresp(doc)) return FALSE;
   Checkid(doc,sentinel);
   /* Apply CSS to body based on class/ID */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"P");
   /* Extract background-color from external stylesheet for text elements */
   {  struct Colorinfo *cssBgcolor;
      cssBgcolor = ExtractBackgroundColorFromRules(doc,class,id,"P");
      doc->parabgcolor = cssBgcolor;
   }
   /* Apply inline CSS if present */
   if(styleAttr)
   {  struct Colorinfo *cssBgcolor;
      ApplyInlineCSSToBody(doc,body,styleAttr,(UBYTE *)"P");
      /* Extract background-color for text elements in this paragraph (inline overrides external) */
      cssBgcolor = ExtractBackgroundColorFromStyle(doc,styleAttr);
      if(cssBgcolor) doc->parabgcolor = cssBgcolor;
   }
   return TRUE;
}

/*** </P> ***/
static BOOL Doparaend(struct Document *doc)
{  Wantbreak(doc,2);
   Asetattrs(Docbodync(doc),AOBDY_Align,-1,TAG_END);
   /* Clear paragraph background color and text transform */
   doc->parabgcolor = NULL;
   doc->texttransform = 0;
   if(!Ensuresp(doc)) return FALSE;
   return TRUE;
}

/*** text ***/
static BOOL Dotext(struct Document *doc,struct Tagattr *ta)
{  void *elt;
   UBYTE *text;
   UBYTE *transformed;
   long i;
   BOOL needTransform;
   BOOL isPreformat;
   BOOL eltPreformat;
   UBYTE preview[101];
   long previewLen;
   
   if(ta->length>0)
   {  text = ATTR(doc,ta);
      transformed = NULL;
      needTransform = FALSE;
      
      /* Apply text-transform if set */
      if(doc->texttransform > 0)
      {  transformed = ALLOCTYPE(UBYTE,ta->length + 1,0);
         if(transformed)
         {  needTransform = TRUE;
            if(doc->texttransform == 1)
            {  /* uppercase */
               for(i = 0; i < ta->length; i++)
               {  transformed[i] = (UBYTE)toupper(text[i]);
               }
            }
            else if(doc->texttransform == 2)
            {  /* lowercase */
               for(i = 0; i < ta->length; i++)
               {  transformed[i] = (UBYTE)tolower(text[i]);
               }
            }
            else if(doc->texttransform == 3)
            {  /* capitalize - capitalize first letter of each word */
               BOOL capitalizeNext;
               capitalizeNext = TRUE;
               for(i = 0; i < ta->length; i++)
               {  if(isspace(text[i]))
                  {  transformed[i] = text[i];
                     capitalizeNext = TRUE;
                  }
                  else
                  {  if(capitalizeNext)
                     {  transformed[i] = (UBYTE)toupper(text[i]);
                        capitalizeNext = FALSE;
                     }
                     else
                     {  transformed[i] = (UBYTE)tolower(text[i]);
                     }
                  }
               }
            }
            transformed[ta->length] = '\0';
            text = transformed;
         }
      }
      
      isPreformat = (BOOL)(doc->pflags & DPF_PREFORMAT);
      if(isPreformat)
      {  /* no debug output */
      }
      
      if(!(elt=Anewobject(AOTP_TEXT,
         AOBJ_Pool,doc->pool,
         AOELT_Textpos,doc->text.length,
         AOELT_Textlength,ta->length,
         AOELT_Preformat,isPreformat,
         AOTXT_Blink,doc->pflags&DPF_BLINK,
         AOTXT_Text,&doc->text,
         (doc->parabgcolor ? AOTXT_Bgcolor : TAG_IGNORE),doc->parabgcolor,
         TAG_END))) 
      {
         if(transformed) FREE(transformed);
         return FALSE;
      }
      
      if(isPreformat)
      {  eltPreformat = (BOOL)Agetattr(elt, AOELT_Preformat);
      }
      
      if(!Addelement(doc,elt)) 
      {
         if(transformed) FREE(transformed);
         return FALSE;
      }
      if(!Addtotextbuf(doc,text,ta->length)) 
      {
         if(transformed) FREE(transformed);
         return FALSE;
      }
      if(transformed) FREE(transformed);
   }
   ta=ta->next;
   if(ta && ta->next && ta->attr==TAGATTR_BR)
   {  Wantbreak(doc,1);
      if(!Solvebreaks(doc)) return FALSE;
      doc->gotbreak=0;
   }
   return TRUE;
}

/*** <WBR> ***/
static BOOL Dowbr(struct Document *doc,struct Tagattr *ta)
{  void *elt;
   if(!STRICT)
   {  if(!(elt=Anewobject(AOTP_BREAK,
         AOBJ_Pool,doc->pool,
         AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
         AOBRK_Wbr,TRUE,
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      Checkid(doc,ta);
   }
   return TRUE;
}

/*--- preformatted ----------------------------------------------------*/

/*** <PRE> ***/
static BOOL Dopre(struct Document *doc,struct Tagattr *ta)
{  void *body;
   short style;
   BOOL fixedfont;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   struct Tagattr *sentinel;
   
   if(!Ensurebody(doc))
   {  return FALSE;
   }
   Wantbreak(doc,1);
   doc->pflags|=DPF_PREFORMAT;

   body = Docbody(doc);
   
   /* Extract class and id attributes for CSS matching */
   sentinel=ta;
   /* Use exact same pattern as Dopara */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
      }
   }
   
   /* Set tag name, class, and id on body for CSS matching */
   if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
   if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
   Asetattrs(body,
      AOBDY_TagName,Dupstrp((UBYTE *)"PRE",-1,doc->pool),
      AOBDY_Style,STYLE_PRE,
      AOBDY_Fixedfont,TRUE,
      AOBDY_WhiteSpace,Dupstrp((UBYTE *)"pre",-1,doc->pool),  /* Set white-space: pre by default for PRE elements */
      AOBDY_Nobr,FALSE,  /* PRE elements should not use no-break (allows line breaks in preformatted text) */
      TAG_END);
   
   /* Verify attributes were set */
   style = (short)Agetattr(body, AOBDY_Style);
   fixedfont = (BOOL)Agetattr(body, AOBDY_Fixedfont);
   
   doc->charcount=0;
   
   Checkid(doc,sentinel);
   
   /* Apply CSS to PRE body based on class/ID */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"PRE");
   
   return TRUE;
}

/*** </PRE> ***/
static BOOL Dopreend(struct Document *doc)
{  void *body;
   BOOL fixedfont;
   
   doc->pflags&=~(DPF_PREFORMAT|DPF_LISTING|DPF_XMP);

   body = Docbody(doc);
   Asetattrs(body,
      AOBDY_Fixedfont,FALSE,
      AOBDY_Fontend,TRUE,
      TAG_END);
   
   /* Verify attributes were set */
   fixedfont = (BOOL)Agetattr(body, AOBDY_Fixedfont);

   if(doc->charcount)
   {  Wantbreak(doc,1);
   }
   if(!Ensuresp(doc))
   {  return FALSE;
   }
   return TRUE;
}

/*** <LISTING> ***/
static BOOL Dolisting(struct Document *doc,struct Tagattr *ta)
{  if(!Dopre(doc,ta)) return FALSE;
   doc->pflags|=DPF_LISTING;
   return TRUE;
}

/*** <XMP> ***/
static BOOL Doxmp(struct Document *doc,struct Tagattr *ta)
{  if(!Dopre(doc,ta)) return FALSE;
   doc->pflags|=DPF_XMP;
   return TRUE;
}

/*--- hard styles -----------------------------------------------------*/

/*** <B> <I> <U> <STRIKE> </B> </I> </U> </STRIKE> ***/
/* Set or unset child's style if it is a body. */
static BOOL Dohardstyle(struct Document *doc,struct Tagattr *ta,USHORT style,BOOL set)
{  if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         (set?AOBDY_Sethardstyle:AOBDY_Unsethardstyle),style,
         TAG_END);
   }
   Checkid(doc,ta);
   return TRUE;
}

/*** <TT> </TT> ***/
static BOOL Dott(struct Document *doc,struct Tagattr *ta,BOOL set)
{  if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),AOBDY_Fixedfont,set,TAG_END);
   }
   Checkid(doc,ta);
   return TRUE;
}

/*--- logical styles --------------------------------------------------*/

/*** <CITE> <CODE> <DFN> <EM> <KBD> <SAMP> <STRONG> <VAR> <BIG> <SMALL> ***/
static BOOL Dostyle(struct Document *doc,struct Tagattr *ta,USHORT style)
{  if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),AOBDY_Style,style,TAG_END);
   }
   Checkid(doc,ta);
   return TRUE;
}

/*** </CITE> </CODE> </DFN> </EM> </KBD> </SAMP> </STRONG> </VAR> </BIG> </SMALL> ***/
static BOOL Dostyleend(struct Document *doc)
{  if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),AOBDY_Fontend,TRUE,TAG_END);
   }
   return TRUE;
}

/*** <SPAN> ***/
static BOOL Dospan(struct Document *doc,struct Tagattr *ta)
{  UBYTE *styleAttr=NULL;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   struct Tagattr *tap;  /* Save original ta pointer for Checkid */
   void *body;
   short depth;
   USHORT style;
   UBYTE *prevTag;
   UBYTE *prevClass;
   UBYTE *prevId;
   
   /* Save original ta pointer (sentinel) for Checkid */
   tap = ta;
   
   /* Extract class, id, and style attributes */
   /* ta is the sentinel (list head), ta->next is the first real attribute */
   /* Use exact same pattern as Dopara */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
      }
   }
   
   /* Ensure body exists */
   if(!Ensurebody(doc)) return FALSE;

   /* Snapshot style state so </SPAN> can restore it. Note that SPAN is inline and we
    * apply CSS onto the current body; without an explicit restore, CSS font styles
    * bleed into subsequent content. */
   if(doc->doctype==DOCTP_BODY)
   {  body = Docbody(doc);
      if(doc->spansp < SPAN_STACK_MAX)
      {  style = (USHORT)Agetattr(body, AOBDY_Style);
         depth = (short)Agetattr(body, AOBDY_Fontdepth);
         doc->spanstate[doc->spansp].style = style;
         doc->spanstate[doc->spansp].fontdepth = depth;
         prevTag = (UBYTE *)Agetattr(body, AOBDY_TagName);
         prevClass = (UBYTE *)Agetattr(body, AOBDY_Class);
         prevId = (UBYTE *)Agetattr(body, AOBDY_Id);
         doc->spanstate[doc->spansp].tagname = prevTag ? Dupstrp(prevTag, -1, doc->pool) : NULL;
         doc->spanstate[doc->spansp].class = prevClass ? Dupstrp(prevClass, -1, doc->pool) : NULL;
         doc->spanstate[doc->spansp].id = prevId ? Dupstrp(prevId, -1, doc->pool) : NULL;
         doc->spansp++;
      }
   }
   
   /* Apply inline styles if present */
   if(styleAttr && doc->doctype==DOCTP_BODY)
   {  ApplyInlineCSSToBody(doc,Docbody(doc),styleAttr,(UBYTE *)"SPAN");
   }
   
   /* Apply CSS based on class/id if stylesheet exists */
   if((class || id) && doc->doctype==DOCTP_BODY && doc->cssstylesheet)
   {  void *body = Docbody(doc);
      /* Store class/id/tagname on body for CSS matching */
      if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
      if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
      Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"SPAN",-1,doc->pool),TAG_END);
      /* Apply CSS to body based on class/ID */
      if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"SPAN");
   }
   
   /* Handle id for anchor navigation */
   Checkid(doc,tap);  /* Use original sentinel pointer */
   
   return TRUE;
}

/*** </SPAN> ***/
static BOOL Dospanend(struct Document *doc)
{  void *body;
   short curdepth;
   short targetdepth;
   USHORT curstyle;
   USHORT savestyle;
   USHORT mask;
   UBYTE *prevTag;
   UBYTE *prevClass;
   UBYTE *prevId;

   if(doc->doctype==DOCTP_BODY && doc->spansp > 0)
   {  doc->spansp--;
      savestyle = doc->spanstate[doc->spansp].style;
      targetdepth = doc->spanstate[doc->spansp].fontdepth;
      prevTag = doc->spanstate[doc->spansp].tagname;
      prevClass = doc->spanstate[doc->spansp].class;
      prevId = doc->spanstate[doc->spansp].id;
      body = Docbody(doc);

      /* Unwind any font pushes that occurred inside the span. */
      curdepth = (short)Agetattr(body, AOBDY_Fontdepth);
      while(curdepth > targetdepth)
      {  Asetattrs(body, AOBDY_Fontend, TRUE, TAG_END);
         curdepth = (short)Agetattr(body, AOBDY_Fontdepth);
      }

      /* Restore hardstyle bits that might have been set by CSS (bold/italic/underline/strike). */
      curstyle = (USHORT)Agetattr(body, AOBDY_Style);
      mask = (USHORT)(FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED | FSF_STRIKE);
      /* Only touch the hardstyle bits; leave other style bits alone. */
      if((curstyle & mask) != (savestyle & mask))
      {  /* Bold */
         if(savestyle & FSF_BOLD) Asetattrs(body, AOBDY_Sethardstyle, FSF_BOLD, TAG_END);
         else Asetattrs(body, AOBDY_Unsethardstyle, FSF_BOLD, TAG_END);
         /* Italic */
         if(savestyle & FSF_ITALIC) Asetattrs(body, AOBDY_Sethardstyle, FSF_ITALIC, TAG_END);
         else Asetattrs(body, AOBDY_Unsethardstyle, FSF_ITALIC, TAG_END);
         /* Underline */
         if(savestyle & FSF_UNDERLINED) Asetattrs(body, AOBDY_Sethardstyle, FSF_UNDERLINED, TAG_END);
         else Asetattrs(body, AOBDY_Unsethardstyle, FSF_UNDERLINED, TAG_END);
         /* Strike */
         if(savestyle & FSF_STRIKE) Asetattrs(body, AOBDY_Sethardstyle, FSF_STRIKE, TAG_END);
         else Asetattrs(body, AOBDY_Unsethardstyle, FSF_STRIKE, TAG_END);
      }

      /* Restore tag/class/id to what they were before this SPAN. This is critical because
       * the CSS matcher will fall back to AOBDY_Class/AOBDY_Id/AOBDY_TagName when hints are
       * not provided; leaving SPAN values behind causes selector matches to "bleed" into
       * subsequent elements. */
      Asetattrs(body,
         AOBDY_TagName, prevTag,
         AOBDY_Class, prevClass,
         AOBDY_Id, prevId,
         TAG_END);
   }

   return TRUE;
}

/*** <INS> ***/
static BOOL Doins(struct Document *doc, struct Tagattr *ta)
{  struct Colorinfo *ci;
   if(!Ensurebody(doc)) return FALSE;
   /* Choose red colour for inserted text. Might be better configurable */
   ci = Finddoccolor(doc,0xff0000L);
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Sethardstyle,FSF_UNDERLINED,
         AOBDY_Fontcolor,(Tag)ci,
         TAG_END);
   }
   Checkid(doc, ta);
   return TRUE;
}

/*** </INS> ***/
static BOOL Doinsend(struct Document *doc)
{  if(doc->doctype == DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Unsethardstyle,FSF_UNDERLINED,
         AOBDY_Fontend,TRUE,
         TAG_END);
   }
   return TRUE;
}

/*** <DEL> ***/
static BOOL Dodel(struct Document *doc, struct Tagattr *ta)
{  struct Colorinfo *ci;
   if(!Ensurebody(doc)) return FALSE;
   /* Choose grey colour for deleted text. Make it configurable, too! */
   ci = Finddoccolor(doc,0xccccccL);
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Sethardstyle,FSF_STRIKE,
         AOBDY_Fontcolor,(Tag)ci,
         TAG_END);
   }
   Checkid(doc, ta);
   return TRUE;
}

/*** </DEL> ***/
static BOOL Dodelend(struct Document *doc)
{  if(doc->doctype == DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Unsethardstyle, FSF_STRIKE,
         AOBDY_Fontend, TRUE,
         TAG_END);
   }
   return TRUE;
}

/*** <ADDRESS> ***/
static BOOL Doaddress(struct Document *doc,struct Tagattr *ta)
{  void *body;
   UBYTE *class = NULL;
   UBYTE *id = NULL;
   struct Tagattr *tap;
   tap = ta;  /* Save original ta pointer for Checkid */
   if(!Ensurebody(doc)) return FALSE;
   body = Docbody(doc);
   /* Parse class and id attributes */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_CLASS:
            class = ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id = ATTR(doc,ta);
            break;
      }
   }
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(body,
         AOBDY_Style,STYLE_ADDRESS,
         TAG_END);
   }
   /* Store class/id/tagname on body for CSS matching */
   if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
   if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
   Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"ADDRESS",-1,doc->pool),TAG_END);
   /* Apply CSS to ADDRESS element based on its class/id/tagname */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"ADDRESS");
   Checkid(doc,tap);  /* Use original ta pointer */
   Wantbreak(doc,1);
   return TRUE;
}

/*** </ADDRESS> ***/
static BOOL Doaddressend(struct Document *doc)
{  if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Fontend,TRUE,
         TAG_END);
   }
   Wantbreak(doc,1);
   if(!Ensuresp(doc)) return FALSE;
   return TRUE;
}

/*** <BLOCKQUOTE> ***/
static BOOL Doblockquote(struct Document *doc,struct Tagattr *ta)
{  UBYTE *class=NULL;
   UBYTE *id=NULL;
   void *body;
   struct Tagattr *sentinel;
   
   /* Save sentinel for Checkid */
   sentinel = ta;
   
   /* Parse class and id attributes using same pattern as Dopara */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
      }
   }
   
   if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  body = Docbody(doc);
      Asetattrs(body,
         AOBDY_Style,STYLE_BLOCKQUOTE,
         AOBDY_Blockquote,TRUE,
         TAG_END);
      /* Store class/id/tagname on body for CSS matching */
      if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
      if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
      Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"BLOCKQUOTE",-1,doc->pool),TAG_END);
      /* Apply CSS to body based on class/ID */
      if(doc->cssstylesheet)
      {  if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"BLOCKQUOTE");
      }
   }
   Checkid(doc,sentinel);
   Wantbreak(doc,1);
   return TRUE;
}

/*** </BLOCKQUOTE> ***/
static BOOL Doblockquoteend(struct Document *doc)
{  if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Fontend,TRUE,
         AOBDY_Blockquote,FALSE,
         TAG_END);
   }
   Wantbreak(doc,1);
   if(!Ensuresp(doc)) return FALSE;
   return TRUE;
}

/*** <Hn> ***/
static BOOL Doheading(struct Document *doc,short level,struct Tagattr *ta)
{  short align=-1;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   void *body;
   struct Tagattr *sentinel;
   UBYTE *tagname;
   char tagnamebuf[4];
   
   /* Save sentinel for Checkid */
   sentinel = ta;
   
   /* Parse attributes using same pattern as Dopara */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_ALIGN:
            align=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
      }
   }
   if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  Wantbreak(doc,2);
      if(!Solvebreaks(doc)) return FALSE;
      body = Docbody(doc);
      Asetattrs(body,
         AOBDY_Style,STYLE_H1+level,
         AOBDY_Align,align,
         TAG_END);
      /* Store class/id/tagname on body for CSS matching */
      if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
      if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
      /* Create tagname like "H1", "H2", etc. */
      tagnamebuf[0] = 'H';
      tagnamebuf[1] = '1' + level;
      tagnamebuf[2] = '\0';
      tagname = (UBYTE *)Dupstr((UBYTE *)tagnamebuf,-1);
      Asetattrs(body,AOBDY_TagName,tagname,TAG_END);
      /* Apply CSS to body based on class/ID */
      if(doc->cssstylesheet)
      {  if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,tagname);
      }
   }
   if(!Ensuresp(doc)) return FALSE;
   Checkid(doc,sentinel);
   return TRUE;
}

/*** </Hn> ***/
static BOOL Doheadingend(struct Document *doc)
{  if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Fontend,TRUE,
         AOBDY_Align,-1,
         TAG_END);
   }
   if(!Ensuresp(doc)) return FALSE;
   Wantbreak(doc,2);
   return TRUE;
}

/*** <SUB> ***/
static BOOL Dosub(struct Document *doc,struct Tagattr *ta)
{  if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Style,STYLE_SUB,
         AOBDY_Subscript,TRUE,
         TAG_END);
   }
   Checkid(doc,ta);
   return TRUE;
}

/*** </SUB> ***/
static BOOL Dosubend(struct Document *doc)
{  if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Fontend,TRUE,
         AOBDY_Subscript,FALSE,
         TAG_END);
   }
   return TRUE;
}

/*** <SUP> ***/
static BOOL Dosup(struct Document *doc,struct Tagattr *ta)
{  if(!Ensurebody(doc)) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Style,STYLE_SUP,
         AOBDY_Superscript,TRUE,
         TAG_END);
   }
   Checkid(doc,ta);
   return TRUE;
}

/*** </SUP> ***/
static BOOL Dosupend(struct Document *doc)
{  if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),
         AOBDY_Fontend,TRUE,
         AOBDY_Superscript,FALSE,
         TAG_END);
   }
   return TRUE;
}

/*--- font ------------------------------------------------------------*/

/*** <BASEFONT> ***/
static BOOL Dobasefont(struct Document *doc,struct Tagattr *ta)
{  short size=0;
   struct Number num;
   ULONG sizetag=TAG_IGNORE;
   ULONG colorrgb=~0;
   struct Colorinfo *ci=NULL;
   UBYTE *face=NULL;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_SIZE:
            size=Getnumber(&num,ATTR(doc,ta));
            if(num.type==NUMBER_SIGNED) sizetag=AOBDY_Basefontrel;
            else sizetag=AOBDY_Basefont;
            break;
         case TAGATTR_COLOR:
            Gethexcolor(doc,ATTR(doc,ta),&colorrgb);
            break;
         case TAGATTR_FACE:
            face=ATTR(doc,ta);
            break;
      }
   }
   if(size || colorrgb!=~0 || face)
   {  if(!Ensurebody(doc)) return FALSE;
      if(doc->doctype==DOCTP_BODY)
      {  if(colorrgb!=~0)
         {  if(!(ci=Finddoccolor(doc,colorrgb))) return FALSE;
         }
         Asetattrs(Docbody(doc),
            sizetag,size,
            (ci?AOBDY_Basecolor:TAG_IGNORE),ci,
            (face?AOBDY_Baseface:TAG_IGNORE),face,
            TAG_END);
      }
   }
   Checkid(doc,ta);
   return TRUE;
}

/*** <FONT> ***/
static BOOL Dofont(struct Document *doc,struct Tagattr *ta)
{  short size=0;
   ULONG sizetag=TAG_IGNORE;
   struct Number num;
   ULONG colorrgb=~0;
   struct Colorinfo *ci=NULL;
   UBYTE *face=NULL;
   UBYTE *styleAttr=NULL;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   void *body;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_SIZE:
            size=Getnumber(&num,ATTR(doc,ta));
            if(num.type==NUMBER_SIGNED) sizetag=AOBDY_Fontsizerel;
            else sizetag=AOBDY_Fontsize;
            break;
         case TAGATTR_COLOR:
            Gethexcolor(doc,ATTR(doc,ta),&colorrgb);
            break;
         case TAGATTR_FACE:
            face=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
      }
   }
   if(!Ensurebody(doc)) return FALSE;
   body = Docbodync(doc);
   if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
   if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
   Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"FONT",-1,doc->pool),TAG_END);
   /* Apply CSS to body based on class/ID (before applying FONT attributes) */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"FONT");
   if(sizetag!=TAG_IGNORE || colorrgb!=(ULONG)~0 || face)
   {  if(!Solvebreaks(doc)) return FALSE;
      if(doc->doctype==DOCTP_BODY)
      {  if(colorrgb!=~0)
         {  if(!(ci=Finddoccolor(doc,colorrgb))) return FALSE;
         }
         /* FONT tag color attribute overrides CSS color */
         Asetattrs(Docbody(doc),
            sizetag,size,
            (ci?AOBDY_Fontcolor:TAG_IGNORE),ci,
            (face?AOBDY_Fontface:TAG_IGNORE),face,
            TAG_END);
      }
   }
   /* Apply inline CSS if present (overrides both external CSS and FONT attributes) */
   if(styleAttr && doc->doctype==DOCTP_BODY)
   {  ApplyInlineCSSToBody(doc,Docbody(doc),styleAttr,(UBYTE *)"FONT");
   }
   Checkid(doc,ta);
   return TRUE;
}

/*** </FONT> ***/
static BOOL Dofontend(struct Document *doc)
{  if(doc->doctype==DOCTP_BODY)
   {  Asetattrs(Docbody(doc),AOBDY_Fontend,TRUE,TAG_END);
   }
   return TRUE;
}

/*--- ruler -----------------------------------------------------------*/

/*** <HR> ***/
static BOOL Dohr(struct Document *doc,struct Tagattr *ta)
{  short width=0;
   ULONG wtag=TAG_IGNORE;
   BOOL noshade=FALSE;
   long size=-1;
   short align=-1;
   ULONG colorrgb=(ULONG)~0;
   struct Colorinfo *color=NULL;
   struct Number num;
   void *elt;
   struct Tagattr *sentinel;
   
   /* Save sentinel for Checkid */
   sentinel = ta;
   
   /* Parse attributes using same pattern as Dopara */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_WIDTH:
            width=Getnumber(&num,ATTR(doc,ta));
            wtag=AORUL_Width;
            if(width<=0) width=100;
            else if(num.type!=NUMBER_PERCENT)
            {  wtag=AORUL_Pixelwidth;
            }
            break;
         case TAGATTR_SIZE:
            size=Getnumber(&num,ATTR(doc,ta));
            break;
         case TAGATTR_NOSHADE:
            noshade=TRUE;
            break;
         case TAGATTR_ALIGN:
            align=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_COLOR:
            if(!STRICT)
            {  Gethexcolor(doc,ATTR(doc,ta),&colorrgb);
            }
            break;
      }
   }
   Wantbreak(doc,1);
   Asetattrs(Docbody(doc),AOBDY_Align,-1,TAG_END);
   if(colorrgb!=(ULONG)~0)
   {  if(!(color=Finddoccolor(doc,colorrgb))) return FALSE;
   }
   if(!(elt=Anewobject(AOTP_RULER,
      AOBJ_Pool,doc->pool,
      wtag,width,
      CONDTAG(AORUL_Size,size),
      AORUL_Noshade,noshade,
      AORUL_Color,color,
      CONDTAG(AOELT_Halign,align),
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   if(!Ensuresp(doc)) return FALSE;
   doc->gotbreak=2;
   return TRUE;
}

/*--- anchor ----------------------------------------------------------*/

/*** <A> ***/
static BOOL Doanchor(struct Document *doc,struct Tagattr *ta)
{  UBYTE *href=NULL,*name=NULL,*title=NULL;
   UBYTE *target=doc->target;
   UBYTE *onclick=NULL,*onmouseover=NULL,*onmouseout=NULL;
   UBYTE *styleAttr=NULL;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   void *body;
   BOOL post=FALSE,targetset=FALSE;
   /* Start from ta->next (first real attribute), skip sentinel */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_HREF:
            href=ATTR(doc,ta);
            break;
         case TAGATTR_NAME:
         case TAGATTR_ID:
            name=ATTR(doc,ta);
            id=ATTR(doc,ta);
            break;
         case TAGATTR_TARGET:
            target=ATTR(doc,ta);
            targetset=TRUE;
            break;
         case TAGATTR_TITLE:
            title=ATTR(doc,ta);
            break;
         case TAGATTR_ONCLICK:
            onclick=ATTR(doc,ta);
            break;
         case TAGATTR_ONMOUSEOVER:
            onmouseover=ATTR(doc,ta);
            break;
         case TAGATTR_ONMOUSEOUT:
            onmouseout=ATTR(doc,ta);
            break;
         case TAGATTR_METHOD:
            if(!STRICT && STRIEQUAL(ATTR(doc,ta),"POST"))
            {  post=TRUE;
            }
            break;
         case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
      }
   }
   if(!Ensurebody(doc)) return FALSE;
   body = Docbodync(doc);
   /* Set class and ID on body for A tags (so ApplyCSSToLink can access them) */
   if(class) Asetattrs(body,AOBDY_Class,Dupstrp(class,-1,doc->pool),TAG_END);
   else Asetattrs(body,AOBDY_Class,NULL,TAG_END);
   if(id) Asetattrs(body,AOBDY_Id,Dupstrp(id,-1,doc->pool),TAG_END);
   else Asetattrs(body,AOBDY_Id,NULL,TAG_END);
   /* Apply CSS to anchor based on class/ID */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,class,id,"A");
   if((href || onclick || onmouseover || onmouseout) && doc->doctype==DOCTP_BODY)
   {  void *link;
      struct Url *url=NULL;
      UBYTE *f=NULL;
      if(href)
      {  f=Fragmentpart(href);
         /* If it is a link to a fragment in this doc use its URL (can't use Findurl
          * because of postnr) */
         if(*href=='#')
         {  url=(struct Url *)Agetattr(doc->source->source,AOSRC_Url);
         }
         else
         {  url=Findurl(doc->base,href,0);
         }
         if(!targetset && Mailnewsurl(href))
         {  target="_top";
         }
      }
      if(!(link=Anewobject(AOTP_LINK,
         AOBJ_Pool,doc->pool,
         AOBJ_Frame,doc->frame,
         AOBJ_Cframe,doc->frame,
         AOBJ_Window,doc->win,
         AOLNK_Url,url,
         AOLNK_Fragment,f,
         AOLNK_Target,target,
         AOLNK_Text,&doc->text,
         AOLNK_Title,title,
         AOLNK_Onclick,onclick,
         AOLNK_Onmouseover,onmouseover,
         AOLNK_Onmouseout,onmouseout,
         AOLNK_Post,post,
         TAG_END))) return FALSE;
      ADDTAIL(&doc->links,link);
      Asetattrs(Docbody(doc),AOBDY_Link,link,TAG_END);
      /* Apply CSS from stylesheet to link */
      ApplyCSSToLink(doc,link,body);
      /* Apply inline CSS if present (overrides stylesheet) */
      if(styleAttr)
      {  ApplyInlineCSSToLink(doc,link,body,styleAttr);
      }
   }
   if(name && doc->doctype==DOCTP_BODY)
   {  void *elt;
      struct Fragment *frag;
      if(!Solvebreaks(doc)) return FALSE;
      if(!(elt=Anewobject(AOTP_NAME,
         AOBJ_Pool,doc->pool,
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      if(!(frag=PALLOCSTRUCT(Fragment,1,MEMF_PUBLIC,doc->pool))) return FALSE;
      frag->name=Dupstrp(name,-1,doc->pool);
      frag->elt=elt;
      ADDTAIL(&doc->fragments,frag);
   }   
   return TRUE;
}

/*** </A> ***/
static BOOL Doanchorend(struct Document *doc)
{  if(doc->doctype==DOCTP_BODY)
   {  void *body;
      body = Docbodync(doc);
      Asetattrs(body,AOBDY_Link,NULL,TAG_END);
      /* Clear link text color when link ends */
      Asetattrs(body,AOBDY_LinkTextColor,NULL,TAG_END);
   }
   return TRUE;
}

/*--- frames ----------------------------------------------------------*/

/* Add a FRAME or IFRAME */
static BOOL Addframe(struct Document *doc,struct Tagattr *ta,BOOL iframe)
{  short align=-1,flalign=-1;
   long width,height;
   ULONG wtag=TAG_IGNORE,htag=TAG_IGNORE;
   long mwidth=-1,mheight=-1;
   BOOL scrolling=TRUE,resize=TRUE;
   long border=-1;
   UBYTE *src=NULL,*name=NULL,*p;
   struct Url *url;
   void *elt;
   struct Number num;
   struct Frameref *fref;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_ALIGN:
            align=Getvalign(ATTR(doc,ta),STRICT);
            flalign=Getflalign(ATTR(doc,ta));
            break;
         case TAGATTR_SRC:
            src=ATTR(doc,ta);
            break;
         case TAGATTR_WIDTH:
            width=Getnumber(&num,ATTR(doc,ta));
            wtag=AOFRM_Width;
            if(width<=0) width=100;
            else if(num.type!=NUMBER_PERCENT)
            {  wtag=AOFRM_Pixelwidth;
            }
            break;
         case TAGATTR_HEIGHT:
            height=Getnumber(&num,ATTR(doc,ta));
            htag=AOFRM_Height;
            if(height<=0) height=100;
            else if(num.type!=NUMBER_PERCENT)
            {  htag=AOFRM_Pixelheight;
            }
            break;
         case TAGATTR_MARGINWIDTH:
            mwidth=Getnumber(&num,ATTR(doc,ta));
            break;
         case TAGATTR_MARGINHEIGHT:
            mheight=Getnumber(&num,ATTR(doc,ta));
            break;
         case TAGATTR_SCROLLING:
            scrolling=Getbool(ATTR(doc,ta),scrolling);
            break;
         case TAGATTR_FRAMEBORDER:
         case TAGATTR_BORDER:
            p=ATTR(doc,ta);
            if(toupper(*p)=='N') border=0;
            else if(isdigit(*p)) border=Getnumber(&num,p);
            else border=2;
            break;
         case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_NORESIZE:
            resize=FALSE;
            break;
      }
   }
   if(!iframe)
   {  width=height=100;
      wtag=AOFRM_Width;
      htag=AOFRM_Height;
   }
   if(!src) src="x-nil:";
   if(!(url=Findurl(doc->base,src,0))) return FALSE;
   if(!(elt=Anewobject(AOTP_FRAME,
      AOBJ_Pool,doc->pool,
      CONDTAG(AOELT_Valign,align),
      CONDTAG(AOELT_Floating,flalign),
      AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
      AOFRM_Url,url,
      AOFRM_Seqnr,++doc->frameseqnr,
      AOBJ_Winhis,Agetattr(doc->frame,AOBJ_Winhis),
      wtag,width,
      htag,height,
      CONDTAG(AOFRM_Marginwidth,mwidth),
      CONDTAG(AOFRM_Marginheight,mheight),
      AOFRM_Scrolling,scrolling,
      AOFRM_Resize,resize,
      CONDTAG(AOFRM_Border,border),
      AOFRM_Name,name,
      AOFRM_Inline,iframe,
      AOFRM_Reloadverify,BOOLVAL(doc->pflags&DPF_RELOADVERIFY),
      AOBJ_Nobackground,BOOLVAL(doc->dflags&DDF_NOBACKGROUND),
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   if(!Ensurenosp(doc)) return FALSE;
   if(!(fref=ALLOCSTRUCT(Frameref,1,MEMF_CLEAR))) return FALSE;
   fref->frame=elt;
   ADDTAIL(&doc->frames,fref);
   return TRUE;
}

/*** <FRAME> ***/
static BOOL Doframe(struct Document *doc,struct Tagattr *ta)
{  BOOL result=TRUE;
   if(prefs.browser.doframes)
   {  if(doc->doctype==DOCTP_FRAMESET && !(doc->pflags&DPF_FRAMESETEND))
      {  result=Addframe(doc,ta,FALSE);
      }
/*
      else if(!STRICT)
      {  result=Addframe(doc,ta,TRUE);
      }
*/
   }
   return result;
}

/*** <IFRAME> ***/
static BOOL Doiframe(struct Document *doc,struct Tagattr *ta)
{  BOOL result=TRUE;
   if(prefs.browser.doframes)
   {  if(!Ensurebody(doc)) return FALSE;
      result=Addframe(doc,ta,TRUE);
      doc->pmode=DPM_IFRAME;
   }
   return result;
}

/*** <FRAMESET> ***/
static BOOL Doframeset(struct Document *doc,struct Tagattr *ta)
{  UBYTE *rows=NULL,*cols=NULL,*p;
   long border=-1;
   long spacing=-1;
   struct Number num;
   void *frameset;
   if(prefs.browser.doframes)
   {  for(;ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_COLS:
               cols=ATTR(doc,ta);
               break;
            case TAGATTR_ROWS:
               rows=ATTR(doc,ta);
               break;
            case TAGATTR_FRAMEBORDER:
            case TAGATTR_BORDER:
               p=ATTR(doc,ta);
               if(toupper(*p)=='N') border=0;
               else if(isdigit(*p)) border=Getnumber(&num,p);
               else border=2;
               break;
            case TAGATTR_FRAMESPACING:
               spacing=Getnumber(&num,ATTR(doc,ta));
               break;
         }
      }
      if(doc->body && doc->doctype!=DOCTP_FRAMESET)
      {  Adisposeobject(doc->body);
         doc->body=NULL;
         doc->doctype=DOCTP_NONE;
      }
      if(doc->doctype!=DOCTP_BODY && !(doc->pflags&DPF_FRAMESETEND))
      {  if(!(frameset=Anewobject(AOTP_FRAMESET,
            AOBJ_Pool,doc->pool,
            AOFRS_Cols,cols,
            AOFRS_Rows,rows,
            CONDTAG(AOFRM_Border,border),
            CONDTAG(AOFRS_Spacing,spacing),
            AOBJ_Frame,doc->frame,
            AOBJ_Window,doc->win,
            AOBJ_Nobackground,BOOLVAL(doc->dflags&DDF_NOBACKGROUND),
            TAG_END))) return FALSE;
         if(Agetattr(frameset,AOFRS_Sensible))
         {  if(doc->doctype==DOCTP_NONE)
            {  doc->doctype=DOCTP_FRAMESET;
               doc->pmode=DPM_FRAMESET;
               doc->body=frameset;
            }
            else if(doc->framesets.first->next)
            {  Aaddchild(doc->framesets.first->frameset,frameset,0);
            }
            else
            {  Adisposeobject(frameset);
               return FALSE;
            }
         }
         else
         {  Adisposeobject(frameset);
            return FALSE;
         }
         Pushframeset(doc,frameset);
      }
   }
   return TRUE;
}

/*** </FRAMESET> ***/
static BOOL Doframesetend(struct Document *doc)
{  if(prefs.browser.doframes)
   {  if(doc->doctype==DOCTP_FRAMESET)
      {  Popframeset(doc);
      }
   }
   return TRUE;
}

/*--- images ----------------------------------------------------------*/

/*** <AREA> ***/
static BOOL Doarea(struct Document *doc,struct Tagattr *ta,BOOL validarea)
{  USHORT shape=AREASHAPE_RECTANGLE;
   UBYTE *href=NULL;
   UBYTE *coords=NULL;
   struct Url *url=NULL;
   UBYTE *fragment=NULL;
   UBYTE *target=doc->target;
   long alt=-1,altlen=-1;
   UBYTE *onclick=NULL,*onmouseover=NULL,*onmouseout=NULL;
   void *area;
   BOOL targetset=FALSE;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_SHAPE:
            if(STRNIEQUAL(ATTR(doc,ta),"RECT",4)) shape=AREASHAPE_RECTANGLE;
            else if(STRNIEQUAL(ATTR(doc,ta),"CIRC",4)) shape=AREASHAPE_CIRCLE;
            else if(STRNIEQUAL(ATTR(doc,ta),"POLY",4)) shape=AREASHAPE_POLYGON;
            else if(STRIEQUAL(ATTR(doc,ta),"DEFAULT")) shape=AREASHAPE_DEFAULT;
            validarea=TRUE;
            break;
         case TAGATTR_COORDS:
            coords=ATTR(doc,ta);
            validarea=TRUE;
            break;
         case TAGATTR_HREF:
            href=ATTR(doc,ta);
            break;
         case TAGATTR_NOHREF:
            href=NULL;
            break;
         case TAGATTR_TARGET:
            target=ATTR(doc,ta);
            targetset=TRUE;
            break;
         case TAGATTR_ALT:
            if(ta->length)
            {  alt=doc->text.length;
               altlen=ta->length;
               if(!(Addtotextbuf(doc,ATTR(doc,ta),ta->length))) return FALSE;
            }
            break;
         case TAGATTR_ONCLICK:
            onclick=ATTR(doc,ta);
            break;
         case TAGATTR_ONMOUSEOVER:
            onmouseover=ATTR(doc,ta);
            break;
         case TAGATTR_ONMOUSEOUT:
            onmouseout=ATTR(doc,ta);
            break;
      }
   }
   if(doc->maps.last->prev && validarea)
   {  if(href)
      {  fragment=Fragmentpart(href);
         if(!(url=Findurl(doc->base,href,0))) return FALSE;
         if(!targetset && Mailnewsurl(href))
         {  target="_top";
         }
      }
      if(!(area=Anewobject(AOTP_AREA,
         AOBJ_Pool,doc->pool,
         AOBJ_Frame,doc->frame,
         AOBJ_Cframe,doc->frame,
         AOBJ_Window,doc->win,
         AOLNK_Url,url,
         AOLNK_Fragment,fragment,
         AOLNK_Target,target,
         AOLNK_Text,&doc->text,
         AOLNK_Onclick,onclick,
         AOLNK_Onmouseover,onmouseover,
         AOLNK_Onmouseout,onmouseout,
         AOARA_Shape,shape,
         AOARA_Coords,coords,
         CONDTAG(AOARA_Textpos,alt),
         CONDTAG(AOARA_Textlength,altlen),
         TAG_END))) return FALSE;
      ADDTAIL(&doc->links,area);
      Asetattrs(doc->maps.last,
         AOMAP_Area,area,
         TAG_END);
   }
   return TRUE;
}

/*** <IMG> ***/
static BOOL Doimg(struct Document *doc,struct Tagattr *ta)
{  short border=-1,width=-1,height=-1,hspace=-1,vspace=-1;
   short align=-1,flalign=-1;
   ULONG wtag=TAG_IGNORE,htag=TAG_IGNORE;
   long alt=-1,altlen=-1;
   void *usemap=NULL;
   struct Number num;
   UBYTE *src=NULL,*mapname,*name=NULL;
   UBYTE *onload=NULL,*onerror=NULL,*onabort=NULL,*onclick=NULL;
   UBYTE *onmouseover=NULL,*onmouseout=NULL;
   UBYTE *styleAttr=NULL;
   BOOL ismap=FALSE,wasspace=FALSE;
   void *elt,*url,*referer,*jform=NULL;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_SRC:
            src=ATTR(doc,ta);
            break;
         case TAGATTR_ALIGN:
            align=Getvalign(ATTR(doc,ta),STRICT);
            flalign=Getflalign(ATTR(doc,ta));
            break;
         case TAGATTR_ALT:
            if(ta->length)
            {  wasspace=(doc->text.buffer[doc->text.length-1]==' ');
               alt=doc->text.length;
               altlen=ta->length;
               if(!(Addtotextbuf(doc,ATTR(doc,ta),ta->length))) return FALSE;
            }
            break;
         case TAGATTR_ISMAP:
            ismap=TRUE;
            break;
         case TAGATTR_USEMAP:
            mapname=ATTR(doc,ta);
            if(mapname[0]=='#')
            {  usemap=Findmap(doc,mapname+1);
            }
            else
            {  usemap=Externalmap(doc,mapname);
            }
            break;
         case TAGATTR_BORDER:
            border=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_WIDTH:
            width=Getnumber(&num,ATTR(doc,ta));
            if(width<=0) width=0;
            if(num.type==NUMBER_PERCENT)
            {  wtag=AOCPY_Percentwidth;
            }
            else
            {  wtag=AOCPY_Width;
            }
            break;
         case TAGATTR_HEIGHT:
            height=Getnumber(&num,ATTR(doc,ta));
            if(height<=0) height=0;
            if(num.type==NUMBER_PERCENT)
            {  htag=AOCPY_Percentheight;
            }
            else
            {  htag=AOCPY_Height;
            }
            break;
         case TAGATTR_HSPACE:
            hspace=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_VSPACE:
            vspace=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_ONLOAD:
            onload=ATTR(doc,ta);
            break;
         case TAGATTR_ONERROR:
            onerror=ATTR(doc,ta);
            break;
         case TAGATTR_ONABORT:
            onabort=ATTR(doc,ta);
            break;
         case TAGATTR_ONCLICK:
            onclick=ATTR(doc,ta);
            break;
         case TAGATTR_ONMOUSEOVER:
            onmouseover=ATTR(doc,ta);
            break;
         case TAGATTR_ONMOUSEOUT:
            onmouseout=ATTR(doc,ta);
            break;
      }
   }
   if(src)
   {  if(name && doc->pflags&DPF_FORM) jform=doc->forms.last;
      if(!(url=Findurl(doc->base,src,0))) return FALSE;
      referer=(void *)Agetattr(doc->source->source,AOSRC_Url);
      if(!Ensurebody(doc)) return FALSE;
      if(!(elt=Anewobject(AOTP_COPY,
         AOBJ_Pool,doc->pool,
         CONDTAG(AOELT_Valign,align),
         CONDTAG(AOELT_Floating,flalign),
         CONDTAG(AOELT_Textpos,alt),
         CONDTAG(AOELT_Textlength,altlen),
         AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
         AOCPY_Url,url,
         AOCPY_Embedded,TRUE,
         AOCPY_Usemap,usemap,
         AOCPY_Ismap,ismap,
         AOCPY_Text,&doc->text,
         AOCPY_Referer,referer,
         AOCPY_Defaulttype,"image/x-unknown",
         AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
         AOCPY_Trueimage,TRUE,
         AOCPY_Name,name,
         AOCPY_Onload,onload,
         AOCPY_Onerror,onerror,
         AOCPY_Onabort,onabort,
         AOCPY_Onclick,onclick,
         AOCPY_Onmouseover,onmouseover,
         AOCPY_Onmouseout,onmouseout,
         AOCPY_Jform,jform,
         CONDTAG(AOCPY_Border,border),
         wtag,width,
         htag,height,
         CONDTAG(AOCPY_Hspace,hspace),
         CONDTAG(AOCPY_Vspace,vspace),
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      /* Apply inline CSS if present */
      if(styleAttr)
      {  ApplyCSSToImage(doc,elt,styleAttr);
      }
      if(flalign<0)
      {  if(!Ensurenosp(doc)) return FALSE;
      }
      else if(wasspace)
      {  if(!Ensuresp(doc)) return FALSE;
      }
   }
   return TRUE;
}

/*** &dingbat; ***/
static BOOL Doicon(struct Document *doc,struct Tagattr *ta)
{  UBYTE name[sizeof(DINGBATPATH)+40]="";
   UBYTE *src=NULL;
   struct Url *url;
   void *elt;
   long alt,altlen;
   /* No loop through ta - we *know* that it is the source. Its valuepos
    * points to the name as a string, not as offset in args buffer. */
   src=(UBYTE *)ta->valuepos;
   alt=doc->text.length;
   altlen=strlen(src);
   if(!Addtotextbuf(doc,src,strlen(src))) return FALSE;
   strcpy(name,DINGBATPATH);
   strncat(name,src,32);
   if(!(url=Findurl("",name,0))) return FALSE;
   if(!Ensurebody(doc)) return FALSE;
   if(!(elt=Anewobject(AOTP_COPY,
      AOBJ_Pool,doc->pool,
      AOELT_Textpos,alt,
      AOELT_Textlength,altlen,
      AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
      AOCPY_Url,url,
      AOCPY_Embedded,TRUE,
      AOCPY_Text,&doc->text,
      AOCPY_Defaulttype,"image/x-unknown",
      AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   Asetattrs(elt,AOBJ_Layoutparent,doc->body,TAG_END);
   return TRUE;
}

/*** <MAP> ***/
static BOOL Domap(struct Document *doc,struct Tagattr *ta)
{  void *map;
   UBYTE *name=NULL;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
      }
   }
   if(name)
   {  if(!(map=Findmap(doc,name))) return FALSE;
      /* make sure this map is at the end of the list for Doarea() */
      REMOVE(map);
      ADDTAIL(&doc->maps,map);
      doc->pmode=DPM_MAP;
   }
   return TRUE;
}

/*--- lists -----------------------------------------------------------*/

/*** <DD> ***/
static BOOL Dodd(struct Document *doc,struct Tagattr *ta)
{  UBYTE *classAttr;
   UBYTE *styleAttr;
   void *body;
   
   Wantbreak(doc,1);
   body = Docbody(doc);
   
   /* Extract class and style attributes */
   /* Use exact same pattern as Dopara */
   classAttr = NULL;
   styleAttr = NULL;
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_CLASS:
            classAttr = ATTR(doc,ta);
            break;
         case TAGATTR_STYLE:
            styleAttr = ATTR(doc,ta);
            break;
      }
   }
   
   /* Apply CSS to dd element */
   if(body)
   {  if(doc->cssstylesheet) ApplyCSSToBody(doc,body,classAttr,NULL,"DD");
      /* Also apply inline CSS if present */
      if(styleAttr)
      {  ApplyInlineCSSToBody(doc,body,styleAttr,"DD");
      }
   }
   
   Asetattrs(body,
      AOBDY_Align,-1,
      AOBDY_Dterm,FALSE,
      TAG_END);
   Checkid(doc,ta);
   return TRUE;
}

/*** <DL> ***/
static BOOL Dodl(struct Document *doc,struct Tagattr *ta)
{  struct Listinfo li={0},*nestli;
   UBYTE *classAttr;
   UBYTE *styleAttr;
   void *body;
   
   li.type=BDLT_DL;
   if(!Ensurebody(doc)) return FALSE;
   body = Docbody(doc);
   
   /* Extract class and style attributes */
   /* Use exact same pattern as Dopara */
   classAttr = NULL;
   styleAttr = NULL;
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_CLASS:
            classAttr = ATTR(doc,ta);
            break;
         case TAGATTR_STYLE:
            styleAttr = ATTR(doc,ta);
            break;
      }
   }
   
   /* Apply CSS to dl element */
   if(body)
   {  if(doc->cssstylesheet) ApplyCSSToBody(doc,body,classAttr,NULL,"DL");
   }
   
   /* Add extra space if it is the outer list */
   nestli=(struct Listinfo *)Agetattr(body,AOBDY_List);
   Wantbreak(doc,nestli->next?1:2);
   Asetattrs(body,
      AOBDY_Align,-1,
      AOBDY_List,&li,
      TAG_END);
   Checkid(doc,ta);
   return TRUE;
}

/*** </DL> ***/
static BOOL Dodlend(struct Document *doc)
{  struct Listinfo *nestli;
   Asetattrs(Docbody(doc),
      AOBDY_Align,-1,
      AOBDY_List,NULL,
      TAG_END);
   /* Add extra space if it is the outer list */
   nestli=(struct Listinfo *)Agetattr(Docbody(doc),AOBDY_List);
   doc->wantbreak=nestli->next?1:2;
   return TRUE;
}

/*** <DT> ***/
static BOOL Dodt(struct Document *doc,struct Tagattr *ta)
{  UBYTE *classAttr=NULL;
   void *body;
   struct Tagattr *sentinel;
   Wantbreak(doc,1);
   body = Docbody(doc);
   
   /* Save ta for Checkid */
   sentinel = ta;
   
   /* Extract class attribute from DT element */
   /* Use exact same pattern as Dopara */
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_CLASS:
            classAttr = ATTR(doc,ta);
            break;
      }
   }
   
   /* Apply CSS to DT element based on class */
   if(classAttr)
   {  if(doc->cssstylesheet) ApplyCSSToBody(doc,body,classAttr,NULL,"DT");
   }
   
   Asetattrs(body,
      AOBDY_Align,-1,
      AOBDY_Dterm,TRUE,
      TAG_END);
   Checkid(doc,sentinel);
   return TRUE;
}

/*** <OL> ***/
static BOOL Dool(struct Document *doc,struct Tagattr *ta)
{  struct Listinfo li={0},*nestli;
   UBYTE *styleAttr;
   UBYTE *classAttr;
   UBYTE *idAttr;
   void *body;
   struct Tagattr *tap;
   styleAttr = NULL;
   classAttr = NULL;
   idAttr = NULL;
   tap = ta;  /* Save original ta pointer for Checkid */
   li.type=BDLT_OL;
   /* Clear previous list class */
   if(doc->currentlistclass) FREE(doc->currentlistclass);
   doc->currentlistclass = NULL;
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            classAttr=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            idAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CONTINUE:
            break;
         case TAGATTR_SEQNUM:
            if(!STRICT) li.bulletnr=Getposnumber(ATTR(doc,ta))-1;
            break;
         case TAGATTR_START:
            li.bulletnr=Getposnumber(ATTR(doc,ta))-1;
            if(li.bulletnr<-1) li.bulletnr=-1;
            break;
         case TAGATTR_TYPE:
            switch(*ATTR(doc,ta))
            {  case 'A':   li.bullettype=BDBT_ALPHA;break;
               case 'a':   li.bullettype=BDBT_ALPHALOW;break;
               case 'I':   li.bullettype=BDBT_ROMAN;break;
               case 'i':   li.bullettype=BDBT_ROMANLOW;break;
            }
            break;
      }
   }
   /* Apply CSS list-style-type if present */
   if(styleAttr)
   {  UBYTE *p;
      UBYTE *nameStart;
      UBYTE *nameEnd;
      UBYTE *valueStart;
      UBYTE *valueEnd;
      long nameLen;
      long valueLen;
      p = styleAttr;
      while(*p)
      {  /* Skip whitespace */
         while(*p && isspace(*p)) p++;
         if(!*p) break;
         nameStart = p;
         /* Find colon */
         while(*p && *p != ':') p++;
         if(!*p) break;
         nameEnd = p;
         p++; /* Skip colon */
         /* Skip whitespace after colon */
         while(*p && isspace(*p)) p++;
         valueStart = p;
         /* Find semicolon or end */
         while(*p && *p != ';') p++;
         valueEnd = p;
         if(*p == ';') p++; /* Skip semicolon */
         /* Trim value end */
         while(valueEnd > valueStart && isspace(valueEnd[-1])) valueEnd--;
         /* Calculate lengths */
         nameLen = nameEnd - nameStart;
         valueLen = valueEnd - valueStart;
         /* Check if this is list-style-type */
         if(nameLen == 15 && Strnicmp((char *)nameStart,"list-style-type",15) == 0)
         {  if(valueLen == 7 && Strnicmp((char *)valueStart,"decimal",7) == 0)
            {  li.bullettype = BDBT_NUMBER;
            }
            else if(valueLen == 11 && Strnicmp((char *)valueStart,"lower-alpha",11) == 0)
            {  li.bullettype = BDBT_ALPHALOW;
            }
            else if(valueLen == 11 && Strnicmp((char *)valueStart,"lower-latin",11) == 0)
            {  li.bullettype = BDBT_ALPHALOW;
            }
            else if(valueLen == 11 && Strnicmp((char *)valueStart,"upper-alpha",11) == 0)
            {  li.bullettype = BDBT_ALPHA;
            }
            else if(valueLen == 11 && Strnicmp((char *)valueStart,"upper-latin",11) == 0)
            {  li.bullettype = BDBT_ALPHA;
            }
            else if(valueLen == 11 && Strnicmp((char *)valueStart,"lower-roman",11) == 0)
            {  li.bullettype = BDBT_ROMANLOW;
            }
            else if(valueLen == 11 && Strnicmp((char *)valueStart,"upper-roman",11) == 0)
            {  li.bullettype = BDBT_ROMAN;
            }
            else if(valueLen == 4 && Strnicmp((char *)valueStart,"none",4) == 0)
            {  li.bullettype = BDBT_PLAIN;
            }
         }
      }
   }
   /* Store list class for descendant selector support */
   if(classAttr)
   {  doc->currentlistclass = Dupstrp(classAttr,-1,doc->pool);
   }
   /* Check CSS stylesheet for list-style-type and display:inline */
   if(doc->cssstylesheet && classAttr)
   {  struct CSSRule *rule;
      struct CSSSelector *sel;
      struct CSSProperty *prop;
      struct CSSStylesheet *sheet;
      BOOL hasDisplayInline = FALSE;
      
      sheet = (struct CSSStylesheet *)doc->cssstylesheet;
      for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
          (struct MinNode *)rule->node.mln_Succ;
          rule = (struct CSSRule *)rule->node.mln_Succ)
      {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
            (struct MinNode *)sel->node.mln_Succ;
            sel = (struct CSSSelector *)sel->node.mln_Succ)
         {  /* Check if this selector matches our list's class */
            BOOL matches = FALSE;
            if((sel->type & CSS_SEL_CLASS) && sel->class)
            {  if(MatchClassAttribute(classAttr, sel->class))
               {  /* Check if this selector matches 'ol' or 'ul' element */
                  if(!(sel->type & CSS_SEL_ELEMENT) || !sel->name ||
                     Stricmp((char *)sel->name,"ol") == 0 || Stricmp((char *)sel->name,"ul") == 0)
                  {  matches = TRUE;
                  }
               }
            }
            
            if(matches)
            {  /* Check for list-style-type and display properties */
               for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
                   (struct MinNode *)prop->node.mln_Succ;
                   prop = (struct CSSProperty *)prop->node.mln_Succ)
               {  if(prop->name && prop->value)
                  {  /* Check for list-style-type */
                     if(Stricmp((char *)prop->name,"list-style-type") == 0)
                     {  if(Stricmp((char *)prop->value,"decimal") == 0)
                        {  li.bullettype = BDBT_NUMBER;
                        }
                        else if(Stricmp((char *)prop->value,"lower-alpha") == 0 || 
                                Stricmp((char *)prop->value,"lower-latin") == 0)
                        {  li.bullettype = BDBT_ALPHALOW;
                        }
                        else if(Stricmp((char *)prop->value,"upper-alpha") == 0 || 
                                Stricmp((char *)prop->value,"upper-latin") == 0)
                        {  li.bullettype = BDBT_ALPHA;
                        }
                        else if(Stricmp((char *)prop->value,"lower-roman") == 0)
                        {  li.bullettype = BDBT_ROMANLOW;
                        }
                        else if(Stricmp((char *)prop->value,"upper-roman") == 0)
                        {  li.bullettype = BDBT_ROMAN;
                        }
                        else if(Stricmp((char *)prop->value,"none") == 0)
                        {  li.bullettype = BDBT_PLAIN;
                        }
                        else if(Stricmp((char *)prop->value,"disc") == 0)
                        {  li.bullettype = BDBT_DISC;
                        }
                        else if(Stricmp((char *)prop->value,"circle") == 0)
                        {  li.bullettype = BDBT_CIRCLE;
                        }
                        else if(Stricmp((char *)prop->value,"square") == 0)
                        {  li.bullettype = BDBT_SQUARE;
                        }
                     }
                     /* Check for display:inline */
                     else if(Stricmp((char *)prop->name,"display") == 0 &&
                             Stricmp((char *)prop->value,"inline") == 0)
                     {  hasDisplayInline = TRUE;
                     }
                  }
               }
            }
            
            /* Also check for .classname li with display:inline */
            if((sel->type & CSS_SEL_CLASS) && sel->class && 
               MatchClassAttribute(classAttr, sel->class))
            {  if(sel->type & CSS_SEL_ELEMENT && sel->name && 
                  Stricmp((char *)sel->name,"li") == 0)
               {  for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
                      (struct MinNode *)prop->node.mln_Succ;
                      prop = (struct CSSProperty *)prop->node.mln_Succ)
                  {  if(prop->name && prop->value && 
                        Stricmp((char *)prop->name,"display") == 0 &&
                        Stricmp((char *)prop->value,"inline") == 0)
                     {  hasDisplayInline = TRUE;
                        break;
                     }
                  }
               }
            }
         }
      }
      if(hasDisplayInline)
      {  li.horizontal = TRUE;
         /* Also set list-style-type: none for horizontal lists */
         li.bullettype = BDBT_PLAIN;
      }
   }
   if(!Ensurebody(doc)) return FALSE;
   body = Docbody(doc);
   /* Store class/id/tagname on body for CSS matching */
   if(classAttr) Asetattrs(body,AOBDY_Class,Dupstrp(classAttr,-1,doc->pool),TAG_END);
   if(idAttr) Asetattrs(body,AOBDY_Id,Dupstrp(idAttr,-1,doc->pool),TAG_END);
   Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"OL",-1,doc->pool),TAG_END);
   /* Apply CSS to OL element based on its class/id/tagname */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,classAttr,idAttr,"OL");
   
   /* Check if list-style-image was set via CSS and apply to Listinfo */
   {  UBYTE *listStyleImage;
      listStyleImage = (UBYTE *)Agetattr(body, AOBDY_ListStyleImage);
      if(listStyleImage)
      {  li.bulletsrc = Dupstr(listStyleImage, -1);
         li.bullettype = BDBT_IMAGE;
      }
   }
   
   /* Add extra space if it is the outer list */
   nestli=(struct Listinfo *)Agetattr(body,AOBDY_List);
   Wantbreak(doc,nestli->next?1:2);
   Asetattrs(body,
      AOBDY_Align,-1,
      AOBDY_List,&li,
      TAG_END);
   Checkid(doc,tap);  /* Use original ta pointer */
   return TRUE;
}

/*** </OL> ***/
static BOOL Doolend(struct Document *doc)
{  struct Listinfo *nestli;
   /* Clear list class when list ends */
   if(doc->currentlistclass) FREE(doc->currentlistclass);
   doc->currentlistclass = NULL;
   Asetattrs(Docbody(doc),
      AOBDY_Align,-1,
      AOBDY_List,NULL,
      TAG_END);
   doc->gotbreak=0;
   /* Add extra space if it is the outer list */
   nestli=(struct Listinfo *)Agetattr(Docbody(doc),AOBDY_List);
   doc->wantbreak=nestli->next?1:2;
   return TRUE;
}

/*** <UL> ***/
static BOOL Doul(struct Document *doc,struct Tagattr *ta)
{  UBYTE dingbat[sizeof(DINGBATPATH)+40]="";
   struct Listinfo li={0},*nestli;
   UBYTE *styleAttr;
   UBYTE *classAttr;
   UBYTE *idAttr;
   void *body;
   struct Tagattr *tap;
   styleAttr = NULL;
   classAttr = NULL;
   idAttr = NULL;
   tap = ta;  /* Save original ta pointer for Checkid */
   li.type=BDLT_UL;
   /* Clear previous list class */
   if(doc->currentlistclass) FREE(doc->currentlistclass);
   doc->currentlistclass = NULL;
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            classAttr=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            idAttr=ATTR(doc,ta);
            break;
         case TAGATTR_TYPE:
            if(STRNIEQUAL(ATTR(doc,ta),"DISC",4)) li.bullettype=BDBT_DISC;
            else if(STRNIEQUAL(ATTR(doc,ta),"CIRC",4)) li.bullettype=BDBT_CIRCLE;
            else if(STRNIEQUAL(ATTR(doc,ta),"SQUA",4)) li.bullettype=BDBT_SQUARE;
            break;
         case TAGATTR_SRC:
            li.bulletsrc=ATTR(doc,ta);
            li.bullettype=BDBT_IMAGE;
            break;
         case TAGATTR_DINGBAT:
            strcpy(dingbat,DINGBATPATH);
            strncat(dingbat,ATTR(doc,ta),32);
            li.bulletsrc=dingbat;
            li.bullettype=BDBT_IMAGE;
         break;
      }
   }
   /* Apply CSS list-style-type if present */
   if(styleAttr)
   {  UBYTE *p;
      UBYTE *nameStart;
      UBYTE *nameEnd;
      UBYTE *valueStart;
      UBYTE *valueEnd;
      long nameLen;
      long valueLen;
      p = styleAttr;
      while(*p)
      {  /* Skip whitespace */
         while(*p && isspace(*p)) p++;
         if(!*p) break;
         nameStart = p;
         /* Find colon */
         while(*p && *p != ':') p++;
         if(!*p) break;
         nameEnd = p;
         p++; /* Skip colon */
         /* Skip whitespace after colon */
         while(*p && isspace(*p)) p++;
         valueStart = p;
         /* Find semicolon or end */
         while(*p && *p != ';') p++;
         valueEnd = p;
         if(*p == ';') p++; /* Skip semicolon */
         /* Trim value end */
         while(valueEnd > valueStart && isspace(valueEnd[-1])) valueEnd--;
         /* Calculate lengths */
         nameLen = nameEnd - nameStart;
         valueLen = valueEnd - valueStart;
         /* Check if this is list-style-type */
         if(nameLen == 15 && Strnicmp((char *)nameStart,"list-style-type",15) == 0)
         {  if(valueLen == 4 && Strnicmp((char *)valueStart,"disc",4) == 0)
            {  li.bullettype = BDBT_DISC;
            }
            else if(valueLen == 6 && Strnicmp((char *)valueStart,"circle",6) == 0)
            {  li.bullettype = BDBT_CIRCLE;
            }
            else if(valueLen == 6 && Strnicmp((char *)valueStart,"square",6) == 0)
            {  li.bullettype = BDBT_SQUARE;
            }
            else if(valueLen == 4 && Strnicmp((char *)valueStart,"none",4) == 0)
            {  li.bullettype = BDBT_PLAIN;
            }
         }
      }
   }
   /* Store list class for descendant selector support */
   if(classAttr)
   {  doc->currentlistclass = Dupstrp(classAttr,-1,doc->pool);
   }
   /* Check CSS stylesheet for list-style-type and display:inline */
   if(doc->cssstylesheet && classAttr)
   {  struct CSSRule *rule;
      struct CSSSelector *sel;
      struct CSSProperty *prop;
      struct CSSStylesheet *sheet;
      BOOL hasDisplayInline = FALSE;
      
      sheet = (struct CSSStylesheet *)doc->cssstylesheet;
      for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
          (struct MinNode *)rule->node.mln_Succ;
          rule = (struct CSSRule *)rule->node.mln_Succ)
      {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
            (struct MinNode *)sel->node.mln_Succ;
            sel = (struct CSSSelector *)sel->node.mln_Succ)
         {  /* Check if this selector matches our list's class */
            BOOL matches = FALSE;
            if((sel->type & CSS_SEL_CLASS) && sel->class)
            {  if(MatchClassAttribute(classAttr, sel->class))
               {  /* Check if this selector matches 'ol' or 'ul' element */
                  if(!(sel->type & CSS_SEL_ELEMENT) || !sel->name ||
                     Stricmp((char *)sel->name,"ol") == 0 || Stricmp((char *)sel->name,"ul") == 0)
                  {  matches = TRUE;
                  }
               }
            }
            
            if(matches)
            {  /* Check for list-style-type and display properties */
               for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
                   (struct MinNode *)prop->node.mln_Succ;
                   prop = (struct CSSProperty *)prop->node.mln_Succ)
               {  if(prop->name && prop->value)
                  {  /* Check for list-style-type */
                     if(Stricmp((char *)prop->name,"list-style-type") == 0)
                     {  if(Stricmp((char *)prop->value,"disc") == 0)
                        {  li.bullettype = BDBT_DISC;
                        }
                        else if(Stricmp((char *)prop->value,"circle") == 0)
                        {  li.bullettype = BDBT_CIRCLE;
                        }
                        else if(Stricmp((char *)prop->value,"square") == 0)
                        {  li.bullettype = BDBT_SQUARE;
                        }
                        else if(Stricmp((char *)prop->value,"none") == 0)
                        {  li.bullettype = BDBT_PLAIN;
                        }
                     }
                     /* Check for display:inline */
                     else if(Stricmp((char *)prop->name,"display") == 0 &&
                             Stricmp((char *)prop->value,"inline") == 0)
                     {  hasDisplayInline = TRUE;
                     }
                  }
               }
            }
            
            /* Also check for .classname li with display:inline */
            if((sel->type & CSS_SEL_CLASS) && sel->class && 
               MatchClassAttribute(classAttr, sel->class))
            {  if(sel->type & CSS_SEL_ELEMENT && sel->name && 
                  Stricmp((char *)sel->name,"li") == 0)
               {  for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
                      (struct MinNode *)prop->node.mln_Succ;
                      prop = (struct CSSProperty *)prop->node.mln_Succ)
                  {  if(prop->name && prop->value && 
                        Stricmp((char *)prop->name,"display") == 0 &&
                        Stricmp((char *)prop->value,"inline") == 0)
                     {  hasDisplayInline = TRUE;
                        break;
                     }
                  }
               }
            }
         }
      }
      if(hasDisplayInline)
      {  li.horizontal = TRUE;
         /* Also set list-style-type: none for horizontal lists */
         li.bullettype = BDBT_PLAIN;
      }
   }
   if(!Ensurebody(doc)) return FALSE;
   body = Docbody(doc);
   /* Store class/id/tagname on body for CSS matching */
   /* Note: UL, DIR, and MENU all use this handler - using "UL" as tag name for CSS */
   if(classAttr) Asetattrs(body,AOBDY_Class,Dupstrp(classAttr,-1,doc->pool),TAG_END);
   if(idAttr) Asetattrs(body,AOBDY_Id,Dupstrp(idAttr,-1,doc->pool),TAG_END);
   Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"UL",-1,doc->pool),TAG_END);
   /* Apply CSS to UL element based on its class/id/tagname */
   if(doc->cssstylesheet) ApplyCSSToBody(doc,body,classAttr,idAttr,"UL");
   
   /* Check if list-style-image was set via CSS and apply to Listinfo */
   {  UBYTE *listStyleImage;
      listStyleImage = (UBYTE *)Agetattr(body, AOBDY_ListStyleImage);
      if(listStyleImage)
      {  li.bulletsrc = Dupstr(listStyleImage, -1);
         li.bullettype = BDBT_IMAGE;
      }
   }
   
   /* Add extra space if it is the outer list */
   nestli=(struct Listinfo *)Agetattr(body,AOBDY_List);
   Wantbreak(doc,nestli->next?1:2);
   Asetattrs(body,
      AOBDY_Align,-1,
      AOBDY_List,&li,
      TAG_END);
   Checkid(doc,tap);  /* Use original ta pointer */
   return TRUE;
}

/*** </UL> ***/
static BOOL Doulend(struct Document *doc)
{  struct Listinfo *nestli;
   /* Clear list class when list ends */
   if(doc->currentlistclass) FREE(doc->currentlistclass);
   doc->currentlistclass = NULL;
   Asetattrs(Docbody(doc),
      AOBDY_Align,-1,
      AOBDY_List,NULL,
      TAG_END);
   doc->gotbreak=0;
   /* Add extra space if it is the outer list */
   nestli=(struct Listinfo *)Agetattr(Docbody(doc),AOBDY_List);
   doc->wantbreak=nestli->next?1:2;
   return TRUE;
}

/*** <LI> ***/
static BOOL Doli(struct Document *doc,struct Tagattr *ta)
{  UBYTE dingbat[sizeof(DINGBATPATH)+40]="";
   struct Listinfo *li;
   UBYTE buf[32];
   long length;
   UBYTE *src=NULL;
   void *elt=NULL,*url,*referer;
   short btype;
   UBYTE *classAttr=NULL;
   void *body;
   struct Tagattr *attr;
   if(doc->pflags&DPF_BULLET) doc->gotbreak=0;
   Asetattrs(Docbody(doc),AOBDY_Align,-1,TAG_END);
   li=(struct Listinfo *)Agetattr(Docbody(doc),AOBDY_List);
   if(!li || !li->type)
   {  if(!Doul(doc,&dummyta)) return FALSE;
      li=(struct Listinfo *)Agetattr(Docbody(doc),AOBDY_List);
   }
   
   /* Extract class and id attributes from LI element */
   /* Use exact same pattern as Dopara */
   {  UBYTE *idAttr = NULL;
      for(;ta && ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_CLASS:
               classAttr = ATTR(doc,ta);
               break;
            case TAGATTR_ID:
               idAttr = ATTR(doc,ta);
               break;
         }
      }
      body = Docbody(doc);
      /* Store tagname on body for CSS matching (needed for element selectors like "li") */
      Asetattrs(body,AOBDY_TagName,Dupstrp((UBYTE *)"LI",-1,doc->pool),TAG_END);
      /* Store class/id on body for CSS matching */
      if(classAttr) Asetattrs(body,AOBDY_Class,Dupstrp(classAttr,-1,doc->pool),TAG_END);
      if(idAttr) Asetattrs(body,AOBDY_Id,Dupstrp(idAttr,-1,doc->pool),TAG_END);
      /* Apply CSS to LI element based on its own class/id/tagname */
      if(doc->cssstylesheet) ApplyCSSToBody(doc,body,classAttr,idAttr,"LI");
   }
   
   /* Apply CSS rules for .classname li to this list item (generic, not hardcoded to "menubar") */
   if(doc->cssstylesheet && doc->currentlistclass)
   {  struct CSSRule *rule;
      struct CSSSelector *sel;
      struct CSSProperty *prop;
      struct CSSStylesheet *sheet;
      void *body;
      
      body = Docbody(doc);
      sheet = (struct CSSStylesheet *)doc->cssstylesheet;
      for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
          (struct MinNode *)rule->node.mln_Succ;
          rule = (struct CSSRule *)rule->node.mln_Succ)
      {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
            (struct MinNode *)sel->node.mln_Succ;
            sel = (struct CSSSelector *)sel->node.mln_Succ)
         {  /* Check for .classname li selector (matches parent list's class) */
            if((sel->type & CSS_SEL_CLASS) && sel->class && 
               Stricmp((char *)sel->class,(char *)doc->currentlistclass) == 0 &&
               (sel->type & CSS_SEL_ELEMENT) && sel->name && 
               Stricmp((char *)sel->name,"li") == 0)
            {  /* Apply properties from .classname li rule */
               for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
                   (struct MinNode *)prop->node.mln_Succ;
                   prop = (struct CSSProperty *)prop->node.mln_Succ)
               {  if(prop->name && prop->value)
                  {  /* Apply margin-left and margin-right */
                     if(Stricmp((char *)prop->name,"margin-left") == 0)
                     {  /* Parse margin value (e.g., "1em" or "14px") */
                        long marginValue;
                        struct Number num;
                        UBYTE *valueStr;
                        long valueLen;
                        BOOL isEm;
                        
                        valueStr = prop->value;
                        valueLen = strlen((char *)valueStr);
                        /* Check if value ends with "em" */
                        isEm = (valueLen >= 2 && 
                                tolower(valueStr[valueLen-2]) == 'e' && 
                                tolower(valueStr[valueLen-1]) == 'm');
                        
                        if(Getnumber(&num,prop->value))
                        {  marginValue = num.n;
                           /* Convert "em" to pixels (approximate: 1em = 14px for 14px font) */
                           if(isEm && marginValue > 0)
                           {  marginValue = marginValue * 14; /* Approximate conversion */
                           }
                           if(marginValue > 0)
                           {  Asetattrs(body,AOBDY_Leftmargin,marginValue,TAG_END);
                              /* debug_printf("CSS: Applied margin-left=%ld to list item\n",marginValue); */
                           }
                        }
                     }
                     else if(Stricmp((char *)prop->name,"margin-right") == 0)
                     {  /* Parse margin value */
                        long marginValue;
                        struct Number num;
                        UBYTE *valueStr;
                        long valueLen;
                        BOOL isEm;
                        
                        valueStr = prop->value;
                        valueLen = strlen((char *)valueStr);
                        /* Check if value ends with "em" */
                        isEm = (valueLen >= 2 && 
                                tolower(valueStr[valueLen-2]) == 'e' && 
                                tolower(valueStr[valueLen-1]) == 'm');
                        
                        if(Getnumber(&num,prop->value))
                        {  marginValue = num.n;
                           /* Convert "em" to pixels */
                           if(isEm && marginValue > 0)
                           {  marginValue = marginValue * 14; /* Approximate conversion */
                           }
                           /* Note: AWeb doesn't have margin-right, use left margin as spacing */
                           if(marginValue > 0)
                           {  Asetattrs(body,AOBDY_Leftmargin,marginValue,TAG_END);
                              /* debug_printf("CSS: Applied margin-right=%ld (as left margin) to list item\n",marginValue); */
                           }
                        }
                     }
                  }
               }
            }
         }
      }
   }
   
   /* For horizontal lists, don't add line breaks between items */
   if(li && !li->horizontal)
   {  Wantbreak(doc,1);
   }
   if(li && li->type==BDLT_UL)
   {  /* For horizontal lists, always use BDBT_PLAIN (no bullets) - check FIRST */
      if(li->horizontal)
      {  btype = BDBT_PLAIN;
         li->bullettype = BDBT_PLAIN;
         src = NULL;
         /* debug_printf("CSS: List item is horizontal, skipping bullet (btype=BDBT_PLAIN)\n"); */
      }
      else
   {  btype=li->bullettype;
      src=li->bulletsrc;
      }
      for(;ta && ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_TYPE:
               /* Don't override bullet type for horizontal lists - always keep BDBT_PLAIN */
               if(!li->horizontal)
               {  if(STRNIEQUAL(ATTR(doc,ta),"DISC",4)) li->bullettype=BDBT_DISC;
               else if(STRNIEQUAL(ATTR(doc,ta),"CIRC",4)) li->bullettype=BDBT_CIRCLE;
               else if(STRNIEQUAL(ATTR(doc,ta),"SQUA",4)) li->bullettype=BDBT_SQUARE;
               btype=li->bullettype;
               }
               else
               {  /* For horizontal lists, ensure bullet type stays PLAIN */
                  li->bullettype = BDBT_PLAIN;
                  btype = BDBT_PLAIN;
               }
               break;
            case TAGATTR_SRC:
               src=ATTR(doc,ta);
               btype=BDBT_IMAGE;
               break;
            case TAGATTR_DINGBAT:
               strcpy(dingbat,DINGBATPATH);
               strncat(dingbat,ATTR(doc,ta),32);
               src=dingbat;
               btype=BDBT_IMAGE;
               break;
         }
      }
      /* Only create bullet if not horizontal and btype is not PLAIN */
      if(!li->horizontal && btype != BDBT_PLAIN)
      {  switch(btype)
      {  case BDBT_DISC:
         case BDBT_CIRCLE:
         case BDBT_SQUARE:
         case BDBT_DIAMOND:
         case BDBT_SOLIDDIA:
         case BDBT_RECTANGLE:
            if(!(elt=Anewobject(AOTP_BULLET,
               AOBJ_Pool,doc->pool,
               AOBUL_Type,btype,
               AOELT_Bullet,TRUE,
               AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
               TAG_END))) return FALSE;
            break;
         case BDBT_IMAGE:
            referer=(void *)Agetattr(doc->source->source,AOSRC_Url);
            if(!(url=Findurl(doc->base,src,0))) return FALSE;
            if(!(elt=Anewobject(AOTP_COPY,
               AOBJ_Pool,doc->pool,
               AOELT_Bullet,TRUE,
               AOCPY_Url,url,
               AOCPY_Embedded,TRUE,
               AOCPY_Text,&doc->text,
               AOCPY_Referer,referer,
               AOCPY_Defaulttype,"image/x-unknown",
               AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
               TAG_END))) return FALSE;
            break;
         }
      }
      if(elt)
      {  if(!Addelement(doc,elt)) return FALSE;
         Asetattrs(elt,AOBJ_Layoutparent,doc->body,TAG_END);
      }
      doc->gotbreak=2;
      doc->pflags|=DPF_BULLET;
   }
   else if(li && li->type==BDLT_OL)
   {  for(;ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_SKIP:
               if(!STRICT)
               {  short n;
                  sscanf(ATTR(doc,ta)," %hd",&n);
                  li->bulletnr+=n;
               }
               break;
            case TAGATTR_VALUE:
               if(sscanf(ATTR(doc,ta)," %ld",&li->bulletnr)) li->bulletnr--;
               break;
            case TAGATTR_TYPE:
               switch(*ATTR(doc,ta))
               {  case 'A':   li->bullettype=BDBT_ALPHA;break;
                  case 'a':   li->bullettype=BDBT_ALPHALOW;break;
                  case 'I':   li->bullettype=BDBT_ROMAN;break;
                  case 'i':   li->bullettype=BDBT_ROMANLOW;break;
               }
               break;
         }
      }
      switch(li->bullettype)
      {  case BDBT_ALPHA:
            length=sprintf(buf,"%c ",'A'+((li->bulletnr++)%26));
            break;
         case BDBT_ALPHALOW:
            length=sprintf(buf,"%c ",'a'+((li->bulletnr++)%26));
            break;
         case BDBT_ROMAN:
            length=Makeroman(buf,++li->bulletnr,FALSE);
            break;
         case BDBT_ROMANLOW:
            length=Makeroman(buf,++li->bulletnr,TRUE);
            break;
         default:
            length=sprintf(buf,"%d. ",++li->bulletnr);
            break;
      }
      if(!(elt=Anewobject(AOTP_TEXT,
         AOBJ_Pool,doc->pool,
         AOELT_Textpos,doc->text.length,
         AOELT_Textlength,length,
         AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
         AOELT_Bullet,TRUE,
         AOTXT_Blink,doc->pflags&DPF_BLINK,
         AOTXT_Text,&doc->text,
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      if(!Addtotextbuf(doc,buf,length)) return FALSE;
      doc->gotbreak=2;
      doc->pflags|=DPF_BULLET;
   }
   Checkid(doc,ta);
   return TRUE;
}

/*--- tables ----------------------------------------------------------*/

/*** <TABLE> ***/
static BOOL Dotable(struct Document *doc,struct Tagattr *ta)
{  short border=-1,cellspacing=-1,cellpadding=-1,width=-1;
   short align=-1,flalign=-1;
   short frame=-1,rules=-1;
   ULONG wtag=TAG_IGNORE;
   struct Colorinfo *bgcolor=NULL,*bordercolor=NULL,*borderdark=NULL,*borderlight=NULL;
   struct Number num;
   void *elt,*bgimg=NULL;
   UBYTE *styleAttr=NULL;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
         case TAGATTR_ALIGN:
            flalign=Getflalign(ATTR(doc,ta));
            if(STRIEQUAL(ATTR(doc,ta),"CENTER")) align=HALIGN_CENTER;
            break;
         case TAGATTR_BORDER:
            if(ta->length)
            {  border=Getposnumber(ATTR(doc,ta));
            }
            else border=1;
            if(frame<0) frame=border?TABFRM_ALL:TABFRM_NONE;
            if(rules<0) rules=border?TABRUL_ALL:TABRUL_NONE;
            break;
         case TAGATTR_WIDTH:
            width=Getnumber(&num,ATTR(doc,ta));
            wtag=AOTAB_Percentwidth;
            if(width<=0) width=100;
            else if(num.type!=NUMBER_PERCENT)
            {  wtag=AOTAB_Pixelwidth;
            }
            break;
         case TAGATTR_CELLPADDING:
            cellpadding=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_CELLSPACING:
            cellspacing=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_BACKGROUND:
            if(!STRICT) bgimg=Backgroundimg(doc,ATTR(doc,ta));
            break;
         case TAGATTR_BGCOLOR:
            if(!Setbodycolor(doc,&bgcolor,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLOR:
            if(!STRICT && !Setbodycolor(doc,&bordercolor,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLORDARK:
            if(!STRICT && !Setbodycolor(doc,&borderdark,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLORLIGHT:
            if(!STRICT && !Setbodycolor(doc,&borderlight,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_FRAME:
            if(STRIEQUAL(ATTR(doc,ta),"VOID")) frame=TABFRM_NONE;
            else if(STRIEQUAL(ATTR(doc,ta),"ABOVE")) frame=TABFRM_ABOVE;
            else if(STRIEQUAL(ATTR(doc,ta),"BELOW")) frame=TABFRM_BELOW;
            else if(STRIEQUAL(ATTR(doc,ta),"HSIDES")) frame=TABFRM_HSIDES;
            else if(STRIEQUAL(ATTR(doc,ta),"LHS")) frame=TABFRM_LEFT;
            else if(STRIEQUAL(ATTR(doc,ta),"RHS")) frame=TABFRM_RIGHT;
            else if(STRIEQUAL(ATTR(doc,ta),"VSIDES")) frame=TABFRM_VSIDES;
            else if(STRIEQUAL(ATTR(doc,ta),"BOX")) frame=TABFRM_ALL;
            else if(STRIEQUAL(ATTR(doc,ta),"BORDER")) frame=TABFRM_ALL;
            else frame=TABFRM_ALL;
            if(border<0) border=frame?1:0;
            break;
         case TAGATTR_RULES:
            if(STRIEQUAL(ATTR(doc,ta),"NONE")) rules=TABRUL_NONE;
            else if(STRIEQUAL(ATTR(doc,ta),"GROUPS")) rules=TABRUL_GROUPS;
            else if(STRIEQUAL(ATTR(doc,ta),"ROWS")) rules=TABRUL_ROWS;
            else if(STRIEQUAL(ATTR(doc,ta),"COLS")) rules=TABRUL_COLS;
            else if(STRIEQUAL(ATTR(doc,ta),"ALL")) rules=TABRUL_ALL;
            else rules=TABRUL_ALL;
            break;
      }
   }
   /* If border was not specified, default to no borders/rules */
   if(border<0)
   {  if(frame<0) frame=TABFRM_NONE;
      if(rules<0) rules=TABRUL_NONE;
   }
   if(!(Ensurebody(doc))) return FALSE;
   if(doc->doctype==DOCTP_BODY)
   {  if(flalign<0) Wantbreak(doc,1);
      Asetattrs(Docbody(doc),AOBDY_Align,-1,TAG_END);
      if(!(elt=Anewobject(AOTP_TABLE,
         AOBJ_Pool,doc->pool,
         AOBJ_Nobackground,BOOLVAL(doc->dflags&DDF_NOBACKGROUND),
         CONDTAG(AOELT_Halign,align),
         CONDTAG(AOELT_Floating,flalign),
         AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
         CONDTAG(AOTAB_Border,border),
         CONDTAG(AOTAB_Tabframe,frame),
         CONDTAG(AOTAB_Rules,rules),
         CONDTAG(AOTAB_Cellpadding,cellpadding),
         CONDTAG(AOTAB_Cellspacing,cellspacing),
         AOTAB_Bgimage,bgimg,
         AOTAB_Bgcolor,bgcolor,
         AOTAB_Bordercolor,bordercolor,
         AOTAB_Borderdark,borderdark,
         AOTAB_Borderlight,borderlight,
         wtag,width,
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      if(!Pushtable(doc,elt)) return FALSE;
      /* Apply CSS from external stylesheet */
      /* Apply CSS even if table has no class/id to support element-only selectors (e.g., "table { background-color: red; }") */
      if(doc->cssstylesheet)
      {  ApplyCSSToTableFromRules(doc,elt,class,id);
      }
      /* Apply inline CSS if present */
      if(styleAttr)
      {  ApplyCSSToTable(doc,elt,styleAttr);
      }
      if(!Ensuresp(doc)) return FALSE;
   }
   doc->pflags&=~DPF_BLINK;
   return TRUE;
}

/*** </TABLE> ***/
static BOOL Dotableend(struct Document *doc)
{  USHORT flalign=0;
   if(!ISEMPTY(&doc->tables))
   {  flalign=Agetattr(doc->tables.first->table,AOELT_Floating);
      Asetattrs(doc->tables.first->table,
         AOTAB_Vspacing,doc->gotbreak,
         AOTAB_Endtable,TRUE,
         TAG_END);
      Poptable(doc);
      doc->gotbreak=0;
      if(!Ensuresp(doc)) return FALSE;
      if(!flalign)
      {  Wantbreak(doc,1);
         Solvebreaks(doc);
      }
      if(Agetattr(Docbodync(doc),AOBDY_Style)!=STYLE_PRE) doc->pflags&=~DPF_PREFORMAT;
   }
   doc->pflags&=~DPF_BLINK;
   return TRUE;
}

/*** <CAPTION> ***/
static BOOL Docaption(struct Document *doc,struct Tagattr *ta)
{  short align=-1,halign=-1,a;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_VALIGN:
            if(STRICT) break;
            /* else fall trhough: */
         case TAGATTR_ALIGN:
            a=Getvalign(ATTR(doc,ta),STRICT);
            if(a>=0)
            {  align=a;
            }
            else if(!STRICT)
            {  halign=Gethalign(ATTR(doc,ta));
            }
            break;
      }
   }
   if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         AOTAB_Vspacing,doc->gotbreak,
         AOTAB_Caption,TRUE,
         CONDTAG(AOTAB_Valign,align),
         CONDTAG(AOTAB_Halign,halign),
         TAG_END);
      doc->gotbreak=2;
      if(Agetattr(Docbodync(doc),AOBDY_Style)!=STYLE_PRE) doc->pflags&=~DPF_PREFORMAT;
   }
   if(!Ensuresp(doc)) return FALSE;
   doc->pflags&=~DPF_BLINK;
   Checkid(doc,ta);
   return TRUE;
}

/*** </CAPTION> ***/
static BOOL Docaptionend(struct Document *doc)
{  if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         AOTAB_Vspacing,doc->gotbreak,
         AOTAB_Caption,FALSE,
         TAG_END);
      doc->gotbreak=0;
      doc->wantbreak=0;
      if(Agetattr(Docbodync(doc),AOBDY_Style)!=STYLE_PRE) doc->pflags&=~DPF_PREFORMAT;
   }
   if(!Ensuresp(doc)) return FALSE;
   doc->pflags&=~DPF_BLINK;
   return TRUE;
}

/*** <COLGROUP>, <COL> ***/
static BOOL Docolgrouporcol(struct Document *doc,struct Tagattr *ta,ULONG tagtype)
{  short halign=-1,valign=-1;
   short width=-1;
   ULONG wtag=TAG_IGNORE;
   short span=1;
   struct Number num;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_SPAN:
            span=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_WIDTH:
            width=Getnumber(&num,ATTR(doc,ta));
            if(width<=0) width=0;
            if(num.type==NUMBER_PERCENT)
            {  wtag=AOTAB_Percentwidth;
            }
            else if(num.type==NUMBER_RELATIVE)
            {  wtag=AOTAB_Relwidth;
            }
            else
            {  wtag=AOTAB_Pixelwidth;
            }
            break;
         case TAGATTR_ALIGN:
            halign=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_VALIGN:
            valign=Getvalign(ATTR(doc,ta),STRICT);
            if(STRIEQUAL(ATTR(doc,ta),"BASELINE")) valign=VALIGN_BASELINE;
            break;
      }
   }
   if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         tagtype==MARKUP_COLGROUP?AOTAB_Colgroup:AOTAB_Column,TRUE,
         AOTAB_Colspan,span,
         CONDTAG(AOTAB_Halign,halign),
         CONDTAG(AOTAB_Valign,valign),
         wtag,width,
         TAG_END);
   }
   return TRUE;
}

/*** </COLGROUP> ***/
static BOOL Docolgroupend(struct Document *doc)
{  if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         AOTAB_Colgroup,FALSE,
         TAG_END);
   }
   return TRUE;
}

/*** <THEAD>, <TFOOT>, <TBODY> ***/
static BOOL Dorowgroup(struct Document *doc,struct Tagattr *ta,ULONG tagtype)
{  short halign=-1,valign=-1;
   ULONG gtag=TAG_END;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_ALIGN:
            halign=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_VALIGN:
            valign=Getvalign(ATTR(doc,ta),STRICT);
            if(STRIEQUAL(ATTR(doc,ta),"BASELINE")) valign=VALIGN_BASELINE;
            break;
      }
   }
   switch(tagtype)
   {  case MARKUP_THEAD:
         gtag=AOTAB_Thead;
         break;
      case MARKUP_TFOOT:
         gtag=AOTAB_Tfoot;
         break;
      case MARKUP_TBODY:
         gtag=AOTAB_Tbody;
         break;
   }
   if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         gtag,TRUE,
         CONDTAG(AOTAB_Halign,halign),
         CONDTAG(AOTAB_Valign,valign),
         TAG_END);
   }
   return TRUE;
}

/*** </THEAD>, </TFOOT>, </TBODY> ***/
static BOOL Dorowgroupend(struct Document *doc,ULONG tagtype)
{  ULONG gtag=TAG_END;
   switch(tagtype)
   {  case MARKUP_THEAD:
         gtag=AOTAB_Thead;
         break;
      case MARKUP_TFOOT:
         gtag=AOTAB_Tfoot;
         break;
      case MARKUP_TBODY:
         gtag=AOTAB_Tbody;
         break;
   }
   if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         gtag,FALSE,
         TAG_END);
   }
   return TRUE;
}

/*** <TR> ***/
static BOOL Dotr(struct Document *doc,struct Tagattr *ta)
{  short halign=-1,valign=-1;
   struct Colorinfo *bgcolor=NULL,*bordercolor=NULL,*borderdark=NULL,*borderlight=NULL;
   void *bgimg=NULL;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_ALIGN:
            halign=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_VALIGN:
            valign=Getvalign(ATTR(doc,ta),STRICT);
            if(STRIEQUAL(ATTR(doc,ta),"BASELINE")) valign=VALIGN_BASELINE;
            break;
         case TAGATTR_BACKGROUND:
            if(!STRICT) bgimg=Backgroundimg(doc,ATTR(doc,ta));
            break;
         case TAGATTR_BGCOLOR:
            if(!Setbodycolor(doc,&bgcolor,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLOR:
            if(!STRICT && !Setbodycolor(doc,&bordercolor,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLORDARK:
            if(!STRICT && !Setbodycolor(doc,&borderdark,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLORLIGHT:
            if(!STRICT && !Setbodycolor(doc,&borderlight,ATTR(doc,ta))) return FALSE;
            break;
      }
   }
   if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         AOTAB_Vspacing,doc->gotbreak,
         AOTAB_Row,TRUE,
         CONDTAG(AOTAB_Halign,halign),
         CONDTAG(AOTAB_Valign,valign),
         AOTAB_Bgimage,bgimg,
         AOTAB_Bgcolor,bgcolor,
         AOTAB_Bordercolor,bordercolor,
         AOTAB_Borderdark,borderdark,
         AOTAB_Borderlight,borderlight,
         TAG_END);
      doc->gotbreak=2;
      doc->wantbreak=0;
      if(Agetattr(Docbodync(doc),AOBDY_Style)!=STYLE_PRE) doc->pflags&=~DPF_PREFORMAT;
   }
   if(!Ensuresp(doc)) return FALSE;
   doc->pflags&=~DPF_BLINK;
   return TRUE;
}

/*** </TR> ***/
static BOOL Dotrend(struct Document *doc)
{  if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         AOTAB_Vspacing,doc->gotbreak,
         AOTAB_Row,FALSE,
         TAG_END);
      doc->gotbreak=0;
      doc->wantbreak=0;
      if(Agetattr(Docbodync(doc),AOBDY_Style)!=STYLE_PRE) doc->pflags&=~DPF_PREFORMAT;
   }
   if(!Ensuresp(doc)) return FALSE;
   doc->pflags&=~DPF_BLINK;
   return TRUE;
}

/*** <TD>,<TH> ***/
static BOOL Dotd(struct Document *doc,struct Tagattr *ta,BOOL heading)
{  short halign=-1,valign=-1,rowspan=-1,colspan=-1;
   short width=-1,height=-1;
   ULONG wtag=TAG_IGNORE,htag=TAG_IGNORE;
   BOOL nowrap=FALSE;
   struct Number num;
   struct Colorinfo *bgcolor=NULL,*bordercolor=NULL,*borderdark=NULL,*borderlight=NULL;
   void *bgimg=NULL;
   UBYTE *styleAttr=NULL;
   UBYTE *class=NULL;
   UBYTE *id=NULL;
   void *cellBody=NULL;
   for(;ta && ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_STYLE:
            styleAttr=ATTR(doc,ta);
            break;
         case TAGATTR_CLASS:
            class=ATTR(doc,ta);
            break;
         case TAGATTR_ID:
            id=ATTR(doc,ta);
            break;
         case TAGATTR_ALIGN:
            halign=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_VALIGN:
            valign=Getvalign(ATTR(doc,ta),STRICT);
            if(STRIEQUAL(ATTR(doc,ta),"BASELINE")) valign=VALIGN_BASELINE;
            break;
         case TAGATTR_WIDTH:
            width=Getnumber(&num,ATTR(doc,ta));
            if(width<=0) width=0;
            if(num.type==NUMBER_PERCENT)
            {  wtag=AOTAB_Percentwidth;
            }
            else if(num.type==NUMBER_RELATIVE)
            {  wtag=AOTAB_Relwidth;
            }
            else
            {  wtag=AOTAB_Pixelwidth;
            }
            break;
         case TAGATTR_HEIGHT:
            height=Getnumber(&num,ATTR(doc,ta));
            if(height<0) height=0;
            if(num.type==NUMBER_PERCENT)
            {  htag=AOTAB_Percentheight;
            }
            else
            {  htag=AOTAB_Pixelheight;
            }
            break;
         case TAGATTR_ROWSPAN:
            rowspan=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_COLSPAN:
            colspan=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_NOWRAP:
            nowrap=TRUE;
            break;
         case TAGATTR_BACKGROUND:
            if(!STRICT) bgimg=Backgroundimg(doc,ATTR(doc,ta));
            break;
         case TAGATTR_BGCOLOR:
            if(!Setbodycolor(doc,&bgcolor,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLOR:
            if(!STRICT && !Setbodycolor(doc,&bordercolor,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLORDARK:
            if(!STRICT && !Setbodycolor(doc,&borderdark,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BORDERCOLORLIGHT:
            if(!STRICT && !Setbodycolor(doc,&borderlight,ATTR(doc,ta))) return FALSE;
            break;
      }
   }
   /* CSS width/height on STYLE= must reach Startcell like HTML WIDTH/HEIGHT. */
   if(styleAttr)
   {  short sw;
      short sh;
      ULONG stw;
      ULONG sth;
      sw=-1;
      sh=-1;
      stw=TAG_IGNORE;
      sth=TAG_IGNORE;
      ParseStyleTableCellDims(doc,styleAttr,&sw,&stw,&sh,&sth);
      if(stw!=TAG_IGNORE)
      {  wtag=stw;
         width=sw;
      }
      if(sth!=TAG_IGNORE)
      {  htag=sth;
         height=sh;
      }
   }
   if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         AOTAB_Vspacing,doc->gotbreak,
         (heading?AOTAB_Hcell:AOTAB_Cell),TRUE,
         CONDTAG(AOTAB_Halign,halign),
         CONDTAG(AOTAB_Valign,valign),
         CONDTAG(AOTAB_Rowspan,rowspan),
         CONDTAG(AOTAB_Colspan,colspan),
         wtag,width,
         htag,height,
         AOTAB_Nowrap,nowrap,
         AOTAB_Bgimage,bgimg,
         AOTAB_Bgcolor,bgcolor,
         AOTAB_Bordercolor,bordercolor,
         AOTAB_Borderdark,borderdark,
         AOTAB_Borderlight,borderlight,
         TAG_END);
      doc->gotbreak=2;
      doc->wantbreak=0;
      if(Agetattr(Docbodync(doc),AOBDY_Style)!=STYLE_PRE) doc->pflags&=~DPF_PREFORMAT;
      
      /* Get cell body for CSS application */
      Agetattrs(doc->tables.first->table,
         AOTAB_Bodync,&cellBody,
         TAG_END);
      
      /* Clear mistaken pixel width from legacy CSS (width:50% must not set AOBJ_Width=50) */
      if(cellBody)
      {  Asetattrs(cellBody,AOBJ_Width,0,TAG_END);
      }
      
      /* Apply CSS to table cell based on class/ID */
      if(cellBody)
      {  void *table;
         table = doc->tables.first->table;
         if(doc->cssstylesheet) ApplyCSSToBody(doc,cellBody,class,id,heading ? "TH" : "TD");
         /* Apply table-cell-specific CSS properties from external stylesheet */
         ApplyCSSToTableCellFromRules(doc,table,cellBody,class,id,heading ? "TH" : "TD");
      }
      
      /* Apply inline CSS if present */
      if(styleAttr && cellBody)
      {  struct Colorinfo *cssBgcolor;
         void *table;
         
         table = doc->tables.first->table;
         /* Apply CSS to body */
         ApplyInlineCSSToBody(doc,cellBody,styleAttr,(UBYTE *)(heading?"TH":"TD"));
         
         /* Extract background-color first before other CSS properties */
         /* This ensures it's applied to the table cell structure */
         /* This matches the behavior of the HTML BGCOLOR attribute */
         cssBgcolor = ExtractBackgroundColorFromStyle(doc,styleAttr);
         if(cssBgcolor)
         {  TableApplyCellAttrs(table,cellBody,AOTAB_Bgcolor,(long)cssBgcolor);
         }
         
         /* Apply table-cell-specific CSS properties (width, height, vertical-align, text-align) */
         ApplyCSSToTableCell(doc,table,cellBody,styleAttr);
      }
   }
   if(!Ensuresp(doc)) return FALSE;
   doc->pflags&=~DPF_BLINK;
   Checkid(doc,ta);
   return TRUE;
}

/*** </TD>,</TH> ***/
static BOOL Dotdend(struct Document *doc)
{  if(!ISEMPTY(&doc->tables))
   {  Asetattrs(doc->tables.first->table,
         AOTAB_Vspacing,doc->gotbreak,
         AOTAB_Cell,FALSE,
         TAG_END);
      doc->gotbreak=0;
      doc->wantbreak=0;
      if(Agetattr(Docbodync(doc),AOBDY_Style)!=STYLE_PRE) doc->pflags&=~DPF_PREFORMAT;
   }
   if(!Ensuresp(doc)) return FALSE;
   doc->pflags&=~DPF_BLINK;
   return TRUE;
}

/*--- sound -----------------------------------------------------------*/

/*** <BGSOUND> ***/
static BOOL Dobgsound(struct Document *doc,struct Tagattr *ta)
{  UBYTE *src=NULL;
   long loop=1;
   struct Number num;
   void *url,*referer;
   if(!STRICT)
   {  for(;ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_SRC:
               src=ATTR(doc,ta);
               break;
            case TAGATTR_LOOP:
               if(STRNIEQUAL(ATTR(doc,ta),"INF",3)) loop=-1;
               else loop=Getnumber(&num,ATTR(doc,ta));
               break;
         }
      }
      if(src && *src)
      {  if(!(url=Findurl(doc->base,src,0))) return FALSE;
         referer=(void *)Agetattr(doc->source->source,AOSRC_Url);
         if(!(doc->bgsound=Anewobject(AOTP_COPY,
            AOBJ_Pool,doc->pool,
            AOBJ_Frame,doc->frame,
            AOBJ_Cframe,doc->frame,
            AOBJ_Window,doc->win,
            AOCPY_Url,url,
            AOCPY_Referer,referer,
            AOCPY_Defaulttype,"audio/x-unknown",
            AOCPY_Soundloop,loop,
            AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
            TAG_END))) return FALSE;
         if(doc->win) Asetattrs(doc->win,AOWIN_Bgsound,TRUE,TAG_END);
      }
   }
   return TRUE;
}

/*--- forms -----------------------------------------------------------*/

/*** <FORM> ***/
static BOOL Doform(struct Document *doc,struct Tagattr *ta)
{  UBYTE *action=doc->base;
   UBYTE *target=doc->target;
   UBYTE *name=NULL;
   UBYTE *onreset=NULL,*onsubmit=NULL;
   USHORT method=FORMTH_GET;
   BOOL multipart=FALSE;
   struct Url *url;
   void *form;
   if(doc->pflags&DPF_FORM) return TRUE;  /* No nested forms */
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_METHOD:
            if(STRIEQUAL(ATTR(doc,ta),"GET")) method=FORMTH_GET;
            else if(STRIEQUAL(ATTR(doc,ta),"POST")) method=FORMTH_POST;
            break;
         case TAGATTR_ACTION:
            action=ATTR(doc,ta);
            break;
         case TAGATTR_TARGET:
            target=ATTR(doc,ta);
            break;
         case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_ONRESET:
            onreset=ATTR(doc,ta);
            break;
         case TAGATTR_ONSUBMIT:
            onsubmit=ATTR(doc,ta);
            break;
         case TAGATTR_ENCTYPE:
            if(STRIEQUAL(ATTR(doc,ta),"MULTIPART/FORM-DATA")) multipart=TRUE;
            break;
      }
   }
   if(!(url=Findurl(doc->base,action,0))) return FALSE;
   if(!(form=Anewobject(AOTP_FORM,
      AOBJ_Pool,doc->pool,
      AOBJ_Window,doc->win,
      AOBJ_Frame,doc->frame,
      AOBJ_Cframe,doc->frame,
      AOFOR_Method,method,
      AOFOR_Action,url,
      AOFOR_Target,target,
      AOFOR_Multipart,multipart,
      AOFOR_Name,name,
      AOFOR_Onreset,onreset,
      AOFOR_Onsubmit,onsubmit,
      TAG_END))) return FALSE;
   ADDTAIL(&doc->forms,form);
   doc->pflags|=DPF_FORM;
   Asetattrs(Docbodync(doc),AOBDY_Align,-1,TAG_END);
   Wantbreak(doc,1);
   Checkid(doc,ta);
   return TRUE;
}

/*** </FORM> ***/
static BOOL Doformend(struct Document *doc)
{  if(doc->pflags&DPF_FORM)
   {  Asetattrs(doc->forms.last,AOFOR_Complete,TRUE,TAG_END);
      doc->pflags&=~DPF_FORM;
   }
   return TRUE;
}

/*** <FIELDSET> ***/
static BOOL Dofieldset(struct Document *doc,struct Tagattr *ta)
{  short align=-1;
   struct Colorinfo *bgcolor=NULL;
   void *bgimg=NULL;
   void *body;
   /* Only want break if not inside a table cell */
   if(ISEMPTY(&doc->tables)) Wantbreak(doc,1);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_ALIGN:
            align=Gethalign(ATTR(doc,ta));
            break;
         case TAGATTR_BGCOLOR:
            if(!Setbodycolor(doc,&bgcolor,ATTR(doc,ta))) return FALSE;
            break;
         case TAGATTR_BACKGROUND:
            if(!STRICT) bgimg=Backgroundimg(doc,ATTR(doc,ta));
            break;
      }
   }
   if(!Ensurebody(doc)) return FALSE;
   body=Docbodync(doc);
   if(body)
   {  Asetattrs(body,AOBDY_Divalign,align,TAG_END);
      if(bgcolor && bgcolor->pen>=0)
      {  Asetattrs(body,AOBDY_Bgcolor,bgcolor->pen,TAG_END);
      }
      if(bgimg)
      {  Asetattrs(body,AOBDY_Bgimage,bgimg,TAG_END);
      }
   }
   Checkid(doc,ta);
   return TRUE;
}

/*** </FIELDSET> ***/
static BOOL Dofieldsetend(struct Document *doc)
{  void *body;
   /* Only want break if not inside a table cell */
   if(ISEMPTY(&doc->tables)) Wantbreak(doc,1);
   body=Docbodync(doc);
   if(body)
   {  Asetattrs(body,AOBDY_Divalign,-1,TAG_END);
      Asetattrs(body,AOBDY_Bgcolor,-1,TAG_END);
      Asetattrs(body,AOBDY_Bgimage,NULL,TAG_END);
   }
   if(!Ensuresp(doc)) return FALSE;
   return TRUE;
}

/*** <LEGEND> ***/
static BOOL Dolegend(struct Document *doc,struct Tagattr *ta)
{  short align=-1;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_ALIGN:
            align=Gethalign(ATTR(doc,ta));
            break;
      }
   }
   if(!Ensurebody(doc)) return FALSE;
   Asetattrs(Docbodync(doc),AOBDY_Divalign,align,TAG_END);
   Checkid(doc,ta);
   return TRUE;
}

/*** </LEGEND> ***/
static BOOL Dolegendend(struct Document *doc)
{  Asetattrs(Docbodync(doc),AOBDY_Divalign,-1,TAG_END);
   if(!Ensuresp(doc)) return FALSE;
   return TRUE;
}

/*** Add a form button */
static BOOL Addbutton(struct Document *doc,struct Tagattr *ta,BOOL custom)
{  UBYTE *name=NULL,*value=NULL;
   UBYTE *onclick=NULL,*onfocus=NULL,*onblur=NULL;
   USHORT type;
   void *elt;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_VALUE:
            value=ATTR(doc,ta);
            break;
         case TAGATTR_TYPE:
            if(STRIEQUAL(ATTR(doc,ta),"SUBMIT")) type=BUTTP_SUBMIT;
            else if(STRIEQUAL(ATTR(doc,ta),"RESET")) type=BUTTP_RESET;
            else if(STRIEQUAL(ATTR(doc,ta),"BUTTON")) type=BUTTP_BUTTON;
            break;
         case TAGATTR_ONCLICK:
            onclick=ATTR(doc,ta);
            break;
         case TAGATTR_ONFOCUS:
            onfocus=ATTR(doc,ta);
            break;
         case TAGATTR_ONBLUR:
            onblur=ATTR(doc,ta);
            break;
      }
   }
   if(!(elt=Anewobject(AOTP_BUTTON,
      AOBJ_Pool,doc->pool,
      AOBJ_Frame,doc->frame,
      AOBJ_Cframe,doc->cframe,
      AOBJ_Window,doc->win,
      AOFLD_Form,doc->pflags&DPF_FORM?doc->forms.last:NULL,
      AOFLD_Name,name,
      AOFLD_Value,value,
      AOFLD_Onclick,onclick,
      AOFLD_Onfocus,onfocus,
      AOFLD_Onblur,onblur,
      AOBUT_Custom,custom,
      AOBUT_Type,type,
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   if(custom) doc->button=elt;
   if(!Ensurenosp(doc)) return FALSE;
   return TRUE;
}

/*** </BUTTON> ***/
static BOOL Dobuttonend(struct Document *doc)
{  if(doc->button)
   {  Asetattrs(doc->button,AOBUT_Complete,TRUE,TAG_END);
      doc->button=NULL;
      if(Agetattr(Docbody(doc),AOBDY_Style)!=STYLE_PRE) doc->pflags&=~DPF_PREFORMAT;
      Ensurenosp(doc);
   }
   return TRUE;
}

/*** <BUTTON> ***/
static BOOL Dobutton(struct Document *doc,struct Tagattr *ta)
{  /* Allow BUTTON elements outside forms for JavaScript onclick handlers */
   if(doc->button)
   {  Dobuttonend(doc);
   }
   Addbutton(doc,ta,TRUE);
   Ensuresp(doc);
   return TRUE;
}

/*** <INPUT TYPE=SUBMIT,RESET,BUTTON ***/
static BOOL Dofldbutton(struct Document *doc,struct Tagattr *ta)
{  return Addbutton(doc,ta,FALSE);
}

/*** <INPUT TYPE=CHECKBOX> ***/
static BOOL Dofldcheckbox(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL,*value=NULL;
   UBYTE *onclick=NULL,*onfocus=NULL,*onblur=NULL;
   BOOL checked=FALSE;
   void *elt;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_VALUE:
            value=ATTR(doc,ta);
            break;
         case TAGATTR_CHECKED:
            checked=TRUE;
            break;
         case TAGATTR_ONCLICK:
            onclick=ATTR(doc,ta);
            break;
         case TAGATTR_ONFOCUS:
            onfocus=ATTR(doc,ta);
            break;
         case TAGATTR_ONBLUR:
            onblur=ATTR(doc,ta);
            break;
      }
   }
   if(!(elt=Anewobject(AOTP_CHECKBOX,
      AOBJ_Pool,doc->pool,
      AOFLD_Form,doc->forms.last,
      AOFLD_Name,name,
      AOFLD_Value,value,
      AOFLD_Onclick,onclick,
      AOFLD_Onfocus,onfocus,
      AOFLD_Onblur,onblur,
      AOCHB_Checked,checked,
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   if(!Ensurenosp(doc)) return FALSE;
   return TRUE;
}

/*** <INPUT TYPE=IMAGE> ***/
static BOOL Dofldimage(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL;
   short width=-1,height=-1,hspace=-1,vspace=-1;
   short align=-1,flalign=-1;
   long alt=-1,altlen=-1;
   UBYTE *src=NULL;
   void *elt,*url,*referer;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_SRC:
            src=ATTR(doc,ta);
            break;
         case TAGATTR_ALIGN:
            align=Getvalign(ATTR(doc,ta),STRICT);
            flalign=Getflalign(ATTR(doc,ta));
            break;
         case TAGATTR_ALT:
            alt=doc->text.length;
            altlen=ta->length;
            if(!(Addtotextbuf(doc,ATTR(doc,ta),ta->length))) return FALSE;
            break;
         case TAGATTR_WIDTH:
            width=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_HEIGHT:
            height=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_HSPACE:
            hspace=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_VSPACE:
            vspace=Getposnumber(ATTR(doc,ta));
            break;
      }
   }
   if(src)
   {  if(!(url=Findurl(doc->base,src,0))) return FALSE;
      referer=(void *)Agetattr(doc->source->source,AOSRC_Url);
      if(!Ensurebody(doc)) return FALSE;
      if(!(elt=Anewobject(AOTP_COPY,
         AOBJ_Pool,doc->pool,
         CONDTAG(AOELT_Valign,align),
         CONDTAG(AOELT_Floating,flalign),
         CONDTAG(AOELT_Textpos,alt),
         CONDTAG(AOELT_Textlength,altlen),
         AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
         AOFLD_Form,doc->forms.last,
         AOFLD_Name,name,
         AOCPY_Url,url,
         AOCPY_Embedded,TRUE,
         AOCPY_Text,&doc->text,
         AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
         AOCPY_Defaulttype,"image/x-unknown",
         AOCPY_Trueimage,TRUE,
         CONDTAG(AOCPY_Width,width),
         CONDTAG(AOCPY_Height,height),
         CONDTAG(AOCPY_Hspace,hspace),
         CONDTAG(AOCPY_Vspace,vspace),
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      Asetattrs(elt,AOBJ_Layoutparent,doc->body,TAG_END);
      if(!Ensurenosp(doc)) return FALSE;
   }
   return TRUE;
}

/*** <INPUT TYPE=RADIO> ***/
static BOOL Dofldradio(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL,*value=NULL;
   UBYTE *onclick=NULL,*onfocus=NULL,*onblur=NULL;
   BOOL checked=FALSE;
   void *elt;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_VALUE:
            value=ATTR(doc,ta);
            break;
         case TAGATTR_CHECKED:
            checked=TRUE;
            break;
         case TAGATTR_ONCLICK:
            onclick=ATTR(doc,ta);
            break;
         case TAGATTR_ONFOCUS:
            onfocus=ATTR(doc,ta);
            break;
         case TAGATTR_ONBLUR:
            onblur=ATTR(doc,ta);
            break;
      }
   }
   if(!(elt=Anewobject(AOTP_RADIO,
      AOBJ_Pool,doc->pool,
      AOFLD_Form,doc->forms.last,
      AOFLD_Name,name,
      AOFLD_Value,value,
      AOFLD_Onclick,onclick,
      AOFLD_Onfocus,onfocus,
      AOFLD_Onblur,onblur,
      AORAD_Checked,checked,
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   if(!Ensurenosp(doc)) return FALSE;
   return TRUE;
}

/*** <INPUT TYPE=HIDDEN> ***/
static BOOL Dofldhidden(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL,*value="";
   void *elt;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_VALUE:
            value=ATTR(doc,ta);
            break;
      }
   }
   if(!(elt=Anewobject(AOTP_HIDDEN,
      AOBJ_Pool,doc->pool,
      AOELT_Visible,FALSE,
      AOFLD_Form,doc->forms.last,
      AOFLD_Name,name,
      AOFLD_Value,value,
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   return TRUE;
}

/*** <INPUT TYPE=FILE> ***/
static BOOL Dofldfile(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL,*value=NULL;
   UBYTE *onchange=NULL,*onfocus=NULL,*onblur=NULL;
   long size=-1;
   void *elt;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_VALUE:
            value=ATTR(doc,ta);
            break;
         case TAGATTR_SIZE:
            size=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_ONCHANGE:
            onchange=ATTR(doc,ta);
            break;
         case TAGATTR_ONFOCUS:
            onfocus=ATTR(doc,ta);
            break;
         case TAGATTR_ONBLUR:
            onblur=ATTR(doc,ta);
            break;
      }
   }
   if(!(elt=Anewobject(AOTP_FILEFIELD,
      AOBJ_Pool,doc->pool,
      AOFLD_Form,doc->forms.last,
      AOFLD_Name,name,
      AOFLD_Value,value,
      AOFLD_Onchange,onchange,
      AOFLD_Onfocus,onfocus,
      AOFLD_Onblur,onblur,
      CONDTAG(AOFUF_Size,size),
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   if(!Ensurenosp(doc)) return FALSE;
   return TRUE;
}

/*** <INPUT TYPE=TEXT,PASSWORD> ***/
static BOOL Dofldtext(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL,*value=NULL;
   UBYTE *onchange=NULL,*onfocus=NULL,*onblur=NULL,*onselect=NULL;
   long maxlength=-1,size=-1;
   USHORT type=INPTP_TEXT;
   void *elt;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_TYPE:
            if(STRIEQUAL(ATTR(doc,ta),"PASSWORD")) type=INPTP_PASSWORD;
            else type=INPTP_TEXT;
            break;
         case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_VALUE:
            value=ATTR(doc,ta);
            break;
         case TAGATTR_MAXLENGTH:
            maxlength=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_SIZE:
            size=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_ONCHANGE:
            onchange=ATTR(doc,ta);
            break;
         case TAGATTR_ONFOCUS:
            onfocus=ATTR(doc,ta);
            break;
         case TAGATTR_ONBLUR:
            onblur=ATTR(doc,ta);
            break;
         case TAGATTR_ONSELECT:
            onselect=ATTR(doc,ta);
            break;
      }
   }
   if(!(elt=Anewobject(AOTP_INPUT,
      AOBJ_Pool,doc->pool,
      AOFLD_Form,doc->forms.last,
      AOFLD_Name,name,
      AOFLD_Value,value,
      AOFLD_Onchange,onchange,
      AOFLD_Onfocus,onfocus,
      AOFLD_Onblur,onblur,
      AOFLD_Onselect,onselect,
      AOINP_Type,type,
      CONDTAG(AOINP_Maxlength,maxlength),
      CONDTAG(AOINP_Size,size),
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   if(!Ensurenosp(doc)) return FALSE;
   return TRUE;
}

/*** <INPUT> ***/
static BOOL Doinput(struct Document *doc,struct Tagattr *ta)
{  struct Tagattr *tattrs=ta;
   UBYTE *type=NULL;
   UBYTE *onclick=NULL;
   /* Check for button types and onclick handlers for formless inputs */
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_TYPE:
            type=ATTR(doc,ta);
            break;
         case TAGATTR_ONCLICK:
            onclick=ATTR(doc,ta);
            break;
      }
   }
   /* Allow button-type inputs outside forms for JavaScript events */
   if(!(doc->pflags&DPF_FORM))
   {  if(type && (STRIEQUAL(type,"SUBMIT") || STRIEQUAL(type,"RESET") || STRIEQUAL(type,"BUTTON") || onclick))
      {  ta=tattrs;
         for(;ta->next;ta=ta->next)
         {  switch(ta->attr)
            {  case TAGATTR_TYPE:
                  if(STRIEQUAL(ATTR(doc,ta),"SUBMIT")) return Dofldbutton(doc,tattrs);
                  else if(STRIEQUAL(ATTR(doc,ta),"RESET")) return Dofldbutton(doc,tattrs);
                  else if(STRIEQUAL(ATTR(doc,ta),"BUTTON")) return Dofldbutton(doc,tattrs);
                  break;
            }
         }
         /* If onclick is set, treat as button */
         if(onclick) return Dofldbutton(doc,tattrs);
      }
      return TRUE;
   }
   /* Normal form input handling */
   ta=tattrs;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_TYPE:
            if(STRIEQUAL(ATTR(doc,ta),"TEXT")) return Dofldtext(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"PASSWORD")) return Dofldtext(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"SUBMIT")) return Dofldbutton(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"RESET")) return Dofldbutton(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"BUTTON")) return Dofldbutton(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"HIDDEN")) return Dofldhidden(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"CHECKBOX")) return Dofldcheckbox(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"RADIO")) return Dofldradio(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"IMAGE")) return Dofldimage(doc,tattrs);
            else if(STRIEQUAL(ATTR(doc,ta),"FILE")) return Dofldfile(doc,tattrs);
            break;
      }
   }
   return Dofldtext(doc,tattrs);
}

/*** <ISINDEX> ***/
static BOOL Doisindex(struct Document *doc,struct Tagattr *ta)
{  UBYTE *prompt=NULL;
   void *elt,*form;
   struct Url *url;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_PROMPT:
            prompt=ATTR(doc,ta);
            break;
      }
   }
   if(!prompt) prompt=AWEBSTR(MSG_AWEB_INDEXPROMPT);
   Wantbreak(doc,2);
   if(!(elt=Anewobject(AOTP_TEXT,
      AOBJ_Pool,doc->pool,
      AOELT_Textpos,doc->text.length,
      AOELT_Textlength,strlen(prompt),
      AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
      AOTXT_Blink,doc->pflags&DPF_BLINK,
      AOTXT_Text,&doc->text,
      TAG_END))) return FALSE;
   if(!Addelement(doc,elt)) return FALSE;
   if(!Addtotextbuf(doc,prompt,strlen(prompt))) return FALSE;
   if(!(url=Findurl(doc->base,"",0))) return FALSE;
   if(!(form=Anewobject(AOTP_FORM,
      AOBJ_Pool,doc->pool,
      AOBJ_Window,doc->win,
      AOBJ_Frame,doc->frame,
      AOBJ_Cframe,doc->frame,
      AOFOR_Method,FORMTH_INDEX,
      AOFOR_Action,url,
      TAG_END))) return FALSE;
   ADDTAIL(&doc->forms,form);
   if(!Dofldtext(doc,&dummyta)) return FALSE;
   doc->pflags&=~DPF_FORM;
   Wantbreak(doc,2);
   return TRUE;
}

/*** <OPTION> ***/
static BOOL Dooption(struct Document *doc,struct Tagattr *ta)
{  UBYTE *value=NULL;
   BOOL selected=FALSE;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_VALUE:
            value=ATTR(doc,ta);
            break;
         case TAGATTR_SELECTED:
            selected=TRUE;
            break;
      }
   }
   if(doc->select)
   {  Asetattrs(doc->select,
         AOSEL_Option,TRUE,
         AOSEL_Optionvalue,value,
         AOSEL_Selected,selected,
         TAG_END);
      doc->pmode=DPM_OPTION;
      if(!Ensuresp(doc)) return FALSE;
   }
   return TRUE;
}

/*** text within <OPTION> ***/
static BOOL Dooptiontext(struct Document *doc,struct Tagattr *ta)
{  if(doc->select)
   {  Asetattrs(doc->select,
         AOSEL_Optiontext,ATTR(doc,ta),
         TAG_END);
   }
   return TRUE;
}

/*** <SELECT> ***/
static BOOL Doselect(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL,*value=NULL;
   UBYTE *onchange=NULL,*onfocus=NULL,*onblur=NULL;
   BOOL multiple=FALSE;
   short size=-1;
   void *elt;
   if(doc->pflags&DPF_FORM)
   {  Checkid(doc,ta);
      for(;ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_NAME:
               name=ATTR(doc,ta);
               break;
            case TAGATTR_VALUE:
               value=ATTR(doc,ta);
               break;
            case TAGATTR_MULTIPLE:
               multiple=TRUE;
               break;
            case TAGATTR_SIZE:
               size=Getposnumber(ATTR(doc,ta));
               break;
            case TAGATTR_ONCHANGE:
               onchange=ATTR(doc,ta);
               break;
            case TAGATTR_ONFOCUS:
               onfocus=ATTR(doc,ta);
               break;
            case TAGATTR_ONBLUR:
               onblur=ATTR(doc,ta);
               break;
         }
      }
      if(!(elt=Anewobject(AOTP_SELECT,
         AOBJ_Pool,doc->pool,
         AOFLD_Form,doc->forms.last,
         AOFLD_Name,name,
         AOFLD_Value,value,
         AOFLD_Onchange,onchange,
         AOFLD_Onfocus,onfocus,
         AOFLD_Onblur,onblur,
         CONDTAG(AOSEL_Size,size),
         AOSEL_Multiple,multiple,
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      if(!Ensurenosp(doc)) return FALSE;
      doc->select=elt;
   }
   return TRUE;
}

/*** </SELECT> ***/
static BOOL Doselectend(struct Document *doc)
{  doc->pmode=DPM_BODY; /* clear OPTION mode */
   if(doc->select)
   {  Asetattrs(doc->select,AOSEL_Complete,TRUE,TAG_END);
      doc->select=NULL;
   }
   return TRUE;
}

/*** <TEXTAREA> ***/
static BOOL Dotextarea(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL;
   UBYTE *onchange=NULL,*onfocus=NULL,*onblur=NULL,*onselect=NULL;
   short cols=-1,rows=-1;
   void *elt;
   if(doc->pflags&DPF_FORM)
   {  Checkid(doc,ta);
      for(;ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_NAME:
               name=ATTR(doc,ta);
               break;
            case TAGATTR_COLS:
               cols=Getposnumber(ATTR(doc,ta));
               break;
            case TAGATTR_ROWS:
               rows=Getposnumber(ATTR(doc,ta));
               break;
            case TAGATTR_ONCHANGE:
               onchange=ATTR(doc,ta);
               break;
            case TAGATTR_ONFOCUS:
               onfocus=ATTR(doc,ta);
               break;
            case TAGATTR_ONBLUR:
               onblur=ATTR(doc,ta);
               break;
            case TAGATTR_ONSELECT:
               onselect=ATTR(doc,ta);
               break;
         }
      }
      if(!(elt=Anewobject(AOTP_TEXTAREA,
         AOBJ_Pool,doc->pool,
         AOFLD_Form,doc->forms.last,
         AOFLD_Name,name,
         AOFLD_Onchange,onchange,
         AOFLD_Onfocus,onfocus,
         AOFLD_Onblur,onblur,
         AOFLD_Onselect,onselect,
         CONDTAG(AOTXA_Cols,cols),
         CONDTAG(AOTXA_Rows,rows),
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      if(!Ensurenosp(doc)) return FALSE;
      doc->pmode=DPM_TEXTAREA;
      doc->textarea=elt;
   }
   return TRUE;
}

/*** </TEXTAREA> ***/
static BOOL Dotextareaend(struct Document *doc)
{  doc->pmode=DPM_BODY; /* clear TEXTAREA mode */
   if(doc->textarea)
   {  Asetattrs(doc->textarea,AOTXA_Complete,TRUE,TAG_END);
      doc->textarea=NULL;
      if(!Ensurenosp(doc)) return FALSE;
   }
   return TRUE;
}

/*** text within <TEXTAREA> ***/
static BOOL Dotextareatext(struct Document *doc,struct Tagattr *ta)
{  if(doc->textarea)
   {  Asetattrs(doc->textarea,
         AOTXA_Text,ATTR(doc,ta),
         TAG_END);
   }
   return TRUE;
}

/*--- object ----------------------------------------------------------*/

/*** <OBJECT> ***/
static BOOL Doobject(struct Document *doc,struct Tagattr *ta)
{  short border=-1,width=-1,height=-1,hspace=-1,vspace=-1;
   short align=-1,flalign=-1;
   long alt=-1,altlen=-1;
   void *usemap=NULL;
   UBYTE *data=NULL,*type=NULL,*codebase=NULL,*codetype=NULL,*classid=NULL,*name=NULL;
   UBYTE *mapname,*ttype;
   UBYTE *dummyp;
   UBYTE *onload=NULL,*onerror=NULL,*onabort=NULL,*onclick=NULL;
   UBYTE *onmouseover=NULL,*onmouseout=NULL;
   BOOL ismap=FALSE,declare=FALSE,shapes=TRUE;
   void *elt,*url,*referer;
   Checkid(doc,ta);
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_DATA:
            data=ATTR(doc,ta);
            break;
         case TAGATTR_TYPE:
            type=ATTR(doc,ta);
            break;
         case TAGATTR_CODEBASE:
            codebase=ATTR(doc,ta);
            break;
         case TAGATTR_CODETYPE:
            codetype=ATTR(doc,ta);
            break;
         case TAGATTR_CLASSID:
            classid=ATTR(doc,ta);
            break;
         case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_DECLARE:
            declare=TRUE;
            break;
         case TAGATTR_SHAPES:
            shapes=TRUE;
            /* Create anonymous map */
            usemap=Anewobject(AOTP_MAP,
               AOBJ_Pool,doc->pool,
               TAG_END);
            if(usemap) ADDTAIL(&doc->maps,usemap);
            break;
         case TAGATTR_ALIGN:
            align=Getvalign(ATTR(doc,ta),STRICT);
            flalign=Getflalign(ATTR(doc,ta));
            break;
         case TAGATTR_STANDBY:
            if(ta->length)
            {  alt=doc->text.length;
               altlen=ta->length;
               if(!(Addtotextbuf(doc,ATTR(doc,ta),ta->length))) return FALSE;
            }
            break;
         case TAGATTR_ISMAP:
            if(!STRICT) ismap=TRUE;
            break;
         case TAGATTR_USEMAP:
            mapname=ATTR(doc,ta);
            shapes=FALSE;
            if(mapname[0]=='#')
            {  usemap=Findmap(doc,mapname+1);
            }
            else
            {  usemap=Externalmap(doc,mapname);
            }
            break;
         case TAGATTR_BORDER:
            border=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_WIDTH:
            width=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_HEIGHT:
            height=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_HSPACE:
            hspace=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_VSPACE:
            vspace=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_ONLOAD:
            onload=ATTR(doc,ta);
            break;
         case TAGATTR_ONERROR:
            onerror=ATTR(doc,ta);
            break;
         case TAGATTR_ONABORT:
            onabort=ATTR(doc,ta);
            break;
         case TAGATTR_ONCLICK:
            onclick=ATTR(doc,ta);
            break;
         case TAGATTR_ONMOUSEOVER:
            onmouseover=ATTR(doc,ta);
            break;
         case TAGATTR_ONMOUSEOUT:
            onmouseout=ATTR(doc,ta);
            break;
      }
   }
   /* We cannot handle external code yet */
   if(!codebase && !classid && !declare)
   {  if(data)
      {  /* Check if we understand the type */
         if(!(ttype=type))
         {  ttype=Mimetypefromext(data);
         }
         if(ttype && !STRNIEQUAL(ttype,"TEXT/",5)
         && Getmimedriver(ttype,&dummyp,&dummyp))
         {  if(!(url=Findurl(doc->base,data,0))) return FALSE;
            referer=(void *)Agetattr(doc->source->source,AOSRC_Url);
            if(!Ensurebody(doc)) return FALSE;
            if(!(elt=Anewobject(AOTP_COPY,
               AOBJ_Pool,doc->pool,
               CONDTAG(AOELT_Valign,align),
               CONDTAG(AOELT_Floating,flalign),
               CONDTAG(AOELT_Textpos,alt),
               CONDTAG(AOELT_Textlength,altlen),
               AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
               AOCPY_Url,url,
               AOCPY_Embedded,TRUE,
               AOCPY_Usemap,usemap,
               AOCPY_Ismap,ismap,
               AOCPY_Text,&doc->text,
               AOCPY_Referer,referer,
               AOCPY_Defaulttype,type?type:(UBYTE *)"image/x-unknown",
               AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
               CONDTAG(AOCPY_Border,border),
               CONDTAG(AOCPY_Width,width),
               CONDTAG(AOCPY_Height,height),
               CONDTAG(AOCPY_Hspace,hspace),
               CONDTAG(AOCPY_Vspace,vspace),
               AOCPY_Onload,onload,
               AOCPY_Onerror,onerror,
               AOCPY_Onabort,onabort,
               AOCPY_Onclick,onclick,
               AOCPY_Onmouseover,onmouseover,
               AOCPY_Onmouseout,onmouseout,
               AOCPY_Objectready,FALSE,
               TAG_END))) return FALSE;
            if(!Addelement(doc,elt)) return FALSE;
            Asetattrs(elt,AOBJ_Layoutparent,doc->body,TAG_END);
            if(flalign<0)
            {  if(!Ensurenosp(doc)) return FALSE;
            }
            doc->currentobject=elt;
            doc->objectnest++;
            doc->pmode=DPM_OBJECT;
            if(shapes) doc->pflags|=DPF_OBJECTSHAPES;
         }
      }
   }
   return TRUE;
}

/*** </OBJECT> ***/
static BOOL Doobjectend(struct Document *doc)
{  if(doc->objectnest)
   {  doc->objectnest--;
      if(!doc->objectnest)
      {  Asetattrs(doc->currentobject,AOCPY_Objectready,TRUE,TAG_END);
         doc->currentobject=NULL;
         doc->pmode=DPM_BODY;
         doc->pflags&=~DPF_OBJECTSHAPES;
      }
   }
   return TRUE;
}

/*** <PARAM> ***/
static BOOL Doparam(struct Document *doc,struct Tagattr *ta)
{  UBYTE *name=NULL,*type=NULL,*value=NULL,*valuetype=NULL;
   if(doc->objectnest==1)
   {  for(;ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_NAME:
               name=ATTR(doc,ta);
               break;
            case TAGATTR_TYPE:
               type=ATTR(doc,ta);
               break;
            case TAGATTR_VALUE:
               value=ATTR(doc,ta);
               break;
            case TAGATTR_VALUETYPE:
               valuetype=ATTR(doc,ta);
               break;
         }
      }
      Asetattrs(doc->currentobject,
         AOCPY_Paramname,name,
         AOCPY_Paramtype,type,
         AOCPY_Paramvalue,value,
         AOCPY_Paramvaluetype,valuetype,
         TAG_END);
   }
   return TRUE;
}

/*** <EMBED> ***/
static BOOL Doembed(struct Document *doc,struct Tagattr *ta)
{  struct Tagattr *orgta=ta;
   short width=-1,height=-1;
   UBYTE *src=NULL,*name=NULL,*value;
   void *elt,*url,*referer;
   BOOL hidden=FALSE;
   if(STRICT) return TRUE;
   for(;ta->next;ta=ta->next)
   {  switch(ta->attr)
      {  case TAGATTR_SRC:
            src=ATTR(doc,ta);
            break;
         case TAGATTR_NAME:
            name=ATTR(doc,ta);
            break;
         case TAGATTR_WIDTH:
            width=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_HEIGHT:
            height=Getposnumber(ATTR(doc,ta));
            break;
         case TAGATTR_EMBEDPARAMNAME:
            if(STRIEQUAL(ATTR(doc,ta),"HIDDEN")
            && ta->next->next && ta->next->attr==TAGATTR_EMBEDPARAMVALUE)
            {  if(STRIEQUAL(ATTR(doc,ta->next),"TRUE")
               || STRIEQUAL(ATTR(doc,ta->next),"YES"))
               {  hidden=TRUE;
               }
            }
            break;
      }
   }
   if(src)
   {  if(width==0 && height==0) hidden=TRUE;
      if(!(url=Findurl(doc->base,src,0))) return FALSE;
      referer=(void *)Agetattr(doc->source->source,AOSRC_Url);
      if(!Ensurebody(doc)) return FALSE;
      if(!(elt=Anewobject(AOTP_COPY,
         AOBJ_Pool,doc->pool,
         AOELT_Preformat,doc->pflags&DPF_PREFORMAT,
         AOELT_Visible,!hidden,
         AOCPY_Url,url,
         AOCPY_Embedded,TRUE,
         AOCPY_Referer,referer,
         AOCPY_Defaulttype,"image/x-unknown",
         AOCPY_Reloadverify,(doc->pflags&DPF_RELOADVERIFY),
         CONDTAG(AOCPY_Width,width),
         CONDTAG(AOCPY_Height,height),
         AOCPY_Objectready,FALSE,
         TAG_END))) return FALSE;
      if(!Addelement(doc,elt)) return FALSE;
      Asetattrs(elt,AOBJ_Layoutparent,doc->body,TAG_END);
      /* See if there are odd named parameters */
      name=value=NULL;
      for(ta=orgta;ta->next;ta=ta->next)
      {  switch(ta->attr)
         {  case TAGATTR_EMBEDPARAMNAME:
               name=ATTR(doc,ta);
               break;
            case TAGATTR_EMBEDPARAMVALUE:
               value=ATTR(doc,ta);
               Asetattrs(elt,
                  AOCPY_Paramname,name,
                  AOCPY_Paramvalue,value,
                  TAG_END);
               break;
         }
      }
      Asetattrs(elt,AOCPY_Objectready,TRUE,TAG_END);
      if(!Ensurenosp(doc)) return FALSE;
   }
   return TRUE;
}

/*---------------------------------------------------------------------*/
/*---------------------------------------------------------------------*/

/* Clean up, close all open elements, etc. */
static void Doeof(struct Document *doc)
{  if(doc->pmode==DPM_TITLE)
   {  Dotitleend(doc);
   }
   if(doc->pmode==DPM_OPTION)
   {  Doselectend(doc);
   }
   if(doc->pmode==DPM_TEXTAREA)
   {  Dotextareaend(doc);
   }
   while(!ISEMPTY(&doc->tables))
   {  Dotableend(doc);
   }
   if(doc->pflags&DPF_FORM)
   {  Doformend(doc);
   }
   if(doc->framesets.first->next)
   {  while(doc->framesets.first->next->next)
      {  Popframeset(doc);
      }
      Asetattrs(doc->framesets.first->frameset,AOFRS_Endframeset,TRUE,TAG_END);
   }
   if(doc->doctype==DOCTP_BODY)
   {  /* Add a line break to end the last line and activate trailing <br> */
      Dobr(doc,&dummyta);
      Asetattrs(doc->body,AOBDY_End,TRUE,TAG_END);
   }
}

/*---------------------------------------------------------------------*/

BOOL Processhtml(struct Document *doc,USHORT tagtype,struct Tagattr *ta)
{  BOOL result=TRUE;
   if(tagtype==MARKUP_EOF && !(doc->pflags&DPF_JPARSE))
   {  if(doc->pflags&DPF_JSCRIPT)
      {  /* Parser may reach EOF without a </SCRIPT> token (e.g. truncated source);
         * still run Doscriptend so BODY and the rest of the document can finish. */
         Doscriptend(doc);
      }
      if(doc->pmode==DPM_SCRIPT)
      {  /* Non-JS <SCRIPT> raw mode: leave script context at EOF. */
         doc->pmode=DPM_BODY;
      }
      Doeof(doc);
   }

   if(doc->dflags&DDF_MAPDOCUMENT)
   {  tagtype=Mapdoctag(tagtype);
   }

   switch(doc->pmode)
   {  case DPM_TITLE:
         switch(tagtype)
         {  case MARKUP_TEXT:
               result=Dotitleadd(doc,ta);
               break;
            case MARKUP_TITLE|MARKUP_END:
               result=Dotitleend(doc);
               break;
            case MARKUP_HEAD|MARKUP_END:
               if(doc->htmlmode==HTML_COMPATIBLE)
               {  result=Dotitleend(doc);
               }
               break;
            case MARKUP_BODY:
               if(doc->htmlmode==HTML_COMPATIBLE)
               {  if(result=Dotitleend(doc))
                  {  result=Dobody(doc,ta);
                  }
               }
               break;
         }
         break;
      case DPM_STYLE:
         switch(tagtype)
         {  case MARKUP_TEXT:
               result=Docsssource(doc,ta);
               break;
            case MARKUP_STYLE|MARKUP_END:
               result=Docssend(doc);
               doc->pmode=DPM_BODY;
               break;
            case MARKUP_HEAD|MARKUP_END:
               if(doc->htmlmode==HTML_COMPATIBLE)
               {  result=Docssend(doc);
                  doc->pmode=DPM_BODY;
               }
               break;
            case MARKUP_BODY:
               if(doc->htmlmode==HTML_COMPATIBLE)
               {  result=Docssend(doc);
                  doc->pmode=DPM_BODY;
                  result=Dobody(doc,ta);
               }
               break;
         }
         break;
      case DPM_SCRIPT:
         switch(tagtype)
         {  case MARKUP_SCRIPT|MARKUP_END:
               doc->pmode=DPM_BODY;
               if(doc->pflags&DPF_JSCRIPT)
               {  /* execute external script now */
                  result=Doscriptend(doc);
               }
               break;
            case MARKUP_HEAD|MARKUP_END:
               if(doc->htmlmode==HTML_COMPATIBLE)
               {  doc->pmode=DPM_BODY;
               }
               break;
            case MARKUP_BODY:
               if(doc->htmlmode==HTML_COMPATIBLE)
               {  doc->pmode=DPM_BODY;
                  result=Dobody(doc,ta);
               }
               break;
         }
         break;
      case DPM_MAP:
         switch(tagtype)
         {  case MARKUP_AREA:
               result=Doarea(doc,ta,TRUE);
               break;
            case MARKUP_MAP|MARKUP_END:
               doc->pmode=DPM_BODY;
               break;
         }
         break;
      case DPM_OPTION:
         switch(tagtype)
         {  case MARKUP_TEXT:
               result=Dooptiontext(doc,ta);
               break;
            case MARKUP_OPTION:
               result=Dooption(doc,ta);
               break;
            case MARKUP_SELECT|MARKUP_END:
               result=Doselectend(doc);
               break;
            case MARKUP_FORM|MARKUP_END:
               result=Doselectend(doc)
                     || Doformend(doc);
               break;
         }
         break;
      case DPM_TEXTAREA:
         switch(tagtype)
         {  case MARKUP_TEXT:
               result=Dotextareatext(doc,ta);
               break;
            case MARKUP_TEXTAREA|MARKUP_END:
               result=Dotextareaend(doc);
               break;
            case MARKUP_FORM|MARKUP_END:
               result=Dotextareaend(doc)
                     || Doformend(doc);
               break;
         }
         break;
      case DPM_FRAMESET:
         switch(tagtype)
         {  case MARKUP_FRAME:
               result=Doframe(doc,ta);
               break;
            case MARKUP_FRAMESET:
               result=Doframeset(doc,ta);
               break;
            case MARKUP_FRAMESET|MARKUP_END:
               result=Doframesetend(doc);
               break;
            case MARKUP_NOFRAMES:
               break;
            case MARKUP_NOSCRIPT:
               if(!STRICT)
               {  result=Donoscript(doc,ta);
               }
               break;
         }
         break;
      case DPM_OBJECT:
         switch(tagtype)
         {  case MARKUP_OBJECT:
               doc->objectnest++;
               break;
            case MARKUP_OBJECT|MARKUP_END:
               result=Doobjectend(doc);
               break;
            case MARKUP_PARAM:
               result=Doparam(doc,ta);
               break;
            case MARKUP_A:
               if(doc->pflags&DPF_OBJECTSHAPES)
               {  result=Doarea(doc,ta,FALSE);
               }
               break;
         }
         break;
      case DPM_NOSCRIPT:
         switch(tagtype)
         {  case MARKUP_NOSCRIPT|MARKUP_END:
               result=Donoscriptend(doc);
               break;
         }
         break;
      case DPM_IFRAME:
         switch(tagtype)
         {  case MARKUP_IFRAME|MARKUP_END:
               doc->pmode=DPM_BODY;
               break;
         }
         break;
      default:
         switch(tagtype)
         {  
         /*---- header contents ----*/
            case MARKUP_TITLE:
               result=Dotitle(doc);
               break;
            case MARKUP_STYLE:
               Freebuffer(&doc->csssrc);
               doc->pmode=DPM_STYLE;
               break;
            case MARKUP_SCRIPT:
               result=Doscript(doc,ta);
               break;
            case MARKUP_SCRIPT|MARKUP_END:
               result=Doscriptend(doc);
               break;
            case MARKUP_BASE:
               result=Dobase(doc,ta);
               break;
            case MARKUP_LINK:
               result=Dolink(doc,ta);
               break;
            case MARKUP_META:
               result=Dometa(doc,ta);
               break;
         /*---- body ----*/
            case MARKUP_BODY:
               result=Dobody(doc,ta);
               break;
            case MARKUP_NOSCRIPT:
               result=Donoscript(doc,ta);
               break;
         /*---- text and line breaks ----*/
            case MARKUP_BLINK:
               result=Doblink(doc,ta);
               break;
            case MARKUP_BLINK|MARKUP_END:
               result=Doblinkend(doc);
               break;
            /* MARQUEE disabled (Domarquee/Domarqueeend #if 0)
            case MARKUP_MARQUEE:
               result=Domarquee(doc,ta);
               break;
            case MARKUP_MARQUEE|MARKUP_END:
               result=Domarqueeend(doc);
               break;
            */
            case MARKUP_BR:
               result=Dobr(doc,ta);
               break;
            case MARKUP_CENTER:
               result=Docenter(doc,ta);
               break;
            case MARKUP_CENTER|MARKUP_END:
               result=Dodivend(doc);
               break;
            case MARKUP_DIV:
               result=Dodiv(doc,ta);
               break;
            case MARKUP_DIV|MARKUP_END:
               result=Dodivend(doc);
               break;
            case MARKUP_NOBR:
               result=Donobr(doc,ta);
               break;
            case MARKUP_NOBR|MARKUP_END:
               result=Donobrend(doc);
               break;
            case MARKUP_P:
               result=Dopara(doc,ta);
               break;
            case MARKUP_P|MARKUP_END:
               result=Doparaend(doc);
               break;
            case MARKUP_TEXT:
               if(doc->pflags&DPF_JSCRIPT)
               {  result=Dojsource(doc,ta);
               }
               else
               {  result=Dotext(doc,ta);
               }
               break;
            case MARKUP_WBR:
               result=Dowbr(doc,ta);
               break;
         /*---- preformatted ----*/
            case MARKUP_PRE:
               result=Dopre(doc,ta);
               break;
            case MARKUP_LISTING:
               result=Dolisting(doc,ta);
               break;
            case MARKUP_XMP:
               result=Doxmp(doc,ta);
               break;
            case MARKUP_PRE|MARKUP_END:
            case MARKUP_LISTING|MARKUP_END:
            case MARKUP_XMP|MARKUP_END:
               result=Dopreend(doc);
               break;
         /*---- hard styles ----*/
            case MARKUP_B:
               result=Dohardstyle(doc,ta,FSF_BOLD,TRUE);
               break;
            case MARKUP_B|MARKUP_END:
               result=Dohardstyle(doc,ta,FSF_BOLD,FALSE);
               break;
            case MARKUP_I:
               result=Dohardstyle(doc,ta,FSF_ITALIC,TRUE);
               break;
            case MARKUP_I|MARKUP_END:
               result=Dohardstyle(doc,ta,FSF_ITALIC,FALSE);
               break;
            case MARKUP_STRIKE:
               result=Dohardstyle(doc,ta,FSF_STRIKE,TRUE);
               break;
            case MARKUP_STRIKE|MARKUP_END:
               result=Dohardstyle(doc,ta,FSF_STRIKE,FALSE);
               break;
            case MARKUP_TT:
               result=Dott(doc,ta,TRUE);
               break;
            case MARKUP_TT|MARKUP_END:
               result=Dott(doc,ta,FALSE);
               break;
            case MARKUP_U:
               result=Dohardstyle(doc,ta,FSF_UNDERLINED,TRUE);
               break;
            case MARKUP_U|MARKUP_END:
               result=Dohardstyle(doc,ta,FSF_UNDERLINED,FALSE);
               break;
            case MARKUP_INS:
               result=Doins(doc,ta);
               break;
            case MARKUP_INS|MARKUP_END:
               result=Doinsend(doc);
               break;
            case MARKUP_DEL:
               result=Dodel(doc,ta);
               break;
            case MARKUP_DEL|MARKUP_END:
               result=Dodelend(doc);
               break;
         /*---- logical styles ----*/
            case MARKUP_CITE:
            case MARKUP_CODE:
            case MARKUP_DFN:
            case MARKUP_EM:
            case MARKUP_KBD:
            case MARKUP_SAMP:
            case MARKUP_STRONG:
            case MARKUP_VAR:
               result=Dostyle(doc,ta,STYLE_CITE+(tagtype-MARKUP_CITE));
               break;
            case MARKUP_BIG:
               result=Dostyle(doc,ta,STYLE_BIG);
               break;
            case MARKUP_SMALL:
               result=Dostyle(doc,ta,STYLE_SMALL);
               break;
            case MARKUP_CITE|MARKUP_END:
            case MARKUP_CODE|MARKUP_END:
            case MARKUP_DFN|MARKUP_END:
            case MARKUP_EM|MARKUP_END:
            case MARKUP_KBD|MARKUP_END:
            case MARKUP_SAMP|MARKUP_END:
            case MARKUP_STRONG|MARKUP_END:
            case MARKUP_VAR|MARKUP_END:
            case MARKUP_BIG|MARKUP_END:
            case MARKUP_SMALL|MARKUP_END:
               result=Dostyleend(doc);
               break;
            case MARKUP_SPAN:
               result=Dospan(doc,ta);
               break;
            case MARKUP_SPAN|MARKUP_END:
               result=Dospanend(doc);
               break;
            case MARKUP_ADDRESS:
               result=Doaddress(doc,ta);
               break;
            case MARKUP_ADDRESS|MARKUP_END:
               result=Doaddressend(doc);
               break;
            case MARKUP_BLOCKQUOTE:
               result=Doblockquote(doc,ta);
               break;
            case MARKUP_BLOCKQUOTE|MARKUP_END:
               result=Doblockquoteend(doc);
               break;
            case MARKUP_H1:
            case MARKUP_H2:
            case MARKUP_H3:
            case MARKUP_H4:
            case MARKUP_H5:
            case MARKUP_H6:
               result=Doheading(doc,tagtype-MARKUP_H1,ta);
               break;
            case MARKUP_H1|MARKUP_END:
            case MARKUP_H2|MARKUP_END:
            case MARKUP_H3|MARKUP_END:
            case MARKUP_H4|MARKUP_END:
            case MARKUP_H5|MARKUP_END:
            case MARKUP_H6|MARKUP_END:
               result=Doheadingend(doc);
               break;
            case MARKUP_SUB:
               result=Dosub(doc,ta);
               break;
            case MARKUP_SUB|MARKUP_END:
               result=Dosubend(doc);
               break;
            case MARKUP_SUP:
               result=Dosup(doc,ta);
               break;
            case MARKUP_SUP|MARKUP_END:
               result=Dosupend(doc);
               break;
         /*---- font ----*/
            case MARKUP_BASEFONT:
               result=Dobasefont(doc,ta);
               break;
            case MARKUP_FONT:
               result=Dofont(doc,ta);
               break;
            case MARKUP_FONT|MARKUP_END:
               result=Dofontend(doc);
               break;
         /*---- ruler ----*/
            case MARKUP_HR:
               result=Dohr(doc,ta);
               break;
         /*---- anchor ----*/
            case MARKUP_A:
               result=Doanchor(doc,ta);
               break;
            case MARKUP_A|MARKUP_END:
               result=Doanchorend(doc);
               break;
         /*---- frames ----*/
            case MARKUP_FRAME:
               result=Doframe(doc,ta);
               break;
            case MARKUP_FRAMESET:
               result=Doframeset(doc,ta);
               break;
            case MARKUP_IFRAME:
               result=Doiframe(doc,ta);
               break;
         /*---- images ----*/
            case MARKUP_IMG:
               result=Doimg(doc,ta);
               break;
            case MARKUP_ICON:
               result=Doicon(doc,ta);
               break;
            case MARKUP_MAP:
               result=Domap(doc,ta);
               break;
         /*---- lists ----*/
            case MARKUP_DD:
               result=Dodd(doc,ta);
               break;
            case MARKUP_DL:
               result=Dodl(doc,ta);
               break;
            case MARKUP_DL|MARKUP_END:
               result=Dodlend(doc);
               break;
            case MARKUP_DT:
               result=Dodt(doc,ta);
               break;
            case MARKUP_LI:
               result=Doli(doc,ta);
               break;
            case MARKUP_OL:
               result=Dool(doc,ta);
               break;
            case MARKUP_OL|MARKUP_END:
               result=Doolend(doc);
               break;
            case MARKUP_UL:
            case MARKUP_DIR:
            case MARKUP_MENU:
               result=Doul(doc,ta);
               break;
            case MARKUP_UL|MARKUP_END:
            case MARKUP_DIR|MARKUP_END:
            case MARKUP_MENU|MARKUP_END:
               result=Doulend(doc);
               break;
         /*---- tables ----*/
            case MARKUP_TABLE:
               result=Dotable(doc,ta);
               break;
            case MARKUP_TABLE|MARKUP_END:
               result=Dotableend(doc);
               break;
            case MARKUP_COLGROUP:
            case MARKUP_COL:
               result=Docolgrouporcol(doc,ta,tagtype);
               break;
            case MARKUP_COLGROUP|MARKUP_END:
               result=Docolgroupend(doc);
               break;
            case MARKUP_THEAD:
            case MARKUP_TFOOT:
            case MARKUP_TBODY:
               result=Dorowgroup(doc,ta,tagtype);
               break;
            case MARKUP_THEAD|MARKUP_END:
            case MARKUP_TFOOT|MARKUP_END:
            case MARKUP_TBODY|MARKUP_END:
               result=Dorowgroupend(doc,tagtype&~MARKUP_END);
               break;
            case MARKUP_TR:
               result=Dotr(doc,ta);
               break;
            case MARKUP_TR|MARKUP_END:
               result=Dotrend(doc);
               break;
            case MARKUP_TD:
               result=Dotd(doc,ta,FALSE);
               break;
            case MARKUP_TH:
               result=Dotd(doc,ta,TRUE);
               break;
            case MARKUP_TD|MARKUP_END:
            case MARKUP_TH|MARKUP_END:
               result=Dotdend(doc);
               break;
            case MARKUP_CAPTION:
               result=Docaption(doc,ta);
               break;
            case MARKUP_CAPTION|MARKUP_END:
               result=Docaptionend(doc);
               break;
         /*---- sound ----*/
            case MARKUP_BGSOUND:
               result=Dobgsound(doc,ta);
               break;
         /*---- forms ----*/
            case MARKUP_FORM:
               result=Doform(doc,ta);
               break;
            case MARKUP_FORM|MARKUP_END:
               result=Doformend(doc);
               break;
            case MARKUP_FIELDSET:
               result=Dofieldset(doc,ta);
               break;
            case MARKUP_FIELDSET|MARKUP_END:
               result=Dofieldsetend(doc);
               break;
            case MARKUP_LEGEND:
               result=Dolegend(doc,ta);
               break;
            case MARKUP_LEGEND|MARKUP_END:
               result=Dolegendend(doc);
               break;
            case MARKUP_BUTTON:
               result=Dobutton(doc,ta);
               break;
            case MARKUP_BUTTON|MARKUP_END:
               result=Dobuttonend(doc);
               break;
            case MARKUP_INPUT:
               result=Doinput(doc,ta);
               break;
            case MARKUP_ISINDEX:
               result=Doisindex(doc,ta);
               break;
            case MARKUP_OPTION:
               result=Dooption(doc,ta);
               break;
            case MARKUP_SELECT:
               result=Doselect(doc,ta);
               break;
            case MARKUP_SELECT|MARKUP_END:
               result=Doselectend(doc);
               break;
            case MARKUP_TEXTAREA:
               result=Dotextarea(doc,ta);
               break;
         /*---- object ----*/
            case MARKUP_OBJECT:
               result=Doobject(doc,ta);
               break;
            case MARKUP_EMBED:
               result=Doembed(doc,ta);
               break;
         /*---- unknown ----*/
            case MARKUP_UNKNOWN:
               Checkid(doc,ta);
               break;

         /*----  ----*/
         /*----  ----*/
         /*----  ----*/
         }
   }
   return result;
}
