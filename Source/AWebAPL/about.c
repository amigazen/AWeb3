/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Original aweblib template Copyright (C) 2002 Yvon Rozijn
 * about: page aweblib Copyright (C) 2025-2026 amigazen project
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

/* about.c - AWeb about: protocol plugin */

#include "aweblib.h"
#include "fetchdriver.h"
#include "task.h"
#include "plugin.h"
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/nodes.h>
#include <string.h>
#undef NOPROTOTYPES
#include <proto/exec.h>
#include <graphics/text.h>
#include <libraries/diskfont.h>
#include <libraries/ttengine.h>
#include <utility/tagitem.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/ttengine.h>

/* Library bases for pragma libcall stubs in this module only (about.aweblib). */
extern struct GfxBase *GfxBase;
struct Library *DiskfontBase;
struct Library *TTEngineBase;

struct Library *AboutBase;
void *AwebPluginBase;

/*-----------------------------------------------------------------------*/
/* AWebLib module startup */

__asm __saveds struct Library *Initlib(
   register __a6 struct ExecBase *sysbase,
   register __a0 struct SegList *seglist,
   register __d0 struct Library *libbase);

__asm __saveds struct Library *Openlib(
   register __a6 struct Library *libbase);

__asm __saveds struct SegList *Closelib(
   register __a6 struct Library *libbase);

__asm __saveds struct SegList *Expungelib(
   register __a6 struct Library *libbase);

__asm __saveds ULONG Extfunclib(void);

__asm __saveds void Fetchdrivertask(
   register __a0 struct Fetchdriver *fd);

/* Function declarations for project dependent hook functions */
static ULONG Initaweblib(struct Library *libbase);
static void Expungeaweblib(struct Library *libbase);

static APTR libseglist;

struct ExecBase *SysBase;

LONG __saveds __asm Libstart(void)
{  return -1;
}

static APTR functable[]=
{  Openlib,
   Closelib,
   Expungelib,
   Extfunclib,
   Fetchdrivertask,
   (APTR)-1
};

/* Init table used in library initialization. */
static ULONG inittab[]=
{  sizeof(struct Library),
   (ULONG) functable,
   0,
   (ULONG) Initlib
};

static char __aligned libname[]="about.aweblib";
static char __aligned libid[]="about.aweblib " AWEBLIBVSTRING " " __AMIGADATE__;

/* The ROM tag */
struct Resident __aligned romtag=
{  RTC_MATCHWORD,
   &romtag,
   &romtag+1,
   RTF_AUTOINIT,
   AWEBLIBVERSION,
   NT_LIBRARY,
   0,
   libname,
   libid,
   inittab
};

__asm __saveds struct Library *Initlib(
   register __a6 struct ExecBase *sysbase,
   register __a0 struct SegList *seglist,
   register __d0 struct Library *libbase)
{  SysBase=sysbase;
   AboutBase=libbase;
   libbase->lib_Revision=AWEBLIBREVISION;
   libseglist=seglist;
   if(!Initaweblib(libbase))
   {  Expungeaweblib(libbase);
      libbase=NULL;
   }
   return libbase;
}

__asm __saveds struct Library *Openlib(
   register __a6 struct Library *libbase)
{  libbase->lib_OpenCnt++;
   libbase->lib_Flags&=~LIBF_DELEXP;
   if(libbase->lib_OpenCnt==1)
   {  AwebPluginBase=OpenLibrary("awebplugin.library",0);
   }
#ifndef DEMOVERSION
   if(!Fullversion())
   {  Closelib(libbase);
      return NULL;
   }
#endif
   return libbase;
}

__asm __saveds struct SegList *Closelib(
   register __a6 struct Library *libbase)
{  libbase->lib_OpenCnt--;
   if(libbase->lib_OpenCnt==0)
   {  if(AwebPluginBase)
      {  CloseLibrary(AwebPluginBase);
         AwebPluginBase=NULL;
      }
      if(libbase->lib_Flags&LIBF_DELEXP)
      {  return Expungelib(libbase);
      }
   }
   return NULL;
}

__asm __saveds struct SegList *Expungelib(
   register __a6 struct Library *libbase)
{  if(libbase->lib_OpenCnt==0)
   {  ULONG size=libbase->lib_NegSize+libbase->lib_PosSize;
      UBYTE *ptr=(UBYTE *)libbase-libbase->lib_NegSize;
      Remove((struct Node *)libbase);
      Expungeaweblib(libbase);
      FreeMem(ptr,size);
      return libseglist;
   }
   libbase->lib_Flags|=LIBF_DELEXP;
   return NULL;
}

__asm __saveds ULONG Extfunclib(void)
{  return 0;
}

/*-----------------------------------------------------------------------*/

/*
 * about:fonts — HTML4-style face mapping hints: diskfont OpenDiskFont probe chains
 * (.font / outline under FONTS:) plus optional ttengine TT_OpenFontA per family.
 */
typedef struct FontdiagRow FontdiagRow;
struct FontdiagRow
{
   const char *label;
   const char *ideal_name;
   const char * const *disk_try;
   const char * const *tt_try;
};

static const char * const fd_d_serif[] = {
   "Times New Roman.font","Times.font","times.font","CGTimes.font",NULL
};
static const char * const fd_t_serif[] = {
   "Times New Roman","Times","serif","default",NULL
};

static const char * const fd_d_sans[] = {
   "Arial.font","Helvetica.font","helvetica.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_sans[] = {
   "Arial","Helvetica","sans-serif","default",NULL
};

static const char * const fd_d_mono[] = {
   "Courier New.font","Courier.font","courier.font","LetterGothic.font",NULL
};
static const char * const fd_t_mono[] = {
   "Courier New","Courier","Liberation Mono","monospaced","default",NULL
};

static const char * const fd_d_cursive[] = {
   "Comic Sans MS.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_cursive[] = {
   "Comic Sans MS","cursive","sans-serif","default",NULL
};

static const char * const fd_d_fantasy[] = {
   "Impact.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_fantasy[] = {
   "Impact","fantasy","sans-serif","default",NULL
};

static const char * const fd_d_tnr[] = {
   "Times New Roman.font","Times.font","times.font","CGTimes.font",NULL
};
static const char * const fd_t_tnr[] = {
   "Times New Roman","Times","Liberation Serif",NULL
};

static const char * const fd_d_times[] = {
   "Times.font","times.font","Times New Roman.font","CGTimes.font",NULL
};
static const char * const fd_t_times[] = {
   "Times","Times New Roman","Liberation Serif",NULL
};

static const char * const fd_d_georgia[] = {
   "Georgia.font","CGTimes.font",NULL
};
static const char * const fd_t_georgia[] = {
   "Georgia","Times New Roman","Liberation Serif",NULL
};

static const char * const fd_d_palatino[] = {
   "Palatino.font","CGTimes.font",NULL
};
static const char * const fd_t_palatino[] = {
   "Palatino","Book Antiqua","Liberation Serif",NULL
};

static const char * const fd_d_garamond[] = {
   "Garamond.font","CGTimes.font",NULL
};
static const char * const fd_t_garamond[] = {
   "Garamond","EB Garamond","Times New Roman",NULL
};

static const char * const fd_d_book[] = {
   "Book Antiqua.font","CGTimes.font",NULL
};
static const char * const fd_t_book[] = {
   "Book Antiqua","Palatino","Times New Roman",NULL
};

static const char * const fd_d_arial[] = {
   "Arial.font","Helvetica.font","helvetica.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_arial[] = {
   "Arial","Helvetica","Liberation Sans",NULL
};

static const char * const fd_d_helv[] = {
   "Helvetica.font","Arial.font","helvetica.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_helv[] = {
   "Helvetica","Arial","Liberation Sans",NULL
};

static const char * const fd_d_verdana[] = {
   "Verdana.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_verdana[] = {
   "Verdana","DejaVu Sans","Liberation Sans",NULL
};

static const char * const fd_d_tahoma[] = {
   "Tahoma.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_tahoma[] = {
   "Tahoma","Verdana","DejaVu Sans",NULL
};

static const char * const fd_d_treb[] = {
   "Trebuchet MS.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_treb[] = {
   "Trebuchet MS","Liberation Sans","DejaVu Sans",NULL
};

static const char * const fd_d_lucsans[] = {
   "Lucida Sans Unicode.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_lucsans[] = {
   "Lucida Sans Unicode","Lucida Sans","DejaVu Sans",NULL
};

static const char * const fd_d_comic[] = {
   "Comic Sans MS.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_comic[] = {
   "Comic Sans MS","DejaVu Sans","Liberation Sans",NULL
};

static const char * const fd_d_geneva[] = {
   "Geneva.font","Helvetica.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_geneva[] = {
   "Geneva","Helvetica","Arial",NULL
};

static const char * const fd_d_impact[] = {
   "Impact.font","CGTriumvirate.font",NULL
};
static const char * const fd_t_impact[] = {
   "Impact","Arial Black","Liberation Sans",NULL
};

static const char * const fd_d_courier[] = {
   "Courier New.font","Courier.font","courier.font","LetterGothic.font",NULL
};
static const char * const fd_t_courier[] = {
   "Courier New","Courier","Liberation Mono",NULL
};

static const char * const fd_d_monaco[] = {
   "Monaco.font","Courier.font","LetterGothic.font",NULL
};
static const char * const fd_t_monaco[] = {
   "Monaco","Courier New","Liberation Mono",NULL
};

static const char * const fd_d_luccon[] = {
   "Lucida Console.font","LetterGothic.font",NULL
};
static const char * const fd_t_luccon[] = {
   "Lucida Console","Consolas","Liberation Mono",NULL
};

static const char * const fd_d_consolas[] = {
   "Consolas.font","LetterGothic.font",NULL
};
static const char * const fd_t_consolas[] = {
   "Consolas","Courier New","Liberation Mono",NULL
};

static const FontdiagRow fd_rows[] =
{
   { "serif", "serif", fd_d_serif, fd_t_serif },
   { "sans-serif", "sans-serif", fd_d_sans, fd_t_sans },
   { "monospace", "monospace", fd_d_mono, fd_t_mono },
   { "cursive", "cursive", fd_d_cursive, fd_t_cursive },
   { "fantasy", "fantasy", fd_d_fantasy, fd_t_fantasy },
   { "Times New Roman, serif", "Times New Roman", fd_d_tnr, fd_t_tnr },
   { "Times, serif", "Times", fd_d_times, fd_t_times },
   { "Georgia", "Georgia", fd_d_georgia, fd_t_georgia },
   { "Palatino", "Palatino", fd_d_palatino, fd_t_palatino },
   { "Garamond", "Garamond", fd_d_garamond, fd_t_garamond },
   { "Book Antiqua", "Book Antiqua", fd_d_book, fd_t_book },
   { "Arial, Helvetica, sans-serif", "Arial", fd_d_arial, fd_t_arial },
   { "Helvetica, Arial, sans-serif", "Helvetica", fd_d_helv, fd_t_helv },
   { "Verdana", "Verdana", fd_d_verdana, fd_t_verdana },
   { "Tahoma", "Tahoma", fd_d_tahoma, fd_t_tahoma },
   { "Trebuchet MS", "Trebuchet MS", fd_d_treb, fd_t_treb },
   { "Lucida Sans Unicode", "Lucida Sans Unicode", fd_d_lucsans, fd_t_lucsans },
   { "Comic Sans MS", "Comic Sans MS", fd_d_comic, fd_t_comic },
   { "Geneva", "Geneva", fd_d_geneva, fd_t_geneva },
   { "Impact", "Impact", fd_d_impact, fd_t_impact },
   { "Courier New, Courier, monospace", "Courier New", fd_d_courier, fd_t_courier },
   { "Monaco", "Monaco", fd_d_monaco, fd_t_monaco },
   { "Lucida Console", "Lucida Console", fd_d_luccon, fd_t_luccon },
   { "Consolas", "Consolas", fd_d_consolas, fd_t_consolas }
};

static struct TextFont *FontdiagOpenDiskfont(const char *fontfile, USHORT ysize)
{
   struct TextAttr ta;
   struct TextFont *tf;

   if(!fontfile || !DiskfontBase)
   {
      return NULL;
   }
   ta.ta_Name = (STRPTR)fontfile;
   ta.ta_YSize = ysize;
   ta.ta_Style = FS_NORMAL;
   ta.ta_Flags = 0;
   tf = OpenDiskFont(&ta);
   return tf;
}

static const char *FontdiagFirstDiskHit(const char * const *names, USHORT ysize)
{
   int i;
   struct TextFont *tf;
   const char *hit;

   hit = NULL;
   if(!names)
   {
      return NULL;
   }
   for(i = 0; names[i] != NULL; i++)
   {
      tf = FontdiagOpenDiskfont(names[i], ysize);
      if(tf)
      {
         hit = names[i];
         CloseFont(tf);
         break;
      }
   }
   return hit;
}

static const char *FontdiagFirstTTHit(const char * const *fnames)
{
   int i;
   APTR f;
   struct TagItem tags[6];
   STRPTR ftab[3];
   const char *hit;

   hit = NULL;
   if(!TTEngineBase || !fnames)
   {
      return NULL;
   }
   for(i = 0; fnames[i] != NULL; i++)
   {
      ftab[0] = (STRPTR)fnames[i];
      ftab[1] = NULL;
      tags[0].ti_Tag = TT_FamilyTable;
      tags[0].ti_Data = (ULONG)ftab;
      tags[1].ti_Tag = TT_FontSize;
      tags[1].ti_Data = 15UL;
      tags[2].ti_Tag = TT_FontWeight;
      tags[2].ti_Data = TT_FontWeight_Normal;
      tags[3].ti_Tag = TT_FontStyle;
      tags[3].ti_Data = TT_FontStyle_Regular;
      tags[4].ti_Tag = TAG_END;
      tags[4].ti_Data = 0UL;
      f = TT_OpenFontA(tags);
      if(f)
      {
         hit = fnames[i];
         TT_CloseFont(f);
         break;
      }
   }
   return hit;
}

static int FontdiagSnprintf(char **wpp, long *room, const char *fmt, ...)
{
   va_list ap;
   int n;
   char *wp;
   long rem;

   wp = *wpp;
   rem = *room;
   if(rem < 8)
   {
      return 0;
   }
   va_start(ap, fmt);
   n = vsprintf(wp, fmt, ap);
   va_end(ap);
   if(n < 0)
   {
      n = 0;
   }
   if((long)n > rem - 1)
   {
      n = (int)(rem - 1);
      wp[n] = '\0';
   }
   *wpp = wp + n;
   *room = rem - (long)n;
   return n;
}

static void FontdiagEmitTtengineNotice(char **wpp, long *room, int tt_ok)
{
   if(tt_ok)
   {
      FontdiagSnprintf(wpp, room,
         "<table width=\"100%%\" cellpadding=\"8\" cellspacing=\"0\" border=\"1\" >"
         "<tr><td align=\"left\"><strong>TTEngine is installed on this system</strong></td></tr>"
         "<tr><td>TTEngine is installed on this system. OpenType/TrueType fonts will "
         "be used in preference to Amiga bitmap and scalable fonts when available."
         "</td></tr>"
         "</table><p></p>");
   }
   else
   {
      FontdiagSnprintf(wpp, room,
         "<table width=\"100%%\" cellpadding=\"8\" cellspacing=\"0\" border=\"1\" bordercolor=\"#996600\" bgcolor=\"#FFF0CC\">"
         "<tr><td align=\"left\"><strong>TTEngine is not installed on this system</strong></td></tr>"
         "<tr><td>Without TTEngine, AWeb renders HTML using standard Amiga bitmap and scalable fonts. "
         "Install TTEngine (ttengine.library) and OpenType/TrueType fonts to add UTF-8 support, kerning, and more accurate font family matching."
         "</td></tr>"
         "</table><p></p>");
   }
}

static void FontdiagProbeTable(char **wpp, long *room, int tt_ok)
{
   int ri;
   int nrows;
   const char *ideal_disp;
   const char *primary_file;
   const char *dhit;
   const char *thit;
   int n;

   nrows = (int)(sizeof(fd_rows) / sizeof(fd_rows[0]));
   n = FontdiagSnprintf(wpp, room,
      "<table width=\"100%%\" cellpadding=\"6\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
      "<tr bgcolor=\"#E5E5E5\"><th align=\"left\">Font family</th>"
      "<th align=\"left\">Matched Amiga font</th>"
      "<th align=\"left\">Matched TTEngine font</th></tr>");
   if(n < 1)
   {
      return;
   }
   for(ri = 0; ri < nrows && *room > 200; ri++)
   {
      ideal_disp = fd_rows[ri].ideal_name;
      primary_file = fd_rows[ri].disk_try[0];
      dhit = FontdiagFirstDiskHit(fd_rows[ri].disk_try, 15);
      thit = NULL;
      if(tt_ok)
      {
         thit = FontdiagFirstTTHit(fd_rows[ri].tt_try);
      }
      FontdiagSnprintf(wpp, room,
         "<tr><td><strong>%s</strong><br>%s</td>",
         fd_rows[ri].label,
         ideal_disp ? ideal_disp : "-");
      if(dhit)
      {
         if(primary_file && strcmp((const char *)dhit, (const char *)primary_file) == 0)
         {
            FontdiagSnprintf(wpp, room, "<td>%s <em>(exact match)</em></td>", dhit);
         }
         else
         {
            FontdiagSnprintf(wpp, room, "<td>%s <em>(best match)</em></td>", dhit);
         }
      }
      else
      {
         FontdiagSnprintf(wpp, room, "<td>-</td>");
      }
      if(thit)
      {
         FontdiagSnprintf(wpp, room, "<td>%s</td>", thit);
      }
      else
      {
         FontdiagSnprintf(wpp, room, "<td>-</td>");
      }
      FontdiagSnprintf(wpp, room, "</tr>");
   }
   FontdiagSnprintf(wpp, room, "</table>");
}

static void FontdiagBuildHtml(UBYTE *buf, long maxlen)
{
   char *wp;
   long room;
   int tt_ok;

   if(!buf || maxlen < 128L)
   {
      return;
   }
   buf[0] = '\0';
   wp = (char *)buf;
   room = maxlen - 1L;
   GfxBase = NULL;
   DiskfontBase = NULL;
   TTEngineBase = NULL;
   tt_ok = 0;
   GfxBase = OpenLibrary("graphics.library", 36L);
   DiskfontBase = OpenLibrary("diskfont.library", 37L);
   TTEngineBase = OpenLibrary(TTENGINENAME, (LONG)TTENGINEMINVERSION);
   if(TTEngineBase)
   {
      tt_ok = 1;
   }
   FontdiagSnprintf(&wp, &room,
      "<h2 id=\"mapping\">Font family mapping</h2>"
      "<p>This section dynamically illustrates how font families are mapped to the fonts"
      " installed on this Amiga. AWeb supports both standard Amiga bitmap and scalable fonts, and"
      " will also use TTEngine to render scalable OpenType/TrueType fonts directly if available on the system."
      "</p>");
   FontdiagEmitTtengineNotice(&wp, &room, tt_ok);
   if(DiskfontBase)
   {
      FontdiagProbeTable(&wp, &room, tt_ok);
   }
   else
   {
      FontdiagSnprintf(&wp, &room,
         "<p>diskfont.library could not be opened.</p>");
   }
   if(TTEngineBase)
   {
      CloseLibrary(TTEngineBase);
      TTEngineBase = NULL;
   }
   if(DiskfontBase)
   {
      CloseLibrary(DiskfontBase);
      DiskfontBase = NULL;
   }
   if(GfxBase)
   {
      CloseLibrary(GfxBase);
      GfxBase = NULL;
   }
   *wp = '\0';
}

/* Generate HTML content for about: pages */
static UBYTE *GenerateAboutPage(UBYTE *url)
{  UBYTE *html = NULL;
   UBYTE *page = NULL;
   long len;
   UBYTE *version_str;
   UBYTE *about_str;
   long html_len;
   
   /* Extract page name from about: URL (e.g., "about:blank" -> "blank") */
   if(url && STRNIEQUAL(url,"ABOUT:",6))
   {  page = url + 6;
      if(!*page) page = (UBYTE *)"about";
   }
   else
   {  page = (UBYTE *)"about";
   }
   
   /* Get version strings - use Awebversion() function from plugin interface */
   version_str = Awebversion();
   if(!version_str) version_str = (UBYTE *)"Unknown";
   about_str = (UBYTE *)"AWeb";
   
   /* Check for about:blank - must match exactly "blank" or be empty after "blank" */
   if(STRNIEQUAL(page,"blank",5) && (page[5]=='\0' || page[5]==' ' || page[5]=='\t'))
   {  /* about:blank - blank page */
      len = 256;
      html = ALLOCTYPE(UBYTE,len,MEMF_PUBLIC);
      if(html)
      {  html_len = sprintf(html,
               "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">"
               "<html><head>"
               "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">"
               "<title>about:blank</title>"
               "</head>"
               "<body bgcolor=\"#AAAAAA\"></body>"
               "</html>");
         if(html_len >= len) html[len-1] = '\0';
      }
      return html;
   }
   
   /* Check for about:fonts */
   if(STRNIEQUAL(page,"fonts",5) && (page[5]=='\0' || page[5]==' ' || page[5]=='\t'))
   {  /* about:fonts - font test page (large HTML; keep generous buffer). */
      /* UTF-8 glyph matrix is emitted last in the HTML so Latin demos stay first; the closing
       * prose warns that Install TTF coverage shows real scripts, else mojibake and gaps. */
      /* Fetchdriver task stack is small: never put multi-kilobyte glyph grid on stack (overflow crash). */
      len = 131072;
      html = ALLOCTYPE(UBYTE,len,MEMF_PUBLIC);
      if(html)
      {  static const char fontdiag_empty[] = "";
         UBYTE *fontdiag;
         int fontdiag_heap;
         UBYTE *glyphsec;
         int glyph_heap;
         char *wp;
         long room;
         int n;
         int fi; 
         int li;
         static const char grid_none[]="";
         /* Column samples: a few codepoints each, not words. Scripts need UTF-8 outside ISO 8859-1 Latin-1. */
         static const char * const langglyph[12]=
         {  "\xC4\x85\xC4\x99\xC5\x9B\xC4\x87\xC5\x82",
            "\xD0\xB0\xD0\xB1\xD0\xB2\xD0\xB3\xD0\xB4",
            "\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4\xCE\xB5",
            "\xD7\x90\xD7\x91\xD7\x92\xD7\x93\xD7\x94",
            "\xD8\xA7\xD8\xA8\xD8\xAC\xD8\xAF",
            "\xC4\xB1\xC5\x9F\xC4\x9F\xC3\xBC\xC3\xB6\xC3\xA7",
            "\xC4\x91\xC4\x83\xC3\xA2\xC3\xAA\xC3\xB4\xC6\xA1\xC6\xB0",
            "\xE3\x81\xB2\xE3\x82\xAB\xE4\xB8\x80",
            "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4",
            "\xE4\xBA\xBA\xE6\x97\xA5\xE6\x9C\x88",
            "\xE0\xB8\x81\xE0\xB8\x82\xE0\xB8\x84",
            "\xE0\xA4\x95\xE0\xA4\x96\xE0\xA4\x97"
         };
         static const char * const langhdr[12]=
         {  "Polish","Russian","Greek","Hebrew","Arabic","Turkish","Vietnamese",
            "Japanese","Korean","Chinese","Thai","Hindi"
         };
         static const char * const facename[16]=
         {  "serif",
            "sans-serif",
            "monospace",
            "cursive",
            "fantasy",
            "Times New Roman, serif",
            "Georgia",
            "Palatino",
            "Arial, Helvetica, sans-serif",
            "Verdana",
            "Tahoma",
            "Trebuchet MS",
            "Lucida Sans Unicode",
            "Comic Sans MS",
            "Courier New, Courier, monospace",
            "Lucida Console"
         };
         /* Font diagnostics: bounded OpenDiskFont chains with CloseFont (diskfont.doc); optional ttengine probes. */
         fontdiag_heap=0;
         fontdiag=(UBYTE *)fontdiag_empty;
         fontdiag=ALLOCTYPE(UBYTE,56000,MEMF_PUBLIC);
         if(fontdiag)
         {  fontdiag_heap=1;
            FontdiagBuildHtml(fontdiag,56000L);
         }
         else
         {  fontdiag=(UBYTE *)fontdiag_empty;
         }
         glyph_heap=0;
         glyphsec=ALLOCTYPE(UBYTE,20480,MEMF_PUBLIC);
         if(glyphsec)
            glyph_heap=1;
         else
            glyphsec=(UBYTE *)grid_none;
         if(glyph_heap)
         {
         wp=(char *)glyphsec;
         room=20480-1;
         n=sprintf(wp,
               "<h2 id=\"utf8grid\"></h2>"
               "<table width=\"100%%\" cellpadding=\"6\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><th align=\"left\">Font</th>");
         if(n<0) n=0;
         if((long)n>room) n=(int)room;
         wp+=n; room-=n;
         for(li=0; li<12 && room>32; li++)
         {  n=sprintf(wp,"<th>%s</th>",langhdr[li]);
            if(n<0) n=0;
            if((long)n>room) n=(int)room;
            wp+=n; room-=n;
         }
         if(room>8)
         {  n=sprintf(wp,"</tr>");
            if(n<0) n=0;
            if((long)n>room) n=(int)room;
            wp+=n; room-=n;
         }
         for(fi=0; fi<16 && room>200; fi++)
         {  n=sprintf(wp,"<tr><td>%s</td>",facename[fi]);
            if(n<0) n=0;
            if((long)n>room) n=(int)room;
            wp+=n; room-=n;
            for(li=0; li<12 && room>64; li++)
            {  n=sprintf(wp,"<td><font face=\"%s\">%s</font></td>",facename[fi],langglyph[li]);
               if(n<0) n=0;
               if((long)n>room) n=(int)room;
               wp+=n; room-=n;
            }
            if(room>8)
            {  n=sprintf(wp,"</tr>");
               if(n<0) n=0;
               if((long)n>room) n=(int)room;
               wp+=n; room-=n;
            }
         }
         if(room>16)
         {  n=sprintf(wp,"</table>");
            if(n<0) n=0;
            if((long)n>room) n=(int)room;
            wp+=n; room-=n;
         }
         *wp='\0';
         glyphsec[20480-1]='\0';
         }
         html_len = sprintf(html,
               "<html><head>"
               "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">"
               "<title>AWeb Fonts</title></head>"
               "<body bgcolor=\"#AAAAAA\" text=\"#000000\">"
               "<table width=\"100%%\" cellpadding=\"5\" cellspacing=\"0\" border=\"0\">"
               "<tr><td align=\"center\">"
               "<img width=\"192\" height=\"93\" src=\"file:///AWeb:Docs/aweb.iff\" alt=\"AWeb\" align=\"center\">"
               "<br>"
               "<font face=\"serif\" size=\"+2\" color=\"#CC0000\"><i>The Amiga Web Browser</i></font>"
               "</td></tr>"
               "</table>"
               "<br clear=\"all\">"
               "<h1 id=\"top\">Fonts in AWeb</h1>"
               "<p>Use this <code>about:fonts</code> page to inspect how AWeb maps web font families, Browser preferences, and installed fonts together to render text on this Amiga.</p>"
               "<p>Since AWeb 3.6, AWeb will automatically choose the best font matches available on the system corresponding to standard web font families.</p>"
               "<ul>"
               "<li><a href=\"#safe\">Web safe font families</a></li>"
               "<li><a href=\"#mapping\">Font family mapping</a></li>"
               "<li><a href=\"#serif\">Serif fonts</a></li>"
               "<li><a href=\"#sans\">Sans-serif fonts</a></li>"
               "<li><a href=\"#mono\">Monospace and fixed-element fonts</a></li>"
               "<li><a href=\"#chains\">Font selection chain examples</a></li>"
               "<li><a href=\"#sizes\">Font sizes</a></li>"
               "<li><a href=\"#styles\">Font styles</a></li>"
               "<li><a href=\"#special\">Special characters</a></li>"
               "<li><a href=\"#logic\">Font matching</a></li>"
               "<li><a href=\"#utf8\">UTF-8 rendering</a> of non-Latin alphabets</li>"
               "<li><a href=\"#haiku\">Haiku credits</a></li>"
               "</ul>"
               "<h2 id=\"safe\">Web safe font families</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Font Family</strong></td><td><strong>Sample Text</strong></td></tr>"
               "<tr><td><font face=\"serif\">serif</font></td><td><font face=\"serif\">An old silent pond<br>A frog jumps into the pond<br>Splash! Silence again.</font></td></tr>"
               "<tr><td><font face=\"sans-serif\">sans-serif</font></td><td><font face=\"sans-serif\">Morning glory!<br>the well bucket-entangled,<br>I ask for water.</font></td></tr>"
               "<tr><td><font face=\"monospace\">monospace</font></td><td><font face=\"monospace\">An old silent pond<br>A frog jumps into the pond<br>Splash! Silence again.</font></td></tr>"
               "<tr><td><font face=\"cursive\">cursive</font></td><td><font face=\"cursive\">An old silent pond<br>A frog jumps into the pond<br>Splash! Silence again.</font></td></tr>"
               "<tr><td><font face=\"fantasy\">fantasy</font></td><td><font face=\"fantasy\">An old silent pond<br>A frog jumps into the pond<br>Splash! Silence again.</font></td></tr>"
               "</table>"
               "%s"
               "<h2 id=\"serif\">Serif fonts</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Font Family</strong></td><td><strong>Sample Text</strong></td></tr>"
               "<tr><td><font face=\"Times New Roman, serif\">Times New Roman, serif</font></td><td><font face=\"Times New Roman, serif\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Times, serif\">Times, serif</font></td><td><font face=\"Times, serif\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Georgia\">Georgia</font></td><td><font face=\"Georgia\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Palatino\">Palatino</font></td><td><font face=\"Palatino\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Garamond\">Garamond</font></td><td><font face=\"Garamond\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Book Antiqua\">Book Antiqua</font></td><td><font face=\"Book Antiqua\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "</table>"
               "<h2 id=\"sans\">Sans-serif fonts</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Font Family</strong></td><td><strong>Sample Text</strong></td></tr>"
               "<tr><td><font face=\"Arial, Helvetica, sans-serif\">Arial, Helvetica, sans-serif</font></td><td><font face=\"Arial, Helvetica, sans-serif\">After the storm<br>the moon's brightness<br>on the green pines.</font></td></tr>"
               "<tr><td><font face=\"Helvetica, Arial, sans-serif\">Helvetica, Arial, sans-serif</font></td><td><font face=\"Helvetica, Arial, sans-serif\">After the storm<br>the moon's brightness<br>on the green pines.</font></td></tr>"
               "<tr><td><font face=\"Verdana\">Verdana</font></td><td><font face=\"Verdana\">After the storm<br>the moon's brightness<br>on the green pines.</font></td></tr>"
               "<tr><td><font face=\"Tahoma\">Tahoma</font></td><td><font face=\"Tahoma\">After the storm<br>the moon's brightness<br>on the green pines.</font></td></tr>"
               "<tr><td><font face=\"Trebuchet MS\">Trebuchet MS</font></td><td><font face=\"Trebuchet MS\">After the storm<br>the moon's brightness<br>on the green pines.</font></td></tr>"
               "<tr><td><font face=\"Lucida Sans Unicode\">Lucida Sans Unicode</font></td><td><font face=\"Lucida Sans Unicode\">After the storm<br>the moon's brightness<br>on the green pines.</font></td></tr>"
               "<tr><td><font face=\"Comic Sans MS\">Comic Sans MS</font></td><td><font face=\"Comic Sans MS\">After the storm<br>the moon's brightness<br>on the green pines.</font></td></tr>"
               "<tr><td><font face=\"Geneva\">Geneva</font></td><td><font face=\"Geneva\">After the storm<br>the moon's brightness<br>on the green pines.</font></td></tr>"
               "</table>"
               "<h2 id=\"mono\">Monospace fonts</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Font Family</strong></td><td><strong>Sample Text</strong></td></tr>"
               "<tr><td><font face=\"Courier New, Courier, monospace\">Courier New, Courier, monospace</font></td><td><font face=\"Courier New, Courier, monospace\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Courier, Courier New, monospace\">Courier, Courier New, monospace</font></td><td><font face=\"Courier, Courier New, monospace\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Monaco\">Monaco</font></td><td><font face=\"Monaco\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Lucida Console\">Lucida Console</font></td><td><font face=\"Lucida Console\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><code>&lt;code&gt;</code></td><td><code>O snail<br>climb Mount Fuji,<br>but slowly, slowly!</code></td></tr>"
               "<tr><td><code>&lt;pre&gt;</code></td><td><pre>Morning glory!<br>the well bucket-entangled,<br>I ask for water.</pre></td></tr>"
               "<tr><td><code>&lt;tt&gt;</code></td><td><tt>O snail<br>climb Mount Fuji,<br>but slowly, slowly!</tt></td></tr>"
               "</table>"
               "<h2 id=\"chains\">Font selection chain examples</h2>"
               "<p>These examples demonstrate the font selection priority: direct system font &rarr; alias mapping &rarr; generic family &rarr; default preference.</p>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Font Face Attribute</strong></td><td><strong>Expected Behavior</strong></td><td><strong>Sample</strong></td></tr>"
               "<tr><td><code>face=\"Times New Roman, serif\"</code></td><td>Try Times New Roman.font, then alias, then serif default</td><td><font face=\"Times New Roman, serif\">A summer river being crossed how pleasing with sandals in my hands!</font></td></tr>"
               "<tr><td><code>face=\"Arial, Helvetica, sans-serif\"</code></td><td>Try Arial.font, then Helvetica.font, then alias, then sans-serif default</td><td><font face=\"Arial, Helvetica, sans-serif\">After the storm the moon's brightness on the green pines.</font></td></tr>"
               "<tr><td><code>face=\"Courier New, Courier, monospace\"</code></td><td>Try Courier New.font, then Courier.font, then alias, then monospace default</td><td><font face=\"Courier New, Courier, monospace\">A summer river being crossed how pleasing with sandals in my hands!</font></td></tr>"
               "<tr><td><code>face=\"Verdana, sans-serif\"</code></td><td>Try Verdana.font, then alias, then sans-serif default</td><td><font face=\"Verdana, sans-serif\">After the storm the moon's brightness on the green pines.</font></td></tr>"
               "</table>"
               "<h2 id=\"sizes\">Font sizes</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Size</strong></td><td><strong>Sample Text</strong></td></tr>"
               "<tr><td><code>size=\"-2\"</code></td><td><font size=\"-2\">After the storm</font></td></tr>"
               "<tr><td><code>size=\"-1\"</code></td><td><font size=\"-1\">the moon's brightness</font></td></tr>"
               "<tr><td><code>size=\"1\" (default)</code></td><td><font size=\"1\">on the green pines.</font></td></tr>"
               "<tr><td><code>size=\"+1\"</code></td><td><font size=\"+1\">An old silent pond</font></td></tr>"
               "<tr><td><code>size=\"+2\"</code></td><td><font size=\"+2\">A frog jumps</font></td></tr>"
               "<tr><td><code>size=\"+3\"</code></td><td><font size=\"+3\">Splash! Silence again.</font></td></tr>"
               "<tr><td><code>size=\"+4\"</code></td><td><font size=\"+4\">O snail</font></td></tr>"
               "</table>"
               "<h2 id=\"styles\">Font styles</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Style</strong></td><td><strong>Sample Text</strong></td></tr>"
               "<tr><td><code>&lt;b&gt;</code></td><td><b>Morning glory!</b></td></tr>"
               "<tr><td><code>&lt;i&gt;</code></td><td><i>the well bucket-entangled,</i></td></tr>"
               "<tr><td><code>&lt;u&gt;</code></td><td><u>I ask for water.</u></td></tr>"
               "<tr><td><code>&lt;strike&gt;</code></td><td><strike>An old silent pond</strike></td></tr>"
               "<tr><td><code>&lt;b&gt;&lt;i&gt;</code></td><td><b><i>A summer river</i></b></td></tr>"
               "<tr><td><code>&lt;tt&gt;</code></td><td><tt>being crossed</tt></td></tr>"
               "<tr><td><code>&lt;small&gt;</code></td><td><small>how pleasing</small></td></tr>"
               "<tr><td><code>&lt;big&gt;</code></td><td><big>with sandals in my hands!</big></td></tr>"
               "</table>"
               "<h2 id=\"special\">Special characters</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Category</strong></td><td><strong>Sample</strong></td></tr>"
               "<tr><td>Numbers</td><td>0123456789</td></tr>"
               "<tr><td>Uppercase</td><td>ABCDEFGHIJKLMNOPQRSTUVWXYZ</td></tr>"
               "<tr><td>Lowercase</td><td>abcdefghijklmnopqrstuvwxyz</td></tr>"
               "<tr><td>Punctuation</td><td>! @ # $ %% ^ & * ( ) _ + - = [ ] { } | ; ' : \" , . / &lt; &gt; ?</td></tr>"
               "<tr><td>Special</td><td>&copy; &reg; &trade; &deg; &frac12; &frac14; &frac34; &euro; &pound; &yen;</td></tr>"
               "</table>"
               "<h2 id=\"logic\">Font matching</h2>"
               "<p>The font matching rules are detailed here. Note that in AWeb 3.6, while it remains possible to configure preferred font mappings, AWeb defaults to using the best matching fonts found on the system, especially if TTEngine is installed and OpenType/TrueType fonts are available.</p>"
               "<ol>"
               "<li><strong>Direct System Font:</strong> If a font name (e.g., \"Times New Roman\") exists on the system, it will be used directly.</li>"
               "<li><strong>Alias Mapping:</strong> If the font is not found directly, AWeb checks alias mappings (e.g., \"Times New Roman\" &rarr; \"CGTimes.font\").</li>"
               "<li><strong>Generic Family:</strong> If no alias is found, generic families (serif, sans-serif, monospace) map to default preference fonts.</li>"
               "<li><strong>Default Fallback:</strong> Finally, the default preference font for the type (normal/fixed) is used.</li>"
               "</ol>"
               "<h2 id=\"utf8\">UTF-8 rendering examples</h2>"
               "<p>This table previews UTF-8 multibyte characters from non-Latin languages for checking if the fonts installed contain the necessary glyphs. "
               "With TTEngine installed and OpenType/TrueType fonts available, correct non-Latin glyphs will be displayed. "
               "Without TTEngine, or without fonts with coverage for all character sets, expect a mix of missing or substituted glyphs and garbage Latin characters in the table that follows.</p>"
               "%s"
               "<hr>"
               "<h2 id=\"haiku\">Haiku credits</h2>"
               "<p>Sample texts on this page include <em>haiku</em> from classical Japanese poets:</p>"
               "<ul>"
               "<li><strong>Matsuo Bash&otilde;</strong> (1644-1694): \"An old silent pond... A frog jumps into the pond, splash! Silence again.\"</li>"
               "<li><strong>Yosa Buson</strong> (1716-1784): \"A summer river being crossed how pleasing with sandals in my hands!\"</li>"
               "<li><strong>Kobayashi Issa</strong> (1763-1828): \"O snail climb Mount Fuji, but slowly, slowly!\"</li>"
               "<li><strong>Chiyo-ni</strong> (1703-1775): \"Morning glory! the well bucket-entangled, I ask for water.\"</li>"
               "<li><strong>Masaoka Shiki</strong> (1867-1902): \"After the storm the moon's brightness on the green pines.\"</li>"
               "</ul>"
               "</body></html>",fontdiag,glyphsec);
         if(html_len >= len) html[len-1] = '\0';
         if(fontdiag_heap)
            Freemem(fontdiag);
         if(glyph_heap)
            Freemem(glyphsec);
      }
      return html;
   }
   
   /* Check for about:version - alias for about: */
   if(STRNIEQUAL(page,"version",7) && (page[7]=='\0' || page[7]==' ' || page[7]=='\t'))
   {  /* about:version - same as about: */
      page = (UBYTE *)"about";
   }
   
   /* Check for about:home */
   if(STRNIEQUAL(page,"home",4) && (page[4]=='\0' || page[4]==' ' || page[4]=='\t'))
   {  /* about:home - default home page */
      len = 3072;
      html = ALLOCTYPE(UBYTE,len,MEMF_PUBLIC);
      if(html)
      {  html_len = sprintf(html,
               "<html><head><title>AWeb Home</title></head>"
               "<body bgcolor=\"#AAAAAA\" text=\"#000000\" link=\"#0000CC\" vlink=\"#551A8B\" topmargin=\"0\" leftmargin=\"0\">"
               "<table width=\"100%%\" cellpadding=\"5\" cellspacing=\"0\" border=\"0\">"
               "<tr><td align=\"center\">"
               "<img width=\"192\" height=\"93\" src=\"file:///AWeb:Docs/aweb.iff\" alt=\"AWeb\" align=\"center\">"
               "<br>"
               "<font size=\"+2\" color=\"#CC0000\"><i>The Amiga Web Browser</i></font>"
               "</td></tr>"
               "</table>"
               "<hr>"
               "<table width=\"100%%\" cellpadding=\"5\" cellspacing=\"10\" border=\"0\">"
               "<tr valign=\"top\">"
               "<td width=\"33%%\" align=\"left\">"
               "<table width=\"100%%\" cellpadding=\"5\" cellspacing=\"0\" border=\"1\" bordercolorlight=\"#FFFFFF\" bordercolordark=\"#888888\" bgcolor=\"#E5E5E5\">"
               "<tr><td>"
               "<font face=\"sans-serif\" size=\"-1\">"
               "<strong>AWeb Links</strong><br>"
               "&bull; <a href=\"file:///AWeb:Docs/aweb.html\">Documentation</a><br>"
               "&bull; <a href=\"x-aweb:hotlist\">Hotlist</a><br>"
               "&bull; <a href=\"about:version\">Version</a><br>"
               "&bull; <a href=\"about:plugins\">Plugins</a><br>"
               "&bull; <a href=\"about:fonts\">Web Fonts</a>"
               "</font>"
               "</td></tr>"
               "</table>"
               "</td>"
               "<td width=\"34%%\" align=\"center\">"
               "<font size=\"-1\" face=\"sans-serif\" color=\"#000000\"><strong>Search</strong></font><br>"
               "<font size=\"-2\" face=\"sans-serif\" color=\"#333333\">powered by BoingSearch.com</font><br>"
               "<form method=\"get\" action=\"http://www.boingsearch.com/\">"
               "<br>"
               "<input type=\"text\" name=\"q\" size=\"25\"><br>"
               "<input type=\"submit\" value=\"Search\">"
               "</form>"
               "</td>"
               "<td width=\"33%%\" align=\"right\">"
               "<table width=\"100%%\" cellpadding=\"5\" cellspacing=\"0\" border=\"1\" bordercolorlight=\"#FFFFFF\" bordercolordark=\"#888888\" bgcolor=\"#E5E5E5\">"
               "<tr><td>"
               "<font face=\"sans-serif\" size=\"-1\">"
               "<strong>WWW Links</strong><br>"
               "&bull; <a href=\"https://www.amiga.com/\">Amiga.com</a><br>"
               "&bull; <a href=\"http://www.aminet.net/\">Aminet</a><br>"
               "&bull; <a href=\"http://www.amigazen.com/aweb/\">AWeb Home</a>"
               "</font>"
               "</td></tr>"
               "</table>"
               "</td>"
               "</tr>"
               "</table>"
               "</body></html>");
         if(html_len >= len) html[len-1] = '\0';
      }
      return html;
   }
   
   /* Check for about:plugins */
   if(STRNIEQUAL(page,"plugins",7) && (page[7]=='\0' || page[7]==' ' || page[7]=='\t'))
   {  /* about:plugins - list loaded plugins */
      struct Library *lib;
      struct Node *node;
      UBYTE *html_ptr;
      long html_used;
      UBYTE *libname;
      UBYTE *libid;
#ifdef LOCALONLY
      UBYTE *known_aweblibs[] = {
         (UBYTE *)"AWeb:aweblib/arexx.aweblib",
         (UBYTE *)"AWeb:aweblib/awebjs.aweblib",
         (UBYTE *)"AWeb:aweblib/print.aweblib",
         NULL
      };
#else
      UBYTE *known_aweblibs[] = {
         (UBYTE *)"AWeb:aweblib/about.aweblib",
         (UBYTE *)"AWeb:aweblib/arexx.aweblib",
         (UBYTE *)"AWeb:aweblib/authorize.aweblib",
         (UBYTE *)"AWeb:aweblib/cachebrowser.aweblib",
         (UBYTE *)"AWeb:aweblib/ftp.aweblib",
         (UBYTE *)"AWeb:aweblib/gemini.aweblib",
         (UBYTE *)"AWeb:aweblib/gopher.aweblib",
         (UBYTE *)"AWeb:aweblib/history.aweblib",
         (UBYTE *)"AWeb:aweblib/hotlist.aweblib",
         (UBYTE *)"AWeb:aweblib/mail.aweblib",
         (UBYTE *)"AWeb:aweblib/news.aweblib",
         (UBYTE *)"AWeb:aweblib/print.aweblib",
         (UBYTE *)"AWeb:aweblib/startup.aweblib",
         (UBYTE *)"AWeb:aweblib/awebjs.aweblib",
         NULL
      };
#endif
      int i;
      
      len = 20480;  /* Large buffer for plugin list and configuration instructions */
      html = ALLOCTYPE(UBYTE,len,MEMF_PUBLIC);
      if(html)
      {  html_ptr = html;
         html_used = sprintf(html_ptr,
               "<html><head><title>AWeb Plugins</title></head>"
               "<body bgcolor=\"#AAAAAA\" text=\"#000000\">"
               "<table width=\"100%%\" cellpadding=\"5\" cellspacing=\"0\" border=\"0\">"
               "<tr><td align=\"center\">"
               "<img width=\"192\" height=\"93\" src=\"file:///AWeb:Docs/aweb.iff\" alt=\"AWeb\" align=\"center\">"
               "<br>"
               "<font face=\"serif\" size=\"+2\" color=\"#CC0000\"><i>The Amiga Web Browser</i></font>"
               "</td></tr>"
               "</table>"
               "<br clear=\"all\">"
               "<hr>"
               "<h2>Loaded AWebPlugins</h2>"
               "<p>AWebPlugins are external plugin modules that extend AWeb's functionality.</p>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Plugin Name</strong></td><td><strong>Version</strong></td><td><strong>Revision</strong></td><td><strong>ID String</strong></td></tr>");
         html_ptr += html_used;
         html_used = len - (html_ptr - html);
         
         /* Scan library list for AWebPlugins */
         if(SysBase)
         {  node = (struct Node *)SysBase->LibList.lh_Head;
            while((node = node->ln_Succ) && node->ln_Succ)
            {  lib = (struct Library *)node;
               libname = (UBYTE *)lib->lib_Node.ln_Name;
               if(libname && strstr((char *)libname,".awebplugin"))
               {  libid = (UBYTE *)lib->lib_IdString;
                  if(!libid) libid = (UBYTE *)"";
                  html_used = sprintf(html_ptr,
                        "<tr><td><code>%s</code></td><td>%ld</td><td>%ld</td><td>%s</td></tr>",
                        libname ? (char *)libname : "(unknown)",
                        (long)lib->lib_Version,
                        (long)lib->lib_Revision,
                        libid);
                  html_ptr += html_used;
                  html_used = len - (html_ptr - html);
                  if(html_used < 200) break;
               }
            }
         }
         
         html_used = sprintf(html_ptr, "</table>");
         html_ptr += html_used;
         html_used = len - (html_ptr - html);
         
         html_used = sprintf(html_ptr,
               "<h2>Configuring AWebPlugins</h2>"
               "<p>AWebPlugins are already bundled with the AWeb distribution. To configure a plugin for a specific MIME type:</p>"
               "<ol>"
               "<li>Start AWeb and open the browser settings (Settings menu &rarr; Browser Options).</li>"
               "<li>Navigate to the <em>Viewers</em> page in the settings window.</li>"
               "<li>Select the entry for the MIME type to be configured (e.g., <code>IMAGE/PNG</code>, <code>IMAGE/GIF</code>, <code>IMAGE/JPEG</code>).</li>"
               "<li>Change the following settings:"
               "<ul>"
               "<li><strong>Action:</strong> Select &quot;AWeb Plugin (A)&quot;</li>"
               "<li><strong>Name:</strong> Enter the full path to the plugin file (e.g., <code>AWeb:awebplugins/awebpng.awebplugin</code>)</li>"
               "<li><strong>Arguments:</strong> Leave blank or supply optional parameters as documented for each plugin</li>"
               "</ul>"
               "</li>"
               "<li>Save your settings.</li>"
               "</ol>"
               "<p>Note: Plugin files are typically located in the <code>awebplugins</code> drawer where AWeb is installed. "
               "Each plugin may support optional parameters that can be specified in the Arguments field. "
               "See the plugin documentation for details on available parameters.</p>"
               "<hr>");
         html_ptr += html_used;
         html_used = len - (html_ptr - html);
         
         html_used = sprintf(html_ptr,
               "<h2>Loaded AWebLib Modules</h2>"
               "<p>AWebLib modules are protocol handlers and internal extensions.</p>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Module Name</strong></td><td><strong>Version</strong></td><td><strong>Revision</strong></td><td><strong>ID String</strong></td></tr>");
         html_ptr += html_used;
         html_used = len - (html_ptr - html);
         
         /* Try to open known aweblib plugins */
         for(i = 0; known_aweblibs[i]; i++)
         {  lib = OpenLibrary(known_aweblibs[i], 0);
            if(lib)
            {  libname = (UBYTE *)lib->lib_Node.ln_Name;
               libid = (UBYTE *)lib->lib_IdString;
               if(!libid) libid = (UBYTE *)"";
               html_used = sprintf(html_ptr,
                     "<tr><td><code>%s</code></td><td>%ld</td><td>%ld</td><td>%s</td></tr>",
                     libname ? libname : known_aweblibs[i],
                     (long)lib->lib_Version,
                     (long)lib->lib_Revision,
                     libid);
               html_ptr += html_used;
               html_used = len - (html_ptr - html);
               if(html_used < 200) break;
               CloseLibrary(lib);
            }
         }
         
         html_used = sprintf(html_ptr, "</table>");
         html_ptr += html_used;
         html_used = len - (html_ptr - html);
         
         html_used = sprintf(html_ptr,
               "<hr>"
               "<p>Note: Only currently loaded AWebPlugins are shown. AWebPlugins are loaded on demand when their functionality is needed.</p>"
               "</body></html>");
         html_ptr += html_used;
         *html_ptr = '\0';
      }
      return html;
   }
   
   /* Default about page with technical information */
   /* Calculate required buffer size */
   len = 8192;  /* Buffer for technical info, standards, licenses, and plugin acknowledgements */
   html = ALLOCTYPE(UBYTE,len,MEMF_PUBLIC);
   if(html)
      {  html_len = sprintf(html,
            "<html><head><title>About AWeb</title></head>"
            "<body bgcolor=\"#AAAAAA\" text=\"#000000\">"
            "<table align=\"center\" width=\"90%%\">"
            "<tr><td align=\"center\">"
            "<img width=\"192\" height=\"93\" src=\"file:///AWeb:Docs/aweb.iff\" alt=\"AWeb\" align=\"center\">"
            "<br>"
            "<font size=\"+2\" color=\"#cc0000\"><i>The Amiga Web Browser</i></font>"
            "</td></tr>"
            "</table>"
            "<br clear=\"all\">"
            "<hr>"
            "<p align=\"center\"><strong>%s %s</strong><br>"
            "Beta 8 " __AMIGADATE__ "</p>"
            "<hr>"
            "<h2>Web Standards</h2>"
            "<ul>"
            "<li><a href=\"http://www.w3.org/TR/REC-html32\">HTML 3.2</a> - www.w3.org/TR/REC-html32</li>"
            "<li><a href=\"http://www.w3.org/TR/html4/\">HTML 4</a> - www.w3.org/TR/html4/</li>"
            "<li><a href=\"http://www.w3.org/TR/xhtml1/\">XHTML 1.0</a> - www.w3.org/TR/xhtml1/</li>"
            "<li><a href=\"http://www.w3.org/TR/CSS1/\">CSS1</a> - www.w3.org/TR/CSS1/</li>"
            "<li><a href=\"http://www.w3.org/TR/CSS2/\">CSS2</a> - www.w3.org/TR/CSS2/</li>"
            "<li><a href=\"http://tools.ietf.org/html/rfc1945\">HTTP/1.0</a> - tools.ietf.org/html/rfc1945</li>"
            "<li><a href=\"http://tools.ietf.org/html/rfc2616\">HTTP/1.1</a> - tools.ietf.org/html/rfc2616</li>"
            "<li><a href=\"http://tools.ietf.org/html/rfc2818\">HTTPS (TLS/SSL)</a> - tools.ietf.org/html/rfc2818</li>"
            "<li><a href=\"http://tools.ietf.org/html/rfc959\">FTP</a> - tools.ietf.org/html/rfc959</li>"
            "<li><a href=\"http://tools.ietf.org/html/rfc1436\">Gopher</a> - tools.ietf.org/html/rfc1436</li>"
            "<li><a href=\"https://geminiprotocol.net/\">Gemini Protocol</a> - geminiprotocol.net</li>"
            "<li><a href=\"https://spartan.mozz.us/\">Spartan Protocol</a> - spartan.mozz.us</li>"
            "<li><a href=\"http://tools.ietf.org/html/rfc2368\">Mailto (RFC 2368)</a> - tools.ietf.org/html/rfc2368</li>"
            "<li><a href=\"http://tools.ietf.org/html/rfc977\">NNTP</a> - tools.ietf.org/html/rfc977</li>"
            "<li><a href=\"http://daringfireball.net/projects/markdown/\">Markdown</a> - daringfireball.net/projects/markdown/</li>"
            "<li><a href=\"http://www.ecma-international.org/publications/standards/Ecma-262.htm\">JavaScript 1.1</a> - www.ecma-international.org/publications/standards/Ecma-262.htm</li>"
            "<li><a href=\"http://tools.ietf.org/html/rfc2109\">Cookies (RFC 2109)</a> - tools.ietf.org/html/rfc2109</li>"
            "</ul>"
            "<hr>"
            "<h2>License</h2>"
            "<p>AWeb is distributed under the <strong>AWeb Public License Version 1.0</strong>.</p>"
            "<p>Copyright &copy; 2002 Yvon Rozijn. All rights reserved.<br>"
            "AWeb 3.6 Changes Copyright &copy; 2026 amigazen project</p>"
            "<p>This program is free software; you can redistribute it and/or modify "
            "it under the terms of the AWeb Public License as included in this distribution.</p>"
            "<p>This software is provided \"as is\". No warranties are made, either "
            "expressed or implied, with respect to reliability, quality, performance, "
            "or operation of this software.</p>"
            "<hr>"
            "<h2>Third-Party Components</h2>"
            "<h3>AWeb Image Plugins</h3>"
            "<p>The following image format plugins are included:</p>"
            "<ul>"
            "<li><strong>GIF Plugin:</strong> Copyright &copy; 2002 Yvon Rozijn. "
            "Changes Copyright &copy; 2026 amigazen project. "
            "Distributed under the AWeb Public License.</li>"
            "<li><strong>JPEG/JFIF Plugin:</strong> Copyright &copy; 2002 Yvon Rozijn. "
            "Changes Copyright &copy; 2026 amigazen project. "
            "Distributed under the AWeb Public License. "
            "Uses Independent JPEG Group's software (libjpeg).</li>"
            "<li><strong>PNG Plugin:</strong> Copyright &copy; 2002 Yvon Rozijn. "
            "Changes Copyright &copy; 2026 amigazen project. "
            "Distributed under the AWeb Public License. "
            "Uses libpng reference library.</li>"
            "</ul>"
            "<h3>Independent JPEG Group (libjpeg)</h3>"
            "<p>JPEG compression/decompression library used by the JFIF plugin.</p>"
            "<p>Copyright &copy; 1991-1996, Thomas G. Lane</p>"
            "<p>This software is provided 'as-is', without any express or implied warranty. "
            "Permission is granted to anyone to use this software for any purpose, "
            "including commercial applications, and to alter it and redistribute it freely, "
            "subject to the following restrictions:</p>"
            "<ol>"
            "<li>The origin of this software must not be misrepresented.</li>"
            "<li>Altered source versions must be plainly marked as such and must not be "
            "misrepresented as being the original software.</li>"
            "<li>This notice may not be removed or altered from any source distribution.</li>"
            "</ol>"
            "<h3>libpng 1.6.43</h3>"
            "<p>PNG reference library version 1.6.43 used by the PNG plugin.</p>"
            "<p>Copyright &copy; 1995-2024 The PNG Reference Library Authors<br>"
            "Copyright &copy; 2018-2024 Cosmin Truta<br>"
            "Copyright &copy; 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson<br>"
            "Copyright &copy; 1996-1997 Andreas Dilger<br>"
            "Copyright &copy; 1995-1996 Guy Eric Schalnat, Group 42, Inc.</p>"
            "<p>The software is supplied \"as is\", without warranty of any kind, "
            "express or implied, including, without limitation, the warranties "
            "of merchantability, fitness for a particular purpose, title, and "
            "non-infringement. In no event shall the Copyright owners, or "
            "anyone distributing the software, be liable for any damages or "
            "other liability, whether in contract, tort or otherwise, arising "
            "from, out of, or in connection with the software, or the use or "
            "other dealings in the software, even if advised of the possibility "
            "of such damage.</p>"
            "<p>Permission is hereby granted to use, copy, modify, and distribute "
            "this software, or portions hereof, for any purpose, without fee, "
            "subject to the following restrictions:</p>"
            "<ol>"
            "<li>The origin of this software must not be misrepresented; you "
            "must not claim that you wrote the original software. If you "
            "use this software in a product, an acknowledgment in the product "
            "documentation would be appreciated, but is not required.</li>"
            "<li>Altered source versions must be plainly marked as such, and must "
            "not be misrepresented as being the original software.</li>"
            "<li>This Copyright notice may not be removed or altered from any "
            "source or altered source distribution.</li>"
            "</ol>"
            "<h3>zlib</h3>"
            "<p>zlib compression library version 1.2.11 is used for HTTP content compression.</p>"
            "<p>Copyright &copy; 1995-2023 Jean-loup Gailly and Mark Adler</p>"
            "<p>This software is provided 'as-is', without any express or implied warranty. "
            "Permission is granted to anyone to use this software for any purpose, "
            "including commercial applications, and to alter it and redistribute it freely, "
            "subject to the following restrictions:</p>"
            "<ol>"
            "<li>The origin of this software must not be misrepresented; you must not "
            "claim that you wrote the original software.</li>"
            "<li>Altered source versions must be plainly marked as such, and must not be "
            "misrepresented as being the original software.</li>"
            "<li>This notice may not be removed or altered from any source distribution.</li>"
            "</ol>"
            "<h3>PCRE (Perl Compatible Regular Expressions)</h3>"
            "<p>PCRE library is used for regular expression pattern matching.</p>"
            "<p>Copyright &copy; 1997-2003 University of Cambridge<br>"
            "Written by: Philip Hazel &lt;ph10@cam.ac.uk&gt;</p>"
            "<p>Permission is granted to anyone to use this software for any purpose on any "
            "computer system, and to redistribute it freely, subject to the following "
            "restrictions:</p>"
            "<ol>"
            "<li>This software is distributed in the hope that it will be useful, "
            "but WITHOUT ANY WARRANTY; without even the implied warranty of "
            "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.</li>"
            "<li>The origin of this software must not be misrepresented, either by "
            "explicit claim or by omission.</li>"
            "<li>Altered versions must be plainly marked as such, and must not be "
            "misrepresented as being the original software.</li>"
            "<li>If PCRE is embedded in any software that is released under the GNU "
            "General Purpose Licence (GPL), then the terms of that licence shall "
            "supersede any condition above with which it is incompatible.</li>"
            "</ol>"
            "<hr>"
            "<p><strong>AWeb</strong> <font color=\"#cc0000\"><i>The Amiga Web Browser</i></font></p>"
            "<p>Copyright &copy; 2002 Yvon Rozijn. &nbsp; AWeb 3.6 Changes Copyright &copy; 2025-2026 amigazen project</p>"
            "</body></html>",
            about_str,version_str);
      
      /* Ensure null termination */
      if(html_len >= len) html[len-1] = '\0';
   }
   return html;
}

/*-----------------------------------------------------------------------*/

__saveds __asm void Fetchdrivertask(register __a0 struct Fetchdriver *fd)
{  UBYTE *html;
   long html_len;
   BOOL error = FALSE;
   
   if(!fd || !fd->name)
   {  error = TRUE;
   }
   else
   {  /* Generate HTML content */
      html = GenerateAboutPage(fd->name);
      if(html)
      {  html_len = strlen(html);
         Updatetaskattrs(
            AOURL_Contenttype,"text/html",
            AOURL_Data,html,
            AOURL_Datalength,html_len,
            TAG_END);
         /* Free the HTML buffer - it will be copied by the task system */
         FREE(html);
      }
      else
      {  error = TRUE;
      }
   }
   
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Error,error,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

/*-----------------------------------------------------------------------*/

static ULONG Initaweblib(struct Library *libbase)
{  return TRUE;
}

static void Expungeaweblib(struct Library *libbase)
{
}

