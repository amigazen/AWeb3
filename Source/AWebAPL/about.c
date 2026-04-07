/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Original aweblib template Copyright (C) 2002 Yvon Rozijn
 * about: page aweblib Copyright (C) 2025 amigazen project
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
   {  /* about:fonts - font test page */
      len = 12000;  /* Increased for comprehensive web safe font testing */
      html = ALLOCTYPE(UBYTE,len,MEMF_PUBLIC);
      if(html)
      {  html_len = sprintf(html,
               "<html><head><title>AWeb Fonts</title></head>"
               "<body bgcolor=\"#AAAAAA\" text=\"#000000\">"
               "<table width=\"100%%\" cellpadding=\"5\" cellspacing=\"0\" border=\"0\">"
               "<tr><td align=\"center\">"
               "<img src=\"file:///AWeb:Docs/aweb.iff\" alt=\"AWeb\" align=\"center\">"
               "<br>"
               "<font size=\"+2\" color=\"#CC0000\"><i>The Amiga Web Browser</i></font>"
               "</td></tr>"
               "</table>"
               "<br clear=\"all\">"
               "<hr>"
               "<h2>Web Safe Font Families</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Font Family</strong></td><td><strong>Sample Text</strong></td></tr>"
               "<tr><td><font face=\"serif\">serif</font></td><td><font face=\"serif\">An old silent pond<br>A frog jumps into the pond&mdash;<br>Splash! Silence again.</font></td></tr>"
               "<tr><td><font face=\"sans-serif\">sans-serif</font></td><td><font face=\"sans-serif\">Morning glory!<br>the well bucket-entangled,<br>I ask for water.</font></td></tr>"
               "<tr><td><font face=\"monospace\">monospace</font></td><td><font face=\"monospace\">An old silent pond<br>A frog jumps into the pond&mdash;<br>Splash! Silence again.</font></td></tr>"
               "<tr><td><font face=\"cursive\">cursive</font></td><td><font face=\"cursive\">An old silent pond<br>A frog jumps into the pond&mdash;<br>Splash! Silence again.</font></td></tr>"
               "<tr><td><font face=\"fantasy\">fantasy</font></td><td><font face=\"fantasy\">An old silent pond<br>A frog jumps into the pond&mdash;<br>Splash! Silence again.</font></td></tr>"
               "</table>"
               "<h2>Serif Fonts</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Font Family</strong></td><td><strong>Sample Text</strong></td></tr>"
               "<tr><td><font face=\"Times New Roman, serif\">Times New Roman, serif</font></td><td><font face=\"Times New Roman, serif\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Times, serif\">Times, serif</font></td><td><font face=\"Times, serif\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Georgia\">Georgia</font></td><td><font face=\"Georgia\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Palatino\">Palatino</font></td><td><font face=\"Palatino\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Garamond\">Garamond</font></td><td><font face=\"Garamond\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "<tr><td><font face=\"Book Antiqua\">Book Antiqua</font></td><td><font face=\"Book Antiqua\">A summer river being crossed<br>how pleasing<br>with sandals in my hands!</font></td></tr>"
               "</table>"
               "<h2>Sans-Serif Fonts</h2>"
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
               "<h2>Monospace Fonts</h2>"
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
               "<h2>Font Selection Chain Examples</h2>"
               "<p><small>These examples demonstrate the font selection priority: direct system font &rarr; alias mapping &rarr; generic family &rarr; default preference.</small></p>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Font Face Attribute</strong></td><td><strong>Expected Behavior</strong></td><td><strong>Sample</strong></td></tr>"
               "<tr><td><code>face=\"Times New Roman, serif\"</code></td><td>Try Times New Roman.font, then alias, then serif default</td><td><font face=\"Times New Roman, serif\">A summer river being crossed how pleasing with sandals in my hands!</font></td></tr>"
               "<tr><td><code>face=\"Arial, Helvetica, sans-serif\"</code></td><td>Try Arial.font, then Helvetica.font, then alias, then sans-serif default</td><td><font face=\"Arial, Helvetica, sans-serif\">After the storm the moon's brightness on the green pines.</font></td></tr>"
               "<tr><td><code>face=\"Courier New, Courier, monospace\"</code></td><td>Try Courier New.font, then Courier.font, then alias, then monospace default</td><td><font face=\"Courier New, Courier, monospace\">A summer river being crossed how pleasing with sandals in my hands!</font></td></tr>"
               "<tr><td><code>face=\"Verdana, sans-serif\"</code></td><td>Try Verdana.font, then alias, then sans-serif default</td><td><font face=\"Verdana, sans-serif\">After the storm the moon's brightness on the green pines.</font></td></tr>"
               "</table>"
               "<h2>Font Sizes</h2>"
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
               "<h2>Font Styles</h2>"
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
               "<h2>Special Characters</h2>"
               "<table width=\"100%%\" cellpadding=\"10\" cellspacing=\"0\" border=\"1\" bordercolor=\"#CCCCCC\">"
               "<tr bgcolor=\"#E5E5E5\"><td><strong>Category</strong></td><td><strong>Sample</strong></td></tr>"
               "<tr><td>Numbers</td><td>0123456789</td></tr>"
               "<tr><td>Uppercase</td><td>ABCDEFGHIJKLMNOPQRSTUVWXYZ</td></tr>"
               "<tr><td>Lowercase</td><td>abcdefghijklmnopqrstuvwxyz</td></tr>"
               "<tr><td>Punctuation</td><td>! @ # $ %% ^ & * ( ) _ + - = [ ] { } | ; ' : \" , . / &lt; &gt; ?</td></tr>"
               "<tr><td>Special</td><td>&copy; &reg; &trade; &deg; &frac12; &frac14; &frac34; &euro; &pound; &yen;</td></tr>"
               "</table>"
               "<hr>"
               "<h2>Haiku Acknowledgments</h2>"
               "<p><small>Sample texts on this page include haiku from classical Japanese poets:</small></p>"
               "<ul><small>"
               "<li><strong>Matsuo Bash&otilde;</strong> (1644&ndash;1694): &ldquo;An old silent pond... A frog jumps into the pond, splash! Silence again.\"</li>"
               "<li><strong>Yosa Buson</strong> (1716&ndash;1784): &ldquo;A summer river being crossed how pleasing with sandals in my hands!\"</li>"
               "<li><strong>Kobayashi Issa</strong> (1763&ndash;1828): &ldquo;O snail climb Mount Fuji, but slowly, slowly!\"</li>"
               "<li><strong>Chiyo-ni</strong> (1703&ndash;1775): &ldquo;Morning glory! the well bucket-entangled, I ask for water.\"</li>"
               "<li><strong>Masaoka Shiki</strong> (1867&ndash;1902): &ldquo;After the storm the moon's brightness on the green pines.\"</li>"
               "</small></ul>"
               "<hr>"
               "<h2>Font Selection Logic</h2>"
               "<p><small>This page helps verify that AWeb's font selection works correctly:</small></p>"
               "<ol><small>"
               "<li><strong>Direct System Font:</strong> If a font name (e.g., \"Times New Roman\") exists on the system, it will be used directly.</li>"
               "<li><strong>Alias Mapping:</strong> If the font is not found directly, AWeb checks alias mappings (e.g., \"Times New Roman\" &rarr; \"CGTimes.font\").</li>"
               "<li><strong>Generic Family:</strong> If no alias is found, generic families (serif, sans-serif, monospace) map to default preference fonts.</li>"
               "<li><strong>Default Fallback:</strong> Finally, the default preference font for the type (normal/fixed) is used.</li>"
               "</small></ol>"
               "<hr>"
               "</body></html>");
         if(html_len >= len) html[len-1] = '\0';
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
               "<img src=\"file:///AWeb:Docs/aweb.iff\" alt=\"AWeb\" align=\"center\">"
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
               "<img src=\"file:///AWeb:Docs/aweb.iff\" alt=\"AWeb\" align=\"center\">"
               "<br>"
               "<font size=\"+2\" color=\"#CC0000\"><i>The Amiga Web Browser</i></font>"
               "</td></tr>"
               "</table>"
               "<br clear=\"all\">"
               "<hr>"
               "<h2>Loaded AWebPlugins</h2>"
               "<p><small>AWebPlugins are external plugin modules that extend AWeb's functionality.</small></p>"
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
                        "<tr><td><code>%s</code></td><td>%ld</td><td>%ld</td><td><small>%s</small></td></tr>",
                        libname ? libname : "(unknown)",
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
               "<li>Select the entry for the MIME type you want to configure (e.g., <code>IMAGE/PNG</code>, <code>IMAGE/GIF</code>, <code>IMAGE/JPEG</code>).</li>"
               "<li>Change the following settings:"
               "<ul>"
               "<li><strong>Action:</strong> Select &quot;AWeb Plugin (A)&quot;</li>"
               "<li><strong>Name:</strong> Enter the full path to the plugin file (e.g., <code>AWeb:awebplugins/awebpng.awebplugin</code>)</li>"
               "<li><strong>Arguments:</strong> Leave blank or supply optional parameters as documented for each plugin</li>"
               "</ul>"
               "</li>"
               "<li>Save your settings.</li>"
               "</ol>"
               "<p><small>Note: Plugin files are typically located in the <code>awebplugins</code> drawer where AWeb is installed. "
               "Each plugin may support optional parameters that can be specified in the Arguments field. "
               "See the plugin documentation for details on available parameters.</small></p>"
               "<hr>");
         html_ptr += html_used;
         html_used = len - (html_ptr - html);
         
         html_used = sprintf(html_ptr,
               "<h2>Loaded AWebLib Modules</h2>"
               "<p><small>AWebLib modules are protocol handlers and internal extensions.</small></p>"
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
                     "<tr><td><code>%s</code></td><td>%ld</td><td>%ld</td><td><small>%s</small></td></tr>",
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
               "<p><small>Note: Only currently loaded plugins are shown. Plugins are loaded on demand when their functionality is needed.</small></p>"
               "<hr>"
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
            "<img src=\"file:///AWeb:Docs/aweb.iff\" alt=\"AWeb\" align=\"center\">"
            "<br>"
            "<font size=\"+2\" color=\"#cc0000\"><i>The Amiga Web Browser</i></font>"
            "</td></tr>"
            "</table>"
            "<br clear=\"all\">"
            "<hr>"
            "<p align=\"center\"><strong>%s</strong> %s<br>"
            "3.6 Beta 8" __AMIGADATE__ "</p>"
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
            "Distributed under the AWeb Public License. "
            "Uses Independent JPEG Group's software (libjpeg).</li>"
            "<li><strong>PNG Plugin:</strong> Copyright &copy; 2002 Yvon Rozijn. "
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
            "<p>zlib compression library is used for HTTP content compression.</p>"
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
            "<p><small>Copyright &copy; 2002 Yvon Rozijn. &nbsp; Changes Copyright &copy; 2026 amigazen project</small></p>"
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

