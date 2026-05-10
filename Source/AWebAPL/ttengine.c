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

/* ttengine.c - Optional ttengine.library support for font rendering */

#include "aweb.h"
#include "ttengine.h"
#include "application.h"
#include "awebprefs.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <utility/tagitem.h>

/* Include ttengine library headers */
#include <libraries/ttengine.h>
#include <clib/ttengine_protos.h>
#include <pragma/ttengine_lib.h>

/* Library base pointer */
/* Note: Not static so pragma libcall directives can access it */
extern struct Library *TTEngineBase = NULL;

/* Flag to track if ttengine is available */
static BOOL ttengine_available = FALSE;

/* True while RP uses TT_Encoding_UTF8: extent_loop counts Unicode chars; AWeb passes UTF-8 byte spans. */
static BOOL tte_utf8_byte_strings = FALSE;

/* Last FSF_* style passed to TTEngineSetFont (bold pad when ttengine active on rp). */
static USHORT tte_last_style_flags = 0;

/* Complete UTF-8 codepoints wholly contained in the first nbytes of s. */
static ULONG TteUtf8CompleteCharCount(UBYTE *s,ULONG nbytes)
{
   ULONG i;
   ULONG nch;
   UBYTE b;
   ULONG cl;
   ULONG j;
   int contok;
   if(!s || nbytes==0UL)
   {
      return 0UL;
   }
   i=0UL;
   nch=0UL;
   while(i<nbytes)
   {
      b=s[i];
      if(b<0x80U)
      {
         i+=1UL;
         nch+=1UL;
         continue;
      }
      if(b<=0xC1U)
      {
         i+=1UL;
         nch+=1UL;
         continue;
      }
      if(b<=0xDFU)
      {
         cl=2UL;
      }
      else if(b<=0xEFU)
      {
         cl=3UL;
      }
      else if(b<=0xF4U)
      {
         cl=4UL;
      }
      else
      {
         i+=1UL;
         nch+=1UL;
         continue;
      }
      if(i+cl>nbytes)
      {
         break;
      }
      contok=1;
      for(j=1UL;j<cl;j++)
      {
         if((s[i+j]&0xC0U)!=0x80U)
         {
            contok=0;
            break;
         }
      }
      if(!contok)
      {
         i+=1UL;
         nch+=1UL;
         continue;
      }
      i+=cl;
      nch+=1UL;
   }
   return nch;
}

/* Byte length of the first nchars complete UTF-8 codepoints in s, capped by nbytes_max. */
static ULONG TteUtf8BytesForCharCount(UBYTE *s,ULONG nbytes_max,ULONG nchars)
{
   ULONG i;
   ULONG c;
   UBYTE b;
   ULONG cl;
   ULONG j;
   int contok;
   if(!s || nbytes_max==0UL || nchars==0UL)
   {
      return 0UL;
   }
   i=0UL;
   c=0UL;
   while(c<nchars && i<nbytes_max)
   {
      b=s[i];
      if(b<0x80U)
      {
         i+=1UL;
         c+=1UL;
         continue;
      }
      if(b<=0xC1U)
      {
         i+=1UL;
         c+=1UL;
         continue;
      }
      if(b<=0xDFU)
      {
         cl=2UL;
      }
      else if(b<=0xEFU)
      {
         cl=3UL;
      }
      else if(b<=0xF4U)
      {
         cl=4UL;
      }
      else
      {
         i+=1UL;
         c+=1UL;
         continue;
      }
      if(i+cl>nbytes_max)
      {
         break;
      }
      contok=1;
      for(j=1UL;j<cl;j++)
      {
         if((s[i+j]&0xC0U)!=0x80U)
         {
            contok=0;
            break;
         }
      }
      if(!contok)
      {
         i+=1UL;
         c+=1UL;
         continue;
      }
      i+=cl;
      c+=1UL;
   }
   return i;
}

/*------------------------------------------------------------------------*/

ULONG TTEngineBoldInkPad(struct RastPort *rp)
{
   ULONG n;
   if(!rp || !TTEngineBase || !ttengine_available)
   {
      return 0UL;
   }
   if(!IsTTEngineFontActive(rp))
   {
      return 0UL;
   }
   n=0UL;
   /* Faux-bold / hinting often draws a few pixels past the rounded advance. */
   if((tte_last_style_flags & FSF_BOLD) != 0)
   {
      n+=2UL;
   }
   if((tte_last_style_flags & FSF_ITALIC) != 0)
   {
      n+=1UL;
   }
   return n;
}

/*------------------------------------------------------------------------*/

/* TT_TextFit fits ink to width; reserve same slack as TTEngineTextLength/Textlengthext. */
static UWORD TteTextFitConstrainedWidth(UWORD cwidth, struct RastPort *rp)
{
   ULONG pad;
   ULONG cw;
   if(cwidth==0U)
   {
      return 0U;
   }
   pad=TTEngineBoldInkPad(rp);
   cw=(ULONG)cwidth;
   if(pad>=cw)
   {
      return 1U;
   }
   return (UWORD)(cw-pad);
}

/*------------------------------------------------------------------------*/

/* Diskfont tf_YSize is often tighter than TrueType ink; TT_GetAttrs MaxTop+MaxBottom matches
 * SDK doc (full face vertical span from baseline) for browser line spacing. */
ULONG TTEngineLineBoxHeight(struct RastPort *rp, struct TextFont *tf)
{
   ULONG cell;
   ULONG maxtop;
   ULONG maxbot;
   ULONG sum;
   struct TagItem tags[3];
   if(!tf)
   {
      return 8UL;
   }
   cell=(ULONG)tf->tf_YSize;
   if(!rp || !TTEngineBase || !ttengine_available)
   {
      return cell;
   }
   if(!IsTTEngineFontActive(rp))
   {
      return cell;
   }
   maxtop=0UL;
   maxbot=0UL;
   tags[0].ti_Tag=TT_FontMaxTop;
   tags[0].ti_Data=(ULONG)&maxtop;
   tags[1].ti_Tag=TT_FontMaxBottom;
   tags[1].ti_Data=(ULONG)&maxbot;
   tags[2].ti_Tag=TAG_END;
   (void)TT_GetAttrsA(rp,tags);
   sum=maxtop+maxbot;
   if(sum>cell && sum<4096UL)
   {
      return sum;
   }
   return cell;
}

/*------------------------------------------------------------------------*/

/* Minimal built-in JKFF Shift_JIS renderer (no system patches).
 * JKFFDisp normally patches graphics.library; we instead load the JKFF font
 * data and render Shift_JIS bytes ourselves when the active font name is jkff.
 *
 * This supports fixed 8x16 ASCII and 16x16 kanji bitmaps from kanji.font.all.
 */

/* JKFF bitmap data file. JKFFDisp archives typically include kanji.font.all,
 * but install layouts vary. We try a small list of likely locations. */
#define JKFF_FONTFILE1  "FONTS:jkff/kanji.font.all"
#define JKFF_FONTFILE2  "FONTS:jkff/16/kanji.font.all"
#define JKFF_FONTFILE3  "FONTS:kanji.font.all"

#define JKFF_ASCII_W    8
#define JKFF_KANJI_W    16
#define JKFF_SCAN       16
#define JKFF_CNUM       (1L * 0x100 + 2L * 94 * 94)  /* bytes per scanline in expanded buffer */
#define JKFF_BUFSIZE    (JKFF_CNUM * JKFF_SCAN)

static UBYTE *jkff_buf = NULL;     /* expanded font buffer */
static BOOL jkff_tried = FALSE;    /* tried loading */
static UBYTE *jkff_templ8 = NULL;   /* chip template buffer (16 bytes) */
static UBYTE *jkff_templ16 = NULL;  /* chip template buffer (32 bytes) */

static void JkffFree(void)
{
   if(jkff_templ16)
   {
      Freemem(jkff_templ16);
      jkff_templ16=NULL;
   }
   if(jkff_templ8)
   {
      Freemem(jkff_templ8);
      jkff_templ8=NULL;
   }
   if(jkff_buf)
   {
      Freemem(jkff_buf);
      jkff_buf=NULL;
   }
   jkff_tried=FALSE;
}

static BOOL IsJkffFontActive(struct RastPort *rp)
{
   struct TextFont *fnt;
   UBYTE *name;

   if(!rp) return FALSE;
   fnt=rp->Font;
   if(!fnt) return FALSE;
   name=(UBYTE *)fnt->tf_Message.mn_Node.ln_Name;
   if(!name) return FALSE;
   /* Accept "jkff", "jkff.font", "jkff/16", etc. */
   if(STRNIEQUAL(name,"jkff",4)) return TRUE;
   return FALSE;
}

/* Compute offset into expanded buffer for a Shift_JIS 2-byte code. */
static long JkffKanjiLocate(UWORD kCode)
{
   long lFont;

   if(kCode>=0x8140 && kCode<=0x9ffc)
   {
      lFont=(long)kCode - (long)(((kCode & 0x1f00) / 4)) - 0x8100;
   }
   else if(kCode>=0xe040 && kCode<=0xeffc)
   {
      lFont=(long)kCode - (long)(((kCode & 0x1f00) / 4)) - 0xe040
         + 192 * ((15 + 32 + 15) / 2);
   }
   else
   {
      return 0;
   }

   if(62 < (lFont % 192))
   {
      lFont -= ((lFont / 192) * 4) + 1;
   }
   else
   {
      lFont -= ((lFont / 192) * 4);
   }

   return 0x100 + 2 * lFont;
}

static void JkffEnsureLoaded(void)
{
   BPTR fh;
   UBYTE kbuff[2];
   long scanline;
   UWORD code;
   long koff;
   long got;
   long expect;
   const char *fname;
   ULONG memflags;

   if(jkff_buf || jkff_tried) return;
   jkff_tried=TRUE;

   /* Blitter source must be in CHIP memory for BltTemplate/BltBitMap*.
    * Allocate font data in CHIP to keep rendering correct on classic systems. */
   memflags=(ULONG)(MEMF_CHIP|MEMF_CLEAR);
   jkff_buf=(UBYTE *)Allocmem((ULONG)JKFF_BUFSIZE,memflags);
   if(!jkff_buf) return;

   jkff_templ8=(UBYTE *)Allocmem((ULONG)JKFF_SCAN,(ULONG)(MEMF_CHIP|MEMF_CLEAR));
   jkff_templ16=(UBYTE *)Allocmem((ULONG)(JKFF_SCAN*2),(ULONG)(MEMF_CHIP|MEMF_CLEAR));
   if(!jkff_templ8 || !jkff_templ16)
   {
      if(jkff_templ8) { Freemem(jkff_templ8); jkff_templ8=NULL; }
      if(jkff_templ16) { Freemem(jkff_templ16); jkff_templ16=NULL; }
      Freemem(jkff_buf);
      jkff_buf=NULL;
      return;
   }

   fh=0;
   fname=JKFF_FONTFILE1;
   fh=Open((STRPTR)fname,MODE_OLDFILE);
   if(!fh)
   {  fname=JKFF_FONTFILE2;
      fh=Open((STRPTR)fname,MODE_OLDFILE);
   }
   if(!fh)
   {  fname=JKFF_FONTFILE3;
      fh=Open((STRPTR)fname,MODE_OLDFILE);
   }
   if(!fh)
   {
      if(jkff_templ8) { Freemem(jkff_templ8); jkff_templ8=NULL; }
      if(jkff_templ16) { Freemem(jkff_templ16); jkff_templ16=NULL; }
      Freemem(jkff_buf);
      jkff_buf=NULL;
      return;
   }

   /* File format: for each scanline (0..15): 256 bytes ASCII then 2 bytes per SJIS code in a fixed scan order. */
   for(scanline=0; scanline<JKFF_SCAN; scanline++)
   {
      expect=0x100;
      got=Read(fh,jkff_buf + (JKFF_CNUM * scanline), (LONG)expect);
      if(got != expect)
      {
         Close(fh);
         if(jkff_templ8) { Freemem(jkff_templ8); jkff_templ8=NULL; }
         if(jkff_templ16) { Freemem(jkff_templ16); jkff_templ16=NULL; }
         Freemem(jkff_buf);
         jkff_buf=NULL;
         return;
      }

      code=0x8140;
      while(code<=0xeafc)
      {
         if(code==0x9ffd) code=0xe040;
         else if((code & 0x00ff) == 0x007f) code++;
         else if((code & 0x00ff) == 0x00fd) code = (UWORD)(code + 3 + 0x40);
         else if(code==0x849f) code=0x889f;

         got=Read(fh,kbuff,2);
         if(got != 2)
         {
            Close(fh);
            if(jkff_templ8) { Freemem(jkff_templ8); jkff_templ8=NULL; }
            if(jkff_templ16) { Freemem(jkff_templ16); jkff_templ16=NULL; }
            Freemem(jkff_buf);
            jkff_buf=NULL;
            return;
         }

         koff=JkffKanjiLocate(code);
         jkff_buf[(JKFF_CNUM * scanline) + koff + 0] = kbuff[0];
         jkff_buf[(JKFF_CNUM * scanline) + koff + 1] = kbuff[1];

         code++;
      }
   }

   Close(fh);
}

static BOOL IsSjisLead(UBYTE b)
{
   if((b>=0x81 && b<=0x9f) || (b>=0xe0 && b<=0xfc)) return TRUE;
   return FALSE;
}

static BOOL IsSjisTrail(UBYTE b)
{
   if(b==0x7f) return FALSE;
   if(b>=0x40 && b<=0xfc) return TRUE;
   return FALSE;
}

static void JkffDrawGlyph8(struct RastPort *rp, UBYTE code)
{
   long i;
   long x,y;

   if(!jkff_buf) return;
   if(!jkff_templ8) return;
   for(i=0;i<JKFF_SCAN;i++)
   {
      jkff_templ8[i]=jkff_buf[(JKFF_CNUM * i) + (long)code];
   }
   x=(long)rp->cp_x;
   y=(long)rp->cp_y;
   WaitBlit();
   BltTemplate(jkff_templ8,0,1,rp,(LONG)x,(LONG)(y - (JKFF_SCAN - 1)),(LONG)JKFF_ASCII_W,(LONG)JKFF_SCAN);
   Move(rp,(LONG)(x + JKFF_ASCII_W),(LONG)y);
}

static void JkffDrawGlyph16(struct RastPort *rp, UWORD sjis)
{
   long off;
   long i;
   long x,y;

   if(!jkff_buf) return;
   if(!jkff_templ16) return;
   off=JkffKanjiLocate(sjis);
   for(i=0;i<JKFF_SCAN;i++)
   {
      jkff_templ16[i*2+0]=jkff_buf[(JKFF_CNUM * i) + off + 0];
      jkff_templ16[i*2+1]=jkff_buf[(JKFF_CNUM * i) + off + 1];
   }
   x=(long)rp->cp_x;
   y=(long)rp->cp_y;
   WaitBlit();
   BltTemplate(jkff_templ16,0,2,rp,(LONG)x,(LONG)(y - (JKFF_SCAN - 1)),(LONG)JKFF_KANJI_W,(LONG)JKFF_SCAN);
   Move(rp,(LONG)(x + JKFF_KANJI_W),(LONG)y);
}

static void JkffText(struct RastPort *rp, UBYTE *string, ULONG count)
{
   ULONG i;
   UBYTE b0,b1;
   UWORD sj;

   if(!rp || !string || count==0) return;
   JkffEnsureLoaded();
   if(!jkff_buf)
   {  /* If the JKFF bitmap file isn't available, fall back to standard Text()
       * so the page remains readable (ASCII/Latin-1). */
      Text(rp,string,count);
      return;
   }

   i=0;
   while(i<count)
   {
      b0=string[i];
      if(IsSjisLead(b0) && (i+1<count))
      {
         b1=string[i+1];
         if(IsSjisTrail(b1))
         {
            sj=(UWORD)(((UWORD)b0<<8) | (UWORD)b1);
            JkffDrawGlyph16(rp,sj);
            i+=2;
            continue;
         }
      }
      JkffDrawGlyph8(rp,b0);
      i++;
   }
}

static ULONG JkffTextLength(struct RastPort *rp, UBYTE *string, ULONG count)
{
   ULONG i;
   ULONG w;
   UBYTE b0,b1;

   (void)rp;
   i=0;
   w=0;
   while(i<count)
   {
      b0=string[i];
      if(IsSjisLead(b0) && (i+1<count))
      {
         b1=string[i+1];
         if(IsSjisTrail(b1))
         {
            w += JKFF_KANJI_W;
            i += 2;
            continue;
         }
      }
      w += JKFF_ASCII_W;
      i++;
   }
   return w;
}

static ULONG JkffTextFit(struct RastPort *rp, UBYTE *string, UWORD count, UWORD cwidth)
{
   UWORD i;
   ULONG w;
   UBYTE b0,b1;
   UWORD step;

   (void)rp;
   i=0;
   w=0;
   while(i<count)
   {
      b0=string[i];
      step=1;
      if(IsSjisLead(b0) && (i+1<count))
      {
         b1=string[i+1];
         if(IsSjisTrail(b1))
         {
            step=2;
            if(w + JKFF_KANJI_W > cwidth) break;
            w += JKFF_KANJI_W;
            i = (UWORD)(i + 2);
            continue;
         }
      }
      if(w + JKFF_ASCII_W > cwidth) break;
      w += JKFF_ASCII_W;
      i = (UWORD)(i + step);
   }
   return i;
}

/*------------------------------------------------------------------------*/

/* Initialize ttengine.library support */
BOOL InitTTEngine(void)
{
   /* Try to open ttengine.library */
   TTEngineBase = OpenLibrary("ttengine.library", TTENGINEMINVERSION);
   if(!TTEngineBase)
   {
      ttengine_available = FALSE;
      if(httpdebug)
         AwebLog("tte", "ttengine.library not available (OpenLibrary failed)");
      return FALSE;
   }
   
   /* Check version */
   if(TTEngineBase->lib_Version < TTENGINEMINVERSION)
   {
      CloseLibrary(TTEngineBase);
      TTEngineBase = NULL;
      ttengine_available = FALSE;
      if(httpdebug)
         AwebLog("tte", "ttengine.library version too old (need >= %u)", TTENGINEMINVERSION);
      return FALSE;
   }
   
   ttengine_available = TRUE;
   if(httpdebug)
      AwebLog("tte", "TTengine active library version %u.%u",
         (unsigned)TTEngineBase->lib_Version, (unsigned)TTEngineBase->lib_Revision);
   return TRUE;
}

/*------------------------------------------------------------------------*/

/* Cleanup ttengine.library support */
void FreeTTEngine(void)
{
   JkffFree();
   if(TTEngineBase)
   {
      if(httpdebug)
         AwebLog("tte", "TTengine shutdown");
      CloseLibrary(TTEngineBase);
      TTEngineBase = NULL;
   }
   ttengine_available = FALSE;
}

/*------------------------------------------------------------------------*/

/* Check if ttengine is available */
BOOL TTEngineAvailable(void)
{
   return ttengine_available;
}

/*------------------------------------------------------------------------*/

/* Convert Amiga font name to TrueType family name */
/* Removes .font extension and maps common Amiga font names to TrueType equivalents */
static UBYTE *GetTTFamilyName(UBYTE *fontname, UBYTE *buffer, ULONG bufsize)
{
   UBYTE *p;
   ULONG len;
   
   if(!fontname || !buffer || bufsize == 0)
   {
      return NULL;
   }
   
   /* Copy font name to buffer */
   len = strlen(fontname);
   if(len >= bufsize) len = bufsize - 1;
   strncpy(buffer, fontname, len);
   buffer[len] = '\0';
   
   /* Remove .font extension if present */
   p = buffer + len;
   while(p > buffer && *p != '.') p--;
   if(*p == '.')
   {
      *p = '\0';
   }
   
   /* Map common Amiga font names to TrueType family names */
   /* Try exact match first, then common mappings */
   if(STRIEQUAL(buffer, "CGTimes") || STRIEQUAL(buffer, "times") || STRIEQUAL(buffer, "Times"))
   {
      strncpy(buffer, "Times New Roman", bufsize - 1);
      buffer[bufsize - 1] = '\0';
   }
   else if(STRIEQUAL(buffer, "CGHelvetica") || STRIEQUAL(buffer, "helvetica") || STRIEQUAL(buffer, "Helvetica"))
   {
      strncpy(buffer, "Arial", bufsize - 1);
      buffer[bufsize - 1] = '\0';
   }
   else if(STRIEQUAL(buffer, "Courier") || STRIEQUAL(buffer, "courier"))
   {
      strncpy(buffer, "Courier New", bufsize - 1);
      buffer[bufsize - 1] = '\0';
   }
   else if(STRIEQUAL(buffer, "Topaz") || STRIEQUAL(buffer, "topaz"))
   {
      strncpy(buffer, "Courier New", bufsize - 1);
      buffer[bufsize - 1] = '\0';
   }
   /* If no mapping found, use the name as-is (might match a family in database) */
   /* The database might have the font registered with the same name */
   
   return buffer;
}

/*------------------------------------------------------------------------*/

/* Look up the fontsize from Fontprefs for a given TextFont */
/* Returns the fontsize if found, or tf_YSize as fallback */
static LONG GetFontSizeFromPrefs(struct TextFont *font)
{
   struct Fontalias *fa;
   short i;
   LONG fontsize;
   
   /* Default to tf_YSize */
   fontsize = (LONG)font->tf_YSize;
   
   /* Search through font alias list to find matching TextFont */
   for(fa = prefs.browser.aliaslist.first; fa->next; fa = fa->next)
   {
      for(i = 0; i < NRFONTS; i++)
      {
         if(fa->fp[i].font == font)
         {
            /* Found matching TextFont - use the fontsize from Fontprefs */
            return (LONG)fa->fp[i].fontsize;
         }
      }
   }
   
   /* Also check default font preferences */
   for(i = 0; i < NRFONTS; i++)
   {
      if(prefs.browser.font[0][i].font == font || prefs.browser.font[1][i].font == font)
      {
         /* Found in default fonts - use the fontsize from Fontprefs */
         return (LONG)prefs.browser.font[0][i].fontsize;
      }
   }
   
   /* Not found in Fontprefs - use tf_YSize as fallback */
   return fontsize;
}

/*------------------------------------------------------------------------*/

/* Convert TextFont to ttengine font handle */
APTR TTEngineOpenFont(struct TextFont *font)
{
   struct TagItem tags[6];
   UBYTE *fontname;
   UBYTE familyname[256];
   UBYTE *familytable[4];
   LONG fontsize;
   LONG fontweight;
   LONG fontstyle;
   APTR ttfont;
   
   if(!ttengine_available || !font || !TTEngineBase)
   {
      return NULL;
   }
   
   fontname = font->tf_Message.mn_Node.ln_Name;
   /* Get the actual fontsize from Fontprefs if available */
   /* This ensures we use the exact size that was requested when opening the font */
   /* Falls back to tf_YSize if not found in Fontprefs */
   fontsize = GetFontSizeFromPrefs(font);
   
   /* Convert Amiga font name to TrueType family name */
   if(!GetTTFamilyName(fontname, familyname, sizeof(familyname)))
   {
      return NULL;
   }
   
   /* Build family table - try specific name first, then generic fallbacks */
   familytable[0] = familyname;
   /* Add appropriate generic fallback based on font type */
   if(STRIEQUAL(familyname, "Times New Roman") || STRIEQUAL(familyname, "Georgia") || 
      STRIEQUAL(familyname, "Palatino") || STRIEQUAL(familyname, "Garamond"))
   {
      familytable[1] = "serif";
      familytable[2] = "default";
   }
   else if(STRIEQUAL(familyname, "Arial") || STRIEQUAL(familyname, "Helvetica"))
   {
      familytable[1] = "sans-serif";
      familytable[2] = "default";
   }
   else if(STRIEQUAL(familyname, "Courier New") || STRIEQUAL(familyname, "Courier"))
   {
      familytable[1] = "monospaced";
      familytable[2] = "default";
   }
   else
   {
      /* Try the name as-is, then generic fallback */
      familytable[1] = "default";
      familytable[2] = NULL;
   }
   /* Ensure table is NULL-terminated */
   if(familytable[2] != NULL)
   {
      familytable[3] = NULL;
   }
   else
   {
      familytable[2] = NULL;
   }
   
   /* Determine font weight from flags */
   if(font->tf_Style & FSF_BOLD)
   {
      fontweight = TT_FontWeight_Bold;
   }
   else
   {
      fontweight = TT_FontWeight_Normal;
   }
   
   /* Determine font style from flags */
   if(font->tf_Style & FSF_ITALIC)
   {
      fontstyle = TT_FontStyle_Italic;
   }
   else
   {
      fontstyle = TT_FontStyle_Regular;
   }
   
   /* Build tag list - use FamilyTable to search database */
   tags[0].ti_Tag = TT_FamilyTable;
   tags[0].ti_Data = (ULONG)familytable;
   tags[1].ti_Tag = TT_FontSize;
   tags[1].ti_Data = (ULONG)fontsize;
   tags[2].ti_Tag = TT_FontWeight;
   tags[2].ti_Data = fontweight;
   tags[3].ti_Tag = TT_FontStyle;
   tags[3].ti_Data = fontstyle;
   tags[4].ti_Tag = TAG_END;
   
   /* Open font using ttengine */
   ttfont = TT_OpenFontA(tags);
   
   return ttfont;
}

/*------------------------------------------------------------------------*/

/* Close ttengine font handle */
void TTEngineCloseFont(APTR ttfont)
{
   if(ttengine_available && ttfont && TTEngineBase)
   {
      TT_CloseFont(ttfont);
   }
}

/*------------------------------------------------------------------------*/

/* Parse comma-separated font family list into NULL-terminated table */
/* Note: This function creates a working copy of the fontface string and modifies it */
/* The familytable points into this working copy, so it must remain valid */
static void ParseFontFamilyList(UBYTE *fontface, UBYTE *workbuf, ULONG workbufsize, 
                                 UBYTE **familytable, ULONG maxfamilies)
{
   UBYTE *p, *q, *start;
   ULONG count;
   BOOL inQuotes;
   UBYTE quote;
   
   if(!fontface || !workbuf || !familytable || maxfamilies == 0 || workbufsize == 0)
   {
      if(familytable) familytable[0] = NULL;
      return;
   }
   
   /* Copy fontface to work buffer */
   strncpy(workbuf, fontface, workbufsize - 1);
   workbuf[workbufsize - 1] = '\0';
   
   count = 0;
   p = workbuf;
   inQuotes = FALSE;
   quote = 0;
   
   while(*p && count < maxfamilies - 1)
   {
      /* Skip whitespace */
      while(*p == ' ' || *p == '\t') p++;
      if(!*p) break;
      
      start = p;
      
      /* Check for quoted name */
      if(*p == '"' || *p == '\'')
      {
         quote = *p;
         inQuotes = TRUE;
         start = ++p; /* Skip opening quote */
      }
      
      /* Find end of family name */
      q = p;
      while(*q)
      {
         if(inQuotes)
         {
            if(*q == quote)
            {
               /* End of quoted name */
               break;
            }
         }
         else
         {
            if(*q == ',')
            {
               break;
            }
         }
         q++;
      }
      
      /* Store family name */
      if(q > start)
      {
         /* Null-terminate this family name */
         if(*q) *q++ = '\0';
         familytable[count++] = start;
      }
      
      p = q;
      if(*p == ',') p++;
      inQuotes = FALSE;
      quote = 0;
   }
   
   /* Add generic fallbacks if not already present */
   if(count < maxfamilies - 2)
   {
      ULONG i;
      BOOL hasSerif = FALSE, hasSansSerif = FALSE, hasMonospaced = FALSE, hasDefault = FALSE;
      
      /* Check what generic families are already in the list */
      for(i = 0; i < count; i++)
      {
         if(STRIEQUAL(familytable[i], "serif")) hasSerif = TRUE;
         else if(STRIEQUAL(familytable[i], "sans-serif")) hasSansSerif = TRUE;
         else if(STRIEQUAL(familytable[i], "monospaced")) hasMonospaced = TRUE;
         else if(STRIEQUAL(familytable[i], "default")) hasDefault = TRUE;
      }
      
      /* Add appropriate generic fallback based on first family */
      if(count > 0 && !hasSerif && !hasSansSerif && !hasMonospaced)
      {
         UBYTE *first = familytable[0];
         if(STRIEQUAL(first, "Times New Roman") || STRIEQUAL(first, "Georgia") || 
            STRIEQUAL(first, "Palatino") || STRIEQUAL(first, "Garamond"))
         {
            if(!hasSerif) familytable[count++] = "serif";
         }
         else if(STRIEQUAL(first, "Arial") || STRIEQUAL(first, "Helvetica") ||
                 STRIEQUAL(first, "Verdana"))
         {
            if(!hasSansSerif) familytable[count++] = "sans-serif";
         }
         else if(STRIEQUAL(first, "Courier New") || STRIEQUAL(first, "Courier"))
         {
            if(!hasMonospaced) familytable[count++] = "monospaced";
         }
      }
      
      /* Always add default as final fallback */
      if(!hasDefault) familytable[count++] = "default";
   }
   
   familytable[count] = NULL;
}

/* Get font name from Fontprefs for a given TextFont */
/* Returns the font name if found, or NULL if not found */
static UBYTE *GetFontNameFromPrefs(struct TextFont *font)
{
   struct Fontalias *fa;
   short i;
   
   if(!font)
   {
      return NULL;
   }
   
   /* Search through font alias list to find matching TextFont */
   for(fa = prefs.browser.aliaslist.first; fa->next; fa = fa->next)
   {
      for(i = 0; i < NRFONTS; i++)
      {
         if(fa->fp[i].font == font)
         {
            /* Found matching TextFont - return the font name from Fontprefs */
            return fa->fp[i].fontname;
         }
      }
   }
   
   /* Also check default font preferences */
   for(i = 0; i < NRFONTS; i++)
   {
      if(prefs.browser.font[0][i].font == font || prefs.browser.font[1][i].font == font)
      {
         /* Found in default fonts - return the font name from Fontprefs */
         return prefs.browser.font[0][i].fontname;
      }
   }
   
   /* Not found in Fontprefs - return NULL */
   return NULL;
}

/* Expandgenericsinfontface() yields prefs disk names (CGTimes, CGTriumvirate). Those are not
 * always registered under the same spelling in ttengine's family DB; map each list entry
 * through GetTTFamilyName() like the no-fontface path so UTF-8 (metrics font often DejaVu)
 * can still bind a serif TrueType instead of falling through to the global sans fallback. */
static void Mapfontfacelisttottnames(UBYTE *src,UBYTE *dest,ULONG destlen)
{
   UBYTE *o;
   UBYTE *oend;
   UBYTE *p;
   UBYTE *segstart;
   UBYTE *q;
   UBYTE seg[160];
   UBYTE mapped[256];
   ULONG sl;
   ULONG ml;
   BOOL first;

   if(!src || !dest || destlen<4UL)
   {
      return;
   }
   o=dest;
   oend=dest+destlen-1UL;
   dest[0]='\0';
   first=TRUE;
   p=src;
   while(*p)
   {
      while(*p==' ' || *p=='\t')
      {
         p++;
      }
      if(!*p)
      {
         break;
      }
      segstart=p;
      q=p;
      while(*q && *q!=',' && *q!=';')
      {
         q++;
      }
      sl=(ULONG)(q-segstart);
      if(sl>=160UL)
      {
         sl=159UL;
      }
      if(sl>0UL)
      {
         strncpy((char *)seg,(char *)segstart,(size_t)sl);
         seg[sl]='\0';
         while(sl>0UL && (seg[sl-1UL]==' ' || seg[sl-1UL]=='\t'))
         {
            sl--;
            seg[sl]='\0';
         }
         if(sl>0UL)
         {
            GetTTFamilyName(seg,mapped,sizeof mapped);
            ml=(ULONG)strlen((char *)mapped);
            if(!first)
            {
               if((ULONG)(oend-o)<2UL)
               {
                  break;
               }
               *o++=',';
               *o++=' ';
            }
            first=FALSE;
            if((ULONG)(oend-o)<ml)
            {
               ml=(ULONG)(oend-o);
            }
            if(ml>0UL)
            {
               strncpy((char *)o,(char *)mapped,(size_t)ml);
               o+=ml;
               *o='\0';
            }
         }
      }
      if(!*q)
      {
         break;
      }
      p=q+1;
   }
}

/* Set font on rastport (uses ttengine if available, else standard SetFont) */
/* fontface can be from CSS font-family or HTML FONT face attribute */
/* If fontface is NULL, font name will be looked up from Fontprefs */
/* style is the text element's style flags (FSF_BOLD, FSF_ITALIC) - use this instead of font->tf_Style */
void TTEngineSetFont(struct RastPort *rp, struct TextFont *font, UBYTE *fontface, USHORT style)
{
   APTR ttfont;
   struct TagItem tags[6];
   struct TagItem posttags[6];
   UBYTE *familytable[8];
   UBYTE workbuf[512];
   UBYTE expandedface[512];
   UBYTE mappedface[512];
   UBYTE *fontname;
   LONG fontsize;
   LONG fontweight;
   LONG fontstyle;
   struct Window *window = NULL;
   struct Screen *screen = NULL;
   BOOL was_ttengine_active = FALSE;
   LONG idx;
   ULONG soft;
   
   if(!rp || !font)
   {
      return;
   }
   tte_last_style_flags=style;
   
   /* Always use normal SetFont first - let AWeb handle font sizing and selection */
   SetFont(rp, font);
   
   /* Never call ttengine when library is not open */
   if(!TTEngineBase || !ttengine_available)
   {
      return;
   }
   
   /* Check if a ttengine font is currently active - we'll need to clear it */
   /* if we can't set a new one for this element */
   was_ttengine_active = IsTTEngineFontActive(rp);
   /* Rebinding: drop UTF-8 mode from a prior measure so Latin/CSS ttengine uses default encoding. */
   if(was_ttengine_active)
   {
      struct TagItem rst[2];
      rst[0].ti_Tag=TT_Encoding;
      rst[0].ti_Data=(ULONG)TT_Encoding_Default;
      rst[1].ti_Tag=TAG_END;
      TT_SetAttrsA(rp,rst);
      tte_utf8_byte_strings=FALSE;
   }
   
   /* Only use ttengine if available (we know it is from the check above, but keep for clarity) */
   if(ttengine_available && TTEngineBase)
   {
      /* Get font name from fontface (CSS or HTML face attribute) or from Fontprefs */
      if(fontface && *fontface)
      {
         /* Generics (serif, sans-serif, …) are not TT family names; expand like Matchfont(). */
         Expandgenericsinfontface(fontface,font,expandedface,sizeof expandedface);
         if(expandedface[0])
         {
            Mapfontfacelisttottnames(expandedface,mappedface,sizeof mappedface);
         }
         else
         {
            Mapfontfacelisttottnames(fontface,mappedface,sizeof mappedface);
         }
         if(mappedface[0])
         {
            fontname = mappedface;
         }
         else if(expandedface[0])
         {
            fontname = expandedface;
         }
         else
         {
            fontname = fontface;
         }
      }
      else
      {
         /* No fontface - try to get font name from TextFont name */
         /* Convert TextFont name to TrueType family name */
         if(GetTTFamilyName(font->tf_Message.mn_Node.ln_Name, workbuf, sizeof(workbuf)))
         {
            fontname = workbuf;
         }
         else
         {
            fontname = font->tf_Message.mn_Node.ln_Name;
         }
      }
      
      /* If we have a font name, try to use ttengine */
      if(fontname && *fontname)
      {
         /* Use the exact same size as the TextFont that AWeb opened */
         fontsize = (LONG)font->tf_YSize;
         
         /* Determine font weight from text element style flags (not font->tf_Style) */
         /* The text element's style flags indicate bold/italic, not the TextFont's style */
         if(style & FSF_BOLD)
         {
            fontweight = TT_FontWeight_Bold;
         }
         else
         {
            fontweight = TT_FontWeight_Normal;
         }
         
         /* Determine font style from text element style flags */
         if(style & FSF_ITALIC)
         {
            fontstyle = TT_FontStyle_Italic;
         }
         else
         {
            fontstyle = TT_FontStyle_Regular;
         }
         
         /* Parse comma-separated font family list (from CSS or HTML face attribute) */
         /* Or use single font name (already converted to TrueType family name if from Fontprefs) */
         ParseFontFamilyList(fontname, workbuf, sizeof(workbuf), 
                            familytable, sizeof(familytable)/sizeof(familytable[0]));
         
         /* Build tag list using font name with same size as TextFont */
         tags[0].ti_Tag = TT_FamilyTable;
         tags[0].ti_Data = (ULONG)familytable;
         tags[1].ti_Tag = TT_FontSize;
         tags[1].ti_Data = (ULONG)fontsize;
         tags[2].ti_Tag = TT_FontWeight;
         tags[2].ti_Data = (ULONG)fontweight;
         tags[3].ti_Tag = TT_FontStyle;
         tags[3].ti_Data = (ULONG)fontstyle;
         tags[4].ti_Tag = TAG_END;
         
         /* Try to open font using ttengine */
         ttfont = TT_OpenFontA(tags);
         
         if(ttfont)
         {
            /* TT_Window / TT_Screen: pen-to-RGB for AA and LUT mapping (SDK TT_SetAttrsA). */
            if(rp->Layer && rp->Layer->Window)
            {
               window = rp->Layer->Window;
            }
            else
            {
               screen = (struct Screen *)Agetattr(Aweb(), AOAPP_Screen);
            }
            
            /* Set ttengine font on rastport */
            /* TT_SetFont returns TRUE on success */
            if(TT_SetFont(rp, ttfont))
            {
               /* One TT_SetAttrsA after TT_SetFont: SDK TT_SetFont example chains attrs; fewer
                * library round-trips. TT_DiskFontMetrics FALSE is the library default and matches
                * FreeType glyph metrics for JAM1 HTML layout (TRUE is diskfont-like JAM2 boxes). */
               soft=(ULONG)TT_SoftStyle_None;
               if((style & FSF_UNDERLINED) != 0)
               {
                  soft|=(ULONG)TT_SoftStyle_Underlined;
               }
               if((style & FSF_STRIKE) != 0)
               {
                  soft|=(ULONG)TT_SoftStyle_Overstriked;
               }
               idx=0;
               if(window)
               {
                  posttags[idx].ti_Tag=TT_Window;
                  posttags[idx].ti_Data=(ULONG)window;
                  idx++;
               }
               else if(screen)
               {
                  posttags[idx].ti_Tag=TT_Screen;
                  posttags[idx].ti_Data=(ULONG)screen;
                  idx++;
               }
               posttags[idx].ti_Tag=TT_Antialias;
               posttags[idx].ti_Data=(ULONG)TT_Antialias_Auto;
               idx++;
               posttags[idx].ti_Tag=TT_DiskFontMetrics;
               posttags[idx].ti_Data=FALSE;
               idx++;
               posttags[idx].ti_Tag=TT_SoftStyle;
               posttags[idx].ti_Data=soft;
               idx++;
               posttags[idx].ti_Tag=TAG_END;
               TT_SetAttrsA(rp,posttags);
               /* Success - ttengine font is now active on rastport */
               /* Use TT_Text() for rendering instead of Text() */
               return;
            }
            else
            {
               /* Failed to set, close and fall through to standard font */
               TT_CloseFont(ttfont);
            }
         }
         /* If ttengine font not found, fall through to use standard TextFont */
      }
   }
   
   /* If we reach here, we're not using ttengine for this font */
   /* If a ttengine font was previously active but we couldn't set a new one, */
   /* we need to clear it to prevent font inheritance */
   /* We know ttengine is available from the check above, so we can safely call IsTTEngineFontActive */
   if(was_ttengine_active)
   {
      /* Check if ttengine font is still active (it shouldn't be if we failed to set a new one) */
      if(IsTTEngineFontActive(rp))
      {
         /* Clear ttengine state on rastport - this will reset it to use standard fonts */
         TT_DoneRastPort(rp);
      }
   }
   
   /* If ttengine not available or font not found, normal SetFont was already called above */
}

/*------------------------------------------------------------------------*/

/* Check if ttengine font is currently active on a rastport */
BOOL IsTTEngineFontActive(struct RastPort *rp)
{
   UBYTE *fontname;
   struct TagItem tags[2];
   BOOL result;
   
   if(!ttengine_available || !TTEngineBase || !rp)
   {
      return FALSE;
   }
   
   /* Try to get font name - if we can get it, a ttengine font is active */
   /* TT_GetAttrsA writes the font name pointer to ti_Data */
   /* We've already checked TTEngineBase above, so it's safe to call TT_GetAttrsA */
   fontname = NULL;
   tags[0].ti_Tag = TT_FontName;
   tags[0].ti_Data = (ULONG)&fontname;
   tags[1].ti_Tag = TAG_END;
   TT_GetAttrsA(rp, tags);
   
   /* If fontname is non-NULL, a ttengine font is active */
   result = (BOOL)(fontname != NULL);
   return result;
}

/*------------------------------------------------------------------------*/

void TTEngineClearRastPort(struct RastPort *rp)
{
   if(!ttengine_available || !TTEngineBase || !rp)
   {
      return;
   }
   if(IsTTEngineFontActive(rp))
   {
      TT_DoneRastPort(rp);
      tte_utf8_byte_strings=FALSE;
      tte_last_style_flags=0;
   }
}

/*------------------------------------------------------------------------*/

void TTEngineSetRastPortTextEncoding(struct RastPort *rp,ULONG encoding)
{
   struct TagItem t[2];
   if(!rp || !TTEngineBase || !ttengine_available)
   {
      return;
   }
   if(!IsTTEngineFontActive(rp))
   {
      return;
   }
   t[0].ti_Tag=TT_Encoding;
   t[0].ti_Data=encoding;
   t[1].ti_Tag=TAG_END;
   TT_SetAttrsA(rp,t);
   tte_utf8_byte_strings=(BOOL)(encoding==AWEB_TT_ENCODING_UTF8);
}

/*------------------------------------------------------------------------*/

/* Text rendering wrapper (uses ttengine if available) */
/* Uses TT_Text() if ttengine font is active, otherwise uses standard Text() */
void TTEngineText(struct RastPort *rp, UBYTE *string, ULONG count)
{
   if(!rp || !string || count == 0)
   {
      return;
   }
   if(IsJkffFontActive(rp))
   {
      JkffText(rp,string,count);
      return;
   }
   /* When library not open, use standard Text only - never call TT_* */
   if(!TTEngineBase)
   {
      Text(rp, string, count);
      return;
   }
   /* Check if ttengine font is active on this rastport */
   if(IsTTEngineFontActive(rp))
   {
      ULONG ch;
      ch=count;
      if(tte_utf8_byte_strings)
      {
         ch=TteUtf8CompleteCharCount(string,count);
      }
      TT_Text(rp, string, ch);
   }
   else
   {
      /* Use standard Text() - no ttengine font active */
      Text(rp, string, count);
   }
}

/*------------------------------------------------------------------------*/

/* Text length wrapper (uses ttengine if available) */
ULONG TTEngineTextLength(struct RastPort *rp, UBYTE *string, ULONG count)
{
   if(!rp || !string || count == 0)
   {
      return 0;
   }
   if(IsJkffFontActive(rp))
   {
      return JkffTextLength(rp,string,count);
   }
   /* When library not open, use standard TextLength only - never call TT_* */
   if(!TTEngineBase)
   {
      return TextLength(rp, string, count);
   }
   /* Check if ttengine font is active on this rastport */
   if(IsTTEngineFontActive(rp))
   {
      /* Prefer max(horizontal advance, ink span); italic/overhang can exceed te_Width alone. */
      struct TextExtent te;
      ULONG ch;
      WORD wc;
      ULONG adv;
      LONG ink;
      ULONG outw;
      ch=count;
      if(tte_utf8_byte_strings)
      {
         ch=TteUtf8CompleteCharCount(string,count);
      }
      if(ch>32767UL)
      {
         ch=32767UL;
      }
      wc=(WORD)ch;
      TT_TextExtent(rp,string,wc,&te);
      adv=(ULONG)te.te_Width;
      ink=(LONG)te.te_Extent.MaxX-(LONG)te.te_Extent.MinX+1L;
      if(ink<1L)
      {
         ink=(LONG)adv;
      }
      outw=adv;
      if((ULONG)ink>outw)
      {
         outw=(ULONG)ink;
      }
      outw+=TTEngineBoldInkPad(rp);
      return outw;
   }
   else
   {
      /* Use standard TextLength() - no ttengine font active */
      return TextLength(rp, string, count);
   }
}

/*------------------------------------------------------------------------*/

/* Text extent wrapper (uses ttengine if available) */
void TTEngineTextExtent(struct RastPort *rp, UBYTE *string, WORD count, struct TextExtent *te)
{
   if(!rp || !string || count == 0 || !te)
   {
      return;
   }
   /* When library not open, use standard TextExtent only - never call TT_* */
   if(!TTEngineBase)
   {
      TextExtent(rp, string, count, te);
      return;
   }
   /* Check if ttengine font is active on this rastport */
   if(IsTTEngineFontActive(rp))
   {
      ULONG ch;
      WORD wc;
      ch=(ULONG)count;
      if(tte_utf8_byte_strings && count>0)
      {
         ch=TteUtf8CompleteCharCount(string,(ULONG)count);
      }
      if(ch>32767UL)
      {
         ch=32767UL;
      }
      wc=(WORD)ch;
      TT_TextExtent(rp, string, wc, te);
   }
   else
   {
      /* Use standard TextExtent() - no ttengine font active */
      TextExtent(rp, string, count, te);
   }
}

/*------------------------------------------------------------------------*/

/* Text fit wrapper (uses ttengine if available) */
ULONG TTEngineTextFit(struct RastPort *rp, UBYTE *string, UWORD count, struct TextExtent *te,
   struct TextExtent *tec, WORD dir, UWORD cwidth, UWORD cheight)
{
   if(!rp || !string || count == 0)
   {
      return 0;
   }
   if(IsJkffFontActive(rp))
   {
      (void)te;
      (void)tec;
      (void)dir;
      (void)cheight;
      return JkffTextFit(rp,string,count,cwidth);
   }
   /* When library not open, use standard TextFit only - never call TT_* */
   if(!TTEngineBase)
   {
      return TextFit(rp, string, count, te, tec, dir, cwidth, cheight);
   }
   /* Check if ttengine font is active on this rastport */
   if(IsTTEngineFontActive(rp))
   {
      ULONG got;
      ULONG umax;
      UWORD ucnt;
      UWORD fitw;
      ucnt=count;
      if(tte_utf8_byte_strings && string && count>0)
      {
         umax=TteUtf8CompleteCharCount(string,(ULONG)count);
         if(umax>65535UL)
         {
            umax=65535UL;
         }
         ucnt=(UWORD)umax;
      }
      fitw=TteTextFitConstrainedWidth(cwidth,rp);
      /* Stock ttengine TT_TextFit with NULL constr_extent compares horizontal ink to (height)
       * and vertical to (width); swap last two args to match graphics TextFit semantics. */
      if(tec)
      {
         got=TT_TextFit(rp, string, ucnt, te, tec, dir, fitw, cheight);
      }
      else
      {
         got=TT_TextFit(rp, string, ucnt, te, NULL, dir, cheight, fitw);
      }
      if(tte_utf8_byte_strings && string && got>0UL)
      {
         umax=TteUtf8BytesForCharCount(string,(ULONG)count,got);
         if(umax>(ULONG)count)
         {
            umax=(ULONG)count;
         }
         return umax;
      }
      return got;
   }
   else
   {
      /* Use standard TextFit() - no ttengine font active */
      return TextFit(rp, string, count, te, tec, dir, cwidth, cheight);
   }
}
