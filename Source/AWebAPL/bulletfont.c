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

/* bulletfont.c - AWEB4: HTML fonts via bullet.library.
 *
 * TextFont shell (stub / optional packed CharData) plus a live GlyphEngine
 * path matching stuff/BulletExamples: OT_GlyphMap + BltTemplate, text
 * kerning (larger sizes only), shear/embolden, optional rotate/set-factor.
 * Metrics come from a WidthList prefetch; rasters are cropped black-box
 * CHIP copies, flushed on style/DPI change. Prefer TTEngine when open. */

#include "aweb.h"
#include "bulletfont.h"
#include "awebutf8.h"
#include "ttengine.h"
#include "application.h"
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/displayinfo.h>
#include <intuition/screens.h>
#include <diskfont/diskfont.h>
#include <diskfont/diskfonttag.h>
#include <diskfont/oterrors.h>
#include <diskfont/glyph.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/bullet.h>
#include <proto/diskfont.h>
#include <string.h>

#ifdef AWEB4

/*------------------------------------------------------------------------*/

#define BF_LOCHAR       32
#define BF_HICHAR       255
#define BF_NCHARS       (BF_HICHAR-BF_LOCHAR+1)
#define BF_XDPI_DEF     72UL
#define BF_YDPI_DEF     72UL
#define BF_MAX_CHIP     (96UL*1024UL)
/* Outline ink sits small in the em vs old TTEngine/diskfont look; open a
 * little larger so HTML ysize requests match perceived size. */
#define BF_OPTICAL_NUM  5
#define BF_OPTICAL_DEN  4

/* Soft italic ~15 degrees (from BulletExamples Rotate sine table). */
#define BF_SHEAR_SIN    0x0000424eL
#define BF_SHEAR_COS    0x0000f747L
/* Soft bold ~1/32 em. */
#define BF_EMBOLDEN     0x00000800L

struct Library *BulletBase=NULL;

#define BF_GCACHE_SLOTS 256
#define BF_KERNCACHE_SLOTS 128
/* Below this pixel height, skip OT_TextKernPair (HTML body sizes). */
#define BF_KERN_MIN_YSIZE 20

/* One cached glyph raster + metrics (avoids OT_GlyphMap every paint). */
struct BulletGlyphCache
{  UBYTE valid;       /* metrics known (WidthList and/or GlyphMap) */
   UBYTE hasbits;     /* CHIP black-box raster present */
   UWORD bmModulo;    /* cropped template modulo */
   UWORD blackWidth;
   UWORD blackHeight;
   WORD blit_dx;      /* blackLeft - x0 */
   WORD blit_dy;      /* blackTop - y0 */
   ULONG base_pix;    /* (design_width * emw) >> 16 before kern */
   PLANEPTR blit_src; /* start of cropped CHIP template */
   UWORD blit_shift;  /* BlackLeft & 15 */
   UWORD pad;
   PLANEPTR bitmap;   /* owned CHIP black-box block, or NULL */
   ULONG bitmapbytes;
};

/* Recent OT_TextKernPair results — avoids SetInfo/Obtain per character. */
struct BulletKernCache
{  UWORD key;     /* (c << 8) | c2 ; 0 = empty if c==0 && c2==0 unused carefully */
   UBYTE used;
   UBYTE pad;
   LONG kern;
};

struct BulletFontNode
{  NODE(BulletFontNode);
   struct TextFont *font;
   ULONG refcount;
   UBYTE *chardata;
   ULONG *charloc;
   WORD *charspace;
   WORD *charkern;
   UBYTE *name;
   short ysize;
   BOOL stubonly;
   /* Live GlyphEngine (BulletExamples path). */
   struct GlyphEngine *ge;
   struct TagItem *otags;
   UBYTE *otagpath;
   ULONG emw;
   ULONG emh;
   ULONG xdpi;
   ULONG ydpi;
   ULONG pointfixed;
   USHORT livestyle;
   LONG rot_sin;
   LONG rot_cos;
   LONG shear_sin;
   LONG shear_cos;
   LONG set_factor;
   LONG emb_x;
   LONG emb_y;
   PLANEPTR scratch;
   ULONG scratchbytes;
   /* Cached OT_GlyphMap rasters (Latin-1); flushed on style/DPI change. */
   struct BulletGlyphCache *gcache;
   struct BulletKernCache *kcache;
   USHORT style_applied;
   BOOL style_live;
};

static LIST(BulletFontNode) bfonts;
static BOOL bullet_inited=FALSE;
static BOOL bullet_ok=FALSE;

/* Cached OpenDiskFont faces for <input>/<textarea>/<select>/<button> —
 * Bullet metric stubs have empty CharData so graphics Text() draws nothing. */
struct FormDiskNode
{  NODE(FormDiskNode);
   UBYTE *name;
   short ysize;
   struct TextFont *font;
};
static LIST(FormDiskNode) formdiskfonts;

/* RastPort currently armed for live Bullet drawing. */
static struct RastPort *live_rp=NULL;
static struct BulletFontNode *live_bn=NULL;
/* TRUE when document buffer is UTF-8 (multi-byte); Latin-1 uses raw bytes. */
static BOOL live_src_utf8=FALSE;

static BOOL Isspaceglyph(ULONG c);
static struct BulletFontNode *Findbn(struct TextFont *tf);

static short OpticalYsize(short ysize)
{  long y;
   if(ysize<4) ysize=4;
   if(ysize>128) ysize=128;
   y=((long)ysize*(long)BF_OPTICAL_NUM)/(long)BF_OPTICAL_DEN;
   if(y<(long)ysize) y=(long)ysize;
   if(y>128L) y=128L;
   return (short)y;
}

/*------------------------------------------------------------------------*/

static void FlushGlyphCache(struct BulletFontNode *bn)
{  ULONG i;
   struct BulletGlyphCache *slot;
   if(!bn) return;
   if(bn->gcache)
   {  for(i=0;i<(ULONG)BF_GCACHE_SLOTS;i++)
      {  slot=&bn->gcache[i];
         if(slot->bitmap)
         {  FreeMem(slot->bitmap,slot->bitmapbytes);
            slot->bitmap=NULL;
            slot->bitmapbytes=0;
         }
         slot->valid=0;
         slot->hasbits=0;
         slot->blit_src=NULL;
      }
   }
   if(bn->kcache)
      memset(bn->kcache,0,sizeof(struct BulletKernCache)*(ULONG)BF_KERNCACHE_SLOTS);
}

static void FreeGlyphCache(struct BulletFontNode *bn)
{  if(!bn) return;
   FlushGlyphCache(bn);
   if(bn->gcache)
   {  FREE(bn->gcache);
      bn->gcache=NULL;
   }
   if(bn->kcache)
   {  FREE(bn->kcache);
      bn->kcache=NULL;
   }
}

static BOOL EnsureGlyphCache(struct BulletFontNode *bn)
{  if(!bn) return FALSE;
   if(!bn->gcache)
   {  bn->gcache=ALLOCSTRUCT(BulletGlyphCache,BF_GCACHE_SLOTS,MEMF_PUBLIC|MEMF_CLEAR);
      if(!bn->gcache) return FALSE;
   }
   if(!bn->kcache)
   {  bn->kcache=ALLOCSTRUCT(BulletKernCache,BF_KERNCACHE_SLOTS,MEMF_PUBLIC|MEMF_CLEAR);
      if(!bn->kcache) return FALSE;
   }
   return TRUE;
}

static LONG LookupKern(struct BulletFontNode *bn,ULONG c,ULONG c2)
{  struct BulletKernCache *slot;
   struct TagItem tags[4];
   LONG kern;
   UWORD key;
   ULONG h;
   if(!bn || !bn->ge || !bn->kcache) return 0;
   if(bn->ysize<(short)BF_KERN_MIN_YSIZE) return 0;
   if(Isspaceglyph(c) || Isspaceglyph(c2) || c2==0UL) return 0;
   if(c>255UL) c=255UL;
   if(c2>255UL) c2=255UL;
   key=(UWORD)((c<<8)|c2);
   h=(ULONG)(key%(UWORD)BF_KERNCACHE_SLOTS);
   slot=&bn->kcache[h];
   if(slot->used && slot->key==key) return slot->kern;

   kern=0;
   tags[0].ti_Tag=OT_GlyphCode;
   tags[0].ti_Data=c;
   tags[1].ti_Tag=OT_GlyphCode2;
   tags[1].ti_Data=c2;
   tags[2].ti_Tag=TAG_END;
   if(SetInfoA(bn->ge,tags)==OTERR_Success)
   {  tags[0].ti_Tag=OT_TextKernPair;
      tags[0].ti_Data=(ULONG)&kern;
      tags[1].ti_Tag=TAG_END;
      ObtainInfoA(bn->ge,tags);
   }
   slot->used=1;
   slot->key=key;
   slot->kern=kern;
   return kern;
}

/*
 * Copy only the black-box words into a tight CHIP template.
 * BltTemplate still needs the BlackLeft&15 shift, so the first word is
 * the same word that held BlackLeft in the engine map.
 */
static BOOL StoreCroppedBitmap(struct BulletGlyphCache *slot,struct GlyphMap *gm)
{  UWORD blackLeft,blackTop,blackWidth,blackHeight;
   UWORD srcMod,shift,words,dstMod,row;
   ULONG bytes;
   PLANEPTR srcrow,dstrow;
   if(!slot || !gm || !gm->glm_BitMap) return FALSE;
   blackLeft=gm->glm_BlackLeft;
   blackTop=gm->glm_BlackTop;
   blackWidth=gm->glm_BlackWidth;
   blackHeight=gm->glm_BlackHeight;
   srcMod=gm->glm_BMModulo;
   if(!blackWidth || !blackHeight || !srcMod || !gm->glm_BMRows)
      return FALSE;
   if(blackTop>=gm->glm_BMRows) return FALSE;
   if((ULONG)blackTop+(ULONG)blackHeight>(ULONG)gm->glm_BMRows)
      blackHeight=(UWORD)(gm->glm_BMRows-blackTop);
   shift=(UWORD)(blackLeft&0xF);
   words=(UWORD)((shift+blackWidth+15)>>4);
   if(words<1) words=1;
   dstMod=(UWORD)(words<<1);
   if(((ULONG)((blackLeft>>4)<<1)+(ULONG)dstMod)>(ULONG)srcMod)
   {  /* Engine row shorter than expected — fall back to remaining bytes. */
      words=(UWORD)((srcMod-((blackLeft>>4)<<1))>>1);
      if(words<1) return FALSE;
      dstMod=(UWORD)(words<<1);
   }
   bytes=(ULONG)dstMod*(ULONG)blackHeight;
   if(bytes<1UL || bytes>BF_MAX_CHIP) return FALSE;
   slot->bitmap=(PLANEPTR)AllocMem(bytes,MEMF_CHIP|MEMF_PUBLIC);
   if(!slot->bitmap) return FALSE;
   slot->bitmapbytes=bytes;
   srcrow=(PLANEPTR)((ULONG)gm->glm_BitMap
      +(ULONG)srcMod*(ULONG)blackTop
      +(((ULONG)blackLeft>>4)<<1));
   dstrow=slot->bitmap;
   for(row=0;row<blackHeight;row++)
   {  CopyMem(srcrow,dstrow,(ULONG)dstMod);
      srcrow=(PLANEPTR)((ULONG)srcrow+(ULONG)srcMod);
      dstrow=(PLANEPTR)((ULONG)dstrow+(ULONG)dstMod);
   }
   slot->bmModulo=dstMod;
   slot->blackWidth=blackWidth;
   slot->blackHeight=blackHeight;
   slot->blit_shift=shift;
   slot->blit_src=slot->bitmap;
   slot->hasbits=1;
   return TRUE;
}

/* Fill Latin-1 base_pix from one WidthList so measure skips OT_GlyphMap. */
static void PrefetchWidthMetrics(struct BulletFontNode *bn)
{  struct TagItem tags[4];
   struct MinList *wl;
   struct GlyphWidthEntry *we;
   struct BulletGlyphCache *slot;
   ULONG code;
   if(!bn || !bn->ge) return;
   if(!EnsureGlyphCache(bn)) return;
   wl=NULL;
   tags[0].ti_Tag=OT_GlyphCode;
   tags[0].ti_Data=0;
   tags[1].ti_Tag=OT_GlyphCode2;
   tags[1].ti_Data=255;
   tags[2].ti_Tag=TAG_END;
   if(SetInfoA(bn->ge,tags)!=OTERR_Success) return;
   tags[0].ti_Tag=OT_WidthList;
   tags[0].ti_Data=(ULONG)&wl;
   tags[1].ti_Tag=TAG_END;
   if(ObtainInfoA(bn->ge,tags)!=OTERR_Success || !wl) return;
   for(we=(struct GlyphWidthEntry *)wl->mlh_Head;
       we->gwe_Node.mln_Succ;
       we=(struct GlyphWidthEntry *)we->gwe_Node.mln_Succ)
   {  code=(ULONG)we->gwe_Code;
      if(code>=(ULONG)BF_GCACHE_SLOTS) continue;
      slot=&bn->gcache[code];
      if(slot->valid) continue;
      slot->valid=1;
      slot->hasbits=0;
      slot->base_pix=(ULONG)(((LONG)we->gwe_Width*(LONG)bn->emw)>>16);
      slot->bitmap=NULL;
      slot->bitmapbytes=0;
      slot->blit_src=NULL;
      slot->blackWidth=0;
      slot->blackHeight=0;
   }
   tags[0].ti_Tag=OT_WidthList;
   tags[0].ti_Data=(ULONG)wl;
   tags[1].ti_Tag=TAG_END;
   ReleaseInfoA(bn->ge,tags);
}

/*
 * Ensure metrics (and optionally a cropped raster) for `code`.
 * When needbits is FALSE, WidthList prefetch / single-width is enough.
 */
static struct BulletGlyphCache *CacheGlyph(struct BulletFontNode *bn,ULONG code,
   BOOL needbits)
{  struct BulletGlyphCache *slot;
   struct GlyphMap *gm;
   struct MinList *wl;
   struct GlyphWidthEntry *we;
   struct TagItem tags[4];
   ULONG glm_width;
   if(!bn || !bn->ge) return NULL;
   if(code>=(ULONG)BF_GCACHE_SLOTS) code=(ULONG)'?';
   if(!EnsureGlyphCache(bn)) return NULL;
   slot=&bn->gcache[code];
   if(slot->valid && (!needbits || slot->hasbits))
      return slot;

   /* Metrics-only miss: one-glyph WidthList (prefetch usually covers this). */
   if(!slot->valid && !needbits)
   {  wl=NULL;
      tags[0].ti_Tag=OT_GlyphCode;
      tags[0].ti_Data=code;
      tags[1].ti_Tag=OT_GlyphCode2;
      tags[1].ti_Data=code;
      tags[2].ti_Tag=TAG_END;
      if(SetInfoA(bn->ge,tags)==OTERR_Success)
      {  tags[0].ti_Tag=OT_WidthList;
         tags[0].ti_Data=(ULONG)&wl;
         tags[1].ti_Tag=TAG_END;
         if(ObtainInfoA(bn->ge,tags)==OTERR_Success && wl)
         {  we=(struct GlyphWidthEntry *)wl->mlh_Head;
            if(we->gwe_Node.mln_Succ)
            {  slot->valid=1;
               slot->hasbits=0;
               slot->base_pix=(ULONG)(((LONG)we->gwe_Width*(LONG)bn->emw)>>16);
            }
            tags[0].ti_Tag=OT_WidthList;
            tags[0].ti_Data=(ULONG)wl;
            tags[1].ti_Tag=TAG_END;
            ReleaseInfoA(bn->ge,tags);
         }
      }
      if(slot->valid) return slot;
   }

   tags[0].ti_Tag=OT_GlyphCode;
   tags[0].ti_Data=code;
   tags[1].ti_Tag=OT_GlyphCode2;
   tags[1].ti_Data=0;
   tags[2].ti_Tag=TAG_END;
   if(SetInfoA(bn->ge,tags)!=OTERR_Success) return slot->valid?slot:NULL;

   gm=NULL;
   wl=NULL;
   tags[0].ti_Tag=OT_GlyphMap;
   tags[0].ti_Data=(ULONG)&gm;
   tags[1].ti_Tag=TAG_END;
   if(ObtainInfoA(bn->ge,tags)!=OTERR_Success || !gm)
   {  gm=NULL;
      if(!slot->valid)
      {  tags[0].ti_Tag=OT_GlyphCode2;
         tags[0].ti_Data=code;
         tags[1].ti_Tag=TAG_END;
         if(SetInfoA(bn->ge,tags)==OTERR_Success)
         {  tags[0].ti_Tag=OT_WidthList;
            tags[0].ti_Data=(ULONG)&wl;
            tags[1].ti_Tag=TAG_END;
            ObtainInfoA(bn->ge,tags);
         }
      }
   }

   glm_width=0;
   if(gm)
   {  glm_width=(ULONG)gm->glm_Width;
      slot->blit_dx=(WORD)((LONG)gm->glm_BlackLeft-(LONG)gm->glm_X0);
      slot->blit_dy=(WORD)((LONG)gm->glm_BlackTop-(LONG)gm->glm_Y0);
      if(needbits || !slot->hasbits)
         (void)StoreCroppedBitmap(slot,gm);
      tags[0].ti_Tag=OT_GlyphMap;
      tags[0].ti_Data=(ULONG)gm;
      tags[1].ti_Tag=TAG_END;
      ReleaseInfoA(bn->ge,tags);
   }
   else if(wl)
   {  we=(struct GlyphWidthEntry *)wl->mlh_Head;
      if(we->gwe_Node.mln_Succ)
         glm_width=(ULONG)we->gwe_Width;
      tags[0].ti_Tag=OT_WidthList;
      tags[0].ti_Data=(ULONG)wl;
      tags[1].ti_Tag=TAG_END;
      ReleaseInfoA(bn->ge,tags);
   }
   if(!slot->valid)
   {  slot->valid=1;
      slot->base_pix=(ULONG)(((LONG)glm_width*(LONG)bn->emw)>>16);
   }
   else if(glm_width && !slot->base_pix)
      slot->base_pix=(ULONG)(((LONG)glm_width*(LONG)bn->emw)>>16);
   return slot;
}

static void Freelive(struct BulletFontNode *bn)
{  if(!bn) return;
   if(live_bn==bn)
   {  live_bn=NULL;
      live_rp=NULL;
   }
   FreeGlyphCache(bn);
   bn->style_live=FALSE;
   if(bn->ge)
   {  CloseEngine(bn->ge);
      bn->ge=NULL;
   }
   if(bn->otags)
   {  FREE(bn->otags);
      bn->otags=NULL;
   }
   if(bn->otagpath)
   {  FREE(bn->otagpath);
      bn->otagpath=NULL;
   }
   if(bn->scratch)
   {  FreeMem(bn->scratch,bn->scratchbytes);
      bn->scratch=NULL;
      bn->scratchbytes=0;
   }
}

static void Freenode(struct BulletFontNode *bn)
{  if(!bn) return;
   Freelive(bn);
   if(bn->font) FREE(bn->font);
   if(bn->chardata) FREE(bn->chardata);
   if(bn->charloc) FREE(bn->charloc);
   if(bn->charspace) FREE(bn->charspace);
   if(bn->charkern) FREE(bn->charkern);
   if(bn->name) FREE(bn->name);
   FREE(bn);
}

static struct BulletFontNode *Findbn(struct TextFont *tf)
{  struct BulletFontNode *bn;
   if(!tf) return NULL;
   for(bn=bfonts.first;bn->next;bn=bn->next)
   {  if(bn->font==tf) return bn;
   }
   return NULL;
}

static struct TagItem *Allocotag(UBYTE *otagname)
{  BPTR fh;
   struct FileInfoBlock *fib;
   struct TagItem *buf,*tip,*ti;
   LONG nbytes;
   fib=AllocDosObject(DOS_FIB,NULL);
   if(!fib) return NULL;
   buf=NULL;
   fh=Open(otagname,MODE_OLDFILE);
   if(fh)
   {  if(ExamineFH(fh,fib) && fib->fib_Size>=(LONG)sizeof(struct TagItem))
      {  nbytes=fib->fib_Size;
         buf=(struct TagItem *)ALLOCTYPE(UBYTE,(ULONG)nbytes,MEMF_PUBLIC|MEMF_CLEAR);
         if(buf)
         {  if(Read(fh,(UBYTE *)buf,nbytes)==nbytes
            && buf->ti_Tag==OT_FileIdent
            && buf->ti_Data==(ULONG)nbytes)
            {  tip=buf;
               while((ti=NextTagItem(&tip))!=NULL)
               {  if(ti->ti_Tag&OT_Indirect)
                  {  if(ti->ti_Data<(ULONG)nbytes)
                        ti->ti_Data=(ULONG)buf+ti->ti_Data;
                     else
                        ti->ti_Data=(ULONG)buf;
                  }
               }
            }
            else
            {  FREE(buf);
               buf=NULL;
            }
         }
      }
      Close(fh);
   }
   FreeDosObject(DOS_FIB,fib);
   return buf;
}

static BOOL Makeotagpath(UBYTE *fontname,UBYTE *out,ULONG outlen)
{  UBYTE *p;
   UBYTE *base;
   ULONG n;
   if(!fontname || !out || outlen<16UL) return FALSE;
   base=fontname;
   p=fontname;
   while(*p)
   {  if(*p==':' || *p=='/') base=p+1;
      p++;
   }
   n=strlen(base);
   if(n>=5UL && STRIEQUAL(base+n-5,".font")) n-=5UL;
   if(n+11UL>=outlen) return FALSE;
   strcpy(out,"FONTS:");
   strncat(out,base,n);
   strcat(out,".otag");
   return TRUE;
}

static BOOL Isoutlinefontfile(UBYTE *fontname)
{  UBYTE path[256];
   UBYTE *p;
   UBYTE *base;
   ULONG n;
   BPTR fh;
   UWORD id;
   BOOL ok;
   ok=FALSE;
   if(!fontname) return FALSE;
   base=fontname;
   p=fontname;
   while(*p)
   {  if(*p==':' || *p=='/') base=p+1;
      p++;
   }
   n=strlen(base);
   if(n+7UL>=sizeof(path)) return FALSE;
   strcpy(path,"FONTS:");
   strcat(path,base);
   if(n<5UL || !STRIEQUAL(base+n-5,".font")) strcat(path,".font");
   fh=Open(path,MODE_OLDFILE);
   if(fh)
   {  if(Read(fh,&id,sizeof(id))==sizeof(id) && id==OFCH_ID) ok=TRUE;
      Close(fh);
   }
   return ok;
}

static void GetScreenDPI(ULONG *xdpi,ULONG *ydpi)
{  struct Screen *scr;
   struct DisplayInfo di;
   ULONG modeid;
   *xdpi=BF_XDPI_DEF;
   *ydpi=BF_YDPI_DEF;
   scr=(struct Screen *)Agetattr(Aweb(),AOAPP_Screen);
   if(!scr) return;
   modeid=GetVPModeID(&scr->ViewPort);
   memset(&di,0,sizeof(di));
   if(GetDisplayInfoData(NULL,(UBYTE *)&di,sizeof(di),DTAG_DISP,modeid))
   {  /* Aspect from Resolution ticks; Y nominal 72 DPI (diskfonttag.h). */
      if(di.Resolution.x && di.Resolution.y)
      {  *ydpi=BF_YDPI_DEF;
         *xdpi=(BF_YDPI_DEF*(ULONG)di.Resolution.y)/(ULONG)di.Resolution.x;
         if(*xdpi<1UL) *xdpi=BF_XDPI_DEF;
      }
   }
}

static ULONG Pixelstopointsfixed(short ysize,ULONG ysf)
{  ULONG hi,lo,pts;
   hi=ysf>>16;
   lo=ysf&0xFFFFUL;
   if(!hi) hi=1UL;
   if(!lo) lo=1UL;
   pts=((ULONG)ysize*hi)/lo;
   if(pts<1UL) pts=1UL;
   if(pts>256UL) pts=256UL;
   return pts<<16;
}

static ULONG Clampadv(ULONG pixw,ULONG maxadv)
{  if(pixw<1UL) pixw=1UL;
   if(pixw>maxadv) pixw=maxadv;
   return pixw;
}

static BOOL Isspaceglyph(ULONG c)
{  if(c==0x20 || c==0xa0) return TRUE;
   if(c>=0x2000 && c<=0x200b) return TRUE;
   return FALSE;
}

/* Next outline glyph: UTF-8 docs fold via shared awebutf8 (keymap-aware). */

/*------------------------------------------------------------------------*/
/* Metric stub / packed TextFont                                           */
/*------------------------------------------------------------------------*/

static void Setfontpixel(UBYTE *plane,ULONG modulo,ULONG planew,ULONG x,ULONG y)
{  UBYTE *row;
   if(x>=planew) return;
   row=plane+y*modulo+(x>>3);
   *row|=(UBYTE)(0x80U>>(x&7U));
}

static void Blitglyphtofont(UBYTE *plane,ULONG modulo,ULONG ysize,ULONG planew,
   struct GlyphMap *gm,ULONG xorigin,ULONG cellw,WORD baseline)
{  ULONG r,c;
   ULONG srcx,srcy;
   LONG dstx;
   ULONG dsty;
   UBYTE *srcrow;
   UBYTE bit;
   LONG cellend;
   if(!gm || !gm->glm_BitMap || !gm->glm_BlackWidth || !gm->glm_BlackHeight) return;
   if(cellw<1UL) return;
   cellend=(LONG)(xorigin+cellw);
   for(r=0;r<(ULONG)gm->glm_BlackHeight;r++)
   {  srcy=(ULONG)gm->glm_BlackTop+r;
      if(srcy>=(ULONG)gm->glm_BMRows) break;
      srcrow=gm->glm_BitMap+srcy*(ULONG)gm->glm_BMModulo;
      dsty=(ULONG)((LONG)baseline-(LONG)gm->glm_Y0+(LONG)gm->glm_BlackTop+(LONG)r);
      if(dsty>=ysize) continue;
      for(c=0;c<(ULONG)gm->glm_BlackWidth;c++)
      {  srcx=(ULONG)gm->glm_BlackLeft+c;
         if((srcx>>3)>=(ULONG)gm->glm_BMModulo) break;
         bit=srcrow[srcx>>3]&(UBYTE)(0x80U>>(srcx&7U));
         if(!bit) continue;
         dstx=(LONG)xorigin+(LONG)gm->glm_BlackLeft-(LONG)gm->glm_X0+(LONG)c;
         if(dstx<(LONG)xorigin || dstx>=cellend) continue;
         if(dstx<0) continue;
         Setfontpixel(plane,modulo,planew,(ULONG)dstx,dsty);
      }
   }
}

static struct TextFont *Makemetricstub(UBYTE *name,short ysize,struct BulletFontNode **outbn)
{  struct BulletFontNode *bn;
   struct TextFont *tf;
   ULONG i;
   ULONG modulo;
   ULONG datasize;
   ULONG adv;
   ULONG bitoff;
   WORD baseline;
   if(ysize<4) ysize=4;
   if(ysize>128) ysize=128;
   adv=(ULONG)ysize/2UL;
   if(adv<1UL) adv=1UL;
   baseline=(WORD)((ysize*3)/4);
   if(baseline<1) baseline=1;
   if(baseline>=ysize) baseline=(WORD)(ysize-1);
   modulo=2UL;
   datasize=modulo*(ULONG)ysize;
   bn=ALLOCSTRUCT(BulletFontNode,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(!bn) return NULL;
   bn->name=Dupstr(name,-1);
   bn->ysize=ysize;
   bn->refcount=1;
   bn->stubonly=TRUE;
   bn->rot_sin=0;
   bn->rot_cos=0x00010000L;
   bn->shear_sin=0;
   bn->shear_cos=0x00010000L;
   bn->set_factor=0x00010000L;
   bn->emb_x=0;
   bn->emb_y=0;
   bn->charloc=ALLOCTYPE(ULONG,BF_NCHARS,MEMF_PUBLIC|MEMF_CLEAR);
   bn->charspace=ALLOCTYPE(WORD,BF_NCHARS,MEMF_PUBLIC|MEMF_CLEAR);
   bn->charkern=ALLOCTYPE(WORD,BF_NCHARS,MEMF_PUBLIC|MEMF_CLEAR);
   bn->chardata=ALLOCTYPE(UBYTE,datasize,MEMF_CHIP|MEMF_PUBLIC|MEMF_CLEAR);
   bn->font=ALLOCSTRUCT(TextFont,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(!bn->name || !bn->charloc || !bn->charspace || !bn->charkern
   || !bn->chardata || !bn->font)
   {  Freenode(bn);
      return NULL;
   }
   bitoff=0;
   for(i=0;i<(ULONG)BF_NCHARS;i++)
   {  bn->charloc[i]=(bitoff<<16)|adv;
      bn->charspace[i]=(WORD)adv;
      bn->charkern[i]=0;
   }
   tf=bn->font;
   tf->tf_Message.mn_Node.ln_Name=bn->name;
   tf->tf_Message.mn_Node.ln_Type=NT_FONT;
   tf->tf_YSize=(UWORD)ysize;
   tf->tf_Style=FS_NORMAL;
   tf->tf_Flags=FPF_DESIGNED|FPF_PROPORTIONAL;
   tf->tf_XSize=(UWORD)adv;
   tf->tf_Baseline=(UWORD)baseline;
   tf->tf_LoChar=(UBYTE)BF_LOCHAR;
   tf->tf_HiChar=(UBYTE)BF_HICHAR;
   tf->tf_Modulo=(UWORD)modulo;
   tf->tf_CharData=bn->chardata;
   tf->tf_CharLoc=bn->charloc;
   tf->tf_CharSpace=bn->charspace;
   tf->tf_CharKern=bn->charkern;
   tf->tf_Accessors=1;
   *outbn=bn;
   return tf;
}

/* Open / refresh live GlyphEngine for an outline face. */
static BOOL EnsureLiveEngine(struct BulletFontNode *bn)
{  UBYTE path[256];
   UBYTE *enginename;
   struct TagItem tags[8];
   ULONG ysf;
   ULONG pts;
   if(!bn || !bullet_ok || !BulletBase) return FALSE;
   if(bn->ge) return TRUE;
   if(!bn->name || !Isoutlinefontfile(bn->name)) return FALSE;
   if(!Makeotagpath(bn->name,path,sizeof(path))) return FALSE;
   bn->otags=Allocotag(path);
   if(!bn->otags) return FALSE;
   bn->otagpath=Dupstr(path,-1);
   if(!bn->otagpath)
   {  FREE(bn->otags);
      bn->otags=NULL;
      return FALSE;
   }
   enginename=(UBYTE *)GetTagData(OT_Engine,NULL,bn->otags);
   if(!enginename || !STRIEQUAL(enginename,"bullet"))
   {  Freelive(bn);
      return FALSE;
   }
   bn->ge=OpenEngine();
   if(!bn->ge)
   {  Freelive(bn);
      return FALSE;
   }
   tags[0].ti_Tag=OT_OTagPath;
   tags[0].ti_Data=(ULONG)bn->otagpath;
   tags[1].ti_Tag=OT_OTagList;
   tags[1].ti_Data=(ULONG)bn->otags;
   tags[2].ti_Tag=TAG_END;
   if(SetInfoA(bn->ge,tags)!=OTERR_Success)
   {  Freelive(bn);
      return FALSE;
   }
   GetScreenDPI(&bn->xdpi,&bn->ydpi);
   ysf=GetTagData(OT_YSizeFactor,0x00010001UL,bn->otags);
   bn->pointfixed=Pixelstopointsfixed(bn->ysize,ysf);
   pts=bn->pointfixed>>16;
   bn->emh=(pts*bn->ydpi)/72UL;
   bn->emw=(pts*bn->xdpi)/72UL;
   if(bn->emw<1UL) bn->emw=1UL;
   if(bn->emh<1UL) bn->emh=1UL;
   tags[0].ti_Tag=OT_DeviceDPI;
   tags[0].ti_Data=(bn->xdpi<<16)|bn->ydpi;
   tags[1].ti_Tag=OT_PointHeight;
   tags[1].ti_Data=bn->pointfixed;
   tags[2].ti_Tag=TAG_END;
   if(SetInfoA(bn->ge,tags)!=OTERR_Success)
   {  Freelive(bn);
      return FALSE;
   }
   /* Refine stub advances from WidthList when possible. */
   if(bn->stubonly && bn->charspace)
   {  struct MinList *wl=NULL;
      struct GlyphWidthEntry *we;
      ULONG maxadv=(ULONG)bn->ysize;
      if(maxadv<8UL) maxadv=8UL;
      if(maxadv>96UL) maxadv=96UL;
      tags[0].ti_Tag=OT_GlyphCode;
      tags[0].ti_Data=BF_LOCHAR;
      tags[1].ti_Tag=OT_GlyphCode2;
      tags[1].ti_Data=BF_HICHAR;
      tags[2].ti_Tag=TAG_END;
      if(SetInfoA(bn->ge,tags)==OTERR_Success)
      {  tags[0].ti_Tag=OT_WidthList;
         tags[0].ti_Data=(ULONG)&wl;
         tags[1].ti_Tag=TAG_END;
         if(ObtainInfoA(bn->ge,tags)==OTERR_Success && wl)
         {  for(we=(struct GlyphWidthEntry *)wl->mlh_Head;
               we->gwe_Node.mln_Succ;
               we=(struct GlyphWidthEntry *)we->gwe_Node.mln_Succ)
            {  ULONG i,pixw;
               if(we->gwe_Code<(UWORD)BF_LOCHAR || we->gwe_Code>(UWORD)BF_HICHAR)
                  continue;
               i=(ULONG)(we->gwe_Code-BF_LOCHAR);
               pixw=Clampadv((ULONG)((we->gwe_Width*bn->emw)>>16),maxadv);
               bn->charspace[i]=(WORD)pixw;
               bn->charloc[i]=(bn->charloc[i]&0xFFFF0000UL)|pixw;
            }
            tags[0].ti_Tag=OT_WidthList;
            tags[0].ti_Data=(ULONG)wl;
            tags[1].ti_Tag=TAG_END;
            ReleaseInfoA(bn->ge,tags);
         }
      }
   }
   return TRUE;
}

static BOOL ApplyLiveStyle(struct BulletFontNode *bn,USHORT style)
{  struct TagItem tags[10];
   long i;
   if(!bn || !bn->ge) return FALSE;
   /* Skip engine round-trip + cache flush when nothing changed. */
   if(bn->style_live
      && bn->style_applied==style
      && bn->livestyle==style)
      return TRUE;
   /* Style / DPI / transform change invalidates cached rasters. */
   FlushGlyphCache(bn);
   bn->livestyle=style;
   bn->style_applied=style;
   /* bn->shear_* / emb_* / rot_* / set_factor are set by callers. */
   i=0;
   tags[i].ti_Tag=OT_DeviceDPI;
   tags[i].ti_Data=(bn->xdpi<<16)|bn->ydpi;
   i++;
   tags[i].ti_Tag=OT_PointHeight;
   tags[i].ti_Data=bn->pointfixed;
   i++;
   tags[i].ti_Tag=OT_SetFactor;
   tags[i].ti_Data=(ULONG)bn->set_factor;
   i++;
   /* Shear: sin then cos (NDK). */
   tags[i].ti_Tag=OT_ShearSin;
   tags[i].ti_Data=(ULONG)bn->shear_sin;
   i++;
   tags[i].ti_Tag=OT_ShearCos;
   tags[i].ti_Data=(ULONG)bn->shear_cos;
   i++;
   /* Rotate: sin then cos. */
   tags[i].ti_Tag=OT_RotateSin;
   tags[i].ti_Data=(ULONG)bn->rot_sin;
   i++;
   tags[i].ti_Tag=OT_RotateCos;
   tags[i].ti_Data=(ULONG)bn->rot_cos;
   i++;
   tags[i].ti_Tag=OT_EmboldenX;
   tags[i].ti_Data=(ULONG)bn->emb_x;
   i++;
   tags[i].ti_Tag=OT_EmboldenY;
   tags[i].ti_Data=(ULONG)bn->emb_y;
   i++;
   tags[i].ti_Tag=TAG_END;
   if(SetInfoA(bn->ge,tags)!=OTERR_Success)
   {  bn->style_live=FALSE;
      return FALSE;
   }
   bn->style_live=TRUE;
   PrefetchWidthMetrics(bn);
   return TRUE;
}

/* Measure and optionally draw using cached glyph rasters.
 * Latin-1 HTML (amiga-news.de iso-8859-15) must use one byte = one glyph.
 * UTF-8 mode is only for DOCCHARSET_UTF8 buffers. */
static ULONG LiveRun(struct BulletFontNode *bn,struct RastPort *rp,
   UBYTE *string,ULONG count,BOOL draw)
{  ULONG i;
   ULONG x;
   ULONG c,c2;
   ULONG adv1,adv2;
   LONG kern;
   LONG pix;
   struct BulletGlyphCache *slot;
   ULONG adv;
   WORD basex,basey;
   if(!bn || !bn->ge || !string || count==0UL) return 0UL;
   x=0;
   basex=0;
   basey=0;
   if(draw && rp)
   {  basex=rp->cp_x;
      basey=rp->cp_y;
   }
   for(i=0;i<count;)
   {  if(live_src_utf8)
      {  c=(ULONG)AwebUtf8NextAmigaGlyph(string+i,count-i,&adv1);
         if(adv1==0UL) break;
         c2=0UL;
         if(i+adv1<count)
            c2=(ULONG)AwebUtf8NextAmigaGlyph(string+i+adv1,count-(i+adv1),&adv2);
         else
            adv2=0UL;
      }
      else
      {  /* ISO-8859 / Amiga 8-bit: one source byte per glyph (no UTF-8 assemble). */
         c=(ULONG)AwebUnicodeToAmigaGlyph((ULONG)string[i]);
         adv1=1UL;
         adv2=0UL;
         c2=0UL;
         if(i+1UL<count)
            c2=(ULONG)AwebUnicodeToAmigaGlyph((ULONG)string[i+1UL]);
      }
      (void)adv2;
      i+=adv1;
      if(c==0UL && adv1>0UL) continue; /* soft hyphen etc. */
      slot=CacheGlyph(bn,c,draw);
      if(!slot) continue;

      kern=LookupKern(bn,c,c2);
      /* base_pix is (glm_width*emw)>>16; kern is design units like glm_width. */
      pix=(LONG)slot->base_pix-((((LONG)kern)*(LONG)bn->emw)>>16);
      if(pix<1 && c!=0UL) pix=1;
      adv=(ULONG)pix;

      if(draw && rp && slot->hasbits && slot->blit_src
         && slot->blackWidth && slot->blackHeight)
      {  BltTemplate(
            slot->blit_src,
            (ULONG)slot->blit_shift,
            (LONG)slot->bmModulo,
            rp,
            (LONG)basex+(LONG)x+(LONG)slot->blit_dx,
            (LONG)basey+(LONG)slot->blit_dy,
            (LONG)slot->blackWidth,
            (LONG)slot->blackHeight);
      }
      x+=adv;
   }
   if(draw && rp) rp->cp_x=(WORD)(basex+(WORD)x);
   return x;
}

/* Prefer the rp armed by BulletFontSetFont; else the outline font on rp->Font.
 * Measure uses mrp while paint uses the window rp — both must hit LiveRun, not
 * empty stub Text()/TextFit (that looked like string overrun garbage). */
static struct BulletFontNode *BnForRp(struct RastPort *rp)
{  struct BulletFontNode *bn;
   if(!rp) return NULL;
   if(live_rp==rp && live_bn && live_bn->ge) return live_bn;
   bn=Findbn(rp->Font);
   if(bn && bn->ge) return bn;
   return NULL;
}

/*------------------------------------------------------------------------*/
/* Packed outline fallback (when live engine unavailable)                  */
/*------------------------------------------------------------------------*/

static struct TextFont *Buildtextfont(struct GlyphEngine *ge,UBYTE *name,short ysize,
   ULONG ysf,ULONG spacewidth_dwu,ULONG isfixed,struct BulletFontNode **outbn)
{  struct BulletFontNode *bn;
   struct TextFont *tf;
   ULONG emw;
   ULONG pointfixed;
   ULONG pts;
   ULONG i;
   ULONG code;
   ULONG advances[BF_NCHARS];
   ULONG maxadv;
   WORD maxasc;
   ULONG totalw;
   ULONG modulo;
   ULONG datasize;
   ULONG bitoff;
   ULONG pixw;
   ULONG planew;
   struct GlyphMap *gm;
   struct MinList *wl;
   struct GlyphWidthEntry *we;
   WORD baseline;
   ULONG err;
   struct TagItem tags[4];

   if(ysize<4) ysize=4;
   if(ysize>128) ysize=128;
   maxadv=(ULONG)ysize;
   if(maxadv<8UL) maxadv=8UL;
   if(maxadv>96UL) maxadv=96UL;

   pointfixed=Pixelstopointsfixed(ysize,ysf);
   pts=pointfixed>>16;
   tags[0].ti_Tag=OT_DeviceDPI;
   tags[0].ti_Data=(BF_XDPI_DEF<<16)|BF_YDPI_DEF;
   tags[1].ti_Tag=OT_PointHeight;
   tags[1].ti_Data=pointfixed;
   tags[2].ti_Tag=TAG_END;
   if(SetInfoA(ge,tags)!=OTERR_Success) return NULL;

   emw=(pts*BF_XDPI_DEF)/72UL;
   if(emw<1UL) emw=1UL;

   for(i=0;i<(ULONG)BF_NCHARS;i++) advances[i]=emw/2UL;
   maxasc=0;
   totalw=0;

   tags[0].ti_Tag=OT_GlyphCode;
   tags[0].ti_Data=BF_LOCHAR;
   tags[1].ti_Tag=OT_GlyphCode2;
   tags[1].ti_Data=BF_HICHAR;
   tags[2].ti_Tag=TAG_END;
   wl=NULL;
   if(SetInfoA(ge,tags)==OTERR_Success)
   {  tags[0].ti_Tag=OT_WidthList;
      tags[0].ti_Data=(ULONG)&wl;
      tags[1].ti_Tag=TAG_END;
      if(ObtainInfoA(ge,tags)==OTERR_Success && wl)
      {  for(we=(struct GlyphWidthEntry *)wl->mlh_Head;
            we->gwe_Node.mln_Succ;
            we=(struct GlyphWidthEntry *)we->gwe_Node.mln_Succ)
         {  if(we->gwe_Code<(UWORD)BF_LOCHAR || we->gwe_Code>(UWORD)BF_HICHAR)
               continue;
            i=(ULONG)(we->gwe_Code-BF_LOCHAR);
            pixw=(ULONG)((we->gwe_Width*emw)>>16);
            advances[i]=Clampadv(pixw,maxadv);
         }
         tags[0].ti_Tag=OT_WidthList;
         tags[0].ti_Data=(ULONG)wl;
         tags[1].ti_Tag=TAG_END;
         ReleaseInfoA(ge,tags);
      }
   }

   {  static const ULONG samples[]={ 'H', 'A', 'g', 'y', 'M', 0 };
      ULONG s;
      for(s=0;samples[s];s++)
      {  gm=NULL;
         tags[0].ti_Tag=OT_GlyphCode;
         tags[0].ti_Data=samples[s];
         tags[1].ti_Tag=TAG_END;
         if(SetInfoA(ge,tags)!=OTERR_Success) continue;
         tags[0].ti_Tag=OT_GlyphMap;
         tags[0].ti_Data=(ULONG)&gm;
         tags[1].ti_Tag=TAG_END;
         err=ObtainInfoA(ge,tags);
         if(err==OTERR_Success && gm)
         {  if(gm->glm_Y0>maxasc) maxasc=gm->glm_Y0;
            tags[0].ti_Tag=OT_GlyphMap;
            tags[0].ti_Data=(ULONG)gm;
            tags[1].ti_Tag=TAG_END;
            ReleaseInfoA(ge,tags);
         }
      }
   }

   for(i=0;i<(ULONG)BF_NCHARS;i++) totalw+=advances[i];
   if(totalw<1UL) totalw=1UL;

   baseline=maxasc;
   if(baseline<1) baseline=(WORD)((ysize*3)/4);
   if(baseline>=ysize) baseline=(WORD)(ysize-1);

   modulo=((totalw+15UL)>>4)<<1;
   datasize=modulo*(ULONG)ysize;
   if(datasize==0UL || datasize>BF_MAX_CHIP) return NULL;

   planew=totalw;
   bn=ALLOCSTRUCT(BulletFontNode,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(!bn) return NULL;
   bn->name=Dupstr(name,-1);
   bn->ysize=ysize;
   bn->refcount=1;
   bn->stubonly=FALSE;
   bn->rot_sin=0;
   bn->rot_cos=0x00010000L;
   bn->shear_sin=0;
   bn->shear_cos=0x00010000L;
   bn->set_factor=0x00010000L;
   bn->charloc=ALLOCTYPE(ULONG,BF_NCHARS,MEMF_PUBLIC|MEMF_CLEAR);
   bn->charspace=ALLOCTYPE(WORD,BF_NCHARS,MEMF_PUBLIC|MEMF_CLEAR);
   bn->charkern=ALLOCTYPE(WORD,BF_NCHARS,MEMF_PUBLIC|MEMF_CLEAR);
   bn->chardata=ALLOCTYPE(UBYTE,datasize,MEMF_CHIP|MEMF_PUBLIC|MEMF_CLEAR);
   bn->font=ALLOCSTRUCT(TextFont,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(!bn->name || !bn->charloc || !bn->charspace || !bn->charkern
   || !bn->chardata || !bn->font)
   {  Freenode(bn);
      return NULL;
   }

   bitoff=0;
   for(i=0;i<(ULONG)BF_NCHARS;i++)
   {  code=BF_LOCHAR+i;
      pixw=advances[i];
      bn->charloc[i]=(bitoff<<16)|pixw;
      bn->charspace[i]=(WORD)pixw;
      bn->charkern[i]=0;
      gm=NULL;
      tags[0].ti_Tag=OT_GlyphCode;
      tags[0].ti_Data=code;
      tags[1].ti_Tag=TAG_END;
      if(SetInfoA(ge,tags)==OTERR_Success)
      {  tags[0].ti_Tag=OT_GlyphMap;
         tags[0].ti_Data=(ULONG)&gm;
         tags[1].ti_Tag=TAG_END;
         if(ObtainInfoA(ge,tags)==OTERR_Success && gm)
         {  Blitglyphtofont(bn->chardata,modulo,(ULONG)ysize,planew,gm,
               bitoff,pixw,baseline);
            tags[0].ti_Tag=OT_GlyphMap;
            tags[0].ti_Data=(ULONG)gm;
            tags[1].ti_Tag=TAG_END;
            ReleaseInfoA(ge,tags);
         }
      }
      bitoff+=pixw;
   }

   if(spacewidth_dwu)
   {  pixw=(ULONG)(((spacewidth_dwu*(ULONG)ysize)/250UL)*BF_XDPI_DEF)/2540UL;
      pixw=Clampadv(pixw,maxadv);
      i=(ULONG)(32-BF_LOCHAR);
      bn->charloc[i]=(bn->charloc[i]&0xFFFF0000UL)|pixw;
      bn->charspace[i]=(WORD)pixw;
   }

   tf=bn->font;
   tf->tf_Message.mn_Node.ln_Name=bn->name;
   tf->tf_Message.mn_Node.ln_Type=NT_FONT;
   tf->tf_YSize=(UWORD)ysize;
   tf->tf_Style=FS_NORMAL;
   tf->tf_Flags=FPF_DESIGNED;
   if(!isfixed) tf->tf_Flags|=FPF_PROPORTIONAL;
   tf->tf_XSize=(UWORD)(emw/2UL);
   if(tf->tf_XSize<1) tf->tf_XSize=1;
   tf->tf_Baseline=(UWORD)baseline;
   tf->tf_LoChar=(UBYTE)BF_LOCHAR;
   tf->tf_HiChar=(UBYTE)BF_HICHAR;
   tf->tf_Modulo=(UWORD)modulo;
   tf->tf_CharData=bn->chardata;
   tf->tf_CharLoc=bn->charloc;
   tf->tf_CharSpace=bn->charspace;
   tf->tf_CharKern=bn->charkern;
   tf->tf_Accessors=1;
   *outbn=bn;
   return tf;
}

static struct TextFont *Openoutline(UBYTE *fontname,short ysize)
{  UBYTE otagpath[256];
   UBYTE *enginename;
   struct TagItem *otags;
   struct GlyphEngine *ge;
   struct BulletFontNode *bn;
   struct TextFont *tf;
   ULONG ysf;
   ULONG space_dwu;
   ULONG isfixed;
   struct TagItem tags[4];

   if(!bullet_ok || !fontname || ysize<4) return NULL;
   if(!Isoutlinefontfile(fontname)) return NULL;
   if(!Makeotagpath(fontname,otagpath,sizeof(otagpath))) return NULL;
   otags=Allocotag(otagpath);
   if(!otags) return NULL;

   enginename=(UBYTE *)GetTagData(OT_Engine,NULL,otags);
   ge=NULL;
   tf=NULL;
   bn=NULL;

   if(enginename && STRIEQUAL(enginename,"bullet") && BulletBase)
      ge=OpenEngine();
   if(ge)
   {  tags[0].ti_Tag=OT_OTagPath;
      tags[0].ti_Data=(ULONG)otagpath;
      tags[1].ti_Tag=OT_OTagList;
      tags[1].ti_Data=(ULONG)otags;
      tags[2].ti_Tag=TAG_END;
      if(SetInfoA(ge,tags)==OTERR_Success)
      {  ysf=GetTagData(OT_YSizeFactor,0x00010001UL,otags);
         space_dwu=GetTagData(OT_SpaceWidth,0,otags);
         isfixed=GetTagData(OT_IsFixed,FALSE,otags);
         tf=Buildtextfont(ge,fontname,ysize,ysf,space_dwu,isfixed,&bn);
      }
      /* Keep engine on the node for live kerning draws. */
      if(tf && bn)
      {  bn->ge=ge;
         bn->otags=otags;
         bn->otagpath=Dupstr(otagpath,-1);
         otags=NULL;
         ge=NULL;
         GetScreenDPI(&bn->xdpi,&bn->ydpi);
         bn->pointfixed=Pixelstopointsfixed(bn->ysize,
            GetTagData(OT_YSizeFactor,0x00010001UL,bn->otags));
         bn->emw=((bn->pointfixed>>16)*bn->xdpi)/72UL;
         bn->emh=((bn->pointfixed>>16)*bn->ydpi)/72UL;
         if(bn->emw<1UL) bn->emw=1UL;
         if(bn->emh<1UL) bn->emh=1UL;
         if(!bn->otagpath)
         {  /* Live optional; packed CharData still works. */
            if(bn->ge) { CloseEngine(bn->ge); bn->ge=NULL; }
            if(bn->otags) { FREE(bn->otags); bn->otags=NULL; }
         }
      }
      if(ge) CloseEngine(ge);
   }
   if(otags) FREE(otags);
   if(tf && bn)
   {  ADDTAIL(&bfonts,bn);
   }
   else if(bn)
   {  Freenode(bn);
      tf=NULL;
   }
   return tf;
}

static struct TextFont *Cachelookup(UBYTE *use,short ysize)
{  struct BulletFontNode *bn;
   for(bn=bfonts.first;bn->next;bn=bn->next)
   {  if(bn->ysize==ysize && bn->name && STRIEQUAL(bn->name,use))
      {  bn->refcount++;
         if(bn->font) bn->font->tf_Accessors++;
         return bn->font;
      }
   }
   return NULL;
}

static struct TextFont *Installstub(UBYTE *use,short ysize)
{  struct BulletFontNode *bn;
   struct TextFont *tf;
   bn=NULL;
   tf=Makemetricstub(use,ysize,&bn);
   if(tf && bn)
   {  ADDTAIL(&bfonts,bn);
      /* Prefer TTEngine when present: defer live GlyphEngine until
       * BulletFontSetFont (TT miss). Avoids opening bullet + WidthList
       * for every face TT will bind. */
      if(bullet_ok && Isoutlinefontfile(use) && !TTEngineAvailable())
         (void)EnsureLiveEngine(bn);
      return tf;
   }
   if(bn) Freenode(bn);
   return NULL;
}

static struct TextFont *Openromfallback(UBYTE *fontname,short ysize)
{  struct TextAttr ta={0};
   struct TextFont *tf;
   ta.ta_Name=fontname?(STRPTR)fontname:(STRPTR)"topaz.font";
   ta.ta_YSize=(UWORD)((ysize>0)?ysize:8);
   tf=OpenFont(&ta);
   if(!tf)
   {  ta.ta_Name=(STRPTR)"topaz.font";
      ta.ta_YSize=8;
      tf=OpenFont(&ta);
   }
   return tf;
}

/*------------------------------------------------------------------------*/

BOOL InitBulletFont(void)
{  struct GlyphEngine *ge;
   if(bullet_inited) return TRUE;
   NEWLIST(&bfonts);
   NEWLIST(&formdiskfonts);
   bullet_inited=TRUE;
   bullet_ok=FALSE;
   live_rp=NULL;
   live_bn=NULL;
   live_src_utf8=FALSE;
   BulletBase=OpenLibrary("bullet.library",0);
   if(!BulletBase) return TRUE;
   ge=OpenEngine();
   if(!ge)
   {  CloseLibrary(BulletBase);
      BulletBase=NULL;
      return TRUE;
   }
   CloseEngine(ge);
   bullet_ok=TRUE;
   return TRUE;
}

static void Freeformdiskfonts(void)
{  struct FormDiskNode *n;
   while((n=REMHEAD(&formdiskfonts))!=NULL)
   {  if(n->font) CloseFont(n->font);
      if(n->name) FREE(n->name);
      FREE(n);
   }
}

void FreeBulletFont(void)
{  struct BulletFontNode *bn;
   live_rp=NULL;
   live_bn=NULL;
   live_src_utf8=FALSE;
   Freeformdiskfonts();
   while((bn=REMHEAD(&bfonts))!=NULL) Freenode(bn);
   if(BulletBase)
   {  CloseLibrary(BulletBase);
      BulletBase=NULL;
   }
   bullet_ok=FALSE;
   bullet_inited=FALSE;
}

BOOL BulletFontAvailable(void)
{  return bullet_ok;
}

struct TextFont *BulletFontOpen(UBYTE *fontname,short ysize)
{  struct TextFont *tf;
   UBYTE namebuf[256];
   UBYTE *p;
   UBYTE *use;
   if(!fontname || ysize<1) return NULL;
   if(!bullet_inited) InitBulletFont();
   if(!bullet_inited) return NULL;
   ysize=OpticalYsize(ysize);
   strncpy(namebuf,fontname,sizeof(namebuf)-1);
   namebuf[sizeof(namebuf)-1]='\0';
   p=namebuf;
   while(*p) p++;
   if(p>namebuf && (ULONG)(p-namebuf)>=5UL)
   {  if(!STRIEQUAL(p-5,".font") && (ULONG)(p-namebuf)+5UL<sizeof(namebuf))
         strcpy(p,".font");
   }
   else if((ULONG)(p-namebuf)+5UL<sizeof(namebuf))
      strcpy(p,".font");
   use=namebuf;
   tf=Cachelookup(use,ysize);
   if(tf) return tf;

   /* TTEngine present: metric stub (+ live engine if outline, for TT miss). */
   if(TTEngineAvailable())
   {  tf=Installstub(use,ysize);
      if(tf) return tf;
   }

   /* No TT: stub + live GlyphEngine (kerning blits), else packed CharData. */
   if(bullet_ok && Isoutlinefontfile(use))
   {  tf=Installstub(use,ysize);
      if(tf)
      {  struct BulletFontNode *bn=Findbn(tf);
         if(bn && bn->ge) return tf;
      }
      /* Live failed: packed fallback still gives visible Text(). */
      if(tf)
      {  BulletFontClose(tf);
         tf=NULL;
      }
      tf=Openoutline(use,ysize);
      if(tf) return tf;
   }

   {  UBYTE *base;
      UBYTE *q;
      base=use;
      q=use;
      while(*q)
      {  if(*q==':' || *q=='/') base=q+1;
         q++;
      }
      if(STRIEQUAL(base,"topaz.font") || STRIEQUAL(base,"topaz"))
         return Openromfallback(use,ysize);
   }
   return NULL;
}

BOOL BulletFontIsOurs(struct TextFont *tf)
{  return (BOOL)(Findbn(tf)!=NULL);
}

struct TextFont *BulletFontDup(struct TextFont *tf)
{  struct BulletFontNode *bn=Findbn(tf);
   if(!bn) return NULL;
   bn->refcount++;
   tf->tf_Accessors++;
   return tf;
}

BOOL BulletFontClose(struct TextFont *tf)
{  struct BulletFontNode *bn=Findbn(tf);
   if(!bn) return FALSE;
   if(bn->refcount) bn->refcount--;
   if(tf->tf_Accessors) tf->tf_Accessors--;
   if(bn->refcount==0)
   {  REMOVE(bn);
      Freenode(bn);
   }
   return TRUE;
}

void AwebCloseBrowserFont(struct TextFont *tf)
{  if(!tf) return;
   if(BulletFontClose(tf)) return;
   CloseFont(tf);
}

/*------------------------------------------------------------------------*/
/* Live render API                                                         */
/*------------------------------------------------------------------------*/

void BulletFontClearLive(struct RastPort *rp)
{  if(rp && live_rp==rp)
   {  live_rp=NULL;
      live_bn=NULL;
   }
}

void BulletFontSetSourceUtf8(BOOL utf8)
{  live_src_utf8=utf8?TRUE:FALSE;
}

void BulletFontSetFont(struct RastPort *rp,struct TextFont *tf,USHORT style)
{  struct BulletFontNode *bn;
   LONG want_shear_sin,want_shear_cos,want_emb_x,want_emb_y;
   if(!rp || !tf || !bullet_ok)
   {  BulletFontClearLive(rp);
      return;
   }
   bn=Findbn(tf);
   if(!bn)
   {  BulletFontClearLive(rp);
      return;
   }
   if(!bn->ge && !EnsureLiveEngine(bn))
   {  BulletFontClearLive(rp);
      return;
   }
   SetFont(rp,tf);
   if(style&FSF_ITALIC)
   {  want_shear_sin=BF_SHEAR_SIN;
      want_shear_cos=BF_SHEAR_COS;
   }
   else
   {  want_shear_sin=0;
      want_shear_cos=0x00010000L;
   }
   if(style&FSF_BOLD)
   {  want_emb_x=BF_EMBOLDEN;
      want_emb_y=BF_EMBOLDEN;
   }
   else
   {  want_emb_x=0;
      want_emb_y=0;
   }
   if(bn->shear_sin!=want_shear_sin || bn->shear_cos!=want_shear_cos
      || bn->emb_x!=want_emb_x || bn->emb_y!=want_emb_y)
      bn->style_live=FALSE;
   bn->shear_sin=want_shear_sin;
   bn->shear_cos=want_shear_cos;
   bn->emb_x=want_emb_x;
   bn->emb_y=want_emb_y;
   if(!ApplyLiveStyle(bn,style))
   {  BulletFontClearLive(rp);
      return;
   }
   live_rp=rp;
   live_bn=bn;
}

BOOL BulletFontIsLiveActive(struct RastPort *rp)
{  return (BOOL)(BnForRp(rp)!=NULL);
}

void BulletFontText(struct RastPort *rp,UBYTE *string,ULONG count)
{  struct BulletFontNode *bn;
   bn=BnForRp(rp);
   if(!bn || !string || count==0UL) return;
   (void)LiveRun(bn,rp,string,count,TRUE);
}

ULONG BulletFontTextLength(struct RastPort *rp,UBYTE *string,ULONG count)
{  struct BulletFontNode *bn;
   bn=BnForRp(rp);
   if(!bn || !string || count==0UL) return 0UL;
   return LiveRun(bn,NULL,string,count,FALSE);
}

void BulletFontTextExtent(struct RastPort *rp,UBYTE *string,WORD count,
   struct TextExtent *te)
{  ULONG w;
   struct BulletFontNode *bn;
   if(!te) return;
   memset(te,0,sizeof(*te));
   bn=BnForRp(rp);
   if(!bn || !string || count<=0) return;
   w=LiveRun(bn,NULL,string,(ULONG)count,FALSE);
   te->te_Width=(UWORD)w;
   te->te_Height=bn->font?(UWORD)bn->font->tf_YSize:0;
   te->te_Extent.MinX=0;
   te->te_Extent.MinY=bn->font?-(WORD)bn->font->tf_Baseline:0;
   te->te_Extent.MaxX=(WORD)w-1;
   te->te_Extent.MaxY=bn->font
      ?(WORD)(bn->font->tf_YSize-(WORD)bn->font->tf_Baseline-1):0;
}

ULONG BulletFontTextFit(struct RastPort *rp,UBYTE *string,UWORD count,UWORD cwidth)
{  ULONG i;
   ULONG w;
   ULONG step;
   struct BulletFontNode *bn;
   bn=BnForRp(rp);
   if(!bn || !string || count==0) return 0UL;
   /* Single left-to-right pass (was O(n^2) LiveRun prefixes). */
   w=0;
   for(i=0;i<(ULONG)count;i++)
   {  step=LiveRun(bn,NULL,string+i,1UL,FALSE);
      if(w+step>(ULONG)cwidth) return i;
      w+=step;
   }
   return (ULONG)count;
}

/* Form controls use graphics Text() on Intuition rastports. Bullet HTML
 * stubs have empty CharData — OpenDiskFont (or topaz) so fields show text. */
struct TextFont *AwebFormDiskFont(struct TextFont *prefer)
{  struct FormDiskNode *n;
   struct TextAttr ta={0};
   struct TextFont *tf;
   UBYTE *name;
   short ysize;
   UBYTE namebuf[256];
   if(!bullet_inited) InitBulletFont();
   if(prefer && !BulletFontIsOurs(prefer))
      return prefer;
   ysize=8;
   name=(UBYTE *)"topaz.font";
   if(prefer)
   {  if(prefer->tf_YSize>0) ysize=(short)prefer->tf_YSize;
      if(prefer->tf_Message.mn_Node.ln_Name
         && prefer->tf_Message.mn_Node.ln_Name[0])
         name=(UBYTE *)prefer->tf_Message.mn_Node.ln_Name;
   }
   strncpy((char *)namebuf,(char *)name,sizeof(namebuf)-1);
   namebuf[sizeof(namebuf)-1]='\0';
   for(n=formdiskfonts.first;n->next;n=n->next)
   {  if(n->ysize==ysize && n->name && STRIEQUAL(n->name,namebuf))
         return n->font;
   }
   ta.ta_Name=(STRPTR)namebuf;
   ta.ta_YSize=(UWORD)ysize;
   tf=OpenDiskFont(&ta);
   if(!tf)
   {  ta.ta_Name=(STRPTR)"topaz.font";
      ta.ta_YSize=(UWORD)((ysize>0)?ysize:8);
      tf=OpenDiskFont(&ta);
   }
   if(!tf)
   {  ta.ta_Name=(STRPTR)"topaz.font";
      ta.ta_YSize=8;
      tf=OpenFont(&ta);
   }
   if(!tf) return prefer;
   n=ALLOCSTRUCT(FormDiskNode,1,MEMF_PUBLIC|MEMF_CLEAR);
   if(!n)
   {  CloseFont(tf);
      return prefer;
   }
   n->name=Dupstr(namebuf,-1);
   n->ysize=ysize;
   n->font=tf;
   if(!n->name)
   {  CloseFont(tf);
      FREE(n);
      return prefer;
   }
   ADDTAIL(&formdiskfonts,n);
   return tf;
}

BOOL BulletFontSetRotate(LONG sinFixed,LONG cosFixed)
{  if(!live_bn || !live_bn->ge) return FALSE;
   live_bn->rot_sin=sinFixed;
   live_bn->rot_cos=cosFixed;
   live_bn->style_live=FALSE;
   return ApplyLiveStyle(live_bn,live_bn->livestyle);
}

BOOL BulletFontSetShear(LONG sinFixed,LONG cosFixed)
{  if(!live_bn || !live_bn->ge) return FALSE;
   live_bn->shear_sin=sinFixed;
   live_bn->shear_cos=cosFixed;
   live_bn->style_live=FALSE;
   return ApplyLiveStyle(live_bn,live_bn->livestyle);
}

BOOL BulletFontSetSetFactor(LONG factorFixed)
{  if(!live_bn || !live_bn->ge) return FALSE;
   if(factorFixed==0) factorFixed=0x00010000L;
   live_bn->set_factor=factorFixed;
   live_bn->style_live=FALSE;
   return ApplyLiveStyle(live_bn,live_bn->livestyle);
}

BOOL BulletFontSetEmbolden(LONG xFixed,LONG yFixed)
{  if(!live_bn || !live_bn->ge) return FALSE;
   live_bn->emb_x=xFixed;
   live_bn->emb_y=yFixed;
   live_bn->style_live=FALSE;
   return ApplyLiveStyle(live_bn,live_bn->livestyle);
}

#else /* !AWEB4 */

BOOL InitBulletFont(void){ return FALSE; }
void FreeBulletFont(void){}
BOOL BulletFontAvailable(void){ return FALSE; }
struct TextFont *BulletFontOpen(UBYTE *fontname,short ysize)
{  (void)fontname;(void)ysize;return NULL; }
BOOL BulletFontIsOurs(struct TextFont *tf){ (void)tf;return FALSE; }
struct TextFont *BulletFontDup(struct TextFont *tf){ (void)tf;return NULL; }
BOOL BulletFontClose(struct TextFont *tf){ (void)tf;return FALSE; }
void AwebCloseBrowserFont(struct TextFont *tf)
{  if(tf) CloseFont(tf); }
void BulletFontSetFont(struct RastPort *rp,struct TextFont *tf,USHORT style)
{  (void)rp;(void)tf;(void)style; }
void BulletFontSetSourceUtf8(BOOL utf8){ (void)utf8; }
BOOL BulletFontIsLiveActive(struct RastPort *rp){ (void)rp;return FALSE; }
void BulletFontClearLive(struct RastPort *rp){ (void)rp; }
void BulletFontText(struct RastPort *rp,UBYTE *string,ULONG count)
{  (void)rp;(void)string;(void)count; }
ULONG BulletFontTextLength(struct RastPort *rp,UBYTE *string,ULONG count)
{  (void)rp;(void)string;(void)count;return 0UL; }
void BulletFontTextExtent(struct RastPort *rp,UBYTE *string,WORD count,
   struct TextExtent *te)
{  (void)rp;(void)string;(void)count;(void)te; }
ULONG BulletFontTextFit(struct RastPort *rp,UBYTE *string,UWORD count,UWORD cwidth)
{  (void)rp;(void)string;(void)count;(void)cwidth;return 0UL; }
struct TextFont *AwebFormDiskFont(struct TextFont *prefer)
{  return prefer; }
BOOL BulletFontSetRotate(LONG s,LONG c){ (void)s;(void)c;return FALSE; }
BOOL BulletFontSetShear(LONG s,LONG c){ (void)s;(void)c;return FALSE; }
BOOL BulletFontSetSetFactor(LONG f){ (void)f;return FALSE; }
BOOL BulletFontSetEmbolden(LONG x,LONG y){ (void)x;(void)y;return FALSE; }

#endif /* AWEB4 */
