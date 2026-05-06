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
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * AWeb Public License for more details.
 *
 **********************************************************************/

/* css.c - AWeb CSS parsing and application */

#include <proto/exec.h>
#include <proto/utility.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "aweb.h"
#include "css.h"
#include "docprivate.h"
#include "element.h"
#include "body.h"
#include "html.h"
#include "link.h"
#include "table.h"
#include "copy.h"
#include "colours.h"
#include "url.h"
#include "window.h"

/* COLOR macro - extract pen number from Colorinfo */
#define COLOR(ci) ((ci)?((ci)->pen):(-1))

/* WAF pages ship multi-megabyte CSS. Uncapped rules make ApplyCSSToElement allocate
 * one sort node per match per layout object and blow chip RAM; ApplyCSSToBody also
 * scans the full list per node. Truncate for stability; styling is best-effort. */
#define CSS_SHEET_MAX_RULES 4096L
#define CSS_ELEMENT_MAX_MATCH_NODES 512L

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

/* Forward declarations */
static struct CSSStylesheet* ParseCSS(struct Document *doc,UBYTE *css,long cssBound);
static struct CSSRule* ParseRule(struct Document *doc,UBYTE **p);
static struct CSSSelector* ParseSelector(struct Document *doc,UBYTE **p);
static struct CSSProperty* ParseProperty(struct Document *doc,UBYTE **p);
static void SkipComment(UBYTE **p);
static UBYTE* ParseIdentifier(UBYTE **p);
static UBYTE* ParseValue(UBYTE **p);
static BOOL MatchSelector(struct CSSSelector *sel,void *element);
static void ApplyProperty(struct Document *doc,void *element,struct CSSProperty *prop);
static void FreeCSSRule(struct CSSRule *rule);
/* Static document pointer for hover state checking during CSS matching */
static struct Document *currentCSSDoc = NULL;
static void FreeCSSStylesheetInternal(struct CSSStylesheet *sheet);
void MergeCSSStylesheet(struct Document *doc,UBYTE *css);
void SkipWhitespace(UBYTE **p);
long ParseCSSLengthValue(UBYTE *value,struct Number *num);
ULONG ParseHexColor(UBYTE *pcolor);

/* Simple debug printf - only output if httpdebug is enabled */
extern BOOL httpdebug;

static void css_debug_printf(const char *format, ...)
{  va_list args;
   if(!httpdebug) return;
   va_start(args, format);
   printf("[CSS] ");
   vprintf(format, args);
   va_end(args);
}

static long CssRuleCount(struct Document *doc)
{  struct CSSStylesheet *sheet;
   struct CSSRule *rule;
   long cnt;
   if(!doc || !doc->cssstylesheet) return 0;
   sheet=(struct CSSStylesheet *)doc->cssstylesheet;
   cnt=0;
   for(rule=(struct CSSRule *)sheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule=(struct CSSRule *)rule->node.mln_Succ)
   {  cnt++;
   }
   return cnt;
}

void MarkDocCssDirty(struct Document *doc)
{  if(!doc) return;
   doc->cssserial++;
}

void ApplyDocCssIfReady(struct Document *doc)
{  if(!doc) return;
   if(!doc->cssstylesheet) return;
   if(!doc->body) return;
   if(!doc->frame) return;
   if(doc->cssappliedserial == doc->cssserial) return;
   ApplyCSSToLinkColors(doc);
   ApplyCSSToBody(doc,doc->body,NULL,NULL,"BODY");
   ReapplyCSSToAllElements(doc);
   if(doc->win && doc->frame)
   {  Registerdoccolors(doc);
   }
   doc->cssappliedserial = doc->cssserial;
   if(doc->win)
   {  long ruleCount;
      UBYTE stmsg[STATUSBUFSIZE];
      ruleCount=CssRuleCount(doc);
      sprintf((char *)stmsg,"CSS applied (%ld rules)",ruleCount);
      Asetattrs(doc->win,AOWIN_Status,stmsg,TAG_END);
   }
   /* Layout/repaint is driven by Srcupdatedocument (or explicit resume after
    * DPF_EXTCSSEXPECT), not here, so fast loads do not miss a single coalesced refresh. */
}

/* Parse a CSS stylesheet.
 * cssNBytes: number of CSS text bytes before the terminating NUL (from docsource buffer),
 *            or -1 to use strlen(css) (e.g. merged or @import payload). */
void ParseCSSStylesheet(struct Document *doc,UBYTE *css,long cssNBytes)
{  struct CSSStylesheet *sheet;
   if(!doc || !css) return;
   
   /* debug_printf("CSS: ParseCSSStylesheet called, css length=%ld\n",strlen((char *)css)); */
   
   /* On reload, always replace the stylesheet instead of merging.
    * Check if this is a reload by checking if DPF_RELOADVERIFY is set
    * and stylesheet exists - if so, free it first to start fresh. */
   if(doc->cssstylesheet && (doc->pflags & DPF_RELOADVERIFY))
   {  /* This is a reload - free existing stylesheet and start fresh */
      FreeCSSStylesheet(doc);
   }
   
   /* If there's an existing stylesheet (and not a reload), merge instead of replace */
   if(doc->cssstylesheet)
   {  MergeCSSStylesheet(doc,css);
      return;
   }
   
   /* Parse CSS */
   sheet = ParseCSS(doc,css,cssNBytes);
   if(sheet)
   {  struct CSSRule *rule;
      struct CSSSelector *sel;
      long ruleCount = 0;
      /* If @import (or similar) assigned doc->cssstylesheet while ParseCSS still built
       * a separate outer sheet, fold outer rules into the installed sheet and drop the shell. */
      if(doc->cssstylesheet && (void *)sheet != doc->cssstylesheet)
      {  struct CSSStylesheet *existingSheet;
         existingSheet = (struct CSSStylesheet *)doc->cssstylesheet;
         while((rule = (struct CSSRule *)REMHEAD(&sheet->rules)))
         {  ADDTAIL(&existingSheet->rules, rule);
         }
         FREE(sheet);
         sheet = existingSheet;
      }
      doc->cssstylesheet = (void *)sheet;
      MarkDocCssDirty(doc);
      /* Count rules and log selectors */
      for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
          (struct MinNode *)rule->node.mln_Succ;
          rule = (struct CSSRule *)rule->node.mln_Succ)
      {  ruleCount++;
         /* Log first selector of each rule for debugging */
         sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         if((struct MinNode *)sel->node.mln_Succ)
         {  /* debug_printf("CSS: Rule %ld: selector type=0x%lx name=%s class=%s id=%s\n",
                        ruleCount,
                        (ULONG)sel->type,
                        (sel->name ? (char *)sel->name : "NULL"),
                        (sel->class ? (char *)sel->class : "NULL"),
                        (sel->id ? (char *)sel->id : "NULL")); */
         }
      }
      /* debug_printf("CSS: Stylesheet parsed successfully, %ld rules\n",ruleCount); */
   }
   else
   {  /* debug_printf("CSS: Stylesheet parsing failed\n"); */
   }
}

/* Merge external CSS stylesheet with existing stylesheet */
void MergeCSSStylesheet(struct Document *doc,UBYTE *css)
{  struct CSSStylesheet *existingSheet;
   struct CSSStylesheet *newSheet;
   struct CSSRule *rule;
   struct CSSSelector *sel;
   long ruleCount;
   long existingCount;
   UBYTE stmsg[STATUSBUFSIZE];
   
   if(!doc || !css) return;
   
   if(httpdebug)
   {  printf("[CSS] MergeCSSStylesheet: Starting merge\n");
   }
   /* Parse the new CSS */
   newSheet = ParseCSS(doc,css,-1);
   if(!newSheet)
   {  css_debug_printf("MergeCSSStylesheet: ERROR - ParseCSS failed\n");
      return;
   }
   
   css_debug_printf("MergeCSSStylesheet: ParseCSS succeeded, merging into existing stylesheet\n");
   
   /* Count rules in new sheet and log selectors */
   ruleCount = 0;
   for(rule = (struct CSSRule *)newSheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  ruleCount++;
      /* Log first selector of each rule for debugging */
      sel = (struct CSSSelector *)rule->selectors.mlh_Head;
      if((struct MinNode *)sel->node.mln_Succ)
      {  /* debug_printf("MergeCSSStylesheet: New rule %ld: selector type=0x%lx name=%s class=%s id=%s\n",
                     ruleCount,
                     (ULONG)sel->type,
                     (sel->name ? (char *)sel->name : "NULL"),
                     (sel->class ? (char *)sel->class : "NULL"),
                     (sel->id ? (char *)sel->id : "NULL")); */
      }
   }
   /* debug_printf("MergeCSSStylesheet: Parsed %ld rules from new CSS\n",ruleCount); */
   
   /* If no existing stylesheet, just use the new one */
   if(!doc->cssstylesheet)
   {  doc->cssstylesheet = (void *)newSheet;
      MarkDocCssDirty(doc);
      if(doc->win)
      {  sprintf((char *)stmsg,"CSS applied (%ld rules)",ruleCount);
         Asetattrs(doc->win,AOWIN_Status,stmsg,TAG_END);
      }
      /* debug_printf("MergeCSSStylesheet: No existing sheet, using new one\n"); */
      return;
   }
   
   /* Merge: append rules from new sheet to existing sheet */
   existingSheet = (struct CSSStylesheet *)doc->cssstylesheet;
   existingCount = 0;
   for(rule = (struct CSSRule *)existingSheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  existingCount++;
   }
   while((rule = (struct CSSRule *)REMHEAD(&newSheet->rules)))
   {  if(existingCount >= CSS_SHEET_MAX_RULES)
      {  FreeCSSRule(rule);
      }
      else
      {  ADDTAIL(&existingSheet->rules,rule);
         existingCount++;
      }
   }
   
   /* Free the empty newSheet structure */
   FREE(newSheet);
   
   /* Count total rules after merge */
   ruleCount = 0;
   for(rule = (struct CSSRule *)existingSheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  ruleCount++;
   }
   css_debug_printf("MergeCSSStylesheet: Merge completed, total rules=%ld\n", ruleCount);
   MarkDocCssDirty(doc);
   if(doc->win)
   {  sprintf((char *)stmsg,"CSS merged (%ld rules total)",ruleCount);
      Asetattrs(doc->win,AOWIN_Status,stmsg,TAG_END);
   }
}

/* Parse CSS content */
static struct CSSStylesheet* ParseCSS(struct Document *doc,UBYTE *css,long cssBound)
{  struct CSSStylesheet *sheet;
   struct CSSStylesheet *importSheet;
   struct CSSRule *rule;
   UBYTE *p;
   UBYTE *cssEnd;
   long cssLen;
   long ruleCount = 0;
   long iterationCount = 0;
   UBYTE *cssStart;
   
   if(!doc || !css) return NULL;
   
   importSheet = NULL;
   cssStart = css;
   if(cssBound < 0)
   {  cssLen = (long)strlen((char *)css);
   }
   else
   {  cssLen = cssBound;
   }
   cssEnd = cssStart + cssLen;
   css_debug_printf("ParseCSS: Starting, CSS length=%ld bytes\n", cssLen);
   
   sheet = ALLOCSTRUCT(CSSStylesheet,1,MEMF_FAST);
   if(!sheet)
   {  css_debug_printf("ParseCSS: Failed to allocate stylesheet\n");
      return NULL;
   }
   
   NEWLIST(&sheet->rules);
   sheet->pool = doc->pool;
   
   p = cssStart;
   
   /* Skip UTF-8 BOM if present (0xEF 0xBB 0xBF); do not read past cssEnd. */
   if(cssLen >= 3 && p + 3 <= cssEnd && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
   {  p += 3;
      css_debug_printf("ParseCSS: Skipped UTF-8 BOM\n");
   }
   
   {  long lastPosition = -1;
      long stuckCount = 0;
      
      while(p < cssEnd && *p)
      {  UBYTE *oldp;
         long position;
         UBYTE *contextStart;
         UBYTE *contextEnd;
         long contextLen;
         UBYTE context[101];
         long j;
         
         iterationCount++;
         
         if(iterationCount % 100 == 0)
         {  long pct;
            position = p - cssStart;
            pct = 0;
            if(cssLen > 0)
            {  pct = (100L * position) / cssLen;
            }
            /* Integer percent only: avoid double literals (FPU opcodes on EC020 without FPU). */
            css_debug_printf("ParseCSS: Iteration %ld, position %ld/%ld (%ld%%), rules parsed=%ld\n",
                            iterationCount, position, cssLen, pct, ruleCount);
         }
         
         /* Hard limit: if we've done more than 100000 iterations, something is very wrong */
         if(iterationCount > 100000)
         {  css_debug_printf("ParseCSS: FATAL - Exceeded 100000 iterations, breaking to prevent lockup\n");
            break;
         }
         
         oldp = p;
         SkipWhitespace(&p);
         SkipComment(&p);
         if(!*p) break;
         
         position = p - cssStart;
         
         /* Detect if we're stuck at the same position (after skipping whitespace) */
         if(position == lastPosition)
         {  stuckCount++;
            if(stuckCount > 10)
            {  /* Show context around stuck position */
               contextStart = (position > 50) ? p - 50 : cssStart;
               contextEnd = p + 50;
               if(contextEnd > cssEnd) contextEnd = cssEnd;
               contextLen = contextEnd - contextStart;
               if(contextLen > 100) contextLen = 100;
               for(j = 0; j < contextLen && contextStart + j < cssEnd && contextStart[j]; j++)
               {  context[j] = (contextStart[j] >= 32 && contextStart[j] < 127) ? contextStart[j] : '.';
               }
               context[j] = '\0';
               css_debug_printf("ParseCSS: STUCK - Same position %ld for %ld iterations!\n",
                              position, stuckCount);
               css_debug_printf("ParseCSS: Context around stuck position: '%.100s'\n", context);
               css_debug_printf("ParseCSS: Breaking to prevent infinite loop\n");
               break;
            }
         }
         else
         {  stuckCount = 0;
            lastPosition = position;
         }
      
         /* Check for @media query - skip it for now (basic support) */
         if(*p == '@' && (cssEnd - p) >= 6 && Strnicmp((char *)p, "@media", 6) == 0)
         {  css_debug_printf("ParseCSS: Found @media query at position %ld, skipping\n", p - cssStart);
            p += 6; /* Skip "@media" */
            SkipWhitespace(&p);
            /* Skip media query list until opening brace */
            while(p < cssEnd && *p && *p != '{')
            {  p++;
            }
            if(p < cssEnd && *p == '{')
            {  long braceDepth = 1;
               long mediaStart = p - cssStart;
               p++; /* Skip opening brace */
               /* Skip entire @media block */
               while(p < cssEnd && *p && braceDepth > 0)
               {  if(*p == '{') braceDepth++;
                  else if(*p == '}') braceDepth--;
                  p++;
               }
               css_debug_printf("ParseCSS: Skipped @media block from position %ld to %ld\n",
                              mediaStart, p - cssStart);
            }
            /* Update position after skipping @media block */
            position = p - cssStart;
            lastPosition = position;
            stuckCount = 0;
            continue; /* Skip to next rule */
         }
         
         /* Check for @import rule - process it to load external stylesheets */
         if(*p == '@' && (cssEnd - p) >= 7 && Strnicmp((char *)p, "@import", 7) == 0)
         {  UBYTE *importStart;
            UBYTE *urlStart;
            UBYTE *urlEnd;
            UBYTE *importUrl;
            void *url;
            UBYTE *extcss;
            long urlLen;
            UBYTE quote;
            
            css_debug_printf("ParseCSS: Found @import rule at position %ld\n", p - cssStart);
            p += 7; /* Skip "@import" */
            SkipWhitespace(&p);
            
            /* Parse @import url(...) or @import "..." format */
            if(*p == 'u' && (cssEnd - p) >= 4 && Strnicmp((char *)p, "url(", 4) == 0)
            {  p += 4; /* Skip "url(" */
               SkipWhitespace(&p);
               urlStart = p;
               /* Check for quoted URL */
               if(*p == '"' || *p == '\'')
               {  quote = *p;
                  urlStart = ++p; /* Skip opening quote */
                  urlEnd = p;
                  while(urlEnd < cssEnd && *urlEnd && *urlEnd != quote)
                  {  if(*urlEnd == '\\' && (urlEnd + 1) < cssEnd && urlEnd[1]) urlEnd += 2; /* Skip escaped char */
                     else urlEnd++;
                  }
                  if(urlEnd < cssEnd && *urlEnd == quote)
                  {  urlLen = urlEnd - urlStart;
                     urlEnd++; /* Skip closing quote */
                  }
                  else
                  {  urlLen = 0; /* Unclosed quote */
                  }
                  p = urlEnd;
               }
               else
               {  /* Unquoted URL - parse until closing paren */
                  urlEnd = p;
                  while(urlEnd < cssEnd && *urlEnd && *urlEnd != ')' && !isspace((unsigned char)*urlEnd))
                  {  urlEnd++;
                  }
                  urlLen = urlEnd - urlStart;
                  p = urlEnd;
               }
               SkipWhitespace(&p);
               if(p < cssEnd && *p == ')') p++; /* Skip closing paren */
            }
            else if(*p == '"' || *p == '\'')
            {  /* @import "url" format */
               quote = *p;
               urlStart = ++p; /* Skip opening quote */
               urlEnd = p;
               while(urlEnd < cssEnd && *urlEnd && *urlEnd != quote)
               {  if(*urlEnd == '\\' && (urlEnd + 1) < cssEnd && urlEnd[1]) urlEnd += 2; /* Skip escaped char */
                  else urlEnd++;
               }
               if(urlEnd < cssEnd && *urlEnd == quote)
               {  urlLen = urlEnd - urlStart;
                  urlEnd++; /* Skip closing quote */
               }
               else
               {  urlLen = 0; /* Unclosed quote */
               }
               p = urlEnd;
            }
            else
            {  /* Invalid @import format - skip to semicolon */
               css_debug_printf("ParseCSS: Invalid @import format, skipping\n");
               while(p < cssEnd && *p && *p != ';') p++;
               if(p < cssEnd && *p == ';') p++;
               position = p - cssStart;
               lastPosition = position;
               stuckCount = 0;
               continue;
            }
            
            /* Extract URL and load external stylesheet */
            if(urlLen > 0)
            {  importUrl = ALLOCTYPE(UBYTE, urlLen + 1, MEMF_FAST);
               if(importUrl)
               {  memmove(importUrl, urlStart, urlLen);
                  importUrl[urlLen] = '\0';
                  
                  /* Resolve URL relative to document base */
                  url = Findurl(doc->base, importUrl, 0);
                  if(url)
                  {  /* Try to load external CSS */
                     extcss = Finddocext(doc, url, FALSE);
                     if(extcss && extcss != (UBYTE *)~0)
                     {  /* CSS loaded synchronously: splice into this ParseCSS's `sheet`.
                         * Do not use MergeCSSStylesheet — it sets doc->cssstylesheet while
                         * ParseCSSStylesheet still expects to assign the outer `sheet`, which
                         * overwrote the doc pointer and corrupted rule lists (crash/illegal inst). */
                        css_debug_printf("ParseCSS: @import CSS loaded synchronously, merging\n");
                        importSheet = ParseCSS(doc, extcss, -1);
                        if(importSheet)
                        {  while((rule = (struct CSSRule *)REMHEAD(&importSheet->rules)))
                           {  ADDTAIL(&sheet->rules, rule);
                           }
                           FREE(importSheet);
                           MarkDocCssDirty(doc);
                        }
                        ApplyCSSToLinkColors(doc);
                     }
                     else if(extcss == NULL)
                     {  /* CSS loading asynchronously - will be merged via AODOC_Docextready */
                        css_debug_printf("ParseCSS: @import CSS loading asynchronously, will merge when ready\n");
                     }
                     else
                     {  /* Error loading CSS */
                        css_debug_printf("ParseCSS: @import CSS load failed\n");
                     }
                  }
                  else
                  {  css_debug_printf("ParseCSS: @import URL resolution failed: %s\n", importUrl);
                  }
                  FREE(importUrl);
               }
            }
            
            /* Skip to semicolon */
            SkipWhitespace(&p);
            if(p < cssEnd && *p == ';') p++;
            
            /* Update position after processing @import */
            position = p - cssStart;
            lastPosition = position;
            stuckCount = 0;
            continue; /* Skip to next rule */
         }
         
         /* Check for other @ rules (@charset, etc.) - skip them */
         if(*p == '@')
         {  UBYTE *atRuleStart = p;
            UBYTE atRuleName[32];
            long i = 0;
            /* Extract @ rule name for debugging */
            p++; /* Skip '@' */
            while(p < cssEnd && *p && i < 31 && (isalpha((unsigned char)*p) || *p == '-' || *p == '_'))
            {  atRuleName[i++] = *p++;
            }
            atRuleName[i] = '\0';
            css_debug_printf("ParseCSS: Found @ rule '%s' at position %ld, skipping\n",
                            atRuleName, atRuleStart - cssStart);
            
            /* Skip to semicolon or opening brace */
            while(p < cssEnd && *p && *p != ';' && *p != '{')
            {  p++;
            }
            if(p < cssEnd && *p == ';')
            {  p++;
               /* Update position after skipping @ rule */
               position = p - cssStart;
               lastPosition = position;
               stuckCount = 0;
               continue;
            }
            else if(p < cssEnd && *p == '{')
            {  long braceDepth = 1;
               p++;
               while(p < cssEnd && *p && braceDepth > 0)
               {  if(*p == '{') braceDepth++;
                  else if(*p == '}') braceDepth--;
                  p++;
               }
               /* Update position after skipping @ rule block */
               position = p - cssStart;
               lastPosition = position;
               stuckCount = 0;
               continue;
            }
         }
         
         oldp = p;  /* Remember position before parsing */
         rule = ParseRule(doc,&p);
         if(rule)
         {  ruleCount++;
            ADDTAIL(&sheet->rules,rule);
            if(ruleCount % 50 == 0)
            {  css_debug_printf("ParseCSS: Parsed %ld rules so far, position %ld/%ld\n",
                               ruleCount, p - cssStart, cssLen);
            }
            if(ruleCount >= CSS_SHEET_MAX_RULES)
            {  css_debug_printf("ParseCSS: Rule cap %ld reached, ignoring remainder of stylesheet\n",
                               (long)CSS_SHEET_MAX_RULES);
               break;
            }
         }
         else
         {  /* Parse error - make sure we advance the pointer */
            if(p == oldp)
            {  /* Pointer didn't advance - skip one character to prevent infinite loop */
               {  /* Show context around the problematic character */
                  UBYTE *contextStart2;
                  UBYTE *contextEnd2;
                  long contextLen2;
                  UBYTE context2[41];
                  long j2;
                  
                  contextStart2 = (p - cssStart > 20) ? p - 20 : cssStart;
                  contextEnd2 = p + 20;
                  if(contextEnd2 > cssEnd) contextEnd2 = cssEnd;
                  contextLen2 = contextEnd2 - contextStart2;
                  if(contextLen2 > 40) contextLen2 = 40;
                  for(j2 = 0; j2 < contextLen2 && contextStart2 + j2 < cssEnd && contextStart2[j2]; j2++)
                  {  context2[j2] = (contextStart2[j2] >= 32 && contextStart2[j2] < 127) ? contextStart2[j2] : '.';
                  }
                  context2[j2] = '\0';
                  css_debug_printf("ParseCSS: WARNING - ParseRule failed and pointer didn't advance at position %ld (char=0x%02x '%c')\n",
                                 p - cssStart, *p, (*p >= 32 && *p < 127) ? *p : '?');
                  css_debug_printf("ParseCSS: Context: '%.40s'\n", context2);
               }
               if(p < cssEnd && *p) p++;
               else break;  /* End of string */
            }
            else
            {  /* Pointer advanced but rule failed - skip to next rule */
               css_debug_printf("ParseCSS: ParseRule failed but pointer advanced, skipping to next rule (pos %ld->%ld)\n",
                              oldp - cssStart, p - cssStart);
               while(p < cssEnd && *p && *p != '}')
               {  p++;
               }
               if(p < cssEnd && *p == '}') p++;
            }
         }
         
         /* Final safety check: if we haven't advanced at all, force advance and break */
         if(p == oldp)
         {  css_debug_printf("ParseCSS: ERROR - Pointer stuck at position %ld, forcing advance\n", p - cssStart);
            if(p < cssEnd && *p) p++;
            else break;
            /* If we're still at the same position after forcing advance, something is wrong - break */
            if(p == oldp)
            {  css_debug_printf("ParseCSS: FATAL - Pointer still stuck after force advance, breaking\n");
               break;
            }
         }
         
         /* Safety check: if we've been parsing for too long, warn */
         if(iterationCount > 10000)
         {  css_debug_printf("ParseCSS: WARNING - High iteration count (%ld), position %ld/%ld\n",
                            iterationCount, p - cssStart, cssLen);
         }
      }
   }
   
   css_debug_printf("ParseCSS: Completed, parsed %ld rules in %ld iterations, final position %ld/%ld\n",
                   ruleCount, iterationCount, p - cssStart, cssLen);
   return sheet;
}

/* Parse a CSS rule */
static struct CSSRule* ParseRule(struct Document *doc,UBYTE **p)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   UBYTE *ruleStart;
   long selectorCount = 0;
   long propertyCount = 0;
   
   if(!doc || !p || !*p) return NULL;
   
   ruleStart = *p;
   rule = ALLOCSTRUCT(CSSRule,1,MEMF_FAST);
   if(!rule)
   {  css_debug_printf("ParseRule: Failed to allocate rule\n");
      return NULL;
   }
   
   NEWLIST(&rule->selectors);
   NEWLIST(&rule->properties);
   
   /* Parse selectors */
   {  long selIterationCount = 0;
      while(**p)
      {  UBYTE *oldp;
         
         selIterationCount++;
         if(selIterationCount > 1000)
         {  css_debug_printf("ParseRule: WARNING - Selector parsing loop exceeded 1000 iterations, breaking\n");
            break;
         }
         
         SkipWhitespace(p);
         SkipComment(p);
         if(**p == '{') break;
         if(**p == ';' || **p == '}') return NULL; /* Invalid */
         
         oldp = *p;  /* Remember position before parsing */
         sel = ParseSelector(doc,p);
         if(sel)
         {  selectorCount++;
            ADDTAIL(&rule->selectors,sel);
         }
         else
         {  /* Parse error in selector - make sure pointer advanced */
            if(*p == oldp)
            {  /* Pointer didn't advance - skip one character to prevent infinite loop */
               css_debug_printf("ParseRule: WARNING - ParseSelector failed and pointer didn't advance (pos %ld, char=0x%02x)\n",
                              *p - ruleStart, **p);
               (*p)++;
            }
            else
            {  css_debug_printf("ParseRule: ParseSelector failed but pointer advanced (pos %ld->%ld)\n",
                               oldp - ruleStart, *p - ruleStart);
            }
            FreeCSSRule(rule);
            return NULL;
         }
         
         SkipWhitespace(p);
         if(**p == ',')
         {  (*p)++;
            continue;
         }
         if(**p == '{') break;
      }
   }
   
   if(**p != '{')
   {  css_debug_printf("ParseRule: Expected '{' but found 0x%02x at position %ld\n",
                      **p, *p - ruleStart);
      FreeCSSRule(rule);
      return NULL;
   }
   (*p)++; /* Skip '{' */
   
   css_debug_printf("ParseRule: Parsed %ld selector(s), starting properties at position %ld\n",
                   selectorCount, *p - ruleStart);
   
   /* Parse properties */
   {  long propIterationCount = 0;
      while(**p)
      {  UBYTE *propOldp;
         
         propIterationCount++;
         if(propIterationCount > 1000)
         {  css_debug_printf("ParseRule: WARNING - Property parsing loop exceeded 1000 iterations, breaking\n");
            break;
         }
         
         SkipWhitespace(p);
         SkipComment(p);
         if(**p == '}') break;
         
         propOldp = *p;  /* Remember position before parsing property */
         prop = ParseProperty(doc,p);
         if(prop)
         {  propertyCount++;
            ADDTAIL(&rule->properties,prop);
         }
         else
         {  /* Parse error - skip to next semicolon or closing brace */
            if(*p == propOldp)
            {  /* Pointer didn't advance - force advance */
               css_debug_printf("ParseRule: WARNING - ParseProperty failed and pointer didn't advance (pos %ld, char=0x%02x)\n",
                              *p - ruleStart, **p);
               if(**p && **p != ';' && **p != '}')
               {  (*p)++;
               }
               else if(**p == ';')
               {  (*p)++;
               }
               else if(**p == '}')
               {  break;
               }
               else
               {  /* End of string or invalid - break */
                  break;
               }
            }
            else
            {  /* Pointer advanced but property failed - skip to next */
               css_debug_printf("ParseRule: ParseProperty failed, skipping to next (pos %ld->%ld)\n",
                              propOldp - ruleStart, *p - ruleStart);
               {  long skipIterationCount = 0;
                  while(**p && **p != ';' && **p != '}')
                  {  skipIterationCount++;
                     if(skipIterationCount > 10000)
                     {  /* Safety: if we can't find semicolon or brace in 10000 chars, break */
                        break;
                     }
                     (*p)++;
                  }
               }
            }
         }
         
         SkipWhitespace(p);
         if(**p == ';')
         {  (*p)++;
         }
         else if(**p == '}')
         {  break;
         }
         
         /* Safety check: if pointer didn't advance at all, force advance and break */
         if(*p == propOldp)
         {  if(**p && **p != '}')
            {  (*p)++;
            }
            else
            {  break;
            }
            /* If still at same position after forcing advance, break */
            if(*p == propOldp)
            {  css_debug_printf("ParseRule: Property pointer stuck, breaking\n");
               break;
            }
         }
      }
   }
   
   css_debug_printf("ParseRule: Completed rule with %ld selector(s) and %ld property(ies), final position %ld\n",
                   selectorCount, propertyCount, *p - ruleStart);
   
   if(**p == '}')
   {  (*p)++; /* Skip '}' */
      return rule;
   }
   
   css_debug_printf("ParseRule: ERROR - Expected '}' but found 0x%02x, freeing rule\n", **p);
   FreeCSSRule(rule);
   return NULL;
}

/* Parse a CSS selector */
static struct CSSSelector* ParseSelector(struct Document *doc,UBYTE **p)
{  struct CSSSelector *sel;
   UBYTE *name;
   UBYTE *id;
   UBYTE *class;
   UBYTE *pseudoName;
   long notDepth;
   
   if(!doc || !p || !*p) return NULL;
   
   sel = ALLOCSTRUCT(CSSSelector,1,MEMF_FAST);
   if(!sel) return NULL;
   
   /* Initialize new fields */
   sel->parent = NULL;
   sel->combinator = CSS_COMB_NONE;
   sel->pseudoElement = NULL;
   sel->attr = NULL;
   
   SkipWhitespace(p);
   
   /* Check for :root selector (must be at start) */
   if(**p == ':')
   {  UBYTE *rootCheck = *p + 1;
      UBYTE *rootId = ParseIdentifier(&rootCheck);
      if(rootId && Stricmp((char *)rootId, "root") == 0)
      {  /* Check if next char is whitespace, comma, or brace (end of selector) */
         if(!*rootCheck || isspace(*rootCheck) || *rootCheck == ',' || *rootCheck == '{' || *rootCheck == '}')
         {  FREE(rootId);
            sel->type = CSS_SEL_ROOT;
            sel->specificity = 1; /* :root has element-level specificity */
            *p = rootCheck;
            SkipWhitespace(p);
            return sel;
         }
      }
      if(rootId) FREE(rootId);
   }
   
   /* Parse element name, class, or ID */
   if(**p == '.')
   {  /* Class selector */
      (*p)++;
      class = ParseIdentifier(p);
      if(class)
      {  sel->type = CSS_SEL_CLASS;
         sel->class = Dupstr(class,-1);
         FREE(class);
         sel->specificity = 10; /* Class specificity */
      }
      else
      {  FREE(sel);
         return NULL;
      }
      
      /* Check for descendant element after class (e.g., ".menubar li") */
      SkipWhitespace(p);
      if(**p && **p != ':' && **p != ',' && **p != '{' && **p != '}')
      {  name = ParseIdentifier(p);
         if(name)
         {  sel->type |= CSS_SEL_ELEMENT;
            sel->name = Dupstr(name,-1);
            FREE(name);
            sel->specificity += 1; /* Element adds to specificity */
         }
      }
   }
   else if(**p == '#')
   {  /* ID selector */
      (*p)++;
      id = ParseIdentifier(p);
      if(id)
      {  sel->type = CSS_SEL_ID;
         sel->id = Dupstr(id,-1);
         FREE(id);
         sel->specificity = 100; /* ID specificity */
      }
      else
      {  FREE(sel);
         return NULL;
      }
   }
   else
   {  /* Element name or universal * */
      if(**p == '*')
      {  (*p)++;
         /* Universal selector: must consume '*' — otherwise descendant parsing
          * calls ParseSelector again on the same '*' and blows the stack
          * (e.g. Drupal noscript: form.antibot * :not(...)). */
         sel->type = CSS_SEL_ELEMENT;
         sel->specificity = 0;
      }
      else
      {
         name = ParseIdentifier(p);
         if(name)
         {  sel->type = CSS_SEL_ELEMENT;
            sel->name = Dupstr(name,-1);
            FREE(name);
            sel->specificity = 1; /* Element specificity */
         }
         else
         {  FREE(sel);
            return NULL;
         }
      }
      
      /* Check for class or ID after element name */
      /* Support multiple chained classes (e.g., .class1.class2) */
      SkipWhitespace(p);
      while(**p == '.' || **p == '#')
      {  if(**p == '.')
         {  (*p)++;
            class = ParseIdentifier(p);
            if(class)
            {  sel->type |= CSS_SEL_CLASS;
               if(sel->class)
               {  /* Append to existing class list (space-separated) */
                  UBYTE *oldClass;
                  long oldLen;
                  long newLen;
                  long classLen;
                  UBYTE *newClass;
                  
                  oldClass = sel->class;
                  oldLen = strlen((char *)oldClass);
                  classLen = strlen((char *)class);
                  newLen = oldLen + 1 + classLen; /* old + space + new */
                  newClass = ALLOCTYPE(UBYTE, newLen + 1, MEMF_FAST);
                  if(newClass)
                  {  memmove(newClass, oldClass, oldLen);
                     newClass[oldLen] = ' ';
                     memmove(newClass + oldLen + 1, class, classLen);
                     newClass[newLen] = '\0';
                     FREE(oldClass);
                     sel->class = newClass;
                  }
                  FREE(class);
               }
               else
               {  sel->class = Dupstr(class,-1);
                  FREE(class);
               }
               sel->specificity += 10; /* Each class adds 10 to specificity */
            }
         }
         else if(**p == '#')
         {  (*p)++;
            id = ParseIdentifier(p);
            if(id)
            {  sel->type |= CSS_SEL_ID;
               sel->id = Dupstr(id,-1);
               FREE(id);
               sel->specificity += 100;
               break; /* ID can only appear once, stop parsing classes */
            }
         }
         SkipWhitespace(p);
      }
   }
   
   /* Check for pseudo-class (e.g., :link, :visited, :hover) or pseudo-element (::before, ::after) */
   SkipWhitespace(p);
   if(**p == ':')
   {  if((*p)[1] == ':')
      {  /* Pseudo-element (::before, ::after, ::selection, etc.) */
         (*p) += 2; /* Skip '::' */
         pseudoName = ParseIdentifier(p);
         if(pseudoName)
         {  sel->type |= CSS_SEL_PSEUDOEL;
            sel->pseudoElement = Dupstr(pseudoName,-1);
            FREE(pseudoName);
            sel->specificity += 1; /* Pseudo-element adds element-level specificity */
         }
      }
      else
      {  /* Pseudo-class (:link, :visited, :hover, etc.) */
         (*p)++; /* Skip ':' */
         pseudoName = ParseIdentifier(p);
         if(pseudoName)
         {  sel->type |= CSS_SEL_PSEUDO;
            sel->pseudo = Dupstr(pseudoName,-1);
            FREE(pseudoName);
            sel->specificity += 10; /* Pseudo-class adds to specificity */
            /* :not(selector-list) — we do not match :not yet; skip (...) so '{' is found. */
            if(sel->pseudo && Stricmp((char *)sel->pseudo, "not") == 0 && **p == '(')
            {  (*p)++;
               notDepth = 1;
               while(**p && notDepth > 0)
               {  if(**p == '(') notDepth++;
                  else if(**p == ')') notDepth--;
                  (*p)++;
               }
            }
         }
      }
   }
   
   /* Check for attribute selector [attr], [attr=value], [attr*=value], etc. */
   SkipWhitespace(p);
   if(**p == '[')
   {  struct CSSAttribute *attr;
      UBYTE *attrName;
      UBYTE *attrValue;
      UBYTE *oldp;
      UBYTE quote;
      
      (*p)++; /* Skip '[' */
      SkipWhitespace(p);
      
      attr = ALLOCSTRUCT(CSSAttribute, 1, MEMF_FAST);
      if(attr)
      {  attr->name = NULL;
         attr->value = NULL;
         attr->operator = CSS_ATTR_NONE;
         
         /* Parse attribute name */
         attrName = ParseIdentifier(p);
         if(attrName)
         {  attr->name = Dupstr(attrName,-1);
            FREE(attrName);
            SkipWhitespace(p);
            
            /* Check for operator */
            if(**p == '=')
            {  attr->operator = CSS_ATTR_EQUAL;
               (*p)++;
            }
            else if(**p == '*' && (*p)[1] == '=')
            {  attr->operator = CSS_ATTR_CONTAINS;
               (*p) += 2;
            }
            else if(**p == '^' && (*p)[1] == '=')
            {  attr->operator = CSS_ATTR_STARTS;
               (*p) += 2;
            }
            else if(**p == '$' && (*p)[1] == '=')
            {  attr->operator = CSS_ATTR_ENDS;
               (*p) += 2;
            }
            else if(**p == '~' && (*p)[1] == '=')
            {  attr->operator = CSS_ATTR_WORD;
               (*p) += 2;
            }
            
            /* If we have an operator, parse the value */
            if(attr->operator != CSS_ATTR_NONE)
            {  SkipWhitespace(p);
               oldp = *p;
               
               /* Check for quoted value */
               if(**p == '"' || **p == '\'')
               {  quote = **p;
                  (*p)++; /* Skip opening quote */
                  attrValue = *p;
                  while(**p && **p != quote)
                  {  if(**p == '\\' && (*p)[1])
                     {  (*p) += 2; /* Skip escaped character */
                     }
                     else
                     {  (*p)++;
                     }
                  }
                  if(**p == quote)
                  {  long len = *p - attrValue;
                     attr->value = ALLOCTYPE(UBYTE, len + 1, MEMF_FAST);
                     if(attr->value)
                     {  memmove(attr->value, attrValue, len);
                        attr->value[len] = '\0';
                     }
                     (*p)++; /* Skip closing quote */
                  }
               }
               else
               {  /* Unquoted value - parse identifier */
                  attrValue = ParseIdentifier(p);
                  if(attrValue)
                  {  attr->value = Dupstr(attrValue,-1);
                     FREE(attrValue);
                  }
               }
            }
            
            SkipWhitespace(p);
            if(**p == ']')
            {  (*p)++; /* Skip ']' */
               sel->type |= CSS_SEL_ATTRIBUTE;
               sel->attr = attr;
               sel->specificity += 10; /* Attribute selector adds class-level specificity */
            }
            else
            {  /* Invalid attribute selector - free and ignore */
               if(attr->name) FREE(attr->name);
               if(attr->value) FREE(attr->value);
               FREE(attr);
            }
         }
         else
         {  /* Invalid attribute name - free and ignore */
            FREE(attr);
         }
      }
   }
   
   /* Check for descendant selector (whitespace) or child combinator (>) */
   SkipWhitespace(p);
   if(**p && **p != ',' && **p != '{' && **p != '}')
   {  UBYTE *oldp;
      struct CSSSelector *childSel;
      
      /* Check for child combinator */
      if(**p == '>')
      {  (*p)++; /* Skip '>' */
         SkipWhitespace(p);
         oldp = *p;
         childSel = ParseSelector(doc, p);
         if(childSel)
         {  childSel->parent = sel;
            childSel->combinator = CSS_COMB_CHILD;
            /* Update specificity - child selector adds to parent */
            childSel->specificity += sel->specificity;
            return childSel; /* Return the child selector as the head */
         }
         else
         {  /* Parse error - restore position */
            *p = oldp;
         }
      }
      else
      {  /* Check if there's whitespace followed by another selector (descendant) */
         /* We already skipped whitespace, so check if next char starts an identifier */
         if(isalpha(**p) || **p == '.' || **p == '#' || **p == '*')
         {  oldp = *p;
            childSel = ParseSelector(doc, p);
            if(childSel)
            {  childSel->parent = sel;
               childSel->combinator = CSS_COMB_DESCENDANT;
               /* Update specificity - descendant selector adds to parent */
               childSel->specificity += sel->specificity;
               return childSel; /* Return the child selector as the head */
            }
            else
            {  /* Parse error - restore position */
               *p = oldp;
            }
         }
      }
   }
   
   return sel;
}

/* Parse a CSS property */
static struct CSSProperty* ParseProperty(struct Document *doc,UBYTE **p)
{  struct CSSProperty *prop;
   UBYTE *name;
   UBYTE *value;
   
   if(!doc || !p || !*p) return NULL;
   
   prop = ALLOCSTRUCT(CSSProperty,1,MEMF_FAST);
   if(!prop) return NULL;
   
   SkipWhitespace(p);
   
   /* Parse property name */
   name = ParseIdentifier(p);
   if(!name)
   {  FREE(prop);
      return NULL;
   }
   prop->name = Dupstr(name,-1);
   FREE(name);
   
   SkipWhitespace(p);
   if(**p != ':')
   {  FREE(prop->name);
      FREE(prop);
      return NULL;
   }
   (*p)++; /* Skip ':' */
   
   SkipWhitespace(p);
   
   /* Parse property value */
   value = ParseValue(p);
   if(value)
   {  prop->value = Dupstr(value,-1);
      FREE(value);
   }
   else
   {  /* ParseValue failed - make sure pointer advanced */
      /* ParseValue should have advanced past whitespace, but if it returned NULL
       * without advancing, we need to skip to prevent infinite loop */
      if(*p && **p != ';' && **p != '}')
      {  /* Skip to next semicolon or closing brace */
         while(**p && **p != ';' && **p != '}')
         {  (*p)++;
         }
      }
      FREE(prop->name);
      FREE(prop);
      return NULL;
   }
   
   return prop;
}

/* Skip whitespace */
void SkipWhitespace(UBYTE **p)
{  UBYTE c;
   if(!p || !*p) return;
   for(;;)
   {  c = **p;
      if(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') (*p)++;
      else break;
   }
}

/* Skip CSS comment */
static void SkipComment(UBYTE **p)
{  if(!p || !*p) return;
   if(**p == '/' && (*p)[1] == '*')
   {  (*p) += 2;
      while(**p)
      {  if(**p == '*' && (*p)[1] == '/')
         {  (*p) += 2;
            break;
         }
         (*p)++;
      }
   }
}

/* Parse an identifier */
static UBYTE* ParseIdentifier(UBYTE **p)
{  UBYTE *start;
   long len;
   UBYTE *result;
   
   if(!p || !*p) return NULL;
   
   SkipWhitespace(p);
   start = *p;
   
   /* First character must be letter, underscore, or non-ASCII (>= 128).
    * Reject if it's none of those (i.e. ASCII but not alpha and not underscore). */
   if(!isalpha(**p) && **p != '_' && (unsigned char)**p < 128)
   {  return NULL;
   }
   (*p)++;
   
   /* Subsequent characters can be alphanumeric, underscore, or hyphen */
   while(**p && (isalnum(**p) || **p == '_' || **p == '-' || (unsigned char)**p >= 128))
   {  (*p)++;
   }
   
   len = *p - start;
   if(len == 0) return NULL;
   
   result = ALLOCTYPE(UBYTE,len + 1,MEMF_FAST);
   if(result)
   {  memmove(result,start,len);
      result[len] = '\0';
   }
   
   return result;
}

/* Parse a property value */
static UBYTE* ParseValue(UBYTE **p)
{  UBYTE *start;
   long len;
   UBYTE *result;
   BOOL inString = FALSE;
   UBYTE quote = 0;
   
   if(!p || !*p) return NULL;
   
   SkipWhitespace(p);
   start = *p;
   
   /* If we're already at a semicolon or closing brace, no value to parse */
   if(**p == ';' || **p == '}')
   {  return NULL;
   }
   
   /* Parse until semicolon or closing brace (allow newlines in values) */
   /* Handle quoted strings within the value (e.g., font-family: "Open Sans", "Helvetica Neue", ...) */
   {  long valueIterationCount = 0;
      while(**p && **p != ';' && **p != '}')
      {  valueIterationCount++;
         if(valueIterationCount > 10000)
         {  /* Safety: if we've parsed more than 10000 characters, something is wrong - break */
            break;
         }
         
         if((**p == '"' || **p == '\'') && !inString)
         {  quote = **p;
            inString = TRUE;
            (*p)++;
            /* Skip to closing quote */
            {  long quoteIterationCount = 0;
               while(**p && **p != quote)
               {  quoteIterationCount++;
                  if(quoteIterationCount > 10000)
                  {  /* Unclosed quote - break out */
                     inString = FALSE;
                     break;
                  }
                  if(**p == '\\' && (*p)[1])
                  {  (*p) += 2; /* Skip escaped character */
                  }
                  else
                  {  (*p)++;
                  }
               }
            }
            if(**p == quote)
            {  (*p)++; /* Skip closing quote */
               inString = FALSE;
            }
            else
            {  /* Unclosed quote - treat as end of value */
               inString = FALSE;
            }
         }
         else
         {  (*p)++;
         }
      }
   }
   len = *p - start;
   /* Trim trailing whitespace */
   while(len > 0 && isspace(start[len - 1]))
   {  len--;
   }
   
   if(len == 0) return NULL;
   
   result = ALLOCTYPE(UBYTE,len + 1,MEMF_FAST);
   if(result)
   {  memmove(result,start,len);
      result[len] = '\0';
   }
   
   return result;
}

/* Match class attribute against selector class (handles space-separated classes) */
/* For multiple classes in selector (e.g., "class1 class2"), element must have ALL classes */
/* Uses proper word-boundary matching to avoid partial matches */
static BOOL MatchClassAttribute(UBYTE *elementClass, UBYTE *selectorClass)
{  UBYTE *start;
   UBYTE *selectorP;
   UBYTE *selectorStart;
   size_t selectorWordLen;
   size_t elementWordLen;
   BOOL foundMatch;
   UBYTE *elementP;
   
   if(!elementClass || !selectorClass || !*selectorClass)
   {  return FALSE;
   }
   
   /* If selector has multiple classes (space-separated), element must have ALL */
   selectorP = selectorClass;
   while(*selectorP)
   {  /* Skip whitespace */
      while(*selectorP && isspace(*selectorP)) selectorP++;
      if(!*selectorP) break;
      
      selectorStart = selectorP;
      /* Find end of current selector class word */
      while(*selectorP && !isspace(*selectorP)) selectorP++;
      selectorWordLen = selectorP - selectorStart;
      
      if(selectorWordLen == 0) break;
      
      /* Check if element has this class */
      foundMatch = FALSE;
      elementP = elementClass;
      /* Skip leading whitespace */
      while(*elementP && isspace(*elementP)) elementP++;
      
      while(*elementP)
      {  start = elementP;
         /* Find end of current element class word */
         while(*elementP && !isspace(*elementP)) elementP++;
         elementWordLen = elementP - start;
         
         /* Check if this word matches selector class (case-insensitive) */
         if(elementWordLen == selectorWordLen)
         {  if(Strnicmp((char *)start, (char *)selectorStart, selectorWordLen) == 0)
            {  foundMatch = TRUE;
               break;
            }
         }
         
         /* Skip whitespace to next word */
         while(*elementP && isspace(*elementP)) elementP++;
      }
      
      /* If this selector class wasn't found in element, fail */
      if(!foundMatch)
      {  return FALSE;
      }
   }
   
   return TRUE;
}

/* Case-insensitive string search (stristr equivalent) */
static UBYTE *stristr_case(UBYTE *haystack, UBYTE *needle)
{  long haystackLen;
   long needleLen;
   long i;
   
   if(!haystack || !needle || !*needle) return NULL;
   
   haystackLen = strlen((char *)haystack);
   needleLen = strlen((char *)needle);
   
   if(needleLen > haystackLen) return NULL;
   
   for(i = 0; i <= haystackLen - needleLen; i++)
   {  if(Strnicmp((char *)(haystack + i), (char *)needle, needleLen) == 0)
      {  return haystack + i;
      }
   }
   
   return NULL;
}

/* Match attribute selector */
static BOOL MatchAttributeSelector(struct CSSAttribute *attr, UBYTE *elemAttrValue)
{  UBYTE *attrVal;
   long len;
   
   if(!attr || !attr->name) return FALSE;
   
   if(!elemAttrValue) return (BOOL)((attr->operator == CSS_ATTR_NONE) ? TRUE : FALSE); /* [attr] matches if attribute exists */
   
   attrVal = elemAttrValue;
   
   switch(attr->operator)
   {  case CSS_ATTR_NONE:
         /* [attr] - attribute exists */
         return TRUE;
         
      case CSS_ATTR_EQUAL:
         /* [attr=value] - exact match */
         if(!attr->value) return FALSE;
         return (BOOL)((Stricmp((char *)attrVal, (char *)attr->value) == 0) ? TRUE : FALSE);
         
      case CSS_ATTR_CONTAINS:
         /* [attr*=value] - contains substring */
         if(!attr->value) return FALSE;
         return (BOOL)((stristr_case(attrVal, attr->value) != NULL) ? TRUE : FALSE);
         
      case CSS_ATTR_STARTS:
         /* [attr^=value] - starts with */
         if(!attr->value) return FALSE;
         len = strlen((char *)attr->value);
         return (BOOL)((strlen((char *)attrVal) >= len && 
                 Strnicmp((char *)attrVal, (char *)attr->value, len) == 0) ? TRUE : FALSE);
         
      case CSS_ATTR_ENDS:
         /* [attr$=value] - ends with */
         if(!attr->value) return FALSE;
         len = strlen((char *)attr->value);
         {  long attrLen = strlen((char *)attrVal);
            if(attrLen < len) return FALSE;
            return (BOOL)((Stricmp((char *)(attrVal + attrLen - len), (char *)attr->value) == 0) ? TRUE : FALSE);
         }
         
      case CSS_ATTR_WORD:
         /* [attr~=value] - word match (space-separated) */
         if(!attr->value) return FALSE;
         {  UBYTE *p = attrVal;
            UBYTE *wordStart;
            long wordLen;
            long valueLen = strlen((char *)attr->value);
            
            while(*p)
            {  /* Skip whitespace */
               while(*p && isspace(*p)) p++;
               if(!*p) break;
               
               wordStart = p;
               /* Find end of word */
               while(*p && !isspace(*p)) p++;
               wordLen = p - wordStart;
               
               /* Check if this word matches */
               if(wordLen == valueLen && Strnicmp((char *)wordStart, (char *)attr->value, wordLen) == 0)
               {  return TRUE;
               }
            }
            return FALSE;
         }
         
      default:
         return FALSE;
   }
}

/* CSS :hover applies when the pointer is over the element itself.
 * Note: With AWeb's current parsing model, multiple DIVs reuse the same BODY object,
 * so treating descendants as hovered can incorrectly make unrelated elements match
 * :hover and override display:none (e.g. dropdown menus). */
static BOOL HoverSelectorMatchesObject(void *selObj)
{  void *h;
   
   if(!currentCSSDoc || !selObj)
   {  return FALSE;
   }
   h = currentCSSDoc->hoveredElement;
   if(!h)
   {  return FALSE;
   }
   return (BOOL)(h == selObj);
}

/* Match a single selector component to an element (without checking parent) */
static BOOL MatchSelectorComponent(struct CSSSelector *sel, void *element)
{  UBYTE *elemName;
   UBYTE *elemClass;
   UBYTE *elemId;
   UBYTE *attrValue;
   
   if(!sel || !element) return FALSE;
   
   /* Match :root selector - matches HTML element */
   if(sel->type & CSS_SEL_ROOT)
   {  elemName = (UBYTE *)Agetattr(element,AOELT_TagName);
      return (BOOL)((elemName && Stricmp((char *)elemName, "html") == 0) ? TRUE : FALSE);
   }
   
   /* Get element attributes */
   elemName = (UBYTE *)Agetattr(element,AOELT_TagName);
   elemClass = (UBYTE *)Agetattr(element,AOELT_Class);
   elemId = (UBYTE *)Agetattr(element,AOELT_Id);
   
   /* Match element name */
   if(sel->type & CSS_SEL_ELEMENT && sel->name)
   {  /* For 'html' selector, also match if this is the root element (no tagname) */
      if(Stricmp((char *)sel->name, "html") == 0)
      {  /* html selector matches html element OR root element (no tagname) */
         if(elemName && Stricmp((char *)elemName, "html") != 0)
         {  return FALSE; /* Has tagname but it's not "html" */
         }
         /* If no tagname, this is the root element - html selector matches */
      }
      else
      {  /* For other selectors, require exact match */
         if(!elemName || Stricmp((char *)sel->name,(char *)elemName) != 0)
         {  return FALSE;
         }
      }
   }
   
   /* Match class - use proper word-boundary matching */
   if(sel->type & CSS_SEL_CLASS && sel->class)
   {  if(!MatchClassAttribute(elemClass, sel->class))
      {  return FALSE;
      }
   }
   
   /* Match ID */
   if(sel->type & CSS_SEL_ID && sel->id)
   {  if(!elemId || Stricmp((char *)sel->id,(char *)elemId) != 0)
      {  return FALSE;
      }
   }
   
   /* Match attribute selector */
   if(sel->type & CSS_SEL_ATTRIBUTE && sel->attr)
   {  /* Get attribute value - support class and id attributes directly */
      /* Note: Other HTML attributes (href, src, alt, etc.) are not stored
       * in a queryable format - they're converted to specific object attributes
       * during parsing. Full arbitrary attribute support would require
       * a more comprehensive attribute storage system. */
      if(sel->attr->name && Stricmp((char *)sel->attr->name, "class") == 0)
      {  attrValue = elemClass;
      }
      else if(sel->attr->name && Stricmp((char *)sel->attr->name, "id") == 0)
      {  attrValue = elemId;
      }
      else
      {  /* For other attributes, we cannot query them by name string
           * as AWeb's object system uses tag IDs rather than string-based attributes.
           * Return FALSE for unknown attributes. */
         attrValue = NULL;
      }
      
      if(!MatchAttributeSelector(sel->attr, attrValue))
      {  return FALSE;
      }
   }
   
   /* Match :hover pseudo-class */
   if(sel->type & CSS_SEL_PSEUDO && sel->pseudo)
   {  if(Stricmp((char *)sel->pseudo,"hover") == 0)
      {  if(!HoverSelectorMatchesObject(element))
         {  return FALSE;
         }
      }
   }
   
   /* Note: Pseudo-elements (::before, ::after) are not matched here as they
    * don't correspond to actual DOM elements. They would be handled during rendering. */
   
   return TRUE;
}

/* Match a single selector component to a body (for parent matching) */
static BOOL MatchSelectorComponentBody(struct CSSSelector *sel, void *body)
{  UBYTE *bodyName;
   UBYTE *bodyClass;
   UBYTE *bodyId;
   UBYTE *attrValue;
   
   if(!sel || !body) return FALSE;
   
   /* Match :root selector - matches HTML element */
   if(sel->type & CSS_SEL_ROOT)
   {  bodyName = (UBYTE *)Agetattr(body,AOBDY_TagName);
      return (BOOL)((bodyName && Stricmp((char *)bodyName, "html") == 0) ? TRUE : FALSE);
   }
   
   /* Get body attributes */
   bodyName = (UBYTE *)Agetattr(body,AOBDY_TagName);
   bodyClass = (UBYTE *)Agetattr(body,AOBDY_Class);
   bodyId = (UBYTE *)Agetattr(body,AOBDY_Id);
   
   /* Match element name */
   if(sel->type & CSS_SEL_ELEMENT && sel->name)
   {  /* For 'html' selector, also match if this is the root element (no tagname) */
      if(Stricmp((char *)sel->name, "html") == 0)
      {  /* html selector matches html element OR root element (no tagname) */
         if(bodyName && Stricmp((char *)bodyName, "html") != 0)
         {  return FALSE; /* Has tagname but it's not "html" */
         }
         /* If no tagname, this is the root element - html selector matches */
      }
      else
      {  /* For other selectors, require exact match */
         if(!bodyName || Stricmp((char *)sel->name,(char *)bodyName) != 0)
         {  return FALSE;
         }
      }
   }
   
   /* Match class - use proper word-boundary matching */
   if(sel->type & CSS_SEL_CLASS && sel->class)
   {  if(!MatchClassAttribute(bodyClass, sel->class))
      {  return FALSE;
      }
   }
   
   /* Match ID */
   if(sel->type & CSS_SEL_ID && sel->id)
   {  if(!bodyId || Stricmp((char *)sel->id,(char *)bodyId) != 0)
      {  return FALSE;
      }
   }
   
   /* Match attribute selector */
   if(sel->type & CSS_SEL_ATTRIBUTE && sel->attr)
   {  /* Get attribute value - support class and id attributes directly */
      /* Note: Other HTML attributes are not stored in a queryable format.
       * See MatchSelectorComponent() for details. */
      if(sel->attr->name && Stricmp((char *)sel->attr->name, "class") == 0)
      {  attrValue = bodyClass;
      }
      else if(sel->attr->name && Stricmp((char *)sel->attr->name, "id") == 0)
      {  attrValue = bodyId;
      }
      else
      {  /* For other attributes, we cannot query them by name string */
         attrValue = NULL;
      }
      
      if(!MatchAttributeSelector(sel->attr, attrValue))
      {  return FALSE;
      }
   }
   
   return TRUE;
}

/* Match a selector component to either an element or body.
 * nameHint/classHint/idHint: when non-NULL and obj is AOTP_BODY, use instead of
 * AOBDY_* (ApplyCSSToBody passes parser tag while layout bodies often omit TagName). */
static BOOL MatchSelectorComponentGeneric(struct CSSSelector *sel, void *obj,
   UBYTE *nameHint, UBYTE *classHint, UBYTE *idHint)
{  struct Aobject *ao;
   short objtype;
   UBYTE *name;
   UBYTE *class;
   UBYTE *id;
   UBYTE *attrValue;
   
   if(!sel || !obj) return FALSE;
   
   /* Match :root selector - matches HTML element */
   if(sel->type & CSS_SEL_ROOT)
   {  ao = (struct Aobject *)obj;
      objtype = ao->objecttype;
      if(objtype == AOTP_BODY)
      {  name = nameHint ? nameHint : (UBYTE *)Agetattr(obj, AOBDY_TagName);
      }
      else
      {  name = (UBYTE *)Agetattr(obj, AOELT_TagName);
      }
      return (BOOL)((name && Stricmp((char *)name, "html") == 0) ? TRUE : FALSE);
   }
   
   /* Check object type to determine which attributes to use */
   ao = (struct Aobject *)obj;
   objtype = ao->objecttype;
   
   if(objtype == AOTP_BODY)
   {  /* It's a body - use body attributes (hints override missing AOBDY_* on subject) */
      name = nameHint ? nameHint : (UBYTE *)Agetattr(obj, AOBDY_TagName);
      class = classHint ? classHint : (UBYTE *)Agetattr(obj, AOBDY_Class);
      id = idHint ? idHint : (UBYTE *)Agetattr(obj, AOBDY_Id);
   }
   else
   {  /* Assume it's an element - use element attributes */
      name = (UBYTE *)Agetattr(obj, AOELT_TagName);
      class = (UBYTE *)Agetattr(obj, AOELT_Class);
      id = (UBYTE *)Agetattr(obj, AOELT_Id);
   }
   
   /* Match element name */
   if(sel->type & CSS_SEL_ELEMENT && sel->name)
   {  /* For 'html' selector, also match if this is the root element (no tagname) */
      if(Stricmp((char *)sel->name, "html") == 0)
      {  /* html selector matches html element OR root element (no tagname) */
         if(name && Stricmp((char *)name, "html") != 0)
         {  return FALSE; /* Has tagname but it's not "html" */
         }
         /* If no tagname, this is the root element - html selector matches */
      }
      else
      {  /* For other selectors, require exact match */
         if(!name || Stricmp((char *)sel->name, (char *)name) != 0)
         {  return FALSE;
         }
      }
   }
   
   /* Match class - use proper word-boundary matching */
   if(sel->type & CSS_SEL_CLASS && sel->class)
   {  if(!MatchClassAttribute(class, sel->class))
      {  return FALSE;
      }
   }
   
   /* Match ID */
   if(sel->type & CSS_SEL_ID && sel->id)
   {  if(!id || Stricmp((char *)sel->id, (char *)id) != 0)
      {  return FALSE;
      }
   }
   
   /* Match attribute selector */
   if(sel->type & CSS_SEL_ATTRIBUTE && sel->attr)
   {  /* Get attribute value - support class and id attributes directly */
      /* Note: Other HTML attributes are not stored in a queryable format.
       * See MatchSelectorComponent() for details. */
      if(sel->attr->name && Stricmp((char *)sel->attr->name, "class") == 0)
      {  attrValue = class;
      }
      else if(sel->attr->name && Stricmp((char *)sel->attr->name, "id") == 0)
      {  attrValue = id;
      }
      else
      {  /* For other attributes, we cannot query them by name string */
         attrValue = NULL;
      }
      
      if(!MatchAttributeSelector(sel->attr, attrValue))
      {  return FALSE;
      }
   }
   
   /* :hover on ancestor in compound selectors */
   if(sel->type & CSS_SEL_PSEUDO && sel->pseudo)
   {  if(Stricmp((char *)sel->pseudo, "hover") == 0)
      {  if(!HoverSelectorMatchesObject(obj))
         {  return FALSE;
         }
      }
   }
   
   return TRUE;
}

/* Match a selector to an element (handles descendant/child selectors) */
/* maxDepth limits recursion depth to prevent infinite loops and performance issues */
/* Hints apply only to the initial subject; parent/ancestor steps use NULL hints. */
static BOOL MatchSelectorInternal(struct CSSSelector *sel, void *element, long maxDepth,
   UBYTE *nameHint, UBYTE *classHint, UBYTE *idHint)
{  void *parentBody;
   void *ancestorBody;
   BOOL foundMatch;
   long depth;
   
   if(!sel || !element || maxDepth <= 0) return FALSE;
   
   /* If selector has a parent (descendant/child selector), we need to match the chain */
   if(sel->parent)
   {  /* Match subject with Generic so AOTP_BODY (nested DIV) uses AOBDY_* attrs. */
      if(!MatchSelectorComponentGeneric(sel, element, nameHint, classHint, idHint))
      {  return FALSE;
      }
      
      /* Get parent body */
      parentBody = (void *)Agetattr(element, AOBJ_Layoutparent);
      if(!parentBody)
      {  return FALSE;  /* No parent, can't match parent selector */
      }
      
      /* For child combinator (>), only immediate parent is valid */
      if(sel->combinator == CSS_COMB_CHILD)
      {  /* Check immediate parent only */
         if(!MatchSelectorComponentGeneric(sel->parent, parentBody, NULL, NULL, NULL))
         {  return FALSE;  /* Immediate parent doesn't match */
         }
         /* Matched! Continue up the chain if parent selector has its own parent */
         if(sel->parent->parent)
         {  /* Recursively match parent selector against parent body */
            return MatchSelectorInternal(sel->parent, parentBody, maxDepth - 1,
               NULL, NULL, NULL);
         }
         return TRUE;  /* Full chain matched */
      }
      /* For descendant combinator (space), traverse up ancestor chain */
      else if(sel->combinator == CSS_COMB_DESCENDANT)
      {  ancestorBody = parentBody;
         foundMatch = FALSE;
         depth = 0;
         
         /* Traverse up ancestor chain until we find a match or reach root */
         /* Limit depth to prevent excessive traversal on deep DOM trees */
         while(ancestorBody && depth < 50)
         {  /* Check if this ancestor matches the parent selector */
            if(MatchSelectorComponentGeneric(sel->parent, ancestorBody, NULL, NULL, NULL))
            {  foundMatch = TRUE;
               break;  /* Found matching ancestor */
            }
            
            /* Move up to next ancestor */
            ancestorBody = (void *)Agetattr(ancestorBody, AOBJ_Layoutparent);
            depth++;
         }
         
         if(!foundMatch)
         {  return FALSE;  /* No matching ancestor found */
         }
         
         /* Matched! Continue up the chain if parent selector has its own parent */
         if(sel->parent->parent)
         {  /* Recursively match parent selector against matching ancestor */
            return MatchSelectorInternal(sel->parent, ancestorBody, maxDepth - 1,
               NULL, NULL, NULL);
         }
         return TRUE;  /* Full chain matched */
      }
      else
      {  /* Unknown combinator - shouldn't happen, but be safe */
         return FALSE;
      }
   }
   else
   {  /* Simple selector - no parent chain */
      return MatchSelectorComponentGeneric(sel, element, nameHint, classHint, idHint);
   }
}

/* Match a selector to an element (handles descendant/child selectors) */
/* Public wrapper with default depth limit */
static BOOL MatchSelector(struct CSSSelector *sel,void *element)
{  /* Limit recursion depth to 20 levels to prevent performance issues */
   return MatchSelectorInternal(sel, element, 20, NULL, NULL, NULL);
}

/* Full compound matching for nested DIV bodies (ApplyCSSToBody in html.c). Preserves
 * currentCSSDoc across nested calls (e.g. from ApplyCSSToElement). */
BOOL CssSelectorMatchesLayoutObject(struct Document *doc, void *obj, struct CSSSelector *sel,
   UBYTE *nameHint, UBYTE *classHint, UBYTE *idHint)
{  struct Document *prevDoc;
   BOOL result;
   
   if(!doc || !obj || !sel)
   {  return FALSE;
   }
   prevDoc = currentCSSDoc;
   currentCSSDoc = doc;
   result = MatchSelectorInternal(sel, obj, 20, nameHint, classHint, idHint);
   currentCSSDoc = prevDoc;
   return result;
}

/* Apply a CSS property to an element */
static void ApplyProperty(struct Document *doc,void *element,struct CSSProperty *prop)
{  UBYTE *name;
   UBYTE *value;
   short align;
   struct Aobject *ao;
   short objtype;
   
   if(!element || !prop || !prop->name || !prop->value) return;
   
   /* Check if this is a BODY element or regular element */
   ao = (struct Aobject *)element;
   objtype = ao->objecttype;
   /* Defensive: layout bodies are never Element subclasses; AOELT_* is invalid. */
   if(objtype == AOTP_BODY)
   {  return;
   }
   
   name = prop->name;
   value = prop->value;
   
   /* text-align */
   if(Stricmp((char *)name,"text-align") == 0)
   {  if(Stricmp((char *)value,"center") == 0)
      {  align = HALIGN_CENTER;
      }
      else if(Stricmp((char *)value,"left") == 0)
      {  align = HALIGN_LEFT;
      }
      else if(Stricmp((char *)value,"right") == 0)
      {  align = HALIGN_RIGHT;
      }
      else if(Stricmp((char *)value,"justify") == 0)
      {  align = HALIGN_LEFT; /* Justify not supported, use left */
      }
      else
      {  return;
      }
      Asetattrs(element,AOELT_Halign,align,TAG_END);
   }
   /* font-family on non-body elements (layout AOTP_BODY returns at top of ApplyProperty). */
   else if(Stricmp((char *)name,"font-family") == 0)
   {  /* Regular element - use AOELT_Font */
         struct TextFont *currentFont;
         struct Fontprefs *fp;
         short fontSize;
         BOOL isFixed;
         UBYTE *fontFamily;
         UBYTE *p;
         UBYTE *q;
         long len;
         BOOL inQuotes;
         UBYTE quote;
         UBYTE *fontFamilyStripped;
         
         /* Get current font from element to determine size and type */
         currentFont = (struct TextFont *)Agetattr(element, AOELT_Font);
         if(currentFont)
         {  /* Determine font size from current font's YSize */
            /* Map font YSize to HTML size index (0-6 for sizes 1-7) */
            /* Approximate mapping: use medium (size 3, index 2) as default */
            /* For more accuracy, we'd need to know the document's font size mapping */
            fontSize = 2; /* Default to medium (index 2 = size 3) */
            /* Check if font is fixed-width */
            isFixed = (BOOL)((currentFont->tf_Flags & FPF_PROPORTIONAL) ? FALSE : TRUE);
         }
         else
         {  /* No current font - use defaults */
            fontSize = 2; /* Medium size (index 2 = size 3) */
            isFixed = FALSE; /* Default to proportional */
         }
         
         /* Strip quotes from font names before passing to Matchfont */
         len = 0;
         p = value;
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
         
         fontFamilyStripped = ALLOCTYPE(UBYTE, len + 1, MEMF_FAST);
         if(fontFamilyStripped)
         {  /* Copy font value, stripping quotes */
            p = value;
            q = fontFamilyStripped;
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
            
            /* Matchfont can handle comma-separated font lists and generic families */
            /* Pass the stripped value so Matchfont can try each font in order */
            fontFamily = fontFamilyStripped;
            if(fontFamily && *fontFamily)
            {  if(httpdebug)
               {  printf("[CSS] font-family (inline style): Matchfont list='%s' sizeidx=%d fixed=%d objtype=%ld el=%p\n",
                     (char *)fontFamily, (int)fontSize, (int)isFixed, (long)objtype, element);
               }
               fp = Matchfont(fontFamily, fontSize, isFixed);
               if(fp && fp->font)
               {  if(httpdebug)
                  {  printf("[CSS] font-family (inline style): AOELT_Font set el=%p ysize=%d\n",
                        element, (int)fp->font->tf_YSize);
                  }
                  /* Apply the matched font to the element */
                  Asetattrs(element, AOELT_Font, fp->font, TAG_END);
               }
               else if(httpdebug)
               {  printf("[CSS] font-family (inline style): Matchfont returned no font list='%s'\n",
                     (char *)fontFamily);
               }
            }
            FREE(fontFamilyStripped);
         }
   }
   /* float */
   else if(Stricmp((char *)name,"float") == 0)
   {  UBYTE *floatValue;
      short halign;
      
      floatValue = value;
      /* Skip whitespace */
      while(*floatValue && isspace(*floatValue)) floatValue++;
      
      /* Parse float values */
      if(Stricmp((char *)floatValue,"left") == 0)
      {  /* Set floating left */
         halign = HALIGN_FLOATLEFT;
         Asetattrs(element,AOELT_Floating,halign,TAG_END);
      }
      else if(Stricmp((char *)floatValue,"right") == 0)
      {  /* Set floating right */
         halign = HALIGN_FLOATRIGHT;
         Asetattrs(element,AOELT_Floating,halign,TAG_END);
      }
      else if(Stricmp((char *)floatValue,"none") == 0)
      {  /* Clear float */
         Asetattrs(element,AOELT_Floating,0,TAG_END);
      }
   }
   /* font-size */
   else if(Stricmp((char *)name,"font-size") == 0)
   {  /* Handle font size values */
      struct TextFont *currentFont;
      struct Fontprefs *fp;
      short fontSize;
      short sizeIndex;
      BOOL isFixed;
      BOOL isRelative;
      UBYTE *fontFamily;
      struct Number num;
      long sizeValue;
      
      /* Get current font from element to determine family and type */
      currentFont = (struct TextFont *)Agetattr(element, AOELT_Font);
      if(currentFont)
      {  /* Check if font is fixed-width */
         isFixed = (BOOL)((currentFont->tf_Flags & FPF_PROPORTIONAL) ? FALSE : TRUE);
         /* For font family, we can't easily extract it from TextFont, so use NULL for default */
         fontFamily = NULL;
      }
      else
      {  /* No current font - use defaults */
         isFixed = FALSE; /* Default to proportional */
         fontFamily = NULL; /* Will use default font */
      }
      
      fontSize = 0;
      isRelative = FALSE;
      sizeIndex = 2; /* Default to medium (index 2 = size 3) */
      
      /* Parse font size keywords */
      if(Stricmp((char *)value,"xx-small") == 0)
      {  sizeIndex = 0; /* xx-small = size 1, index 0 */
      }
      else if(Stricmp((char *)value,"x-small") == 0)
      {  sizeIndex = 1; /* x-small = size 2, index 1 */
      }
      else if(Stricmp((char *)value,"small") == 0)
      {  sizeIndex = 2; /* small = size 3, index 2 */
      }
      else if(Stricmp((char *)value,"medium") == 0)
      {  sizeIndex = 2; /* medium = size 3, index 2 */
      }
      else if(Stricmp((char *)value,"large") == 0)
      {  sizeIndex = 4; /* large = size 5, index 4 */
      }
      else if(Stricmp((char *)value,"x-large") == 0)
      {  sizeIndex = 5; /* x-large = size 6, index 5 */
      }
      else if(Stricmp((char *)value,"xx-large") == 0)
      {  sizeIndex = 6; /* xx-large = size 7, index 6 */
      }
      else if(Stricmp((char *)value,"smaller") == 0)
      {  /* Relative: one size smaller */
         if(currentFont)
         {  /* Try to determine current size from font YSize */
            /* Approximate: use medium as baseline, adjust down */
            sizeIndex = 1; /* One size smaller than medium */
         }
         else
         {  sizeIndex = 1; /* Default to small if no current font */
         }
      }
      else if(Stricmp((char *)value,"larger") == 0)
      {  /* Relative: one size larger */
         if(currentFont)
         {  /* Try to determine current size from font YSize */
            /* Approximate: use medium as baseline, adjust up */
            sizeIndex = 3; /* One size larger than medium */
         }
         else
         {  sizeIndex = 3; /* Default to large if no current font */
         }
      }
      else
      {  /* Try to parse as numeric value (px, pt, em, etc.) */
         sizeValue = ParseCSSLengthValue(value, &num);
         if(num.type == NUMBER_NUMBER && sizeValue > 0)
         {  /* Map pixel values to AWeb sizes (1-7, indices 0-6)
              * Approximate mapping: <10px=1, 10-12px=2, 13-14px=3, 15-16px=4, 17-18px=5, 19-22px=6, >22px=7 */
            if(sizeValue < 10)
            {  sizeIndex = 0; /* xx-small */
            }
            else if(sizeValue <= 12)
            {  sizeIndex = 1; /* x-small */
            }
            else if(sizeValue <= 14)
            {  sizeIndex = 2; /* small/medium */
            }
            else if(sizeValue <= 16)
            {  sizeIndex = 3; /* medium-large */
            }
            else if(sizeValue <= 18)
            {  sizeIndex = 4; /* large */
            }
            else if(sizeValue <= 22)
            {  sizeIndex = 5; /* x-large */
            }
            else
            {  sizeIndex = 6; /* xx-large */
            }
         }
         /* Note: em, ex, % values would need parent font size context - not fully implemented yet */
      }
      
      /* Clamp size index to valid range (0-6) */
      if(sizeIndex < 0) sizeIndex = 0;
      if(sizeIndex > 6) sizeIndex = 6;
      
      /* Use Matchfont to get font with current family and new size */
      /* If fontFamily is NULL, Matchfont will use default font */
      fp = Matchfont(fontFamily, sizeIndex, isFixed);
      if(fp && fp->font)
      {  /* Apply the matched font to the element */
         Asetattrs(element, AOELT_Font, fp->font, TAG_END);
      }
   }
   /* color */
   else if(Stricmp((char *)name,"color") == 0)
   {  ULONG colorrgb;
      struct Colorinfo *ci;
      
      if(doc)
      {  colorrgb = ParseHexColor(value);
         if(colorrgb != ~0)
         {  ci = Finddoccolor(doc, colorrgb);
            if(ci)
            {  Asetattrs(element, AOELT_Color, ci, TAG_END);
            }
         }
      }
   }
   /* font-weight */
   else if(Stricmp((char *)name,"font-weight") == 0)
   {  USHORT currentStyle;
      USHORT newStyle;
      
      currentStyle = (USHORT)Agetattr(element, AOELT_Style);
      newStyle = currentStyle;
      
      if(Stricmp((char *)value,"bold") == 0 || Stricmp((char *)value,"700") == 0 || Stricmp((char *)value,"bolder") == 0)
      {  newStyle |= FSF_BOLD;
      }
      else if(Stricmp((char *)value,"normal") == 0 || Stricmp((char *)value,"400") == 0 || Stricmp((char *)value,"lighter") == 0)
      {  newStyle &= ~FSF_BOLD;
      }
      else
      {  /* Try to parse numeric value */
         long weightValue;
         struct Number num;
         weightValue = ParseCSSLengthValue(value, &num);
         if(weightValue >= 600)
         {  newStyle |= FSF_BOLD;
         }
         else if(weightValue >= 0 && weightValue < 600)
         {  newStyle &= ~FSF_BOLD;
         }
      }
      
      if(newStyle != currentStyle)
      {  Asetattrs(element, AOELT_Style, newStyle, TAG_END);
      }
   }
   /* font-style */
   else if(Stricmp((char *)name,"font-style") == 0)
   {  USHORT currentStyle;
      USHORT newStyle;
      
      currentStyle = (USHORT)Agetattr(element, AOELT_Style);
      newStyle = currentStyle;
      
      if(Stricmp((char *)value,"italic") == 0 || Stricmp((char *)value,"oblique") == 0)
      {  newStyle |= FSF_ITALIC;
      }
      else if(Stricmp((char *)value,"normal") == 0)
      {  newStyle &= ~FSF_ITALIC;
      }
      
      if(newStyle != currentStyle)
      {  Asetattrs(element, AOELT_Style, newStyle, TAG_END);
      }
   }
   /* text-decoration - can have multiple space-separated values
    * like "underline line-through". Compare each word individually
    * using Strnicmp with the word length to avoid matching the
    * full remaining string. */
   else if(Stricmp((char *)name,"text-decoration") == 0)
   {  USHORT currentStyle;
      USHORT newStyle;
      UBYTE *decValue;
      UBYTE *pdec;
      UBYTE *wordStart;
      long wordLen;
      
      currentStyle = (USHORT)Agetattr(element, AOELT_Style);
      newStyle = currentStyle;
      
      decValue = Dupstr(value, -1);
      if(decValue)
      {  pdec = decValue;
         while(*pdec)
         {  SkipWhitespace(&pdec);
            if(!*pdec) break;
            
            /* Find current word boundaries */
            wordStart = pdec;
            while(*pdec && !isspace(*pdec)) pdec++;
            wordLen = pdec - wordStart;
            
            if(wordLen == 9 && Strnicmp((char *)wordStart,"underline",9) == 0)
            {  newStyle |= FSF_UNDERLINED;
            }
            else if((wordLen == 12 && Strnicmp((char *)wordStart,"line-through",12) == 0) ||
                    (wordLen == 13 && Strnicmp((char *)wordStart,"strikethrough",13) == 0))
            {  newStyle |= FSF_STRIKE;
            }
            else if(wordLen == 4 && Strnicmp((char *)wordStart,"none",4) == 0)
            {  newStyle &= ~(FSF_UNDERLINED | FSF_STRIKE);
            }
         }
         FREE(decValue);
      }
      
      if(newStyle != currentStyle)
      {  Asetattrs(element, AOELT_Style, newStyle, TAG_END);
      }
   }
}

/* Helper structure for sorting rules by specificity */
struct RuleWithSpecificity
{  struct MinNode node;
   struct CSSRule *rule;
   USHORT maxSpecificity;  /* Maximum specificity of matching selectors */
};

/* Apply CSS to an element */
void ApplyCSSToElement(struct Document *doc,void *element)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   struct MinList matches;
   struct RuleWithSpecificity *ruleSpec;
   struct RuleWithSpecificity *nextRuleSpec;
   struct RuleWithSpecificity *current;
   struct RuleWithSpecificity *insertAfter;
   struct CSSStylesheet *sheet;
   USHORT maxSpec;
   BOOL matched;
   extern BOOL httpdebug;
   short objtype;
   struct Aobject *ao;
   UBYTE *tagname;
   UBYTE *class;
   UBYTE *id;
   long matchCount;
   long matchSlotsUsed;
   
   if(!doc || !element || !doc->cssstylesheet) return;
   
   /* Set static document pointer for hover state checking */
   currentCSSDoc = doc;
   
   ao = (struct Aobject *)element;
   objtype = ao->objecttype;
   if(objtype == AOTP_BODY)
   {  tagname = (UBYTE *)Agetattr(element, AOBDY_TagName);
      class = (UBYTE *)Agetattr(element, AOBDY_Class);
      id = (UBYTE *)Agetattr(element, AOBDY_Id);
   }
   else
   {  tagname = (UBYTE *)Agetattr(element, AOELT_TagName);
      class = (UBYTE *)Agetattr(element, AOELT_Class);
      id = (UBYTE *)Agetattr(element, AOELT_Id);
   }
   /* DIV/SPAN-as-block etc. are AOTP_BODY. ReapplyCSSToBodyRecursiveInternal calls
    * ApplyCSSToElement on every child; ApplyProperty only issues AOELT_* tags and
    * corrupts memory or crashes if the target is a Body. Route bodies through the
    * same implementation as tag open (ApplyCSSToBody uses AOBDY_* / AOBJ_*). */
   if(objtype == AOTP_BODY)
   {  ApplyCSSToBody(doc, element, class, id, tagname);
      currentCSSDoc = NULL;
      return;
   }
   if(httpdebug)
   {  printf("[CSS] ApplyCSSToElement: element=%p, type=%d, tagname=%s, class=%s, id=%s\n",
            element, objtype,
            tagname ? (char *)tagname : "NULL",
            class ? (char *)class : "NULL",
            id ? (char *)id : "NULL");
   }
   
   /* Fast path: if the element has no tagname, class, or id, no CSS
    * selector with an element/class/id requirement can match it.
    * Skip the expensive rule iteration entirely unless there are
    * universal selectors (which have no such requirements). */
   if(!tagname && !class && !id)
   {  if(httpdebug)
      {  printf("[CSS] ApplyCSSToElement: Skipping - no tagname/class/id\n");
      }
      currentCSSDoc = NULL;
      return;
   }
   
   sheet = (struct CSSStylesheet *)doc->cssstylesheet;
   
   NEWLIST(&matches);
   matchSlotsUsed = 0;
   
   /* Find all matching rules and calculate their maximum specificity */
   /* Optimize: Check element attributes once to avoid repeated Agetattr calls */
   for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  matched = FALSE;
      maxSpec = 0;
      for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         (struct MinNode *)sel->node.mln_Succ;
         sel = (struct CSSSelector *)sel->node.mln_Succ)
      {  /* Quick rejection: If selector has an ID and element doesn't match, skip */
         if(sel->type & CSS_SEL_ID && sel->id)
         {  /* Reuse id already fetched above to avoid redundant Agetattr calls */
            if(!id || Stricmp((char *)sel->id, (char *)id) != 0)
            {  continue;  /* ID doesn't match, skip this selector */
            }
         }
         
         if(MatchSelector(sel,element))
         {  matched = TRUE;
            /* Find maximum specificity among matching selectors */
            if(sel->specificity > maxSpec)
            {  maxSpec = sel->specificity;
            }
            /* Early exit: If we found a match, we can stop checking other selectors in this rule */
            /* (One matching selector is enough to apply the rule) */
            break;
         }
      }
      
      /* If at least one selector matched, add rule with its specificity.
       * Use matched flag rather than maxSpec > 0 so that universal selector (*)
       * with specificity 0 is correctly applied. */
      if(matched)
      {  if(matchSlotsUsed >= CSS_ELEMENT_MAX_MATCH_NODES)
         {  continue;
         }
         ruleSpec = ALLOCSTRUCT(RuleWithSpecificity, 1, MEMF_FAST);
         if(ruleSpec)
         {  ruleSpec->rule = rule;
            ruleSpec->maxSpecificity = maxSpec;
            
            /* Insert sorted by specificity (lower first, so higher wins when applied last) */
            /* For same specificity, maintain document order (last in document wins) */
            insertAfter = NULL;
            for(current = (struct RuleWithSpecificity *)matches.mlh_Head;
                (struct MinNode *)current->node.mln_Succ;
                current = (struct RuleWithSpecificity *)current->node.mln_Succ)
            {  if(current->maxSpecificity > maxSpec)
               {  /* Insert before current (after insertAfter, which is the previous node) */
                  break;
               }
               insertAfter = current; /* Continue to maintain document order for same specificity */
            }
            
            if(insertAfter)
            {  /* Insert(ruleSpec) after insertAfter. Swapped args corrupt MinList; Freemem then hits bad pool header (wild free) and may fault as illegal instruction. */
               INSERT(&matches, (struct MinNode *)ruleSpec, (struct MinNode *)insertAfter);
            }
            else
            {  /* No node with lower or equal specificity, add at head */
               ADDHEAD(&matches, (struct MinNode *)ruleSpec);
            }
            matchSlotsUsed++;
         }
      }
   }
   
   /* Apply properties from matching rules sorted by specificity */
   /* Rules with same specificity maintain document order (last wins) */
   matchCount = 0;
   /* Advance via next pointer before FREE: iterator was use-after-free and leaked nodes */
   for(ruleSpec = (struct RuleWithSpecificity *)matches.mlh_Head;
       (struct MinNode *)ruleSpec->node.mln_Succ;
       ruleSpec = nextRuleSpec)
   {  nextRuleSpec = (struct RuleWithSpecificity *)ruleSpec->node.mln_Succ;
      matchCount++;
      rule = ruleSpec->rule;
      for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
         (struct MinNode *)prop->node.mln_Succ;
         prop = (struct CSSProperty *)prop->node.mln_Succ)
      {  if(httpdebug)
         {  printf("[CSS] ApplyCSSToElement: Applying property %s to element\n",
                  prop->name ? (char *)prop->name : "NULL");
         }
         ApplyProperty(doc,element,prop);
      }
      /* Free the helper structure */
      REMOVE((struct MinNode *)ruleSpec);
      FREE(ruleSpec);
   }
   
   if(httpdebug)
   {  printf("[CSS] ApplyCSSToElement: Found %ld matching rule(s) for element\n", matchCount);
   }
   
   /* Clear static document pointer */
   currentCSSDoc = NULL;
}

/* Recursively apply CSS to a body element and all its child elements */
/* depth: recursion depth to prevent infinite loops (max 100) */
static void ReapplyCSSToBodyRecursiveInternal(struct Document *doc, void *body, long depth)
{  struct Element *child;
   struct Aobject *ao;
   short objtype;
   UBYTE *tagname;
   UBYTE *class;
   UBYTE *id;
   void *childBody;
   extern BOOL httpdebug;
   struct BodyMinimal *bd;
   long iterationCount;
   long childCount;
   
   if(!doc || !body || !doc->cssstylesheet) return;
   
   /* Prevent infinite recursion - max depth of 100 */
   if(depth > 100)
   {  if(httpdebug)
      {  printf("[CSS] ReapplyCSSToBodyRecursive: WARNING - Max recursion depth (100) exceeded, stopping to prevent infinite loop\n");
      }
      return;
   }
   
   /* Apply CSS to this body element */
   tagname = (UBYTE *)Agetattr(body, AOBDY_TagName);
   class = (UBYTE *)Agetattr(body, AOBDY_Class);
   id = (UBYTE *)Agetattr(body, AOBDY_Id);
   if(httpdebug)
   {  if(tagname && (Stricmp((char *)tagname,"DIV") == 0 || Stricmp((char *)tagname,"PRE") == 0))
      {  printf("[CSS] ReapplyCSSToBodyRecursive: Applying CSS to %s element, body=%p, class=%s, id=%s, depth=%ld\n",
                tagname ? (char *)tagname : "unknown", body,
                class ? (char *)class : "NULL",
                id ? (char *)id : "NULL",
                depth);
      }
   }
   ApplyCSSToBody(doc, body, class, id, tagname);
   
   /* Get the Body structure to access contents */
   /* We use BodyMinimal to access the contents field - this is safe because we know it's a Body */
   bd = (struct BodyMinimal *)body;
   iterationCount = 0;
   childCount = 0;
   
   /* Count children first for debug output */
   /* LIST(Element) termination pattern: child->next == NULL (see body.c) */
   for(child = bd->contents.first; child && child->next; child = child->next)
   {  childCount++;
      /* Safety check: prevent infinite loops in corrupted lists */
      if(childCount > 10000)
      {  if(httpdebug)
         {  printf("[CSS] ReapplyCSSToBodyRecursive: WARNING - Child count exceeded 10000, list may be corrupted\n");
         }
         break;
      }
   }
   
   if(httpdebug && depth == 0 && childCount > 100)
   {  printf("[CSS] ReapplyCSSToBodyRecursive: Processing %ld child elements at root level\n", childCount);
   }
   
   /* Iterate through all child elements with protection against circular references */
   /* LIST(Element) termination pattern: child->next == NULL (see body.c) */
   for(child = bd->contents.first; child && child->next; child = child->next)
   {  iterationCount++;
      
      /* Progress reporting for large lists */
      if(httpdebug && depth == 0 && iterationCount % 100 == 0)
      {  printf("[CSS] ReapplyCSSToBodyRecursive: Progress - %ld/%ld elements processed\n", iterationCount, childCount);
      }
      
      /* Prevent infinite loops in corrupted lists - max 10000 iterations per body */
      if(iterationCount > 10000)
      {  if(httpdebug)
         {  printf("[CSS] ReapplyCSSToBodyRecursive: WARNING - Max iterations (10000) exceeded for body %p, stopping to prevent infinite loop\n", body);
         }
         break;
      }
      
      ao = (struct Aobject *)child;
      objtype = ao->objecttype;
      
      /* AOTP_BODY (nested DIV, etc.): recursion begins with ApplyCSSToBody on that
       * node. Do not call ApplyCSSToElement/ApplyCSSToBody here first — that ran
       * the full stylesheet twice per nested body (font stack, display, margins) */
      if(objtype == AOTP_BODY)
      {  childBody = (void *)child;
         if(childBody != body)
         {  ReapplyCSSToBodyRecursiveInternal(doc, childBody, depth + 1);
         }
         else if(httpdebug)
         {  printf("[CSS] ReapplyCSSToBodyRecursive: WARNING - Body element %p contains itself, skipping to prevent infinite recursion\n", body);
         }
      }
      else
      {  ApplyCSSToElement(doc, child);
      }
   }
   
   if(httpdebug && depth == 0)
   {  printf("[CSS] ReapplyCSSToBodyRecursive: Completed processing %ld child elements at root level\n", iterationCount);
   }
}

/* Recursively apply CSS to a body element and all its child elements */
static void ReapplyCSSToBodyRecursive(struct Document *doc, void *body)
{  ReapplyCSSToBodyRecursiveInternal(doc, body, 0);
}

/* Reapply CSS to all existing elements when CSS loads asynchronously or on reload */
/* This ensures CSS is applied deterministically regardless of load timing */
void ReapplyCSSToAllElements(struct Document *doc)
{  extern BOOL httpdebug;
   
   if(!doc || !doc->cssstylesheet || !doc->body)
   {  if(httpdebug)
      {  printf("[CSS] ReapplyCSSToAllElements: Skipped - doc=%p stylesheet=%p body=%p\n",
                doc, (doc ? doc->cssstylesheet : NULL), (doc ? doc->body : NULL));
      }
      return;
   }
   /* Skip if document has no frame yet: reapply/attribute path may use doc->frame
    * (e.g. Registerdoccolors or object propagation). Prevents crash on about:home
    * and when CSS is ready before the document is attached to a frame. */
   if(!doc->frame)
   {  if(httpdebug)
      {  printf("[CSS] ReapplyCSSToAllElements: Skipped - no frame (doc=%p), will reapply when attached\n", doc);
      }
      return;
   }
   if(httpdebug)
   {  printf("[CSS] ReapplyCSSToAllElements: ENTER doc=%p cssserial=%lu applied=%lu sheet=%p body=%p frame=%p\n",
             doc, (ULONG)doc->cssserial, (ULONG)doc->cssappliedserial,
             doc->cssstylesheet, doc->body, doc->frame);
   }
   
   /* Recursively apply CSS to document body and all child elements */
   ReapplyCSSToBodyRecursive(doc, doc->body);
   
   if(httpdebug)
   {  printf("[CSS] ReapplyCSSToAllElements: EXIT doc=%p cssserial=%lu applied=%lu sheet=%p body=%p frame=%p\n",
             doc, (ULONG)doc->cssserial, (ULONG)doc->cssappliedserial,
             doc->cssstylesheet, doc->body, doc->frame);
   }
}

/* Free CSS stylesheet for a document */
void FreeCSSStylesheet(struct Document *doc)
{  if(doc && doc->cssstylesheet)
   {  FreeCSSStylesheetInternal((struct CSSStylesheet *)doc->cssstylesheet);
      doc->cssstylesheet = NULL;
      MarkDocCssDirty(doc);
      doc->cssappliedserial = 0;
   }
}

/* Free a single CSS selector and its parent chain (for descendant/child selectors).
 * The selector passed here is the leaf; each ->parent pointer is walked and freed
 * up to the root of the chain. */
static void FreeCSSSelector(struct CSSSelector *sel)
{  struct CSSSelector *parent;
   while(sel)
   {  parent = sel->parent;
      if(sel->name) FREE(sel->name);
      if(sel->class) FREE(sel->class);
      if(sel->id) FREE(sel->id);
      if(sel->pseudo) FREE(sel->pseudo);
      if(sel->pseudoElement) FREE(sel->pseudoElement);
      if(sel->attr)
      {  if(sel->attr->name) FREE(sel->attr->name);
         if(sel->attr->value) FREE(sel->attr->value);
         FREE(sel->attr);
      }
      FREE(sel);
      sel = parent;
   }
}

/* Free a single CSS rule and all its selectors and properties */
static void FreeCSSRule(struct CSSRule *rule)
{  struct CSSSelector *sel;
   struct CSSProperty *prop;
   
   if(!rule) return;
   
   /* Remove and free all selectors from the rule's list, including parent chains */
   while((sel = (struct CSSSelector *)REMHEAD(&rule->selectors)))
   {  FreeCSSSelector(sel);
   }
   /* Remove and free all properties from the rule's list */
   while((prop = (struct CSSProperty *)REMHEAD(&rule->properties)))
   {  if(prop->name) FREE(prop->name);
      if(prop->value) FREE(prop->value);
      FREE(prop);
   }
   FREE(rule);
}

/* Free CSS stylesheet structure */
static void FreeCSSStylesheetInternal(struct CSSStylesheet *sheet)
{  struct CSSRule *rule;
   
   if(!sheet) return;
   
   /* Remove and free all rules from the list */
   while((rule = (struct CSSRule *)REMHEAD(&sheet->rules)))
   {  FreeCSSRule(rule);
   }
   
   FREE(sheet);
}

/* Parse and apply inline CSS to an element */
void ApplyInlineCSS(struct Document *doc,void *element,UBYTE *style)
{  struct CSSProperty *prop;
   UBYTE *p;
   
   if(!doc || !element || !style) return;
   
   p = style;
   while(*p)
   {  SkipWhitespace(&p);
      if(!*p) break;
      
      /* Parse property */
      prop = ParseProperty(doc,&p);
      if(prop)
      {  ApplyProperty(doc,element,prop);
         /* Free the property */
         if(prop->name) FREE(prop->name);
         if(prop->value) FREE(prop->value);
         FREE(prop);
      }
      else
      {  /* Skip to next semicolon on parse error */
         while(*p && *p != ';')
         {  p++;
         }
      }
      
      /* Skip semicolon */
      if(*p == ';') p++;
   }
}

/* Remove ASCII quotes from a Dupstr'd font-family value in place for AOBDY_Fontface. */
static void Stripcssfontfamilyquotes(UBYTE *s)
{
   UBYTE *p;
   UBYTE *q;
   BOOL inQuotes;
   UBYTE quote;

   if(!s)
      return;
   p = s;
   q = s;
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
}

/* Parse and apply inline CSS to a Body object */
void ApplyInlineCSSToBody(struct Document *doc,void *body,UBYTE *style,UBYTE *tagname)
{  struct CSSProperty *prop;
   struct Number num;
   UBYTE *p;
   ULONG colorrgb;
   struct Colorinfo *ci;
   short align;
   UBYTE *fontFace;
   short fontSize;
   BOOL isRelative;
   long marginTop;
   long marginRight;
   long marginBottom;
   long marginLeft;
   UBYTE *marginValue;
   UBYTE *marginTokens[4];
   long marginCount;
   long i;
   float lineHeightValue;
   UBYTE *lineHeightStr;
   UBYTE *displayValue;
   
   /* Initialize margin tokens array */
   for(i = 0; i < 4; i++) marginTokens[i] = NULL;
   
   if(!doc || !body || !style) return;
   
   p = style;
   while(*p)
   {  SkipWhitespace(&p);
      if(!*p) break;
      
      /* Parse property */
      prop = ParseProperty(doc,&p);
      if(prop && prop->name && prop->value)
      {           /* Apply padding shorthand */
         if(Stricmp((char *)prop->name,"padding") == 0)
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
            struct Number paddingNum;
            
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
                  {  tokenBuf = ALLOCTYPE(UBYTE,tokenLen + 1,MEMF_FAST);
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
            {  paddingTop = ParseCSSLengthValue(paddingTokens[0],&paddingNum);
               if(paddingCount == 1)
               {  paddingRight = paddingBottom = paddingLeft = paddingTop;
               }
               else if(paddingCount == 2)
               {  paddingBottom = paddingTop;
                  paddingRight = ParseCSSLengthValue(paddingTokens[1],&paddingNum);
                  paddingLeft = paddingRight;
               }
               else if(paddingCount == 3)
               {  paddingRight = ParseCSSLengthValue(paddingTokens[1],&paddingNum);
                  paddingLeft = paddingRight;
                  paddingBottom = ParseCSSLengthValue(paddingTokens[2],&paddingNum);
               }
               else if(paddingCount == 4)
               {  paddingRight = ParseCSSLengthValue(paddingTokens[1],&paddingNum);
                  paddingBottom = ParseCSSLengthValue(paddingTokens[2],&paddingNum);
                  paddingLeft = ParseCSSLengthValue(paddingTokens[3],&paddingNum);
               }
               
               /* Apply padding values to body */
               if(paddingTop >= 0 && paddingNum.type == NUMBER_NUMBER)
               {  Asetattrs(body,AOBDY_PaddingTop,paddingTop,TAG_END);
               }
               if(paddingRight >= 0 && paddingNum.type == NUMBER_NUMBER)
               {  Asetattrs(body,AOBDY_PaddingRight,paddingRight,TAG_END);
               }
               if(paddingBottom >= 0 && paddingNum.type == NUMBER_NUMBER)
               {  Asetattrs(body,AOBDY_PaddingBottom,paddingBottom,TAG_END);
               }
               if(paddingLeft >= 0 && paddingNum.type == NUMBER_NUMBER)
               {  Asetattrs(body,AOBDY_PaddingLeft,paddingLeft,TAG_END);
               }
            }
            
            /* Free temporary token buffers */
            for(j = 0; j < paddingCount; j++)
            {  if(paddingTokens[j]) FREE(paddingTokens[j]);
            }
         }
         /* Apply background-color */
         else if(Stricmp((char *)prop->name,"background-color") == 0)
         {  /* Check for transparent keyword first */
            UBYTE *pval = prop->value;
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
            {  /* Parse color (hex or color name) */
               Gethexcolor(doc,prop->value,&colorrgb);
               if(colorrgb != (ULONG)~0)
               {  /* Use Finddoccolor from docprivate.h */
                  ci = Finddoccolor(doc,colorrgb);
                  if(ci)
                  {  Asetattrs(body,AOBDY_Bgcolor,COLOR(ci),TAG_END);
                  }
               }
            }
         }
         /* Apply background-image */
         else if(Stricmp((char *)prop->name,"background-image") == 0)
         {  UBYTE *urlValue;
            UBYTE *url;
            void *bgimg;
            /* Parse url(...) format */
            urlValue = prop->value;
            /* Skip whitespace */
            while(*urlValue && isspace(*urlValue)) urlValue++;
            /* Check for url( */
            if(Strnicmp((char *)urlValue,"url(",4) == 0)
            {  UBYTE *start;
               UBYTE *end;
               long len;
               start = urlValue + 4;
               /* Skip whitespace after url( */
               while(*start && isspace(*start)) start++;
               /* Find closing ) */
               end = (UBYTE *)strchr((char *)start,')');
               if(end && end > start)
               {  /* Trim quotes if present */
                  if((*start == '"' || *start == '\'') && end > start + 1)
                  {  start++;
                     if(*(end - 1) == '"' || *(end - 1) == '\'')
                     {  end--;
                     }
                  }
                  len = end - start;
                  if(len > 0)
                     {  url = ALLOCTYPE(UBYTE,len + 1,MEMF_FAST);
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
         /* Apply background-repeat */
         else if(Stricmp((char *)prop->name,"background-repeat") == 0)
         {  UBYTE *repeatStr;
            repeatStr = Dupstr(prop->value, -1);
            if(repeatStr)
            {  /* Validate values: repeat, no-repeat, repeat-x, repeat-y */
               if(Stricmp((char *)repeatStr, "repeat") == 0 ||
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
         /* Apply background-position */
         else if(Stricmp((char *)prop->name,"background-position") == 0)
         {  UBYTE *positionStr;
            positionStr = Dupstr(prop->value, -1);
            if(positionStr)
            {  /* Store position value as-is - can be keywords (center, top, bottom, left, right)
                 * or percentages/lengths (e.g., "50% 50%", "10px 20px", "center top").
                 * Asetattrs takes ownership of the Dupstr'd string. */
               Asetattrs(body, AOBDY_BackgroundPosition, positionStr, TAG_END);
            }
         }
         /* Apply background-attachment */
         else if(Stricmp((char *)prop->name,"background-attachment") == 0)
         {  UBYTE *attachmentStr;
            attachmentStr = Dupstr(prop->value, -1);
            if(attachmentStr)
            {  /* Validate values: scroll, fixed */
               if(Stricmp((char *)attachmentStr, "scroll") == 0 ||
                  Stricmp((char *)attachmentStr, "fixed") == 0)
               {  Asetattrs(body, AOBDY_BackgroundAttachment, attachmentStr, TAG_END);
               }
               else
               {  FREE(attachmentStr);
               }
            }
         }
         /* Apply background shorthand - parse all background properties */
         else if(Stricmp((char *)prop->name,"background") == 0)
         {  UBYTE *bgValue;
            UBYTE *tokenStart;
            UBYTE *tokenEnd;
            UBYTE *token;
            long tokenLen;
            UBYTE *bgColorStr;
            UBYTE *bgImageStr;
            UBYTE *bgRepeatStr;
            UBYTE *bgPositionStr;
            UBYTE *bgAttachmentStr;
            ULONG bgColorRgb;
            struct Colorinfo *bgColorCi;
            void *bgimg;
            UBYTE *url;
            long urlLen;
            UBYTE *urlStart;
            UBYTE *urlEnd;
            
            bgValue = prop->value;
            bgColorStr = NULL;
            bgImageStr = NULL;
            bgRepeatStr = NULL;
            bgPositionStr = NULL;
            bgAttachmentStr = NULL;
            
            /* Parse background shorthand: [color] [image] [repeat] [attachment] [position] */
            /* Values can be in any order, but typically: background: color image repeat attachment position */
            while(*bgValue)
            {  /* Skip whitespace */
               while(*bgValue && isspace(*bgValue)) bgValue++;
               if(!*bgValue) break;
               
               tokenStart = bgValue;
               
               /* Check for url(...) - background image */
               if(Strnicmp((char *)bgValue, "url(", 4) == 0)
               {  urlStart = bgValue + 4;
                  while(*urlStart && isspace(*urlStart)) urlStart++;
                  urlEnd = (UBYTE *)strchr((char *)urlStart, ')');
                  if(urlEnd && urlEnd > urlStart)
                  {  /* Trim quotes if present */
                     if((*urlStart == '"' || *urlStart == '\'') && urlEnd > urlStart + 1)
                     {  urlStart++;
                        if(*(urlEnd - 1) == '"' || *(urlEnd - 1) == '\'')
                        {  urlEnd--;
                        }
                     }
                     urlLen = urlEnd - urlStart;
                     if(urlLen > 0)
                     {  bgImageStr = ALLOCTYPE(UBYTE, urlLen + 1, MEMF_FAST);
                        if(bgImageStr)
                        {  memmove(bgImageStr, urlStart, urlLen);
                           bgImageStr[urlLen] = '\0';
                        }
                     }
                     bgValue = urlEnd + 1;
                     continue;
                  }
               }
               
               /* Check for keywords: repeat, no-repeat, repeat-x, repeat-y */
               if(Strnicmp((char *)bgValue, "repeat", 6) == 0)
               {  if(Strnicmp((char *)bgValue, "repeat-x", 8) == 0)
                  {  bgRepeatStr = Dupstr("repeat-x", -1);
                     bgValue += 8;
                  }
                  else if(Strnicmp((char *)bgValue, "repeat-y", 8) == 0)
                  {  bgRepeatStr = Dupstr("repeat-y", -1);
                     bgValue += 8;
                  }
                  else if(Strnicmp((char *)bgValue, "no-repeat", 9) == 0)
                  {  bgRepeatStr = Dupstr("no-repeat", -1);
                     bgValue += 9;
                  }
                  else
                  {  bgRepeatStr = Dupstr("repeat", -1);
                     bgValue += 6;
                  }
                  continue;
               }
               
               /* Check for attachment: scroll, fixed */
               if(Strnicmp((char *)bgValue, "scroll", 6) == 0)
               {  bgAttachmentStr = Dupstr("scroll", -1);
                  bgValue += 6;
                  continue;
               }
               if(Strnicmp((char *)bgValue, "fixed", 5) == 0)
               {  bgAttachmentStr = Dupstr("fixed", -1);
                  bgValue += 5;
                  continue;
               }
               
               /* Check for position keywords: center, top, bottom, left, right */
               if(Strnicmp((char *)bgValue, "center", 6) == 0)
               {  if(!bgPositionStr)
                  {  bgPositionStr = Dupstr("center", -1);
                  }
                  bgValue += 6;
                  continue;
               }
               if(Strnicmp((char *)bgValue, "top", 3) == 0)
               {  if(!bgPositionStr)
                  {  bgPositionStr = Dupstr("top", -1);
                  }
                  else
                  {  /* Append to existing position */
                     UBYTE *oldPos = bgPositionStr;
                     long oldLen = strlen((char *)oldPos);
                     bgPositionStr = ALLOCTYPE(UBYTE, oldLen + 6, MEMF_FAST);
                     if(bgPositionStr)
                     {  memmove(bgPositionStr, oldPos, oldLen);
                        bgPositionStr[oldLen] = ' ';
                        memmove(bgPositionStr + oldLen + 1, "top", 3);
                        bgPositionStr[oldLen + 4] = '\0';
                     }
                     FREE(oldPos);
                  }
                  bgValue += 3;
                  continue;
               }
               if(Strnicmp((char *)bgValue, "bottom", 6) == 0)
               {  if(!bgPositionStr)
                  {  bgPositionStr = Dupstr("bottom", -1);
                  }
                  else
                  {  UBYTE *oldPos = bgPositionStr;
                     long oldLen = strlen((char *)oldPos);
                     bgPositionStr = ALLOCTYPE(UBYTE, oldLen + 7, MEMF_FAST);
                     if(bgPositionStr)
                     {  memmove(bgPositionStr, oldPos, oldLen);
                        bgPositionStr[oldLen] = ' ';
                        memmove(bgPositionStr + oldLen + 1, "bottom", 6);
                        bgPositionStr[oldLen + 7] = '\0';
                     }
                     FREE(oldPos);
                  }
                  bgValue += 6;
                  continue;
               }
               if(Strnicmp((char *)bgValue, "left", 4) == 0)
               {  if(!bgPositionStr)
                  {  bgPositionStr = Dupstr("left", -1);
                  }
                  else
                  {  UBYTE *oldPos = bgPositionStr;
                     long oldLen = strlen((char *)oldPos);
                     bgPositionStr = ALLOCTYPE(UBYTE, oldLen + 5, MEMF_FAST);
                     if(bgPositionStr)
                     {  memmove(bgPositionStr, oldPos, oldLen);
                        bgPositionStr[oldLen] = ' ';
                        memmove(bgPositionStr + oldLen + 1, "left", 4);
                        bgPositionStr[oldLen + 5] = '\0';
                     }
                     FREE(oldPos);
                  }
                  bgValue += 4;
                  continue;
               }
               if(Strnicmp((char *)bgValue, "right", 5) == 0)
               {  if(!bgPositionStr)
                  {  bgPositionStr = Dupstr("right", -1);
                  }
                  else
                  {  UBYTE *oldPos = bgPositionStr;
                     long oldLen = strlen((char *)oldPos);
                     bgPositionStr = ALLOCTYPE(UBYTE, oldLen + 6, MEMF_FAST);
                     if(bgPositionStr)
                     {  memmove(bgPositionStr, oldPos, oldLen);
                        bgPositionStr[oldLen] = ' ';
                        memmove(bgPositionStr + oldLen + 1, "right", 5);
                        bgPositionStr[oldLen + 6] = '\0';
                     }
                     FREE(oldPos);
                  }
                  bgValue += 5;
                  continue;
               }
               
               /* Check for percentages or lengths (position values) */
               if(isdigit(*bgValue) || *bgValue == '-' || *bgValue == '+' || *bgValue == '.')
               {  /* Parse number with optional unit */
                  tokenEnd = bgValue;
                  while(*tokenEnd && !isspace(*tokenEnd) && 
                        (*tokenEnd == '.' || *tokenEnd == '-' || *tokenEnd == '+' ||
                         isdigit(*tokenEnd) || *tokenEnd == '%' || 
                         (*tokenEnd >= 'a' && *tokenEnd <= 'z') ||
                         (*tokenEnd >= 'A' && *tokenEnd <= 'Z')))
                  {  tokenEnd++;
                  }
                  tokenLen = tokenEnd - bgValue;
                  if(tokenLen > 0)
                  {  if(!bgPositionStr)
                     {  token = ALLOCTYPE(UBYTE, tokenLen + 1, MEMF_FAST);
                        if(token)
                        {  memmove(token, bgValue, tokenLen);
                           token[tokenLen] = '\0';
                           bgPositionStr = token;
                        }
                     }
                     else
                     {  /* Append to existing position */
                        UBYTE *oldPos = bgPositionStr;
                        long oldLen = strlen((char *)oldPos);
                        token = ALLOCTYPE(UBYTE, tokenLen + 1, MEMF_FAST);
                        if(token)
                        {  memmove(token, bgValue, tokenLen);
                           token[tokenLen] = '\0';
                           bgPositionStr = ALLOCTYPE(UBYTE, oldLen + 1 + tokenLen + 1, MEMF_FAST);
                           if(bgPositionStr)
                           {  memmove(bgPositionStr, oldPos, oldLen);
                              bgPositionStr[oldLen] = ' ';
                              memmove(bgPositionStr + oldLen + 1, token, tokenLen);
                              bgPositionStr[oldLen + 1 + tokenLen] = '\0';
                           }
                           FREE(token);
                        }
                        FREE(oldPos);
                     }
                     bgValue = tokenEnd;
                     continue;
                  }
               }
               
               /* Check for color (hex # or color name) */
               if(*bgValue == '#')
               {  /* Hex color */
                  tokenEnd = bgValue + 1;
                  while(*tokenEnd && ((*tokenEnd >= '0' && *tokenEnd <= '9') ||
                                      (*tokenEnd >= 'a' && *tokenEnd <= 'f') ||
                                      (*tokenEnd >= 'A' && *tokenEnd <= 'F')))
                  {  tokenEnd++;
                  }
                  tokenLen = tokenEnd - bgValue;
                  if(tokenLen > 1 && tokenLen <= 7)
                  {  bgColorStr = ALLOCTYPE(UBYTE, tokenLen + 1, MEMF_FAST);
                     if(bgColorStr)
                     {  memmove(bgColorStr, bgValue, tokenLen);
                        bgColorStr[tokenLen] = '\0';
                     }
                     bgValue = tokenEnd;
                     continue;
                  }
               }
               else
               {  /* Try color name - parse until whitespace */
                  tokenEnd = bgValue;
                  while(*tokenEnd && !isspace(*tokenEnd) && 
                        (isalnum(*tokenEnd) || *tokenEnd == '-'))
                  {  tokenEnd++;
                  }
                  tokenLen = tokenEnd - bgValue;
                  if(tokenLen > 0)
                  {  /* Check if it's "transparent" keyword first */
                     token = ALLOCTYPE(UBYTE, tokenLen + 1, MEMF_FAST);
                     if(token)
                     {  memmove(token, bgValue, tokenLen);
                        token[tokenLen] = '\0';
                        if(Stricmp((char *)token, "transparent") == 0)
                        {  /* Set bgcolor to -1 to indicate transparent */
                           Asetattrs(body, AOBDY_Bgcolor, -1, TAG_END);
                           FREE(token);
                           bgValue = tokenEnd;
                           continue;
                        }
                        /* Check if it's a valid color name by trying to parse it */
                        bgColorRgb = ParseHexColor(token);
                        if(bgColorRgb != ~0)
                        {  /* Valid color name */
                           bgColorStr = token;
                           token = NULL; /* Don't free it */
                        }
                        if(token) FREE(token);
                     }
                     bgValue = tokenEnd;
                     continue;
                  }
               }
               
               /* Unknown token - skip one character to avoid infinite loop */
               bgValue++;
            }
            
            /* Apply parsed values */
            if(bgColorStr)
            {  bgColorRgb = ParseHexColor(bgColorStr);
               if(bgColorRgb != ~0)
               {  bgColorCi = Finddoccolor(doc, bgColorRgb);
                  if(bgColorCi)
                  {  Asetattrs(body, AOBDY_Bgcolor, COLOR(bgColorCi), TAG_END);
                  }
               }
               FREE(bgColorStr);
            }
            if(bgImageStr)
            {  bgimg = Backgroundimg(doc, bgImageStr);
               if(bgimg)
               {  Asetattrs(body, AOBDY_Bgimage, bgimg, TAG_END);
               }
               FREE(bgImageStr);
            }
            if(bgRepeatStr)
            {  Asetattrs(body, AOBDY_BackgroundRepeat, bgRepeatStr, TAG_END);
            }
            if(bgPositionStr)
            {  Asetattrs(body, AOBDY_BackgroundPosition, bgPositionStr, TAG_END);
            }
            if(bgAttachmentStr)
            {  Asetattrs(body, AOBDY_BackgroundAttachment, bgAttachmentStr, TAG_END);
            }
         }
         /* Apply border - parse width, style, and color from "2px solid #color" or "2px" format */
         else if(Stricmp((char *)prop->name,"border") == 0)
         {  /* Parse border shorthand: width style color */
            UBYTE *pval;
            UBYTE *token;
            long borderWidth;
            struct Number num;
            ULONG borderColor;
            
            pval = prop->value;
            borderWidth = -1;
            borderColor = ~0;
            
            /* Parse tokens separated by spaces */
            while(*pval)
            {  SkipWhitespace(&pval);
               if(!*pval) break;
               
               token = pval;
               while(*pval && !isspace(*pval)) pval++;
               
               /* Check if it's a width (starts with digit or has px/em/etc) */
               if(isdigit(*token) || *token == '+' || *token == '-')
               {  UBYTE *end;
                  long len;
                  len = pval - token;
                  end = token + len;
                  if(borderWidth < 0)
                  {  borderWidth = ParseCSSLengthValue(token,&num);
                  }
               }
               /* Check if it's a color (starts with #) */
               else if(*token == '#')
               {  borderColor = ParseHexColor(token);
               }
               /* Otherwise it's probably a style (solid, dashed, dotted, etc.) */
               else
               {  UBYTE *styleStr;
                  UBYTE *end;
                  long len;
                  len = pval - token;
                  end = token + len;
                  styleStr = ALLOCTYPE(UBYTE, len + 1, MEMF_FAST);
                  if(styleStr)
                  {  memmove(styleStr, token, len);
                     styleStr[len] = '\0';
                     /* Asetattrs takes ownership of the allocated string */
                     Asetattrs(body, AOBDY_BorderStyle, styleStr, TAG_END);
                  }
               }
            }
            
            /* Store border width and color */
            if(borderWidth >= 0 && num.type == NUMBER_NUMBER)
            {  Asetattrs(body, AOBDY_BorderWidth, borderWidth, TAG_END);
            }
            if(borderColor != ~0)
            {  ci = Finddoccolor(doc, borderColor);
               if(ci)
               {  Asetattrs(body, AOBDY_BorderColor, ci, TAG_END);
               }
            }
         }
         /* Apply border-style - Asetattrs takes ownership of the Dupstr'd string */
         else if(Stricmp((char *)prop->name,"border-style") == 0)
         {  UBYTE *styleStr;
            styleStr = Dupstr(prop->value, -1);
            if(styleStr)
            {  Asetattrs(body, AOBDY_BorderStyle, styleStr, TAG_END);
            }
         }
         /* Apply border-color */
         else if(Stricmp((char *)prop->name,"border-color") == 0)
         {  ULONG colorrgb;
            struct Colorinfo *ci;
            colorrgb = ParseHexColor(prop->value);
            if(colorrgb != ~0)
            {  ci = Finddoccolor(doc,colorrgb);
               if(ci)
               {  Asetattrs(body, AOBDY_BorderColor, ci, TAG_END);
               }
            }
         }
         /* Apply border-radius (CSS3 - not implemented, but parse to avoid errors) */
         else if(Stricmp((char *)prop->name,"border-radius") == 0)
         {  /* CSS3 feature - not implemented */
         }
         /* Apply text-align */
         else if(Stricmp((char *)prop->name,"text-align") == 0)
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
            {  align = -1;
            }
            if(align >= 0)
            {  /* Apply text-align to various elements that support align attribute */
               if(tagname && Stricmp((char *)tagname,"DIV") == 0)
               {  Asetattrs(body,AOBDY_Divalign,align,TAG_END);
               }
               else if(tagname && Stricmp((char *)tagname,"P") == 0)
               {  Asetattrs(body,AOBDY_Align,align,TAG_END);
               }
               else if(tagname && (Stricmp((char *)tagname,"TD") == 0 || Stricmp((char *)tagname,"TH") == 0))
               {  /* For table cells, alignment is handled via table attributes in ApplyCSSToTableCell */
                  /* But we can also set it on the body */
                  Asetattrs(body,AOBDY_Divalign,align,TAG_END);
               }
               else
               {  /* Default: apply to div align for other block elements */
                  Asetattrs(body,AOBDY_Divalign,align,TAG_END);
               }
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
         /* Apply font-weight */
         else if(Stricmp((char *)prop->name,"font-weight") == 0)
         {  if(Stricmp((char *)prop->value,"bold") == 0 || Stricmp((char *)prop->value,"700") == 0 || Stricmp((char *)prop->value,"bolder") == 0)
            {  Asetattrs(body,AOBDY_Sethardstyle,FSF_BOLD,TAG_END);
            }
            else if(Stricmp((char *)prop->value,"normal") == 0 || Stricmp((char *)prop->value,"400") == 0 || Stricmp((char *)prop->value,"lighter") == 0)
            {  Asetattrs(body,AOBDY_Unsethardstyle,FSF_BOLD,TAG_END);
            }
            /* Also support numeric values 100-900 */
            else
            {  long weightValue;
               weightValue = ParseCSSLengthValue(prop->value,&num);
               if(weightValue >= 600)
               {  Asetattrs(body,AOBDY_Sethardstyle,FSF_BOLD,TAG_END);
               }
               else if(weightValue >= 0 && weightValue < 600)
               {  Asetattrs(body,AOBDY_Unsethardstyle,FSF_BOLD,TAG_END);
               }
            }
         }
         /* Apply text-decoration - can have multiple space-separated values
          * like "underline line-through". Compare each word individually
          * using Strnicmp with the word length. */
         else if(Stricmp((char *)prop->name,"text-decoration") == 0)
         {  UBYTE *decValue;
            UBYTE *pdec;
            UBYTE *wordStart;
            long wordLen;
            decValue = Dupstr(prop->value,-1);
            if(decValue)
            {  pdec = decValue;
               while(*pdec)
               {  SkipWhitespace(&pdec);
                  if(!*pdec) break;
                  
                  /* Find current word boundaries */
                  wordStart = pdec;
                  while(*pdec && !isspace(*pdec)) pdec++;
                  wordLen = pdec - wordStart;
                  
                  if((wordLen == 12 && Strnicmp((char *)wordStart,"line-through",12) == 0) ||
                     (wordLen == 13 && Strnicmp((char *)wordStart,"strikethrough",13) == 0))
                  {  Asetattrs(body,AOBDY_Sethardstyle,FSF_STRIKE,TAG_END);
                  }
                  else if(wordLen == 9 && Strnicmp((char *)wordStart,"underline",9) == 0)
                  {  Asetattrs(body,AOBDY_Sethardstyle,FSF_UNDERLINED,TAG_END);
                  }
                  else if(wordLen == 4 && Strnicmp((char *)wordStart,"none",4) == 0)
                  {  /* Remove all text decorations */
                     Asetattrs(body,AOBDY_Unsethardstyle,FSF_STRIKE,TAG_END);
                     Asetattrs(body,AOBDY_Unsethardstyle,FSF_UNDERLINED,TAG_END);
                  }
               }
               FREE(decValue);
            }
         }
         /* Apply white-space */
         /* Apply cursor - Asetattrs takes ownership of the Dupstr'd string */
         else if(Stricmp((char *)prop->name,"cursor") == 0)
         {  UBYTE *cursorStr;
            cursorStr = Dupstr(prop->value, -1);
            if(cursorStr)
            {  Asetattrs(body, AOBDY_Cursor, cursorStr, TAG_END);
            }
         }
         /* Apply white-space */
         else if(Stricmp((char *)prop->name,"white-space") == 0)
         {  UBYTE *whitespaceStr;
            whitespaceStr = Dupstr(prop->value, -1);
            if(whitespaceStr)
            {  /* Asetattrs takes ownership of the Dupstr'd string */
               Asetattrs(body, AOBDY_WhiteSpace, whitespaceStr, TAG_END);
               /* Also apply to AOBDY_Nobr for nowrap.
                * Use prop->value for comparisons since whitespaceStr
                * is now owned by the body object. */
               if(Stricmp((char *)prop->value, "nowrap") == 0)
               {  Asetattrs(body, AOBDY_Nobr, TRUE, TAG_END);
               }
               else if(Stricmp((char *)prop->value, "normal") == 0 || 
                       Stricmp((char *)prop->value, "pre-wrap") == 0 || 
                       Stricmp((char *)prop->value, "pre-line") == 0)
               {  Asetattrs(body, AOBDY_Nobr, FALSE, TAG_END);
               }
               /* Note: "pre" is handled by STYLE_PRE, not white-space */
            }
         }
         /* Apply text-transform - Asetattrs takes ownership of the Dupstr'd string */
         else if(Stricmp((char *)prop->name,"text-transform") == 0)
         {  UBYTE *transformStr;
            transformStr = Dupstr(prop->value, -1);
            if(transformStr)
            {  Asetattrs(body, AOBDY_TextTransform, transformStr, TAG_END);
               /* Also set document-level text-transform for compatibility.
                * Use prop->value for comparisons since transformStr is
                * now owned by the body object. */
               if(Stricmp((char *)prop->value, "uppercase") == 0)
               {  doc->texttransform = 1;
               }
               else if(Stricmp((char *)prop->value, "lowercase") == 0)
               {  doc->texttransform = 2;
               }
               else if(Stricmp((char *)prop->value, "capitalize") == 0)
               {  doc->texttransform = 3;
               }
               else if(Stricmp((char *)prop->value, "none") == 0)
               {  doc->texttransform = 0;
               }
            }
         }
         /* Apply font-family (full comma-separated list; Matchfont parses fallbacks) */
         else if(Stricmp((char *)prop->name,"font-family") == 0)
         {  fontFace = Dupstr(prop->value,-1);
            if(fontFace)
            {  Stripcssfontfamilyquotes(fontFace);
               if(httpdebug)
               {  printf("[CSS] font-family (inline on body tag=%s): raw='%s' -> AOBDY_Fontface='%s' body=%p\n",
                     tagname ? (char *)tagname : "(null)",
                     prop->value ? (char *)prop->value : "(null)",
                     (char *)fontFace, body);
               }
               Asetattrs(body,AOBDY_Fontface,fontFace,TAG_END);
               FREE(fontFace);
            }
            else if(httpdebug)
            {  printf("[CSS] font-family (inline): Dupstr failed raw='%s'\n",
                  prop->value ? (char *)prop->value : "(null)");
            }
         }
         /* Apply font-size */
         else if(Stricmp((char *)prop->name,"font-size") == 0)
         {  fontSize = 0;
            isRelative = FALSE;
            if(Stricmp((char *)prop->value,"xx-small") == 0)
            {  fontSize = 1;
            }
            else if(Stricmp((char *)prop->value,"x-small") == 0)
            {  fontSize = 2;
            }
            else if(Stricmp((char *)prop->value,"small") == 0)
            {  fontSize = 3;
            }
            else if(Stricmp((char *)prop->value,"medium") == 0)
            {  fontSize = 4;
            }
            else if(Stricmp((char *)prop->value,"large") == 0)
            {  fontSize = 5;
            }
            else if(Stricmp((char *)prop->value,"x-large") == 0)
            {  fontSize = 6;
            }
            else if(Stricmp((char *)prop->value,"xx-large") == 0)
            {  fontSize = 7;
            }
            else if(Stricmp((char *)prop->value,"smaller") == 0)
            {  fontSize = -1;
               isRelative = TRUE;
            }
            else if(Stricmp((char *)prop->value,"larger") == 0)
            {  fontSize = 1;
               isRelative = TRUE;
            }
            if(fontSize != 0)
            {  if(isRelative)
               {  Asetattrs(body,AOBDY_Fontsizerel,fontSize,TAG_END);
               }
               else
               {  Asetattrs(body,AOBDY_Fontsize,fontSize,TAG_END);
               }
            }
         }
         /* Apply color */
         else if(Stricmp((char *)prop->name,"color") == 0)
         {  /* Don't apply color to body font color for anchor tags.
              * Link colors are handled at the document level via ApplyCSSToLinkColors.
              * Setting color on the body would incorrectly affect all body text, not just the link. */
            if(!tagname || Stricmp((char *)tagname,"A") != 0)
            {  Gethexcolor(doc,prop->value,&colorrgb);
               if(colorrgb != (ULONG)~0)
               {  ci = Finddoccolor(doc,colorrgb);
                  if(ci)
                  {  Asetattrs(body,AOBDY_Fontcolor,ci,TAG_END);
                  }
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
            BOOL marginTopAuto;
            BOOL marginRightAuto;
            BOOL marginBottomAuto;
            BOOL marginLeftAuto;
            marginValue = prop->value;
            marginCount = 0;
            marginTop = marginRight = marginBottom = marginLeft = 0;
            marginTopAuto = marginRightAuto = marginBottomAuto = marginLeftAuto = FALSE;
            
            /* Parse margin values - can be 1, 2, 3, or 4 values */
            marginP = marginValue;
            for(i = 0; i < 4 && marginP && *marginP; i++)
            {  /* Skip whitespace */
               while(*marginP && isspace(*marginP)) marginP++;
               if(!*marginP) break;
               tokenStart = marginP;
               /* Find end of token */
               while(*marginP && !isspace(*marginP)) marginP++;
               tokenEnd = marginP;
               tokenLen = tokenEnd - tokenStart;
               if(tokenLen > 0)
               {  /* Copy token to temporary buffer */
                  tokenBuf = ALLOCTYPE(UBYTE,tokenLen + 1,MEMF_FAST);
                  if(tokenBuf)
                  {  memmove(tokenBuf,tokenStart,tokenLen);
                     tokenBuf[tokenLen] = '\0';
                     marginTokens[marginCount] = tokenBuf;
                     marginCount++;
                  }
               }
            }
            
            /* Apply margin values based on count */
            if(marginCount >= 1)
            {  /* Check if first token is "auto" */
               if(Stricmp((char *)marginTokens[0], "auto") == 0)
               {  marginTopAuto = TRUE;
                  marginTop = 0;
               }
               else
               {  marginTop = ParseCSSLengthValue(marginTokens[0],&num);
                  marginTopAuto = FALSE;
               }
               
               if(marginCount == 1)
               {  /* All sides same */
                  marginRight = marginBottom = marginLeft = marginTop;
                  marginRightAuto = marginBottomAuto = marginLeftAuto = marginTopAuto;
               }
               else if(marginCount == 2)
               {  /* Top/bottom, left/right */
                  marginBottom = marginTop;
                  marginBottomAuto = marginTopAuto;
                  /* Check if second token is "auto" */
                  if(Stricmp((char *)marginTokens[1], "auto") == 0)
                  {  marginRightAuto = TRUE;
                     marginLeftAuto = TRUE;
                     marginRight = 0;
                     marginLeft = 0;
                  }
                  else
                  {  marginRight = ParseCSSLengthValue(marginTokens[1],&num);
                     marginLeft = marginRight;
                     marginRightAuto = FALSE;
                     marginLeftAuto = FALSE;
                  }
               }
               else if(marginCount == 3)
               {  /* Top, left/right, bottom */
                  /* Check if second token is "auto" */
                  if(Stricmp((char *)marginTokens[1], "auto") == 0)
                  {  marginRightAuto = TRUE;
                     marginLeftAuto = TRUE;
                     marginRight = 0;
                     marginLeft = 0;
                  }
                  else
                  {  marginRight = ParseCSSLengthValue(marginTokens[1],&num);
                     marginLeft = marginRight;
                     marginRightAuto = FALSE;
                     marginLeftAuto = FALSE;
                  }
                  /* Check if third token is "auto" */
                  if(Stricmp((char *)marginTokens[2], "auto") == 0)
                  {  marginBottomAuto = TRUE;
                     marginBottom = 0;
                  }
                  else
                  {  marginBottom = ParseCSSLengthValue(marginTokens[2],&num);
                     marginBottomAuto = FALSE;
                  }
               }
               else if(marginCount == 4)
               {  /* Top, right, bottom, left */
                  /* Check if second token is "auto" */
                  if(Stricmp((char *)marginTokens[1], "auto") == 0)
                  {  marginRightAuto = TRUE;
                     marginRight = 0;
                  }
                  else
                  {  marginRight = ParseCSSLengthValue(marginTokens[1],&num);
                     marginRightAuto = FALSE;
                  }
                  /* Check if third token is "auto" */
                  if(Stricmp((char *)marginTokens[2], "auto") == 0)
                  {  marginBottomAuto = TRUE;
                     marginBottom = 0;
                  }
                  else
                  {  marginBottom = ParseCSSLengthValue(marginTokens[2],&num);
                     marginBottomAuto = FALSE;
                  }
                  /* Check if fourth token is "auto" */
                  if(Stricmp((char *)marginTokens[3], "auto") == 0)
                  {  marginLeftAuto = TRUE;
                     marginLeft = 0;
                  }
                  else
                  {  marginLeft = ParseCSSLengthValue(marginTokens[3],&num);
                     marginLeftAuto = FALSE;
                  }
               }
               
               /* Apply margins */
               if(marginTop >= 0 && !marginTopAuto) Asetattrs(body,AOBDY_Topmargin,marginTop,TAG_END);
               if(marginLeft >= 0 && !marginLeftAuto) Asetattrs(body,AOBDY_Leftmargin,marginLeft,TAG_END);
               
               /* Set auto flags for left and right margins (for centering) */
               if(marginLeftAuto)
               {  SetBodyMarginLeftAuto((struct Body *)body, TRUE);
               }
               if(marginRightAuto)
               {  SetBodyMarginRightAuto((struct Body *)body, TRUE);
               }
               /* Apply margin-right and margin-bottom */
               if(marginRight >= 0 && !marginRightAuto) Asetattrs(body, AOBDY_MarginRight, marginRight, TAG_END);
               if(marginBottom >= 0 && !marginBottomAuto) Asetattrs(body, AOBDY_MarginBottom, marginBottom, TAG_END);
            }
            
            /* Free temporary token buffers */
            for(i = 0; i < marginCount; i++)
            {  if(marginTokens[i]) FREE(marginTokens[i]);
            }
         }
         /* Apply line-height */
         else if(Stricmp((char *)prop->name,"line-height") == 0)
         {  struct Body *bd;
            lineHeightStr = prop->value;
            /* Skip whitespace */
            while(*lineHeightStr && isspace(*lineHeightStr)) lineHeightStr++;
            /* Parse unitless value (e.g., 1.42857143) or pixel value (e.g., 20px) */
            if(sscanf((char *)lineHeightStr,"%f",&lineHeightValue) == 1)
            {  /* Store line-height per body for layout calculations */
               SetBodyLineHeight((struct Body *)body, lineHeightValue);
            }
         }
         /* Apply font-variant */
         else if(Stricmp((char *)prop->name,"font-variant") == 0)
         {  /* font-variant: normal (default) - no special handling needed */
            /* font-variant: small-caps - would require special rendering logic
             * to convert lowercase letters to smaller uppercase letters.
             * This would need changes to the text rendering code in body.c
             * and element rendering code. For now, we parse and ignore
             * (normal is the default behavior). */
            /* Note: Small-caps rendering is a complex feature that would require:
             * 1. Detecting lowercase characters in text
             * 2. Rendering them as smaller uppercase letters
             * 3. Adjusting character spacing and metrics
             * This is beyond the scope of CSS property application. */
         }
         /* Apply display */
         else if(Stricmp((char *)prop->name,"display") == 0)
         {  UBYTE *dispStr;
            UBYTE *rawValue;
            UBYTE *end;
            long dispLen;
            displayValue = prop->value;
            rawValue = displayValue;
            /* Skip whitespace */
            while(*displayValue && isspace(*displayValue)) displayValue++;
            /* Trim trailing whitespace and an optional trailing ';' */
            end = displayValue;
            while(*end) end++;
            while(end > displayValue && (isspace(*(end - 1)) || *(end - 1) == ';'))
            {  end--;
            }
            dispLen = end - displayValue;
            if(dispLen > 0)
            {  /* Store trimmed display value.
                * body.c uses Stricmp(bd->display,"none") so trailing whitespace/';'
                * would prevent hiding. */
               dispStr = Dupstr(displayValue, dispLen);
               if(dispStr)
               {  Asetattrs(body, AOBDY_Display, dispStr, TAG_END);
               }
            }
            else
            {  dispStr = NULL;
            }
         }
         /* Apply width */
         else if(Stricmp((char *)prop->name,"width") == 0)
         {  long widthValue;
            struct Number widthNum;
            
            widthValue = ParseCSSLengthValue(prop->value, &widthNum);
            if(widthValue >= 0 && widthNum.type != NUMBER_NONE)
            {  /* Apply width to body object */
               Asetattrs(body, AOBJ_Width, widthValue, TAG_END);
            }
         }
         /* Apply height */
         else if(Stricmp((char *)prop->name,"height") == 0)
         {  long heightValue;
            struct Number heightNum;
            
            heightValue = ParseCSSLengthValue(prop->value, &heightNum);
            if(heightValue >= 0 && heightNum.type != NUMBER_NONE)
            {  /* Apply height to body object */
               Asetattrs(body, AOBJ_Height, heightValue, TAG_END);
            }
         }
         /* Apply position */
         else if(Stricmp((char *)prop->name,"position") == 0)
         {  UBYTE *posValue;
            UBYTE *posStr;
            
            posValue = prop->value;
            SkipWhitespace(&posValue);
            
            /* Asetattrs takes ownership of the Dupstr'd string */
            posStr = Dupstr(posValue, -1);
            if(posStr)
            {  Asetattrs(body, AOBDY_Position, posStr, TAG_END);
            }
         }
         /* Apply top */
         else if(Stricmp((char *)prop->name,"top") == 0)
         {  long topValue;
            struct Number topNum;
            
            topValue = ParseCSSLengthValue(prop->value, &topNum);
            /* Allow negative values for positioning (e.g., top: -12px) */
            if(topNum.type == NUMBER_NUMBER || topNum.type == NUMBER_SIGNED)
            {  /* Apply top position (can be negative) */
               Asetattrs(body, AOBJ_Top, topValue, TAG_END);
            }
            else if(topNum.type == NUMBER_PERCENT)
            {  /* Store percentage value (0-10000 for 0-100%) */
               /* Convert percentage (0-100) to 0-10000 scale */
               long percentValue = topNum.n * 100;
               if(percentValue < 0) percentValue = 0;
               if(percentValue > 10000) percentValue = 10000;
               Asetattrs(body, AOBDY_TopPercent, percentValue, TAG_END);
            }
         }
         /* Apply left */
         else if(Stricmp((char *)prop->name,"left") == 0)
         {  long leftValue;
            struct Number leftNum;
            
            leftValue = ParseCSSLengthValue(prop->value, &leftNum);
            /* Allow negative values for positioning (e.g., left: -1px) */
            if(leftNum.type == NUMBER_NUMBER || leftNum.type == NUMBER_SIGNED)
            {  /* Apply left position (can be negative) */
               Asetattrs(body, AOBJ_Left, leftValue, TAG_END);
            }
            else if(leftNum.type == NUMBER_PERCENT)
            {  /* Store percentage value (0-10000 for 0-100%) */
               /* Convert percentage (0-100) to 0-10000 scale */
               long percentValue = leftNum.n * 100;
               if(percentValue < 0) percentValue = 0;
               if(percentValue > 10000) percentValue = 10000;
               Asetattrs(body, AOBDY_LeftPercent, percentValue, TAG_END);
            }
         }
         /* Apply right */
         else if(Stricmp((char *)prop->name,"right") == 0)
         {  long rightValue;
            struct Number rightNum;
            
            rightValue = ParseCSSLengthValue(prop->value, &rightNum);
            if(rightValue >= 0 && rightNum.type == NUMBER_NUMBER)
            {  /* Store right position - will be calculated during layout */
               Asetattrs(body, AOBDY_Right, rightValue, TAG_END);
            }
         }
         /* Apply bottom */
         else if(Stricmp((char *)prop->name,"bottom") == 0)
         {  long bottomValue;
            struct Number bottomNum;
            
            bottomValue = ParseCSSLengthValue(prop->value, &bottomNum);
            if(bottomValue >= 0 && bottomNum.type == NUMBER_NUMBER)
            {  /* Store bottom position - will be calculated during layout */
               Asetattrs(body, AOBDY_Bottom, bottomValue, TAG_END);
            }
         }
         /* Apply z-index */
         else if(Stricmp((char *)prop->name,"z-index") == 0)
         {  long zIndexValue;
            UBYTE *zval;
            
            zval = prop->value;
            SkipWhitespace(&zval);
            
            if(Stricmp((char *)zval,"auto") == 0)
            {  /* Auto z-index (default) - store as 0 */
               zIndexValue = 0;
            }
            else
            {  /* Parse numeric z-index value */
               zIndexValue = strtol((char *)zval, NULL, 10);
            }
            Asetattrs(body, AOBDY_ZIndex, zIndexValue, TAG_END);
         }
         /* NOTE: display property is handled earlier in this function (not duplicated here) */
         /* Apply vertical-align */
         else if(Stricmp((char *)prop->name,"vertical-align") == 0)
         {  UBYTE *valignValue;
            short valign;
            
            valignValue = prop->value;
            SkipWhitespace(&valignValue);
            
            valign = -1;
            if(Stricmp((char *)valignValue,"top") == 0)
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
            
            if(valign >= 0)
            {  Asetattrs(body, AOBDY_VerticalAlign, valign, TAG_END);
            }
         }
         /* Apply clear */
         else if(Stricmp((char *)prop->name,"clear") == 0)
         {  UBYTE *clearValue;
            UBYTE *clearStr;
            
            clearValue = prop->value;
            SkipWhitespace(&clearValue);
            
            /* Asetattrs takes ownership of the Dupstr'd string */
            clearStr = Dupstr(clearValue, -1);
            if(clearStr)
            {  Asetattrs(body, AOBDY_Clear, clearStr, TAG_END);
            }
         }
         /* Apply overflow */
         else if(Stricmp((char *)prop->name,"overflow") == 0)
         {  UBYTE *overflowValue;
            UBYTE *overflowStr;
            
            overflowValue = prop->value;
            SkipWhitespace(&overflowValue);
            
            /* Asetattrs takes ownership of the Dupstr'd string */
            overflowStr = Dupstr(overflowValue, -1);
            if(overflowStr)
            {  Asetattrs(body, AOBDY_Overflow, overflowStr, TAG_END);
            }
         }
         /* Apply list-style */
         else if(Stricmp((char *)prop->name,"list-style") == 0)
         {  UBYTE *listStyleValue;
            UBYTE *listStyleStr;
            
            listStyleValue = prop->value;
            SkipWhitespace(&listStyleValue);
            
            /* Asetattrs takes ownership of the Dupstr'd string */
            listStyleStr = Dupstr(listStyleValue, -1);
            if(listStyleStr)
            {  Asetattrs(body, AOBDY_ListStyle, listStyleStr, TAG_END);
            }
         }
         /* Apply list-style-image */
         else if(Stricmp((char *)prop->name,"list-style-image") == 0)
         {  UBYTE *urlValue;
            UBYTE *url;
            UBYTE *urlStr;
            
            /* Parse url(...) format */
            urlValue = prop->value;
            /* Skip whitespace */
            while(*urlValue && isspace(*urlValue)) urlValue++;
            
            /* Check for url( */
            if(Strnicmp((char *)urlValue,"url(",4) == 0)
            {  UBYTE *start;
               UBYTE *end;
               long len;
               start = urlValue + 4;
               /* Skip whitespace after url( */
               while(*start && isspace(*start)) start++;
               /* Find closing ) */
               end = (UBYTE *)strchr((char *)start,')');
               if(end && end > start)
               {  /* Trim quotes if present */
                  if((*start == '"' || *start == '\'') && end > start + 1)
                  {  start++;
                     if(*(end - 1) == '"' || *(end - 1) == '\'')
                     {  end--;
                     }
                  }
                  len = end - start;
                  if(len > 0)
                  {  urlStr = ALLOCTYPE(UBYTE,len + 1,MEMF_FAST);
                     if(urlStr)
                     {  memmove(urlStr,start,len);
                        urlStr[len] = '\0';
                        Asetattrs(body, AOBDY_ListStyleImage, urlStr, TAG_END);
                     }
                  }
               }
            }
            else if(Strnicmp((char *)urlValue,"none",4) == 0)
            {  /* list-style-image: none - clear the image */
               Asetattrs(body, AOBDY_ListStyleImage, NULL, TAG_END);
            }
         }
         /* Apply min-width */
         else if(Stricmp((char *)prop->name,"min-width") == 0)
         {  long minWidthValue;
            struct Number minWidthNum;
            
            minWidthValue = ParseCSSLengthValue(prop->value, &minWidthNum);
            if(minWidthValue >= 0 && minWidthNum.type == NUMBER_NUMBER)
            {  Asetattrs(body, AOBDY_MinWidth, minWidthValue, TAG_END);
            }
         }
         /* Apply max-width */
         else if(Stricmp((char *)prop->name,"max-width") == 0)
         {  long maxWidthValue;
            struct Number maxWidthNum;
            
            maxWidthValue = ParseCSSLengthValue(prop->value, &maxWidthNum);
            if(maxWidthValue >= 0 && maxWidthNum.type == NUMBER_NUMBER)
            {  Asetattrs(body, AOBDY_MaxWidth, maxWidthValue, TAG_END);
            }
         }
         /* Apply min-height */
         else if(Stricmp((char *)prop->name,"min-height") == 0)
         {  long minHeightValue;
            struct Number minHeightNum;
            
            minHeightValue = ParseCSSLengthValue(prop->value, &minHeightNum);
            if(minHeightValue >= 0 && minHeightNum.type == NUMBER_NUMBER)
            {  Asetattrs(body, AOBDY_MinHeight, minHeightValue, TAG_END);
            }
         }
         /* Apply max-height */
         else if(Stricmp((char *)prop->name,"max-height") == 0)
         {  long maxHeightValue;
            struct Number maxHeightNum;
            
            maxHeightValue = ParseCSSLengthValue(prop->value, &maxHeightNum);
            if(maxHeightValue >= 0 && maxHeightNum.type == NUMBER_NUMBER)
            {  Asetattrs(body, AOBDY_MaxHeight, maxHeightValue, TAG_END);
            }
         }
         /* Apply float - Note: Body objects don't directly support floating */
         /* But we parse it here for IsDivInline to detect float:left */
         /* The actual floating is handled by preventing line breaks in Dodiv */
         else if(Stricmp((char *)prop->name,"float") == 0)
         {  /* Float is parsed but not directly applied to body objects */
            /* IsDivInline will check for float:left and prevent line breaks */
         }
         /* Apply transform */
         else if(Stricmp((char *)prop->name,"transform") == 0)
         {  UBYTE *transformStr;
            UBYTE *pval;
            
            pval = prop->value;
            SkipWhitespace(&pval);
            
            /* Store transform value as-is - will be parsed during layout */
            /* Supported formats: translate(x, y), translateX(x), translateY(y) */
            /* Asetattrs takes ownership of the Dupstr'd string */
            transformStr = Dupstr(pval, -1);
            if(transformStr)
            {  Asetattrs(body, AOBDY_Transform, transformStr, TAG_END);
            }
         }
         /* Apply margin-right */
         else if(Stricmp((char *)prop->name,"margin-right") == 0)
         {  long marginRightValue;
            struct Number marginRightNum;
            
            marginRightValue = ParseCSSLengthValue(prop->value, &marginRightNum);
            /* Allow negative values for margin-right */
            if(marginRightNum.type == NUMBER_NUMBER || marginRightNum.type == NUMBER_SIGNED)
            {  /* Store margin-right value (can be negative) */
               Asetattrs(body, AOBDY_MarginRight, marginRightValue, TAG_END);
            }
            else if(marginRightNum.type == NUMBER_PERCENT)
            {  /* Store percentage value (0-10000 for 0-100%) */
               /* Convert percentage (0-100) to 0-10000 scale */
               long percentValue = marginRightNum.n * 100;
               if(percentValue < -10000) percentValue = -10000;
               if(percentValue > 10000) percentValue = 10000;
               /* For negative percentages, store as negative value */
               Asetattrs(body, AOBDY_MarginRight, percentValue, TAG_END);
            }
         }
         /* Apply margin-bottom */
         else if(Stricmp((char *)prop->name,"margin-bottom") == 0)
         {  long marginBottomValue;
            struct Number marginBottomNum;
            
            marginBottomValue = ParseCSSLengthValue(prop->value, &marginBottomNum);
            /* Allow negative values for margin-bottom */
            if(marginBottomNum.type == NUMBER_NUMBER || marginBottomNum.type == NUMBER_SIGNED)
            {  /* Store margin-bottom value (can be negative) */
               Asetattrs(body, AOBDY_MarginBottom, marginBottomValue, TAG_END);
            }
            else if(marginBottomNum.type == NUMBER_PERCENT)
            {  /* Store percentage value (0-10000 for 0-100%) */
               /* Convert percentage (0-100) to 0-10000 scale */
               long percentValue = marginBottomNum.n * 100;
               if(percentValue < -10000) percentValue = -10000;
               if(percentValue > 10000) percentValue = 10000;
               /* For negative percentages, store as negative value */
               Asetattrs(body, AOBDY_MarginBottom, percentValue, TAG_END);
            }
         }
         /* Apply grid-column-start (for grid layout positioning) */
         else if(Stricmp((char *)prop->name,"grid-column-start") == 0)
         {  long gridColStart;
            long leftMargin;
            UBYTE *pval;
            
            /* Parse grid-column-start value - can be a number (e.g., "2") or a length */
            pval = prop->value;
            SkipWhitespace(&pval);
            if(isdigit(*pval))
            {  /* Parse as integer */
               gridColStart = strtol((char *)pval,NULL,10);
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
            }
         }
         /* Apply grid-column-end (for grid layout positioning) */
         else if(Stricmp((char *)prop->name,"grid-column-end") == 0)
         {  /* Parse but not fully implemented */
         }
         /* Apply grid-row-start (for grid layout positioning) */
         else if(Stricmp((char *)prop->name,"grid-row-start") == 0)
         {  /* Parse but not fully implemented */
         }
         /* Apply grid-row-end (for grid layout positioning) */
         else if(Stricmp((char *)prop->name,"grid-row-end") == 0)
         {  /* Parse but not fully implemented */
         }
         /* Note: width, height, and vertical-align for table cells are handled
          * separately in ApplyCSSToTableCell() */
         
         /* Free the property */
         if(prop->name) FREE(prop->name);
         if(prop->value) FREE(prop->value);
         FREE(prop);
      }
      else
      {  /* Skip to next semicolon on parse error */
         while(*p && *p != ';')
         {  p++;
         }
      }
      
      /* Skip semicolon */
      if(*p == ';') p++;
   }
}

/* Parse CSS length value (pixels, percentages, etc.) */
long ParseCSSLengthValue(UBYTE *value,struct Number *num)
{  UBYTE *pval;
   char c = '\0';
   short m;
   BOOL sign = FALSE;
   double dval;
   long ival;
   UBYTE *endptr;
   
   if(!value || !num) return 0;
   
   num->n = 0;
   num->type = NUMBER_NONE;
   
   pval = value;
   while(*pval && isspace(*pval)) pval++;
   
   sign = (*pval == '+' || *pval == '-');
   if(*pval == '*')
   {  num->n = 1;
      c = '*';
      m = 1;
   }
   else
   {  /* Use strtod to parse decimal values (handles .5, 1.5, 0.5, etc.) */
      dval = strtod((char *)pval, (char **)&endptr);
      if(endptr != pval)
      {  /* Successfully parsed a number */
         /* Convert double to long, rounding */
         ival = (long)(dval + (dval >= 0 ? 0.5 : -0.5));
         num->n = ival;
         /* Check what comes after the number */
         if(*endptr)
         {  c = *endptr;
            if(c == '%') num->type = NUMBER_PERCENT;
            else if(c == '*') num->type = NUMBER_RELATIVE;
            else if(sign) num->type = NUMBER_SIGNED;
            else num->type = NUMBER_NUMBER;
            m = 2;
         }
         else
         {  /* No character - just a number */
            if(sign) num->type = NUMBER_SIGNED;
            else num->type = NUMBER_NUMBER;
            m = 1;
         }
      }
      else
      {  /* Fallback: try integer parsing with sscanf */
         m = sscanf((char *)pval," %ld%c",&num->n,&c);
         if(m)
         {  if(c == '%') num->type = NUMBER_PERCENT;
            else if(c == '*') num->type = NUMBER_RELATIVE;
            else if(sign) num->type = NUMBER_SIGNED;
            else num->type = NUMBER_NUMBER;
         }
      }
   }
   /* Don't clamp negative values - they're valid for positioning properties (top, left) */
   /* Only clamp if it's not a signed number and not a regular number (preserve negative for positioning) */
   if(num->type != NUMBER_SIGNED && num->type != NUMBER_NUMBER && num->n < 0) num->n = 0;
   
   return num->n;
}

/* Parse hex color value */
ULONG ParseHexColor(UBYTE *pcolor)
{  ULONG rgbval = ~0;
   ULONG rgb = 0;
   long len;
   UBYTE *start;
   UBYTE *p;
   
   if(!pcolor) return ~0;
   
   p = pcolor;
   /* Skip whitespace */
   while(*p && isspace(*p)) p++;
   if(!*p) return ~0;
   
   /* Check for # */
   if(*p == '#') p++;
   
   /* Parse hex digits */
   start = p;
   len = 0;
   while(*p && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
   {  p++;
      len++;
   }
   
   if(len == 3)
   {  /* Short format: #RGB -> #RRGGBB
       * Each nibble is duplicated: R -> RR, G -> GG, B -> BB */
      ULONG r = 0, g = 0, b = 0;
      if(start[0] >= '0' && start[0] <= '9') r = start[0] - '0';
      else if(start[0] >= 'a' && start[0] <= 'f') r = start[0] - 'a' + 10;
      else if(start[0] >= 'A' && start[0] <= 'F') r = start[0] - 'A' + 10;
      if(start[1] >= '0' && start[1] <= '9') g = start[1] - '0';
      else if(start[1] >= 'a' && start[1] <= 'f') g = start[1] - 'a' + 10;
      else if(start[1] >= 'A' && start[1] <= 'F') g = start[1] - 'A' + 10;
      if(start[2] >= '0' && start[2] <= '9') b = start[2] - '0';
      else if(start[2] >= 'a' && start[2] <= 'f') b = start[2] - 'a' + 10;
      else if(start[2] >= 'A' && start[2] <= 'F') b = start[2] - 'A' + 10;
      /* Duplicate each nibble: 0xR -> 0xRR, etc. */
      rgbval = (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
   }
   else if(len == 6)
   {  /* Full format: #RRGGBB */
      rgb = 0;
      if(start[0] >= '0' && start[0] <= '9') rgb |= (start[0] - '0') << 20;
      else if(start[0] >= 'a' && start[0] <= 'f') rgb |= (start[0] - 'a' + 10) << 20;
      else if(start[0] >= 'A' && start[0] <= 'F') rgb |= (start[0] - 'A' + 10) << 20;
      if(start[1] >= '0' && start[1] <= '9') rgb |= (start[1] - '0') << 16;
      else if(start[1] >= 'a' && start[1] <= 'f') rgb |= (start[1] - 'a' + 10) << 16;
      else if(start[1] >= 'A' && start[1] <= 'F') rgb |= (start[1] - 'A' + 10) << 16;
      if(start[2] >= '0' && start[2] <= '9') rgb |= (start[2] - '0') << 12;
      else if(start[2] >= 'a' && start[2] <= 'f') rgb |= (start[2] - 'a' + 10) << 12;
      else if(start[2] >= 'A' && start[2] <= 'F') rgb |= (start[2] - 'A' + 10) << 12;
      if(start[3] >= '0' && start[3] <= '9') rgb |= (start[3] - '0') << 8;
      else if(start[3] >= 'a' && start[3] <= 'f') rgb |= (start[3] - 'a' + 10) << 8;
      else if(start[3] >= 'A' && start[3] <= 'F') rgb |= (start[3] - 'A' + 10) << 8;
      if(start[4] >= '0' && start[4] <= '9') rgb |= (start[4] - '0') << 4;
      else if(start[4] >= 'a' && start[4] <= 'f') rgb |= (start[4] - 'a' + 10) << 4;
      else if(start[4] >= 'A' && start[4] <= 'F') rgb |= (start[4] - 'A' + 10) << 4;
      if(start[5] >= '0' && start[5] <= '9') rgb |= (start[5] - '0');
      else if(start[5] >= 'a' && start[5] <= 'f') rgb |= (start[5] - 'a' + 10);
      else if(start[5] >= 'A' && start[5] <= 'F') rgb |= (start[5] - 'A' + 10);
      rgbval = rgb;
   }
   
   /* If hex parsing failed, try color name lookup */
   if(rgbval == ~0)
   {  UBYTE buf[32];
      UBYTE *q;
      long bufLen;
      short a = 0;
      short b = NR_COLORNAMES - 1;
      short mid;
      long c;
      
      /* Copy color name to buffer, removing whitespace */
      q = pcolor;
      bufLen = 0;
      while(*q && bufLen < 31 && !isspace(*q))
      {  buf[bufLen++] = *q++;
      }
      buf[bufLen] = '\0';
      
      /* Binary search for color name */
      while(a <= b)
      {  mid = (a + b) / 2;
         c = Stricmp((char *)colornames[mid].name, (char *)buf);
         if(c == 0)
         {  rgbval = colornames[mid].color;
            break;
         }
         if(c < 0) a = mid + 1;
         else b = mid - 1;
      }
   }
   
   return rgbval;
}

/* Parse and apply inline CSS to a Link object */
void ApplyInlineCSSToLink(struct Document *doc,void *link,void *body,UBYTE *style)
{  struct CSSProperty *prop;
   UBYTE *p;
   
   if(!doc || !link || !style) return;
   
   p = style;
   while(*p)
   {  SkipWhitespace(&p);
      if(!*p) break;
      
      /* Parse property */
      prop = ParseProperty(doc,&p);
      if(prop && prop->name && prop->value)
      {  /* Apply text-decoration: none */
         if(Stricmp((char *)prop->name,"text-decoration") == 0)
         {  if(Stricmp((char *)prop->value,"none") == 0)
            {  Asetattrs(link,AOLNK_NoDecoration,TRUE,TAG_END);
            }
            else if(Stricmp((char *)prop->value,"underline") == 0)
            {  /* Remove no-decoration flag if underline is explicitly set */
               Asetattrs(link,AOLNK_NoDecoration,FALSE,TAG_END);
            }
         }
         /* Apply color to link (for inline styles) */
         else if(Stricmp((char *)prop->name,"color") == 0)
         {  ULONG colorrgb;
            struct Colorinfo *ci;
            colorrgb = ParseHexColor(prop->value);
            if(colorrgb != ~0)
            {  ci = Finddoccolor(doc,colorrgb);
               if(ci && body)
               {  /* Store link text color on body - will be applied to text elements when created */
                  Asetattrs(body,AOBDY_LinkTextColor,ci,TAG_END);
               }
            }
         }
         /* Note: a:link and a:visited colors are handled at the document level via ApplyCSSToLinkColors */
      }
      
      if(prop)
      {  /* Free the property after applying */
         if(prop->name) FREE(prop->name);
         if(prop->value) FREE(prop->value);
         FREE(prop);
      }
      else
      {  /* Parse error - skip to next semicolon */
         while(*p && *p != ';')
         {  p++;
         }
      }
      
      /* Skip semicolon */
      if(*p == ';') p++;
   }
}

/* Apply CSS from stylesheet to document link colors (a:link, a:visited) */
void ApplyCSSToLinkColors(struct Document *doc)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   struct CSSStylesheet *sheet;
   BOOL matches;
   ULONG colorrgb;
   struct Colorinfo *ci;
   BOOL linkColorSet = FALSE;
   BOOL visitedColorSet = FALSE;
   
   if(!doc || !doc->cssstylesheet) return;
   
   sheet = (struct CSSStylesheet *)doc->cssstylesheet;
   
   /* Find a:link and a:visited rules to set document link colors */
   /* Process :link and :visited FIRST, then fall back to 'a' without pseudo-class */
   /* Guard rule so we never dereference NULL when list is empty or uninitialized */
   for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
       rule && (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         sel && (struct MinNode *)sel->node.mln_Succ;
         sel = (struct CSSSelector *)sel->node.mln_Succ)
      {  matches = TRUE;
         
         /* Match element name - must be 'a' */
         if(sel->type & CSS_SEL_ELEMENT && sel->name)
         {  if(Stricmp((char *)sel->name,"a") != 0)
            {  matches = FALSE;
            }
         }
         
         /* Match pseudo-class - :link and :visited take priority */
         if(matches && (sel->type & CSS_SEL_PSEUDO) && sel->pseudo)
         {  if(Stricmp((char *)sel->pseudo,"link") == 0)
            {  /* Apply a:link color to doc->linkcolor */
               for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
                   prop && (struct MinNode *)prop->node.mln_Succ;
                   prop = (struct CSSProperty *)prop->node.mln_Succ)
               {  if(prop->name && prop->value && Stricmp((char *)prop->name,"color") == 0)
                  {  colorrgb = ParseHexColor(prop->value);
                     /* debug_printf("CSS: Parsing a:link color='%s', rgb=0x%08lx\n",
                                  prop->value ? (char *)prop->value : "NULL", colorrgb); */
                     if(colorrgb != ~0)
                     {  ci = Finddoccolor(doc,colorrgb);
                        if(ci)
                        {  doc->linkcolor = ci;
                           linkColorSet = TRUE;
                           /* debug_printf("CSS: Set doc->linkcolor from a:link (color=0x%08lx)\n", colorrgb); */
                        }
                        else
                        {  /* debug_printf("CSS: Failed to find/create color for a:link (rgb=0x%08lx)\n", colorrgb); */
                        }
                     }
                     else
                     {  /* debug_printf("CSS: Failed to parse a:link color='%s'\n",
                                      prop->value ? (char *)prop->value : "NULL"); */
                     }
                  }
               }
            }
            else if(Stricmp((char *)sel->pseudo,"visited") == 0)
            {  /* Apply a:visited color to doc->vlinkcolor */
               for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
                   prop && (struct MinNode *)prop->node.mln_Succ;
                   prop = (struct CSSProperty *)prop->node.mln_Succ)
               {  if(prop->name && prop->value && Stricmp((char *)prop->name,"color") == 0)
                  {  colorrgb = ParseHexColor(prop->value);
                     if(colorrgb != ~0)
                     {  ci = Finddoccolor(doc,colorrgb);
                        if(ci)
                        {  doc->vlinkcolor = ci;
                           visitedColorSet = TRUE;
                           /* debug_printf("CSS: Set doc->vlinkcolor from a:visited\n"); */
                        }
                     }
                  }
               }
            }
         }
      }
   }
   
   /* Second pass: Handle 'a' without pseudo-class as fallback default link color */
   if(!linkColorSet)
   {  for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
          rule && (struct MinNode *)rule->node.mln_Succ;
          rule = (struct CSSRule *)rule->node.mln_Succ)
      {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
            sel && (struct MinNode *)sel->node.mln_Succ;
            sel = (struct CSSSelector *)sel->node.mln_Succ)
         {  matches = TRUE;
            
            /* Match element name - must be 'a' */
            if(sel->type & CSS_SEL_ELEMENT && sel->name)
            {  if(Stricmp((char *)sel->name,"a") != 0)
               {  matches = FALSE;
               }
            }
            
            /* Must NOT have pseudo-class */
            if(matches && ((sel->type & CSS_SEL_PSEUDO) && sel->pseudo))
            {  matches = FALSE;
            }
            
            /* Apply 'a' without pseudo-class as default link color */
            if(matches)
            {  for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
                   prop && (struct MinNode *)prop->node.mln_Succ;
                   prop = (struct CSSProperty *)prop->node.mln_Succ)
               {  if(prop->name && prop->value && Stricmp((char *)prop->name,"color") == 0)
                  {  colorrgb = ParseHexColor(prop->value);
                     if(colorrgb != ~0)
                     {  ci = Finddoccolor(doc,colorrgb);
                        if(ci)
                        {  doc->linkcolor = ci;
                           linkColorSet = TRUE;
                           /* debug_printf("CSS: Set doc->linkcolor from 'a' (default fallback)\n"); */
                           break;
                        }
                     }
                  }
               }
            }
         }
      }
   }
   
   /* Register colors if they were set */
   if(linkColorSet || visitedColorSet)
   {  /* debug_printf("CSS: Link colors set - linkColorSet=%d visitedColorSet=%d frame=%p\n",
                   linkColorSet, visitedColorSet, doc->frame); */
      if(doc->frame)
      {  Registerdoccolors(doc);
         /* debug_printf("CSS: Registered link colors with frame\n"); */
      }
      else
      {  /* debug_printf("CSS: Frame not ready yet, colors will be registered when frame is created\n"); */
      }
   }
}

/* Apply CSS from stylesheet to a Link object with pseudo-class matching */
void ApplyCSSToLink(struct Document *doc,void *link,void *body)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   struct CSSStylesheet *sheet;
   BOOL matches;
   BOOL isVisited;
   UBYTE *linkClass;
   UBYTE *linkId;
   
   if(!doc || !link || !doc->cssstylesheet)
   {  /* debug_printf("CSS: ApplyCSSToLink skipped - doc=%p link=%p stylesheet=%p\n",
                   doc, link, (doc ? doc->cssstylesheet : NULL)); */
      return;
   }
   
   /* debug_printf("CSS: ApplyCSSToLink called\n"); */
   
   sheet = (struct CSSStylesheet *)doc->cssstylesheet;
   isVisited = (BOOL)Agetattr(link,AOLNK_Visited);
   
   /* Get class and ID from body element associated with link */
   linkClass = NULL;
   linkId = NULL;
   if(body)
   {  linkClass = (UBYTE *)Agetattr(body,AOBDY_Class);
      linkId = (UBYTE *)Agetattr(body,AOBDY_Id);
   }
   
   /* Find matching rules for 'a' element with pseudo-classes and class/ID selectors */
   for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         (struct MinNode *)sel->node.mln_Succ;
         sel = (struct CSSSelector *)sel->node.mln_Succ)
      {  matches = TRUE;
         
         /* Match element name - must be 'a' */
         if(sel->type & CSS_SEL_ELEMENT && sel->name)
         {  if(Stricmp((char *)sel->name,"a") != 0)
            {  matches = FALSE;
            }
         }
         
         /* Match class */
         if(matches && sel->type & CSS_SEL_CLASS && sel->class)
         {  if(!MatchClassAttribute(linkClass, sel->class))
            {  matches = FALSE;
            }
         }
         
         /* Match ID */
         if(matches && sel->type & CSS_SEL_ID && sel->id)
         {  if(!linkId || Stricmp((char *)sel->id,(char *)linkId) != 0)
            {  matches = FALSE;
            }
         }
         
         /* Match pseudo-class */
         if(matches && (sel->type & CSS_SEL_PSEUDO) && sel->pseudo)
         {  if(Stricmp((char *)sel->pseudo,"link") == 0)
            {  if(isVisited) matches = FALSE;
            }
            else if(Stricmp((char *)sel->pseudo,"visited") == 0)
            {  if(!isVisited) matches = FALSE;
            }
            else if(Stricmp((char *)sel->pseudo,"hover") == 0)
            {  /* Check if this link's body is currently hovered */
               if(!doc->hoveredElement || doc->hoveredElement != body)
               {  matches = FALSE;
               }
            }
            else if(Stricmp((char *)sel->pseudo,"active") == 0)
            {  /* Check if this link's body is currently active (clicked) */
               if(!doc->activeElement || doc->activeElement != body)
               {  matches = FALSE;
               }
            }
         }
         
         /* Apply properties if selector matches */
         if(matches)
         {  /* debug_printf("CSS: Link selector matched! Element=%s pseudo=%s\n",
                         (sel->name ? (char *)sel->name : "any"),
                         (sel->pseudo ? (char *)sel->pseudo : "none")); */
            for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
               (struct MinNode *)prop->node.mln_Succ;
               prop = (struct CSSProperty *)prop->node.mln_Succ)
            {  if(prop->name && prop->value)
               {                    /* Apply text-decoration: none */
                  if(Stricmp((char *)prop->name,"text-decoration") == 0)
                  {  /* debug_printf("CSS: Link property text-decoration=%s\n",prop->value); */
                     if(Stricmp((char *)prop->value,"none") == 0)
                     {  /* debug_printf("CSS: Setting link NoDecoration=TRUE\n"); */
                        Asetattrs(link,AOLNK_NoDecoration,TRUE,TAG_END);
                     }
                     else if(Stricmp((char *)prop->value,"underline") == 0)
                     {  /* Remove no-decoration flag if underline is explicitly set */
                        Asetattrs(link,AOLNK_NoDecoration,FALSE,TAG_END);
                     }
                  }
                  /* Apply color to link (for class-based selectors like .link-red) */
                  else if(Stricmp((char *)prop->name,"color") == 0)
                  {  ULONG colorrgb;
                     struct Colorinfo *ci;
                     colorrgb = ParseHexColor(prop->value);
                     if(colorrgb != ~0)
                     {  ci = Finddoccolor(doc,colorrgb);
                        if(ci && body)
                        {  /* Store link text color on body - will be applied to text elements when created */
                           Asetattrs(body,AOBDY_LinkTextColor,ci,TAG_END);
                        }
                     }
                  }
                  /* Note: font-family is inherited from parent elements, not set on links */
                  /* Note: a:link and a:visited colors are handled by ApplyCSSToLinkColors for document-level colors */
               }
            }
         }
      }
   }
}

/* Extract background-color from external CSS stylesheet rules matching class/ID */
struct Colorinfo *ExtractBackgroundColorFromRules(struct Document *doc,UBYTE *class,UBYTE *id,UBYTE *tagname)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   struct CSSStylesheet *sheet;
   BOOL matches;
   ULONG colorrgb;
   struct Colorinfo *ci;
   
   if(!doc || !doc->cssstylesheet) return NULL;
   
   ci = NULL;
   sheet = (struct CSSStylesheet *)doc->cssstylesheet;
   
   /* Find matching CSS rules and extract background-color */
   for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         (struct MinNode *)sel->node.mln_Succ;
         sel = (struct CSSSelector *)sel->node.mln_Succ)
      {  matches = TRUE;
         
         /* Match element name */
         if(sel->type & CSS_SEL_ELEMENT && sel->name)
         {  if(!tagname || Stricmp((char *)sel->name,(char *)tagname) != 0)
            {  matches = FALSE;
            }
         }
         
         /* Match class */
         if(matches && sel->type & CSS_SEL_CLASS && sel->class)
         {  if(!MatchClassAttribute(class, sel->class))
            {  matches = FALSE;
            }
         }
         
         /* Match ID */
         if(matches && sel->type & CSS_SEL_ID && sel->id)
         {  if(!id || Stricmp((char *)sel->id,(char *)id) != 0)
            {  matches = FALSE;
            }
         }
         
         /* Skip rules with pseudo-classes */
         if(matches && (sel->type & CSS_SEL_PSEUDO) && sel->pseudo)
         {  matches = FALSE;
         }
         
         /* If selector matches, extract background-color */
         if(matches)
         {  for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
               (struct MinNode *)prop->node.mln_Succ;
               prop = (struct CSSProperty *)prop->node.mln_Succ)
            {  if(prop->name && prop->value && 
                  Stricmp((char *)prop->name,"background-color") == 0)
               {  colorrgb = ParseHexColor(prop->value);
                  if(colorrgb != ~0)
                  {  ci = Finddoccolor(doc,colorrgb);
                     /* Return first matching background-color */
                     if(ci) return ci;
                  }
               }
            }
         }
      }
   }
   
   return ci;
}

/* Extract background-color from a style string and return Colorinfo */
struct Colorinfo *ExtractBackgroundColorFromStyle(struct Document *doc,UBYTE *style)
{  struct CSSProperty *prop;
   UBYTE *p;
   ULONG colorrgb;
   struct Colorinfo *ci;
   
   if(!doc || !style) return NULL;
   
   ci = NULL;
   p = style;
   while(*p)
   {  SkipWhitespace(&p);
      if(!*p) break;
      
      /* Parse property */
      prop = ParseProperty(doc,&p);
      if(prop && prop->name && prop->value)
      {  if(Stricmp((char *)prop->name,"background-color") == 0)
         {  colorrgb = ParseHexColor(prop->value);
            if(colorrgb != ~0)
            {  ci = Finddoccolor(doc,colorrgb);
            }
         }
         if(prop->name) FREE(prop->name);
         if(prop->value) FREE(prop->value);
         FREE(prop);
      }
      else
      {  /* Skip to next semicolon on parse error */
         while(*p && *p != ';')
         {  p++;
         }
      }
      
      /* Skip semicolon */
      if(*p == ';') p++;
   }
   
   return ci;
}

/* Apply CSS properties specific to table cells from external stylesheet rules */
void ApplyCSSToTableCellFromRules(struct Document *doc,void *table,UBYTE *class,UBYTE *id,UBYTE *tagname)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   struct CSSStylesheet *sheet;
   BOOL matches;
   long widthValue;
   long heightValue;
   short valign;
   short halign;
   ULONG wtag;
   ULONG htag;
   struct Number num;
   ULONG colorrgb;
   struct Colorinfo *cssBgcolor;
   
   if(!doc || !table || !doc->cssstylesheet) return;
   
   sheet = (struct CSSStylesheet *)doc->cssstylesheet;
   wtag = TAG_IGNORE;
   htag = TAG_IGNORE;
   widthValue = -1;
   heightValue = -1;
   valign = -1;
   halign = -1;
   cssBgcolor = NULL;
   
   /* Find matching CSS rules and extract table-cell-specific properties */
   for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         (struct MinNode *)sel->node.mln_Succ;
         sel = (struct CSSSelector *)sel->node.mln_Succ)
      {  matches = TRUE;
         
         /* Match element name */
         if(sel->type & CSS_SEL_ELEMENT && sel->name)
         {  if(!tagname || Stricmp((char *)sel->name,(char *)tagname) != 0)
            {  matches = FALSE;
            }
         }
         
         /* Match class */
         if(matches && sel->type & CSS_SEL_CLASS && sel->class)
         {  if(!MatchClassAttribute(class, sel->class))
            {  matches = FALSE;
            }
         }
         
         /* Match ID */
         if(matches && sel->type & CSS_SEL_ID && sel->id)
         {  if(!id || Stricmp((char *)sel->id,(char *)id) != 0)
            {  matches = FALSE;
            }
         }
         
         /* Skip rules with pseudo-classes */
         if(matches && (sel->type & CSS_SEL_PSEUDO) && sel->pseudo)
         {  matches = FALSE;
         }
         
         /* If selector matches, extract table-cell-specific properties */
         if(matches)
         {  for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
               (struct MinNode *)prop->node.mln_Succ;
               prop = (struct CSSProperty *)prop->node.mln_Succ)
            {  if(prop->name && prop->value)
               {  /* Extract width */
                  if(Stricmp((char *)prop->name,"width") == 0)
                  {  widthValue = ParseCSSLengthValue(prop->value,&num);
                     if(widthValue >= 0 && num.type != NUMBER_NONE)
                     {  if(num.type == NUMBER_PERCENT)
                        {  wtag = AOTAB_Percentwidth;
                        }
                        else
                        {  wtag = AOTAB_Pixelwidth;
                        }
                     }
                  }
                  /* Extract height */
                  else if(Stricmp((char *)prop->name,"height") == 0)
                  {  heightValue = ParseCSSLengthValue(prop->value,&num);
                     if(heightValue >= 0 && num.type != NUMBER_NONE)
                     {  if(num.type == NUMBER_PERCENT)
                        {  htag = AOTAB_Percentheight;
                        }
                        else
                        {  htag = AOTAB_Pixelheight;
                        }
                     }
                  }
                  /* Extract vertical-align */
                  else if(Stricmp((char *)prop->name,"vertical-align") == 0)
                  {  if(Stricmp((char *)prop->value,"top") == 0)
                     {  valign = VALIGN_TOP;
                     }
                     else if(Stricmp((char *)prop->value,"middle") == 0)
                     {  valign = VALIGN_MIDDLE;
                     }
                     else if(Stricmp((char *)prop->value,"bottom") == 0)
                     {  valign = VALIGN_BOTTOM;
                     }
                     else if(Stricmp((char *)prop->value,"baseline") == 0)
                     {  valign = VALIGN_BASELINE;
                     }
                  }
                  /* Extract text-align for horizontal alignment */
                  else if(Stricmp((char *)prop->name,"text-align") == 0)
                  {  if(Stricmp((char *)prop->value,"center") == 0)
                     {  halign = HALIGN_CENTER;
                     }
                     else if(Stricmp((char *)prop->value,"left") == 0)
                     {  halign = HALIGN_LEFT;
                     }
                     else if(Stricmp((char *)prop->value,"right") == 0)
                     {  halign = HALIGN_RIGHT;
                     }
                  }
                  /* Extract background-color for table cells */
                  else if(Stricmp((char *)prop->name,"background-color") == 0)
                  {  colorrgb = ParseHexColor(prop->value);
                     if(colorrgb != ~0)
                     {  cssBgcolor = Finddoccolor(doc,colorrgb);
                     }
                  }
               }
            }
         }
      }
   }
   
   /* Apply extracted values to table cell */
   if(wtag != TAG_IGNORE && widthValue >= 0)
   {  Asetattrs(table,wtag,widthValue,TAG_END);
   }
   if(htag != TAG_IGNORE && heightValue >= 0)
   {  Asetattrs(table,htag,heightValue,TAG_END);
   }
   if(valign >= 0)
   {  Asetattrs(table,AOTAB_Valign,valign,TAG_END);
   }
   if(halign >= 0)
   {  Asetattrs(table,AOTAB_Halign,halign,TAG_END);
   }
   if(cssBgcolor)
   {  Asetattrs(table,AOTAB_Bgcolor,cssBgcolor,TAG_END);
   }
}

/* Apply CSS properties specific to table cells (width, height, vertical-align) */
void ApplyCSSToTableCell(struct Document *doc,void *table,UBYTE *style)
{  struct CSSProperty *prop;
   struct Number num;
   UBYTE *p;
   long widthValue;
   long heightValue;
   short valign;
   ULONG wtag;
   ULONG htag;
   
   if(!doc || !table || !style) return;
   
   p = style;
   wtag = TAG_IGNORE;
   htag = TAG_IGNORE;
   widthValue = -1;
   heightValue = -1;
   valign = -1;
   
   while(*p)
   {  SkipWhitespace(&p);
      if(!*p) break;
      
      /* Parse property */
      prop = ParseProperty(doc,&p);
      if(prop && prop->name && prop->value)
      {  /* Extract width */
         if(Stricmp((char *)prop->name,"width") == 0)
         {  widthValue = ParseCSSLengthValue(prop->value,&num);
            if(widthValue >= 0 && num.type != NUMBER_NONE)
            {  if(num.type == NUMBER_PERCENT)
               {  wtag = AOTAB_Percentwidth;
               }
               else
               {  wtag = AOTAB_Pixelwidth;
               }
            }
         }
         /* Extract height */
         else if(Stricmp((char *)prop->name,"height") == 0)
         {  heightValue = ParseCSSLengthValue(prop->value,&num);
            if(heightValue >= 0 && num.type != NUMBER_NONE)
            {  if(num.type == NUMBER_PERCENT)
               {  htag = AOTAB_Percentheight;
               }
               else
               {  htag = AOTAB_Pixelheight;
               }
            }
         }
         /* Extract vertical-align */
         else if(Stricmp((char *)prop->name,"vertical-align") == 0)
         {  if(Stricmp((char *)prop->value,"top") == 0)
            {  valign = VALIGN_TOP;
            }
            else if(Stricmp((char *)prop->value,"middle") == 0)
            {  valign = VALIGN_MIDDLE;
            }
            else if(Stricmp((char *)prop->value,"bottom") == 0)
            {  valign = VALIGN_BOTTOM;
            }
            else if(Stricmp((char *)prop->value,"baseline") == 0)
            {  valign = VALIGN_BASELINE;
            }
         }
         /* Extract text-align for horizontal alignment */
         else if(Stricmp((char *)prop->name,"text-align") == 0)
         {  short halign;
            halign = -1;
            if(Stricmp((char *)prop->value,"center") == 0)
            {  halign = HALIGN_CENTER;
            }
            else if(Stricmp((char *)prop->value,"left") == 0)
            {  halign = HALIGN_LEFT;
            }
            else if(Stricmp((char *)prop->value,"right") == 0)
            {  halign = HALIGN_RIGHT;
            }
            if(halign >= 0)
            {  Asetattrs(table,AOTAB_Halign,halign,TAG_END);
            }
         }
         
         if(prop->name) FREE(prop->name);
         if(prop->value) FREE(prop->value);
         FREE(prop);
      }
      else
      {  /* Skip to next semicolon on parse error */
         while(*p && *p != ';')
         {  p++;
         }
      }
      
      /* Skip semicolon */
      if(*p == ';') p++;
   }
   
   /* Apply extracted values to table cell */
   /* CSS should override HTML attributes, so always apply if found */
   if(wtag != TAG_IGNORE && widthValue >= 0)
   {  /* Apply width value (can be pixel or percentage) */
      Asetattrs(table,wtag,widthValue,TAG_END);
   }
   if(htag != TAG_IGNORE && heightValue >= 0)
   {  /* Apply height value (can be pixel or percentage) */
      Asetattrs(table,htag,heightValue,TAG_END);
   }
   if(valign >= 0)
   {  Asetattrs(table,AOTAB_Valign,valign,TAG_END);
   }
}

/* Apply CSS properties to an image (IMG tag) */
void ApplyCSSToImage(struct Document *doc,void *copy,UBYTE *style)
{  struct CSSProperty *prop;
   struct Number num;
   UBYTE *p;
   long borderValue;
   long widthValue;
   long heightValue;
   long hspaceValue;
   long vspaceValue;
   ULONG wtag;
   ULONG htag;
   
   if(!doc || !copy || !style) return;
   
   p = style;
   wtag = TAG_IGNORE;
   htag = TAG_IGNORE;
   borderValue = -1;
   widthValue = -1;
   heightValue = -1;
   hspaceValue = -1;
   vspaceValue = -1;
   
   while(*p)
   {  SkipWhitespace(&p);
      if(!*p) break;
      
      /* Parse property */
      prop = ParseProperty(doc,&p);
      if(prop && prop->name && prop->value)
      {  /* Extract border */
         if(Stricmp((char *)prop->name,"border") == 0)
         {  borderValue = ParseCSSLengthValue(prop->value,&num);
            if(borderValue < 0) borderValue = 0;
         }
         /* Extract width */
         else if(Stricmp((char *)prop->name,"width") == 0)
         {  widthValue = ParseCSSLengthValue(prop->value,&num);
            if(widthValue > 0)
            {  if(num.type == NUMBER_PERCENT)
               {  wtag = AOCPY_Percentwidth;
               }
               else
               {  wtag = AOCPY_Width;
               }
            }
         }
         /* Extract height */
         else if(Stricmp((char *)prop->name,"height") == 0)
         {  heightValue = ParseCSSLengthValue(prop->value,&num);
            if(heightValue > 0)
            {  if(num.type == NUMBER_PERCENT)
               {  htag = AOCPY_Percentheight;
               }
               else
               {  htag = AOCPY_Height;
               }
            }
         }
         /* Extract hspace via margin-left and margin-right */
         else if(Stricmp((char *)prop->name,"margin-left") == 0 || Stricmp((char *)prop->name,"margin-right") == 0)
         {  long marginValue;
            marginValue = ParseCSSLengthValue(prop->value,&num);
            if(marginValue > 0 && num.type == NUMBER_NUMBER)
            {  if(hspaceValue < 0) hspaceValue = marginValue;
               else hspaceValue = (hspaceValue + marginValue) / 2; /* Average if both set */
            }
         }
         /* Extract vspace via margin-top and margin-bottom */
         else if(Stricmp((char *)prop->name,"margin-top") == 0 || Stricmp((char *)prop->name,"margin-bottom") == 0)
         {  long marginValue;
            marginValue = ParseCSSLengthValue(prop->value,&num);
            if(marginValue > 0 && num.type == NUMBER_NUMBER)
            {  if(vspaceValue < 0) vspaceValue = marginValue;
               else vspaceValue = (vspaceValue + marginValue) / 2; /* Average if both set */
            }
         }
         
         if(prop->name) FREE(prop->name);
         if(prop->value) FREE(prop->value);
         FREE(prop);
      }
      else
      {  /* Skip to next semicolon on parse error */
         while(*p && *p != ';')
         {  p++;
         }
      }
      
      /* Skip semicolon */
      if(*p == ';') p++;
   }
   
   /* Apply extracted values to image */
   if(borderValue >= 0)
   {  Asetattrs(copy,AOCPY_Border,borderValue,TAG_END);
   }
   if(wtag != TAG_IGNORE && widthValue > 0)
   {  Asetattrs(copy,wtag,widthValue,TAG_END);
   }
   if(htag != TAG_IGNORE && heightValue > 0)
   {  Asetattrs(copy,htag,heightValue,TAG_END);
   }
   if(hspaceValue >= 0)
   {  Asetattrs(copy,AOCPY_Hspace,hspaceValue,TAG_END);
   }
   if(vspaceValue >= 0)
   {  Asetattrs(copy,AOCPY_Vspace,vspaceValue,TAG_END);
   }
}

/* Apply CSS properties to a table (TABLE tag) */
void ApplyCSSToTable(struct Document *doc,void *table,UBYTE *style)
{  struct CSSProperty *prop;
   struct Number num;
   UBYTE *p;
   long borderValue;
   long widthValue;
   long cellpaddingValue;
   long cellspacingValue;
   ULONG wtag;
   struct Colorinfo *cssBgcolor;
   
   if(!doc || !table || !style) return;
   
   p = style;
   wtag = TAG_IGNORE;
   borderValue = -1;
   widthValue = -1;
   cellpaddingValue = -1;
   cellspacingValue = -1;
   cssBgcolor = NULL;
   
   while(*p)
   {  SkipWhitespace(&p);
      if(!*p) break;
      
      /* Parse property */
      prop = ParseProperty(doc,&p);
      if(prop && prop->name && prop->value)
      {  /* Extract border */
         if(Stricmp((char *)prop->name,"border") == 0)
         {  borderValue = ParseCSSLengthValue(prop->value,&num);
            if(borderValue < 0) borderValue = 0;
         }
         /* Extract width */
         else if(Stricmp((char *)prop->name,"width") == 0)
         {  widthValue = ParseCSSLengthValue(prop->value,&num);
            if(widthValue > 0)
            {  if(num.type == NUMBER_PERCENT)
               {  wtag = AOTAB_Percentwidth;
               }
               else
               {  wtag = AOTAB_Pixelwidth;
               }
            }
         }
         /* Extract cellpadding via padding */
         else if(Stricmp((char *)prop->name,"padding") == 0)
         {  cellpaddingValue = ParseCSSLengthValue(prop->value,&num);
            if(cellpaddingValue < 0) cellpaddingValue = 0;
         }
         /* Extract cellspacing - no direct CSS equivalent, but we can parse it if needed */
         /* Note: CSS border-spacing is CSS2 and not yet supported */
         /* Extract background-color */
         else if(Stricmp((char *)prop->name,"background-color") == 0)
         {  ULONG colorrgb;
            colorrgb = ParseHexColor(prop->value);
            if(colorrgb != ~0)
            {  cssBgcolor = Finddoccolor(doc,colorrgb);
            }
         }
         /* Extract border-color */
         else if(Stricmp((char *)prop->name,"border-color") == 0)
         {  ULONG colorrgb;
            struct Colorinfo *ci;
            colorrgb = ParseHexColor(prop->value);
            if(colorrgb != ~0)
            {  ci = Finddoccolor(doc,colorrgb);
               if(ci)
               {  Asetattrs(table,AOTAB_Bordercolor,ci,TAG_END);
               }
            }
         }
         
         if(prop->name) FREE(prop->name);
         if(prop->value) FREE(prop->value);
         FREE(prop);
      }
      else
      {  /* Skip to next semicolon on parse error */
         while(*p && *p != ';')
         {  p++;
         }
      }
      
      /* Skip semicolon */
      if(*p == ';') p++;
   }
   
   /* Apply extracted values to table */
   if(borderValue >= 0)
   {  Asetattrs(table,AOTAB_Border,borderValue,TAG_END);
   }
   if(wtag != TAG_IGNORE && widthValue > 0)
   {  Asetattrs(table,wtag,widthValue,TAG_END);
   }
   if(cellpaddingValue >= 0)
   {  Asetattrs(table,AOTAB_Cellpadding,cellpaddingValue,TAG_END);
   }
   if(cellspacingValue >= 0)
   {  Asetattrs(table,AOTAB_Cellspacing,cellspacingValue,TAG_END);
   }
   if(cssBgcolor)
   {  Asetattrs(table,AOTAB_Bgcolor,cssBgcolor,TAG_END);
   }
}

/* Apply CSS properties to a table from external stylesheet rules */
void ApplyCSSToTableFromRules(struct Document *doc,void *table,UBYTE *class,UBYTE *id)
{  struct CSSRule *rule;
   struct CSSSelector *sel;
   struct CSSProperty *prop;
   struct CSSStylesheet *sheet;
   BOOL matches;
   struct Number num;
   long borderValue;
   long widthValue;
   long cellpaddingValue;
   ULONG wtag;
   struct Colorinfo *cssBgcolor;
   ULONG colorrgb;
   struct Colorinfo *ci;
   
   if(!doc || !table || !doc->cssstylesheet) return;
   
   sheet = (struct CSSStylesheet *)doc->cssstylesheet;
   wtag = TAG_IGNORE;
   borderValue = -1;
   widthValue = -1;
   cellpaddingValue = -1;
   cssBgcolor = NULL;
   
   /* Find matching CSS rules and apply properties */
   for(rule = (struct CSSRule *)sheet->rules.mlh_Head;
       (struct MinNode *)rule->node.mln_Succ;
       rule = (struct CSSRule *)rule->node.mln_Succ)
   {  for(sel = (struct CSSSelector *)rule->selectors.mlh_Head;
         (struct MinNode *)sel->node.mln_Succ;
         sel = (struct CSSSelector *)sel->node.mln_Succ)
      {  matches = TRUE;
         
         /* Match element name (should be "table") */
         if(sel->type & CSS_SEL_ELEMENT && sel->name)
         {  if(Stricmp((char *)sel->name,"table") != 0)
            {  matches = FALSE;
            }
         }
         
         /* Match class */
         if(matches && sel->type & CSS_SEL_CLASS && sel->class)
         {  if(!MatchClassAttribute(class, sel->class))
            {  matches = FALSE;
            }
         }
         
         /* Match ID */
         if(matches && sel->type & CSS_SEL_ID && sel->id)
         {  if(!id || Stricmp((char *)sel->id,(char *)id) != 0)
            {  matches = FALSE;
            }
         }
         
         /* Skip rules with pseudo-classes */
         if(matches && (sel->type & CSS_SEL_PSEUDO) && sel->pseudo)
         {  matches = FALSE;
         }
         
         /* If selector matches, apply properties */
         if(matches)
         {  for(prop = (struct CSSProperty *)rule->properties.mlh_Head;
               (struct MinNode *)prop->node.mln_Succ;
               prop = (struct CSSProperty *)prop->node.mln_Succ)
            {  if(!prop->name || !prop->value) continue;
               
               /* Extract border */
               if(Stricmp((char *)prop->name,"border") == 0)
               {  borderValue = ParseCSSLengthValue(prop->value,&num);
                  if(borderValue < 0) borderValue = 0;
               }
               /* Extract width */
               else if(Stricmp((char *)prop->name,"width") == 0)
               {  widthValue = ParseCSSLengthValue(prop->value,&num);
                  if(widthValue > 0)
                  {  if(num.type == NUMBER_PERCENT)
                     {  wtag = AOTAB_Percentwidth;
                     }
                     else
                     {  wtag = AOTAB_Pixelwidth;
                     }
                  }
               }
               /* Extract cellpadding via padding */
               else if(Stricmp((char *)prop->name,"padding") == 0)
               {  cellpaddingValue = ParseCSSLengthValue(prop->value,&num);
                  if(cellpaddingValue < 0) cellpaddingValue = 0;
               }
               /* Extract background-color */
               else if(Stricmp((char *)prop->name,"background-color") == 0)
               {  colorrgb = ParseHexColor(prop->value);
                  if(colorrgb != ~0)
                  {  cssBgcolor = Finddoccolor(doc,colorrgb);
                  }
               }
               /* Extract border-color */
               else if(Stricmp((char *)prop->name,"border-color") == 0)
               {  colorrgb = ParseHexColor(prop->value);
                  if(colorrgb != ~0)
                  {  ci = Finddoccolor(doc,colorrgb);
                     if(ci)
                     {  Asetattrs(table,AOTAB_Bordercolor,ci,TAG_END);
                     }
                  }
               }
            }
         }
      }
   }
   
   /* Apply extracted values to table */
   if(borderValue >= 0)
   {  Asetattrs(table,AOTAB_Border,borderValue,TAG_END);
   }
   if(wtag != TAG_IGNORE && widthValue > 0)
   {  Asetattrs(table,wtag,widthValue,TAG_END);
   }
   if(cellpaddingValue >= 0)
   {  Asetattrs(table,AOTAB_Cellpadding,cellpaddingValue,TAG_END);
   }
   if(cssBgcolor)
   {  Asetattrs(table,AOTAB_Bgcolor,cssBgcolor,TAG_END);
   }
}

