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

/* svgsource.c - AWeb SVG plugin sourcedriver.
 *
 * Architecture
 * ------------
 *   - One subtask per SVG source object.  The main task feeds it data
 *     blocks (AOM_SRCUPDATE) and sets an EOF flag on the source.  The
 *     subtask waits for EOF, then concatenates all blocks into a
 *     single buffer and parses the whole thing into a DOM tree using
 *     xmlparse.c.
 *   - All DOM nodes plus all transient renderer state allocate from a
 *     single exec-pool, freed in one shot when the subtask exits.
 *   - The renderer walks the DOM recursively, carrying a style frame
 *     (fill, stroke, stroke-width, current 2x3 transform matrix in
 *     16.16 fixed point).  Children inherit and may override.
 *   - Drawing is implemented in-house against graphics.library
 *     primitives (RectFill, Move/Draw, WritePixel).  Filled polygons
 *     use a scanline algorithm with the even-odd rule; ellipses use
 *     a midpoint rasteriser.  This avoids the historical surprises
 *     of vrastport.lib's signed 64-bit coordinate scaling and gives
 *     us deterministic, easily-debugged geometry.
 *   - Curves and arcs in <path d="..."> are flattened to line
 *     segments in code before being handed to the polygon helpers.
 *   - All allocated screen pens are tracked in the Svgsource and
 *     released in Disposesource so we are good citizens on shared
 *     colormaps. */

#include "pluginlib.h"
#include "awebsvg.h"
#include "xmlparse.h"
#include "ezlists.h"

#include <libraries/awebplugin.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/intuitionbase.h>
#include <proto/awebplugin.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <libraries/Picasso96.h>
#include <proto/Picasso96.h>

#include <string.h>
#include <math.h>

extern struct Library *P96Base;

/* Forward decls (provided by awebplugin.library) */
extern struct Taskmsg *Gettaskmsg(void);
extern void Replytaskmsg(struct Taskmsg *msg);
extern BOOL Checktaskbreak(void);
extern ULONG Waittask(ULONG signals);
extern long Updatetaskattrs(ULONG tag,...);

/* Workaround for missing AOSDV_Displayed in older awebplugin.library. */
#define AOSRC_Displayed    (AOSRC_Dummy+2)

/* Limits */
#define MAX_BITMAP_DIM     2048L
#define DEFAULT_DIM        100L
/* Maximum number of vertices we will accumulate for a single subpath
 * before silently truncating.  Inkscape-generated maps in the wild can
 * easily reach a few thousand sampled bezier vertices on a single big
 * boundary path - if we cap too low, the subpath wraps back to its
 * start vertex prematurely and the user sees a shortcut line cutting
 * across the shape.  Keep this <=16384 because the in-struct capacity
 * counter is a WORD and capacity*2 must not overflow. */
#define POLY_MAX_VERTS     8192
#define BEZIER_SUBDIV      16

#define DECOF_P96MAP       0x0004
#define DECOF_P96DEEP      0x0008

/*--------------------------------------------------------------------*/
/* Plugin data structures                                             */
/*--------------------------------------------------------------------*/

struct Datablock
{  NODE(Datablock);
   UBYTE *data;
   long length;
};

struct Svgsource
{  struct Sourcedriver sourcedriver;
   struct Aobject *source;
   struct Aobject *task;
   LIST(Datablock) data;
   long width,height;             /* Bitmap dimensions (raster pixels)   */
   struct BitMap *bitmap;
   UBYTE *mask;
   long memory;
   struct SignalSemaphore sema;
   USHORT flags;
   /* Pen tracking - released on dispose */
   struct ColorMap *colormap;
   short allocated[256];
   struct BitMap *friendbitmap;
};

#define SVGSF_EOF          0x0001
#define SVGSF_DISPLAYED    0x0002
#define SVGSF_MEMORY       0x0004
#define SVGSF_IMAGEREADY   0x0010

/* Matrix is 2x3 affine in 16.16 fixed point:
 *    | a c e |
 *    | b d f |
 *    | 0 0 1 |
 * (x,y) maps to (a*x + c*y + e, b*x + d*y + f).
 */
struct Matrix
{  LONG a,b,c,d,e,f;
};

/* Local raster point type.  vrastport.h used to provide Point32, but
 * the renderer no longer links vrastport; keep this small geometry
 * type private to svgsource.c. */
struct Point32
{  LONG x;
   LONG y;
};

/* Render state inherited along the element tree. */
struct Renderstate
{  struct Matrix M;            /* Current transform: SVG -> raster pixels */
   UBYTE fillpen;
   UBYTE strokepen;
   UBYTE fillvalid;            /* TRUE means fill enabled */
   UBYTE strokevalid;          /* TRUE means stroke enabled */
   UBYTE visible;              /* FALSE means display:none in effect */
   UBYTE pad0;
   LONG strokewidth;           /* Stroke width in 16.16 SVG units */
   ULONG fillrgb;              /* 0xRRGGBB for Picasso96 deep fills */
   ULONG strokergb;            /* 0xRRGGBB for Picasso96 deep strokes */
};

/* Hash entry for the id->node map.  Pool-allocated, so it lives as
 * long as the decoder's pool. */
struct Iddef
{  struct Iddef *next;
   UBYTE *id;                  /* pointer into the in-place parse buffer */
   struct XmlNode *node;
};
#define IDTABLE_SIZE 64
#define USE_MAX_DEPTH 16

/* Per-decoder colour cache.  Keeps repeated palette lookups fast and,
 * critically, avoids handing out stale pen numbers across decodes
 * because the cache lives on the stack with the decoder. */
struct Pencache
{  ULONG rgb;
   UBYTE pen;
   UBYTE valid;
};
#define PENCACHESIZE 16

/* The decoder context - owned by the subtask. */
struct Decoder
{  struct Svgsource *source;
   struct Screen *screen;
   struct RastPort rp;
   struct BitMap *bitmap;
   long bmw, bmh;
   APTR pool;
   UBYTE *buffer;
   long buflen;
   USHORT flags;
   struct Pencache cache[PENCACHESIZE];
   UBYTE cachenext;
   USHORT decflags;
   struct Iddef *idtable[IDTABLE_SIZE];
   LONG usedepth;              /* recursion guard for <use> elements */
   UBYTE *chunky;              /* R8G8B8 row scratch for p96WritePixelArray */
   long chunkybpr;
   struct RenderInfo ri;
};

#define DECOF_STOP         0x0001
#define DECOF_EOF          0x0002

/*--------------------------------------------------------------------*/
/* Optional debug                                                     */
/*--------------------------------------------------------------------*/

#ifdef DEBUG_PLUGINS
#define SVGLOG(args) Aprintf args
#else
#define SVGLOG(args)
#endif

/*--------------------------------------------------------------------*/
/* Forward decls                                                      */
/*--------------------------------------------------------------------*/

static void Parsertask(void *userdata);
static void Renderelement(struct Decoder *dec, struct XmlNode *node, struct Renderstate *parent);

/*--------------------------------------------------------------------*/
/* Number parsing                                                     */
/*--------------------------------------------------------------------*/

/* Parse a single SVG number starting at *pp.  Skips leading
 * whitespace and a single optional comma.  Returns the value in
 * 16.16 fixed point and advances *pp.  Sets *ok to TRUE on success,
 * FALSE if nothing was parsed.
 *
 * IMPORTANT: this routine deliberately does NOT consume any trailing
 * letters.  Path data (`m0 0h77z`), transform lists (`translate(1 2)`),
 * polygon points etc. all interleave numbers with command/grouping
 * letters that we must hand back to the caller.  Callers that accept
 * CSS-style unit suffixes (`100px`, `2em`, `50%`) call Skipunits()
 * after Parsefixed() to swallow them. */
static LONG Parsefixed(UBYTE **pp, UBYTE *end, BOOL *ok)
{  UBYTE *p=*pp;
   LONG sign=1;
   LONG ipart=0;
   LONG fpart=0;
   LONG fscale=1;
   BOOL anydigits=FALSE;
   BOOL hasdot=FALSE;
   LONG result;

   while(p<end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==',')) p++;
   if(p<end && (*p=='+' || *p=='-'))
   {  if(*p=='-') sign=-1;
      p++;
   }
   while(p<end && *p>='0' && *p<='9')
   {  ipart=ipart*10+(*p-'0');
      anydigits=TRUE;
      p++;
   }
   if(p<end && *p=='.')
   {  hasdot=TRUE;
      p++;
      while(p<end && *p>='0' && *p<='9' && fscale<1000000)
      {  fpart=fpart*10+(*p-'0');
         fscale*=10;
         anydigits=TRUE;
         p++;
      }
      /* Discard remaining digits beyond our precision. */
      while(p<end && *p>='0' && *p<='9') p++;
   }
   /* Optional exponent. */
   if(p<end && (*p=='e' || *p=='E'))
   {  LONG esign=1;
      LONG eval=0;
      UBYTE *save=p;
      p++;
      if(p<end && (*p=='+' || *p=='-'))
      {  if(*p=='-') esign=-1;
         p++;
      }
      if(p<end && *p>='0' && *p<='9')
      {  while(p<end && *p>='0' && *p<='9')
         {  eval=eval*10+(*p-'0');
            p++;
         }
         /* Apply exponent to ipart/fpart.  Very rough but adequate. */
         eval*=esign;
         while(eval>0) { ipart*=10; fpart*=10; eval--; }
         while(eval<0)
         {  fpart+=ipart*fscale;
            ipart=0;
            fscale*=10;
            eval++;
         }
      }
      else
      {  p=save;
      }
   }

   if(!anydigits)
   {  if(ok) *ok=FALSE;
      return 0;
   }
   /* result = sign * (ipart + fpart/fscale) in 16.16 */
   result=ipart<<16;
   if(hasdot && fscale>1)
   {  result+=(fpart<<16)/fscale;
   }
   if(sign<0) result=-result;
   *pp=p;
   if(ok) *ok=TRUE;
   return result;
}

/* Skip optional CSS-style unit suffix (px, pt, em, %, ...) sitting at
 * *pp.  Used by callers that accept attribute-style dimensions, NOT by
 * the path/transform/points parsers which need to keep command and
 * separator letters intact. */
static void Skipunits(UBYTE **pp, UBYTE *end)
{  UBYTE *p=*pp;
   while(p<end && ((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='%')) p++;
   *pp=p;
}

/* Parse a single integer value (used for width/height attrs etc).  
 * Returns 0 if no digits were found. */
static LONG Parselong(UBYTE *s)
{  UBYTE *p=s;
   UBYTE *end;
   BOOL ok;
   LONG v;
   if(!s) return 0;
   end=s;
   while(*end) end++;
   v=Parsefixed(&p,end,&ok);
   if(!ok) return 0;
   Skipunits(&p,end);
   return v>>16;
}

/*--------------------------------------------------------------------*/
/* Colour parsing                                                     */
/*--------------------------------------------------------------------*/

struct Namedcolor
{  const char *name;
   ULONG rgb;
};

/* The 17 CSS 2.1 named colours plus a few extras commonly used in
 * SVG icons.  Stored as 0xRRGGBB. */
static const struct Namedcolor named_colors[]=
{  {"aqua",    0x00ffffUL},
   {"black",   0x000000UL},
   {"blue",    0x0000ffUL},
   {"brown",   0xa52a2aUL},
   {"cyan",    0x00ffffUL},
   {"fuchsia", 0xff00ffUL},
   {"gray",    0x808080UL},
   {"grey",    0x808080UL},
   {"green",   0x008000UL},
   {"lime",    0x00ff00UL},
   {"magenta", 0xff00ffUL},
   {"maroon",  0x800000UL},
   {"navy",    0x000080UL},
   {"olive",   0x808000UL},
   {"orange",  0xffa500UL},
   {"pink",    0xffc0cbUL},
   {"purple",  0x800080UL},
   {"red",     0xff0000UL},
   {"silver",  0xc0c0c0UL},
   {"teal",    0x008080UL},
   {"white",   0xffffffUL},
   {"yellow",  0xffff00UL},
   {NULL,      0UL}
};

static int hexdigit(UBYTE c)
{  if(c>='0'&&c<='9') return c-'0';
   if(c>='a'&&c<='f') return c-'a'+10;
   if(c>='A'&&c<='F') return c-'A'+10;
   return -1;
}

static int strieq(const UBYTE *a, const char *b)
{  while(*a && *b)
   {  UBYTE ca=*a;
      UBYTE cb=(UBYTE)*b;
      if(ca>='A'&&ca<='Z') ca=ca-'A'+'a';
      if(cb>='A'&&cb<='Z') cb=cb-'A'+'a';
      if(ca!=cb) return 0;
      a++; b++;
   }
   return (*a==0 && *b==0);
}

/* Parse an SVG colour spec.  Sets *rgb to 0xRRGGBB and returns TRUE.
 * Recognises "none" (returns FALSE, no fill/stroke), hex (#fff and
 * #ffffff), rgb(r,g,b) with byte or percent components, and the
 * named-colour table.  Returns FALSE for "currentColor" (treated as
 * black for now) - the renderer can override. */
static BOOL Parsecolor(const UBYTE *str, ULONG *rgb, BOOL *enabled)
{  const UBYTE *p=str;
   if(!p) { *enabled=FALSE; return FALSE; }
   while(*p==' '||*p=='\t') p++;
   if(*p==0) { *enabled=FALSE; return FALSE; }
   if(strieq(p,"none") || strieq(p,"transparent"))
   {  *enabled=FALSE;
      *rgb=0;
      return TRUE;
   }
   *enabled=TRUE;
   if(*p=='#')
   {  ULONG v=0;
      int len=0;
      int i;
      p++;
      while(p[len] && hexdigit(p[len])>=0) len++;
      if(len==3)
      {  for(i=0;i<3;i++)
         {  int d=hexdigit(p[i]);
            v=(v<<8)|((d<<4)|d);
         }
         *rgb=v;
         return TRUE;
      }
      if(len>=6)
      {  for(i=0;i<6;i++) v=(v<<4)|(ULONG)hexdigit(p[i]);
         *rgb=v;
         return TRUE;
      }
      *enabled=FALSE;
      return FALSE;
   }
   if((p[0]=='r' || p[0]=='R')
   && (p[1]=='g' || p[1]=='G')
   && (p[2]=='b' || p[2]=='B'))
   {  UBYTE *q=(UBYTE *)p+3;
      LONG c[3];
      LONG i;
      BOOL ispct;
      BOOL ok;
      LONG v;
      UBYTE *qend;
      double dv;
      while(*q==' '||*q=='\t') q++;
      if(*q!='(') { *enabled=FALSE; return FALSE; }
      q++;
      qend=q;
      while(*qend) qend++;
      for(i=0;i<3;i++)
      {  v=Parsefixed(&q,qend,&ok);
         if(!ok) { *enabled=FALSE; return FALSE; }
         ispct=FALSE;
         while(*q==' '||*q=='\t') q++;
         if(*q=='%') { ispct=TRUE; q++; }
         while(*q==' '||*q=='\t'||*q==',') q++;
         /* Convert via double to avoid 32-bit overflow for percents
          * outside 0..100% and for fractional component inputs. */
         dv=(double)v/65536.0;
         if(ispct) dv=dv*255.0/100.0;
         if(dv<0.0) dv=0.0;
         if(dv>255.0) dv=255.0;
         c[i]=(LONG)dv;
      }
      *rgb=((ULONG)c[0]<<16)|((ULONG)c[1]<<8)|((ULONG)c[2]);
      return TRUE;
   }
   /* Named colour. */
   {  const struct Namedcolor *nc;
      for(nc=named_colors; nc->name; nc++)
      {  if(strieq(p,nc->name))
         {  *rgb=nc->rgb;
            return TRUE;
         }
      }
   }
   /* Unknown colour - leave caller's value alone. */
   *enabled=FALSE;
   return FALSE;
}

/*--------------------------------------------------------------------*/
/* Pen allocation                                                     */
/*--------------------------------------------------------------------*/

/* Convert 0..255 byte to RGB() macro form used by ObtainBestPen. */
#define RGBEXP(b) (((ULONG)(b)<<24)|((ULONG)(b)<<16)|((ULONG)(b)<<8)|(ULONG)(b))

/* Obtain a screen pen for the given 0xRRGGBB colour.  The pen is
 * tracked in source->allocated so Disposesource releases it later.
 * Returns the pen number; on failure returns 0 (background pen).
 *
 * We also keep a tiny per-decoder cache so repeated colours during a
 * single render don't go through ObtainBestPen each time. */
static UBYTE Getpen(struct Decoder *dec, ULONG rgb)
{  int i;
   LONG map;
   if(!dec->source->colormap) return 1;
   for(i=0;i<PENCACHESIZE;i++)
   {  if(dec->cache[i].valid && dec->cache[i].rgb==rgb) return dec->cache[i].pen;
   }
   map=ObtainBestPen(dec->source->colormap,
      RGBEXP((rgb>>16)&0xff),
      RGBEXP((rgb>>8)&0xff),
      RGBEXP(rgb&0xff),
      OBP_Precision,PRECISION_IMAGE,
      TAG_END);
   if(map<0) return 1;
   dec->source->allocated[map]++;
   dec->cache[dec->cachenext].rgb=rgb;
   dec->cache[dec->cachenext].pen=(UBYTE)map;
   dec->cache[dec->cachenext].valid=1;
   dec->cachenext=(UBYTE)((dec->cachenext+1)&(PENCACHESIZE-1));
   return (UBYTE)map;
}

/*--------------------------------------------------------------------*/
/* Matrix and transform                                               */
/*--------------------------------------------------------------------*/

static void Midentity(struct Matrix *m)
{  m->a=0x10000L; m->b=0; m->c=0; m->d=0x10000L; m->e=0; m->f=0;
}

/* Fixed-point multiply: (a*b) >> 16 with 64-bit intermediate to avoid
 * overflow.  Inputs are 16.16, result is 16.16. */
static LONG fmul(LONG a, LONG b)
{  double da=(double)a;
   double db=(double)b;
   double dr=(da*db)/65536.0;
   if(dr>2147483647.0) return 2147483647L;
   if(dr<-2147483648.0) return -2147483647L-1L;
   return (LONG)dr;
}

/* Compose two matrices: result = M * T (T applied first then M). */
static void Mcompose(struct Matrix *r, struct Matrix *M, struct Matrix *T)
{  struct Matrix out;
   out.a = fmul(M->a,T->a) + fmul(M->c,T->b);
   out.b = fmul(M->b,T->a) + fmul(M->d,T->b);
   out.c = fmul(M->a,T->c) + fmul(M->c,T->d);
   out.d = fmul(M->b,T->c) + fmul(M->d,T->d);
   out.e = fmul(M->a,T->e) + fmul(M->c,T->f) + M->e;
   out.f = fmul(M->b,T->e) + fmul(M->d,T->f) + M->f;
   *r=out;
}

/* Transform an SVG point (16.16) to raster pixel (integer).
 * Returns raster-pixel coordinates. */
static void Mxform(struct Matrix *M, LONG x, LONG y, LONG *rx, LONG *ry)
{  LONG nx=fmul(M->a,x) + fmul(M->c,y) + M->e;
   LONG ny=fmul(M->b,x) + fmul(M->d,y) + M->f;
   *rx=nx>>16;
   *ry=ny>>16;
}

/* Parse an SVG transform attribute like
 *   "translate(50,30) rotate(45) scale(2)"
 * applying to the matrix in left-to-right order. */
static void Parsetransform(const UBYTE *str, struct Matrix *m)
{  UBYTE *p=(UBYTE *)str;
   UBYTE *end;
   if(!p) return;
   end=p;
   while(*end) end++;
   while(p<end)
   {  UBYTE *kw;
      LONG args[6];
      LONG nargs=0;
      BOOL ok;
      struct Matrix T;
      while(p<end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==',')) p++;
      if(p>=end) break;
      kw=p;
      while(p<end && ((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z'))) p++;
      {  LONG kwlen=(LONG)(p-kw);
         while(p<end && (*p==' '||*p=='\t')) p++;
         if(p>=end || *p!='(') break;
         p++;
         while(nargs<6)
         {  args[nargs]=Parsefixed(&p,end,&ok);
            if(!ok) break;
            nargs++;
         }
         while(p<end && *p!=')') p++;
         if(p<end) p++;
         if(kwlen==9 && (kw[0]=='t'||kw[0]=='T'))
         {  /* translate(tx,ty) */
            Midentity(&T);
            T.e=args[0];
            if(nargs>1) T.f=args[1];
            Mcompose(m,m,&T);
         }
         else if(kwlen==5 && (kw[0]=='s'||kw[0]=='S') && (kw[1]=='c'||kw[1]=='C'))
         {  /* scale(sx[,sy]) */
            Midentity(&T);
            T.a=args[0];
            T.d=(nargs>1)?args[1]:args[0];
            Mcompose(m,m,&T);
         }
         else if(kwlen==6 && (kw[0]=='r'||kw[0]=='R'))
         {  /* rotate(angle [, cx, cy]) */
            double a_rad=(double)args[0]*3.14159265358979323846/180.0/65536.0;
            double cs=cos(a_rad);
            double sn=sin(a_rad);
            Midentity(&T);
            T.a=(LONG)(cs*65536.0);
            T.b=(LONG)(sn*65536.0);
            T.c=(LONG)(-sn*65536.0);
            T.d=(LONG)(cs*65536.0);
            if(nargs>=3)
            {  struct Matrix Tt;
               Midentity(&Tt);
               Tt.e=args[1]; Tt.f=args[2];
               Mcompose(m,m,&Tt);
               Mcompose(m,m,&T);
               Tt.e=-args[1]; Tt.f=-args[2];
               Mcompose(m,m,&Tt);
            }
            else
            {  Mcompose(m,m,&T);
            }
         }
         else if(kwlen==6 && (kw[0]=='m'||kw[0]=='M'))
         {  /* matrix(a,b,c,d,e,f) */
            if(nargs==6)
            {  T.a=args[0]; T.b=args[1]; T.c=args[2]; T.d=args[3];
               T.e=args[4]; T.f=args[5];
               Mcompose(m,m,&T);
            }
         }
         else if(kwlen==5 && (kw[0]=='s'||kw[0]=='S') && (kw[1]=='k'||kw[1]=='K'))
         {  /* skewX/skewY */
            double a_rad=(double)args[0]*3.14159265358979323846/180.0/65536.0;
            double tn=tan(a_rad);
            Midentity(&T);
            if(kw[4]=='X' || kw[4]=='x') T.c=(LONG)(tn*65536.0);
            else                         T.b=(LONG)(tn*65536.0);
            Mcompose(m,m,&T);
         }
      }
   }
}

/*--------------------------------------------------------------------*/
/* Style attribute parser                                             */
/*--------------------------------------------------------------------*/

/* Walk a CSS-style "k:v;k2:v2" string, calling the callback for each
 * pair.  Both key and value are temporarily NUL-terminated inside the
 * input string.  This is destructive but the input lives in the
 * pool-allocated buffer so it doesn't matter. */
static void Parsestyle(UBYTE *str,
   void (*cb)(UBYTE *,UBYTE *,void *), void *userdata)
{  UBYTE *p=str;
   if(!p) return;
   while(*p)
   {  UBYTE *key,*val,*pend;
      while(*p==' '||*p=='\t'||*p==';'||*p=='\n'||*p=='\r') p++;
      if(!*p) break;
      key=p;
      while(*p && *p!=':' && *p!=';') p++;
      if(*p!=':') { while(*p && *p!=';') p++; continue; }
      pend=p;
      while(pend>key && (pend[-1]==' '||pend[-1]=='\t')) pend--;
      *pend=0;
      p++;
      while(*p==' '||*p=='\t') p++;
      val=p;
      while(*p && *p!=';') p++;
      pend=p;
      while(pend>val && (pend[-1]==' '||pend[-1]=='\t')) pend--;
      {  UBYTE save=*pend;
         *pend=0;
         cb(key,val,userdata);
         *pend=save;
      }
      if(*p==';') p++;
   }
}

/*--------------------------------------------------------------------*/
/* Drawing helpers                                                    */
/*--------------------------------------------------------------------*/

/* These helpers talk to graphics.library directly using raster-pixel
 * coordinates.  We deliberately do NOT route through vrastport.lib
 * because its virtual-to-raster scaling uses a 64-bit signed multiply
 * whose register convention (especially in the bundled math64.a) is
 * subtle and has produced surprising results when paired with our
 * pixel-space input.  Going straight to graphics.library is far
 * easier to reason about and gives identical functionality for the
 * shapes we actually need (rects, lines, polygons, ellipses). */

/* Clip helpers: bitmap is 0..bmw-1, 0..bmh-1. */
static LONG Clip(LONG v, LONG lo, LONG hi)
{  if(v<lo) return lo;
   if(v>hi) return hi;
   return v;
}

/* Write one horizontal span in 0xRRGGBB.  On palette bitmaps this uses
 * RectFill with the current pen (caller must SetAPen first).  On
 * Picasso96 deep surfaces we blast true RGB via p96WritePixelArray. */
static void Fillspan_rgb(struct Decoder *dec, LONG x0, LONG x1, LONG y, ULONG rgb)
{  LONG w;
   LONG i;
   UBYTE *p;
   if(x0>x1) return;
   if(y<0 || y>=dec->bmh) return;
   if(x0<0) x0=0;
   if(x1>=dec->bmw) x1=dec->bmw-1;
   w=x1-x0+1;
   if(w<=0) return;
   if(dec->decflags&DECOF_P96DEEP)
   {  p=dec->chunky;
      for(i=0;i<w;i++)
      {  p[(ULONG)i*3+0]=(UBYTE)((rgb>>16)&0xff);
         p[(ULONG)i*3+1]=(UBYTE)((rgb>>8)&0xff);
         p[(ULONG)i*3+2]=(UBYTE)(rgb&0xff);
      }
      dec->ri.BytesPerRow=w*3;
      p96WritePixelArray(&dec->ri,0,0,&dec->rp,x0,y,w,1);
      dec->ri.BytesPerRow=dec->chunkybpr;
   }
   else
   {  RectFill(&dec->rp,x0,y,x1,y);
   }
}

static void Plotpixel_rgb(struct Decoder *dec, LONG x, LONG y, ULONG rgb)
{  UBYTE *p;
   if(x<0 || x>=dec->bmw || y<0 || y>=dec->bmh) return;
   if(dec->decflags&DECOF_P96DEEP)
   {  p=dec->chunky;
      p[0]=(UBYTE)((rgb>>16)&0xff);
      p[1]=(UBYTE)((rgb>>8)&0xff);
      p[2]=(UBYTE)(rgb&0xff);
      dec->ri.BytesPerRow=3;
      p96WritePixelArray(&dec->ri,0,0,&dec->rp,x,y,1,1);
      dec->ri.BytesPerRow=dec->chunkybpr;
   }
   else
   {  WritePixel(&dec->rp,x,y);
   }
}

static void Drawline_rgb(struct Decoder *dec, LONG x0, LONG y0, LONG x1, LONG y1, ULONG rgb)
{  LONG dx,dy,sx,sy,err,e2;
   if(dec->decflags&DECOF_P96DEEP)
   {  dx=(x1>x0)?(x1-x0):(x0-x1);
      dy=(y1>y0)?(y1-y0):(y0-y1);
      sx=(x0<x1)?1:-1;
      sy=(y0<y1)?1:-1;
      err=dx-dy;
      for(;;)
      {  Plotpixel_rgb(dec,x0,y0,rgb);
         if(x0==x1 && y0==y1) break;
         e2=err<<1;
         if(e2>-dy) { err-=dy; x0+=sx; }
         if(e2<dx)  { err+=dx; y0+=sy; }
      }
   }
   else
   {  Move(&dec->rp,(WORD)x0,(WORD)y0);
      Draw(&dec->rp,(WORD)x1,(WORD)y1);
   }
}

static void Drawline(struct Decoder *dec, LONG x0, LONG y0, LONG x1, LONG y1, ULONG rgb)
{  Drawline_rgb(dec,x0,y0,x1,y1,rgb);
}

/* Draw a filled ellipse by horizontal-scanline rasterisation.
 * Uses the standard midpoint algorithm.  Y axis points down (raster
 * convention).  cx,cy is the centre and rx,ry are the half-axes, all
 * in raster pixels. */
static void Drawellipse_fill(struct Decoder *dec, LONG cx, LONG cy, LONG rx, LONG ry, ULONG rgb)
{  LONG bmw=dec->bmw;
   LONG bmh=dec->bmh;
   LONG x,y;
   LONG xl,xr,yt,yb;
   LONG rx2,ry2,twoRx2,twoRy2;
   LONG px,py;
   LONG p;
   LONG lasty=-1;
   if(rx<=0 || ry<=0) return;
   rx2=rx*rx;
   ry2=ry*ry;
   twoRx2=rx2<<1;
   twoRy2=ry2<<1;
   /* Region 1 */
   x=0;
   y=ry;
   p=(LONG)(ry2 - rx2*ry + (rx2+2)/4);
   px=0;
   py=twoRx2*y;
   while(px<py)
   {  if(y!=lasty)
      {  xl=Clip(cx-x,0,bmw-1);
         xr=Clip(cx+x,0,bmw-1);
         yt=cy-y;
         yb=cy+y;
         if(yt>=0 && yt<bmh && xl<=xr) Fillspan_rgb(dec,xl,xr,yt,rgb);
         if(yb>=0 && yb<bmh && yb!=yt && xl<=xr) Fillspan_rgb(dec,xl,xr,yb,rgb);
         lasty=y;
      }
      x++;
      px+=twoRy2;
      if(p<0)
      {  p+=ry2 + px;
      }
      else
      {  y--;
         py-=twoRx2;
         p+=ry2 + px - py;
      }
   }
   /* Region 2 - rounding constant is ry2/4 (derived from expanding
    * b2(x+1/2)2 + a2(y-1)2 - a2b2 at the region transition). */
   p=(LONG)(ry2*(x*x + x) + rx2*(y-1)*(y-1) - rx2*ry2 + (ry2+2)/4);
   while(y>=0)
   {  if(y!=lasty)
      {  xl=Clip(cx-x,0,bmw-1);
         xr=Clip(cx+x,0,bmw-1);
         yt=cy-y;
         yb=cy+y;
         if(yt>=0 && yt<bmh && xl<=xr) Fillspan_rgb(dec,xl,xr,yt,rgb);
         if(yb>=0 && yb<bmh && yb!=yt && xl<=xr) Fillspan_rgb(dec,xl,xr,yb,rgb);
         lasty=y;
      }
      y--;
      py-=twoRx2;
      if(p>0)
      {  p+=rx2 - py;
      }
      else
      {  x++;
         px+=twoRy2;
         p+=rx2 - py + px;
      }
   }
}

/* Stroke an ellipse outline.  Plots 4-symmetric pixels per midpoint
 * step using Bresenham.  Cheap and predictable. */
static void Drawellipse_stroke(struct Decoder *dec, LONG cx, LONG cy, LONG rx, LONG ry, ULONG rgb)
{  LONG bmw=dec->bmw;
   LONG bmh=dec->bmh;
   LONG x,y;
   LONG q[4][2];
   LONG k;
   LONG rx2,ry2,twoRx2,twoRy2;
   LONG px,py;
   LONG p;
   if(rx<=0 || ry<=0) return;
   rx2=rx*rx;
   ry2=ry*ry;
   twoRx2=rx2<<1;
   twoRy2=ry2<<1;
   x=0;
   y=ry;
   p=(LONG)(ry2 - rx2*ry + (rx2+2)/4);
   px=0;
   py=twoRx2*y;
   while(px<py)
   {  q[0][0]=cx+x; q[0][1]=cy+y;
      q[1][0]=cx-x; q[1][1]=cy+y;
      q[2][0]=cx+x; q[2][1]=cy-y;
      q[3][0]=cx-x; q[3][1]=cy-y;
      for(k=0;k<4;k++)
      {  if(q[k][0]>=0 && q[k][0]<bmw && q[k][1]>=0 && q[k][1]<bmh)
            Plotpixel_rgb(dec,q[k][0],q[k][1],rgb);
      }
      x++;
      px+=twoRy2;
      if(p<0)
      {  p+=ry2 + px;
      }
      else
      {  y--;
         py-=twoRx2;
         p+=ry2 + px - py;
      }
   }
   p=(LONG)(ry2*(x*x + x) + rx2*(y-1)*(y-1) - rx2*ry2 + (ry2+2)/4);
   while(y>=0)
   {  q[0][0]=cx+x; q[0][1]=cy+y;
      q[1][0]=cx-x; q[1][1]=cy+y;
      q[2][0]=cx+x; q[2][1]=cy-y;
      q[3][0]=cx-x; q[3][1]=cy-y;
      for(k=0;k<4;k++)
      {  if(q[k][0]>=0 && q[k][0]<bmw && q[k][1]>=0 && q[k][1]<bmh)
            Plotpixel_rgb(dec,q[k][0],q[k][1],rgb);
      }
      y--;
      py-=twoRx2;
      if(p>0)
      {  p+=rx2 - py;
      }
      else
      {  x++;
         px+=twoRy2;
         p+=rx2 - py + px;
      }
   }
}

static void Drawellipse(struct Decoder *dec, LONG cx, LONG cy, LONG rx, LONG ry, BOOL fill, ULONG rgb)
{  if(fill) Drawellipse_fill(dec,cx,cy,rx,ry,rgb);
   else     Drawellipse_stroke(dec,cx,cy,rx,ry,rgb);
}

/* Scanline-based polygon fill using the even-odd rule.  Works for
 * any simple polygon, convex or concave.  Robust against the
 * idiosyncrasies of graphics.library's AreaFill (which needs a
 * blitter-friendly chip-ram TmpRas), at the cost of O(rows * verts).
 * For SVG icons the vertex count is small so this is plenty fast. */
static void Drawpolygon_fill(struct Decoder *dec, struct Point32 *pts, WORD count, ULONG rgb)
{  LONG bmw=dec->bmw;
   LONG bmh=dec->bmh;
   LONG ymin,ymax;
   LONG y;
   LONG x0,y0,x1,y1;
   LONG yA,yB,xA,xB;
   LONG xi;
   LONG sx0,sx1;
   WORD i;
   WORD j,k;
   /* Max number of edges that can cross a single scanline in a simple
    * polygon equals the vertex count, but in practice for natural
    * shapes it is tiny (a dozen even for very complex maps).  Sized
    * to keep heavy Inkscape boundary paths happy without blowing the
    * stack frame. */
   LONG xs[256];
   WORD nx;
   if(count<3) return;
   ymin=pts[0].y;
   ymax=pts[0].y;
   for(i=1;i<count;i++)
   {  if(pts[i].y<ymin) ymin=pts[i].y;
      if(pts[i].y>ymax) ymax=pts[i].y;
   }
   if(ymin<0) ymin=0;
   if(ymax>=bmh) ymax=bmh-1;
   for(y=ymin;y<=ymax;y++)
   {  nx=0;
      /* Collect x-intersections of the horizontal line y=y+0.5 with
       * each polygon edge. */
      for(i=0;i<count;i++)
      {  k=(i+1)%count;
         x0=pts[i].x;
         y0=pts[i].y;
         x1=pts[k].x;
         y1=pts[k].y;
         if(y0==y1) continue;                /* skip horizontal edge */
         if(y0<y1) { yA=y0; yB=y1; xA=x0; xB=x1; }
         else       { yA=y1; yB=y0; xA=x1; xB=x0; }
         /* Half-open [yA, yB): include lower end, exclude upper. */
         if(y<yA || y>=yB) continue;
         xi=xA + ((y-yA)*(xB-xA))/(yB-yA);
         if(nx<256)
         {  /* Insertion sort into xs[]. */
            j=nx;
            while(j>0 && xs[j-1]>xi) { xs[j]=xs[j-1]; j--; }
            xs[j]=xi;
            nx++;
         }
      }
      /* Pairs (xs[0],xs[1]), (xs[2],xs[3]), ... form filled spans. */
      for(i=0;i+1<nx;i+=2)
      {  sx0=xs[i];
         sx1=xs[i+1];
         if(sx0<0) sx0=0;
         if(sx1>=bmw) sx1=bmw-1;
         if(sx0<=sx1) Fillspan_rgb(dec,sx0,sx1,y,rgb);
      }
   }
}

static void Drawpolygon(struct Decoder *dec, struct Point32 *pts, WORD count,
   BOOL fill, BOOL close, ULONG fillrgb, ULONG strokergb)
{  WORD i;
   if(count<2) return;
   if(count>POLY_MAX_VERTS) count=POLY_MAX_VERTS;
   if(fill && count>=3)
   {  Drawpolygon_fill(dec,pts,count,fillrgb);
   }
   else
   {  for(i=1;i<count;i++)
         Drawline(dec,pts[i-1].x,pts[i-1].y,pts[i].x,pts[i].y,strokergb);
      if(close && count>=3)
         Drawline(dec,pts[count-1].x,pts[count-1].y,pts[0].x,pts[0].y,strokergb);
   }
}

/*--------------------------------------------------------------------*/
/* Points list parsing                                                */
/*--------------------------------------------------------------------*/

/* Parse the "points" attribute of polygon/polyline into a transformed
 * Point32 array allocated from the pool.  Returns vertex count or 0. */
static WORD Parsepoints(struct Decoder *dec, struct Matrix *M, UBYTE *str,
   struct Point32 **outpts)
{  UBYTE *p=str;
   UBYTE *end;
   WORD count=0;
   WORD capacity=64;
   struct Point32 *pts;
   BOOL ok;
   *outpts=NULL;
   if(!p) return 0;
   end=p;
   while(*end) end++;
   pts=(struct Point32 *)AllocPooled(dec->pool,sizeof(struct Point32)*capacity);
   if(!pts) return 0;
   while(p<end && count<POLY_MAX_VERTS)
   {  LONG fx,fy,rx,ry;
      fx=Parsefixed(&p,end,&ok);
      if(!ok) break;
      fy=Parsefixed(&p,end,&ok);
      if(!ok) break;
      if(count>=capacity)
      {  WORD newcap=capacity*2;
         struct Point32 *npts;
         if(newcap>POLY_MAX_VERTS) newcap=POLY_MAX_VERTS;
         npts=(struct Point32 *)AllocPooled(dec->pool,sizeof(struct Point32)*newcap);
         if(!npts) break;
         memcpy(npts,pts,sizeof(struct Point32)*count);
         /* Old pts leaks until pool delete - acceptable. */
         pts=npts;
         capacity=newcap;
      }
      Mxform(M,fx,fy,&rx,&ry);
      pts[count].x=rx;
      pts[count].y=ry;
      count++;
   }
   *outpts=pts;
   return count;
}

/*--------------------------------------------------------------------*/
/* Path data parsing                                                  */
/*--------------------------------------------------------------------*/

/* Path emitter - accumulates transformed raster-space points into a
 * subpath that is rendered (filled or stroked) on Z/end. */
struct Pathemit
{  struct Decoder *dec;
   struct Renderstate *rs;
   struct Matrix *M;
   struct Point32 *pts;
   WORD count;
   WORD capacity;
   LONG startx,starty;    /* Subpath start in SVG 16.16 (for Z) */
   LONG curx,cury;        /* Current point in SVG 16.16 */
   LONG ctrlx,ctrly;      /* Last cubic ctrl in SVG 16.16 (for S/T continuation) */
   BOOL hasctrl;
   BOOL subpathopen;
   BOOL truncated;           /* TRUE if vertex cap was hit */
};

static void Pathreset(struct Pathemit *pe)
{  pe->count=0;
   pe->hasctrl=FALSE;
   pe->truncated=FALSE;
}

static void Pathemitpt(struct Pathemit *pe, LONG svgx, LONG svgy)
{  LONG rx,ry;
   if(pe->count>=POLY_MAX_VERTS) { pe->truncated=TRUE; return; }
   Mxform(pe->M,svgx,svgy,&rx,&ry);
   /* Drop consecutive duplicate raster vertices.  Bezier subdivision
    * generates 16 samples per curve; many adjacent samples land on the
    * same integer pixel after Mxform, especially on small or nearly
    * straight curves.  Coalescing them keeps the polygon below the
    * vertex cap on complex maps without changing the rendered shape. */
   if(pe->count>0
   && pe->pts[pe->count-1].x==rx
   && pe->pts[pe->count-1].y==ry) return;
   if(pe->count>=pe->capacity)
   {  WORD newcap=pe->capacity*2;
      struct Point32 *npts;
      if(newcap>POLY_MAX_VERTS || newcap<0) newcap=POLY_MAX_VERTS;
      npts=(struct Point32 *)AllocPooled(pe->dec->pool,sizeof(struct Point32)*newcap);
      if(!npts) return;
      memcpy(npts,pe->pts,sizeof(struct Point32)*pe->count);
      pe->pts=npts;
      pe->capacity=newcap;
   }
   pe->pts[pe->count].x=rx;
   pe->pts[pe->count].y=ry;
   pe->count++;
}

/* Flush the current subpath, stroking or filling as needed. */
static void Pathflush(struct Pathemit *pe, BOOL closepath)
{  BOOL doclose;
   if(pe->count<2) { pe->count=0; pe->subpathopen=FALSE; pe->truncated=FALSE; return; }
   /* If we hit the vertex cap, do not auto-close: connecting the last
    * kept vertex back to the first would draw a shortcut across the shape. */
   doclose=closepath;
   if(pe->truncated) doclose=FALSE;
   if(pe->rs->fillvalid && doclose && pe->count>=3)
   {  if(!(pe->dec->decflags&DECOF_P96DEEP))
         SetAPen(&pe->dec->rp,pe->rs->fillpen);
      Drawpolygon_fill(pe->dec,pe->pts,pe->count,pe->rs->fillrgb);
   }
   if(pe->rs->strokevalid)
   {  WORD i;
      if(!(pe->dec->decflags&DECOF_P96DEEP))
         SetAPen(&pe->dec->rp,pe->rs->strokepen);
      for(i=1;i<pe->count;i++)
         Drawline(pe->dec,pe->pts[i-1].x,pe->pts[i-1].y,
            pe->pts[i].x,pe->pts[i].y,pe->rs->strokergb);
      if(doclose)
         Drawline(pe->dec,pe->pts[pe->count-1].x,pe->pts[pe->count-1].y,
            pe->pts[0].x,pe->pts[0].y,pe->rs->strokergb);
   }
   pe->count=0;
   pe->subpathopen=FALSE;
   pe->truncated=FALSE;
}

/* Sample a cubic Bezier from p0..p3 into the path.  We use uniform
 * subdivision at BEZIER_SUBDIV steps - cheap, good enough for icons.
 *
 * B(t) = (1-t)^3 p0 + 3(1-t)^2 t p1 + 3(1-t)t^2 p2 + t^3 p3
 * Done in 16.16 fixed point via fmul to avoid overflow. */
static void Bezier3(struct Pathemit *pe, LONG x0, LONG y0,
   LONG x1, LONG y1, LONG x2, LONG y2, LONG x3, LONG y3)
{  int s;
   LONG t,mt,mt2,mt3,t2,t3,c1,c2,x,y;
   for(s=1;s<=BEZIER_SUBDIV;s++)
   {  t=((LONG)s<<16)/BEZIER_SUBDIV;
      mt=0x10000L - t;
      mt2=fmul(mt,mt);
      mt3=fmul(mt2,mt);
      t2=fmul(t,t);
      t3=fmul(t2,t);
      c1=fmul(3L<<16,fmul(mt2,t));
      c2=fmul(3L<<16,fmul(mt,t2));
      x=fmul(mt3,x0)+fmul(c1,x1)+fmul(c2,x2)+fmul(t3,x3);
      y=fmul(mt3,y0)+fmul(c1,y1)+fmul(c2,y2)+fmul(t3,y3);
      Pathemitpt(pe,x,y);
   }
}

/* Sample a quadratic Bezier.  Same idea, lower degree. */
static void Bezier2(struct Pathemit *pe, LONG x0, LONG y0,
   LONG x1, LONG y1, LONG x2, LONG y2)
{  int s;
   LONG t,mt,mt2,t2,c1,x,y;
   for(s=1;s<=BEZIER_SUBDIV;s++)
   {  t=((LONG)s<<16)/BEZIER_SUBDIV;
      mt=0x10000L - t;
      mt2=fmul(mt,mt);
      t2=fmul(t,t);
      c1=fmul(2L<<16,fmul(mt,t));
      x=fmul(mt2,x0)+fmul(c1,x1)+fmul(t2,x2);
      y=fmul(mt2,y0)+fmul(c1,y1)+fmul(t2,y2);
      Pathemitpt(pe,x,y);
   }
}

/* Approximate an elliptical arc by a polyline.  We do not implement
 * the full SVG arc parameterisation here (it is fiddly).  Instead we
 * approximate by sampling along the straight chord, which means arcs
 * render as straight lines.  This is acceptable for most icon
 * artwork; a future enhancement could implement endpoint-to-centre
 * conversion. */
static void Arcapprox(struct Pathemit *pe, LONG x0, LONG y0, LONG x1, LONG y1)
{  Pathemitpt(pe,x1,y1);
   (void)x0; (void)y0;
}

/* Parse and emit an SVG path 'd' attribute. */
static void Parsepath(struct Pathemit *pe, UBYTE *d)
{  UBYTE *p=d;
   UBYTE *end;
   UBYTE cmd=0;
   BOOL relative;
   BOOL ok;
   LONG a,b,c,d0,e,f,g;
   if(!p) return;
   end=p;
   while(*end) end++;
   pe->curx=0; pe->cury=0;
   pe->startx=0; pe->starty=0;
   pe->subpathopen=FALSE;
   while(p<end)
   {  while(p<end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==',')) p++;
      if(p>=end) break;
      if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z'))
      {  cmd=*p++;
      }
      relative=(cmd>='a'&&cmd<='z');
      switch(cmd|0x20)
      {  case 'm':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) { a+=pe->curx; b+=pe->cury; }
            if(pe->subpathopen) Pathflush(pe,FALSE);
            pe->curx=a; pe->cury=b;
            pe->startx=a; pe->starty=b;
            Pathemitpt(pe,a,b);
            pe->subpathopen=TRUE;
            pe->hasctrl=FALSE;
            cmd=relative?'l':'L';
            break;
         case 'l':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) { a+=pe->curx; b+=pe->cury; }
            Pathemitpt(pe,a,b);
            pe->curx=a; pe->cury=b;
            pe->hasctrl=FALSE;
            break;
         case 'h':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) a+=pe->curx;
            Pathemitpt(pe,a,pe->cury);
            pe->curx=a;
            pe->hasctrl=FALSE;
            break;
         case 'v':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) a+=pe->cury;
            Pathemitpt(pe,pe->curx,a);
            pe->cury=a;
            pe->hasctrl=FALSE;
            break;
         case 'z':
            if(pe->subpathopen)
            {  Pathflush(pe,TRUE);
               pe->curx=pe->startx; pe->cury=pe->starty;
            }
            pe->hasctrl=FALSE;
            break;
         case 'c':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            e=Parsefixed(&p,end,&ok); if(!ok) goto done;
            f=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative)
            {  a+=pe->curx; b+=pe->cury;
               c+=pe->curx; d0+=pe->cury;
               e+=pe->curx; f+=pe->cury;
            }
            Bezier3(pe,pe->curx,pe->cury,a,b,c,d0,e,f);
            pe->ctrlx=c; pe->ctrly=d0;
            pe->hasctrl=TRUE;
            pe->curx=e; pe->cury=f;
            break;
         case 's':
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            e=Parsefixed(&p,end,&ok); if(!ok) goto done;
            f=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative)
            {  c+=pe->curx; d0+=pe->cury;
               e+=pe->curx; f+=pe->cury;
            }
            if(pe->hasctrl)
            {  a=2*pe->curx - pe->ctrlx;
               b=2*pe->cury - pe->ctrly;
            }
            else
            {  a=pe->curx; b=pe->cury;
            }
            Bezier3(pe,pe->curx,pe->cury,a,b,c,d0,e,f);
            pe->ctrlx=c; pe->ctrly=d0;
            pe->hasctrl=TRUE;
            pe->curx=e; pe->cury=f;
            break;
         case 'q':
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative)
            {  a+=pe->curx; b+=pe->cury;
               c+=pe->curx; d0+=pe->cury;
            }
            Bezier2(pe,pe->curx,pe->cury,a,b,c,d0);
            pe->ctrlx=a; pe->ctrly=b;
            pe->hasctrl=TRUE;
            pe->curx=c; pe->cury=d0;
            break;
         case 't':
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) { c+=pe->curx; d0+=pe->cury; }
            if(pe->hasctrl)
            {  a=2*pe->curx - pe->ctrlx;
               b=2*pe->cury - pe->ctrly;
            }
            else
            {  a=pe->curx; b=pe->cury;
            }
            Bezier2(pe,pe->curx,pe->cury,a,b,c,d0);
            pe->ctrlx=a; pe->ctrly=b;
            pe->hasctrl=TRUE;
            pe->curx=c; pe->cury=d0;
            break;
         case 'a':
            /* A rx ry x-axis-rotation large-arc-flag sweep-flag x y */
            a=Parsefixed(&p,end,&ok); if(!ok) goto done;
            b=Parsefixed(&p,end,&ok); if(!ok) goto done;
            c=Parsefixed(&p,end,&ok); if(!ok) goto done;
            d0=Parsefixed(&p,end,&ok); if(!ok) goto done;
            e=Parsefixed(&p,end,&ok); if(!ok) goto done;
            f=Parsefixed(&p,end,&ok); if(!ok) goto done;
            g=Parsefixed(&p,end,&ok); if(!ok) goto done;
            if(relative) { f+=pe->curx; g+=pe->cury; }
            (void)a; (void)b; (void)c; (void)d0; (void)e;
            Arcapprox(pe,pe->curx,pe->cury,f,g);
            pe->curx=f; pe->cury=g;
            pe->hasctrl=FALSE;
            break;
         default:
            /* Unknown command - skip one token to avoid infinite loop. */
            p++;
            cmd=0;
            break;
      }
   }
done:
   if(pe->subpathopen) Pathflush(pe,FALSE);
}

/*--------------------------------------------------------------------*/
/* Style frame helpers                                                */
/*--------------------------------------------------------------------*/

struct StyleCtx
{  struct Decoder *dec;
   struct Renderstate *rs;
};

static void Stylepair(UBYTE *key, UBYTE *value, void *u)
{  struct StyleCtx *ctx=(struct StyleCtx *)u;
   ULONG rgb;
   BOOL enabled;
   if(strieq(key,"fill"))
   {  if(Parsecolor(value,&rgb,&enabled))
      {  ctx->rs->fillvalid=enabled;
         if(enabled)
         {  ctx->rs->fillrgb=rgb;
            ctx->rs->fillpen=Getpen(ctx->dec,rgb);
         }
      }
      else
      {  ctx->rs->fillvalid=FALSE;
      }
   }
   else if(strieq(key,"stroke"))
   {  if(Parsecolor(value,&rgb,&enabled))
      {  ctx->rs->strokevalid=enabled;
         if(enabled)
         {  ctx->rs->strokergb=rgb;
            ctx->rs->strokepen=Getpen(ctx->dec,rgb);
         }
      }
      else
      {  ctx->rs->strokevalid=FALSE;
      }
   }
   else if(strieq(key,"stroke-width"))
   {  UBYTE *p=value;
      UBYTE *e=value;
      BOOL ok;
      LONG w;
      while(*e) e++;
      w=Parsefixed(&p,e,&ok);
      Skipunits(&p,e);          /* tolerate `2px`, `1em` etc. */
      if(ok) ctx->rs->strokewidth=w;
   }
   else if(strieq(key,"display"))
   {  ctx->rs->visible=(UBYTE)(strieq((UBYTE *)value,"none")?FALSE:TRUE);
   }
   else if(strieq(key,"visibility"))
   {  /* SVG: "hidden" or "collapse" hides this element but still
       * lets it participate in layout.  For our raster output the
       * effect is identical to display:none. */
      ctx->rs->visible=(UBYTE)((strieq((UBYTE *)value,"hidden")
         || strieq((UBYTE *)value,"collapse"))?FALSE:TRUE);
   }
}

/* Read fill/stroke/style from a node and update the render state. */
static void Applystyle(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  UBYTE *v;
   ULONG rgb;
   BOOL enabled;
   v=XmlAttrValue(node,"fill");
   if(v && Parsecolor(v,&rgb,&enabled))
   {  rs->fillvalid=enabled;
      if(enabled)
      {  rs->fillrgb=rgb;
         rs->fillpen=Getpen(dec,rgb);
      }
   }
   v=XmlAttrValue(node,"stroke");
   if(v && Parsecolor(v,&rgb,&enabled))
   {  rs->strokevalid=enabled;
      if(enabled)
      {  rs->strokergb=rgb;
         rs->strokepen=Getpen(dec,rgb);
      }
   }
   v=XmlAttrValue(node,"stroke-width");
   if(v)
   {  UBYTE *p=v;
      UBYTE *e=v;
      BOOL ok;
      LONG w;
      while(*e) e++;
      w=Parsefixed(&p,e,&ok);
      Skipunits(&p,e);
      if(ok) rs->strokewidth=w;
   }
   v=XmlAttrValue(node,"display");
   if(v) rs->visible=(UBYTE)(strieq(v,"none")?FALSE:TRUE);
   v=XmlAttrValue(node,"visibility");
   if(v) rs->visible=(UBYTE)((strieq(v,"hidden")||strieq(v,"collapse"))?FALSE:TRUE);
   v=XmlAttrValue(node,"style");
   if(v)
   {  struct StyleCtx ctx;
      ctx.dec=dec;
      ctx.rs=rs;
      Parsestyle(v,Stylepair,&ctx);
   }
}

/* Apply a transform attribute if present.  Modifies *rs->M in place. */
static void Applytransform(struct XmlNode *node, struct Renderstate *rs)
{  UBYTE *v=XmlAttrValue(node,"transform");
   if(v) Parsetransform(v,&rs->M);
}

/*--------------------------------------------------------------------*/
/* Shape attribute helpers                                            */
/*--------------------------------------------------------------------*/

static LONG Numattr(struct XmlNode *node, const char *name, LONG def)
{  UBYTE *v=XmlAttrValue(node,name);
   UBYTE *p,*e;
   BOOL ok;
   LONG r;
   if(!v) return def;
   p=v;
   e=v;
   while(*e) e++;
   r=Parsefixed(&p,e,&ok);
   /* Attribute-level values may carry a CSS-style unit suffix
    * (`100px`, `2em`, `50%`) - swallow it so we don't trip up on it
    * here.  We treat all units as user units for now; px is the only
    * one most SVG files actually use. */
   Skipunits(&p,e);
   return ok?r:def;
}

/*--------------------------------------------------------------------*/
/* Element renderers                                                  */
/*--------------------------------------------------------------------*/

static void Renderrect(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG x=Numattr(node,"x",0);
   LONG y=Numattr(node,"y",0);
   LONG w=Numattr(node,"width",0);
   LONG h=Numattr(node,"height",0);
   LONG x0,y0,x1,y1;
   struct Point32 pts[4];
   if(w<=0 || h<=0) return;
   /* Build the 4 corners in SVG space then transform.  This handles
    * rotation/skew correctly. */
   Mxform(&rs->M,x,y,&x0,&y0);
   Mxform(&rs->M,x+w,y,&x1,&y1);
   pts[0].x=x0; pts[0].y=y0;
   pts[1].x=x1; pts[1].y=y1;
   Mxform(&rs->M,x+w,y+h,&x0,&y0);
   Mxform(&rs->M,x,y+h,&x1,&y1);
   pts[2].x=x0; pts[2].y=y0;
   pts[3].x=x1; pts[3].y=y1;
   if(rs->fillvalid)
   {  if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->fillpen);
      Drawpolygon(dec,pts,4,TRUE,TRUE,rs->fillrgb,rs->strokergb);
   }
   if(rs->strokevalid)
   {  if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->strokepen);
      Drawpolygon(dec,pts,4,FALSE,TRUE,rs->fillrgb,rs->strokergb);
   }
}

static void Rendercircle(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG cx=Numattr(node,"cx",0);
   LONG cy=Numattr(node,"cy",0);
   LONG r=Numattr(node,"r",0);
   LONG rcx,rcy,redge,dummy;
   LONG rr;
   if(r<=0) return;
   Mxform(&rs->M,cx,cy,&rcx,&rcy);
   Mxform(&rs->M,cx+r,cy,&redge,&dummy);
   rr=redge-rcx;
   if(rr<0) rr=-rr;
   if(rr<=0) return;
   if(rs->fillvalid)
   {  if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->fillpen);
      Drawellipse(dec,rcx,rcy,rr,rr,TRUE,rs->fillrgb);
   }
   if(rs->strokevalid)
   {  if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->strokepen);
      Drawellipse(dec,rcx,rcy,rr,rr,FALSE,rs->strokergb);
   }
}

static void Renderellipse(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG cx=Numattr(node,"cx",0);
   LONG cy=Numattr(node,"cy",0);
   LONG rx=Numattr(node,"rx",0);
   LONG ry=Numattr(node,"ry",0);
   LONG rcx,rcy,redgex,redgey,dummyx,dummyy;
   LONG rrx,rry;
   if(rx<=0 || ry<=0) return;
   Mxform(&rs->M,cx,cy,&rcx,&rcy);
   Mxform(&rs->M,cx+rx,cy,&redgex,&dummyy);
   Mxform(&rs->M,cx,cy+ry,&dummyx,&redgey);
   rrx=redgex-rcx; if(rrx<0) rrx=-rrx;
   rry=redgey-rcy; if(rry<0) rry=-rry;
   if(rrx<=0 || rry<=0) return;
   if(rs->fillvalid)
   {  if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->fillpen);
      Drawellipse(dec,rcx,rcy,rrx,rry,TRUE,rs->fillrgb);
   }
   if(rs->strokevalid)
   {  if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->strokepen);
      Drawellipse(dec,rcx,rcy,rrx,rry,FALSE,rs->strokergb);
   }
}

static void Renderline(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  LONG x1=Numattr(node,"x1",0);
   LONG y1=Numattr(node,"y1",0);
   LONG x2=Numattr(node,"x2",0);
   LONG y2=Numattr(node,"y2",0);
   LONG rx1,ry1,rx2,ry2;
   if(!rs->strokevalid) return;
   Mxform(&rs->M,x1,y1,&rx1,&ry1);
   Mxform(&rs->M,x2,y2,&rx2,&ry2);
   if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->strokepen);
   Drawline(dec,rx1,ry1,rx2,ry2,rs->strokergb);
}

static void Renderpoly(struct Decoder *dec, struct XmlNode *node,
   struct Renderstate *rs, BOOL closeit)
{  UBYTE *pts_str=XmlAttrValue(node,"points");
   struct Point32 *pts;
   WORD n;
   if(!pts_str) return;
   n=Parsepoints(dec,&rs->M,pts_str,&pts);
   if(n<2) return;
   if(rs->fillvalid && closeit && n>=3)
   {  if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->fillpen);
      Drawpolygon(dec,pts,n,TRUE,TRUE,rs->fillrgb,rs->strokergb);
   }
   if(rs->strokevalid)
   {  if(!(dec->decflags&DECOF_P96DEEP)) SetAPen(&dec->rp,rs->strokepen);
      Drawpolygon(dec,pts,n,FALSE,closeit,rs->fillrgb,rs->strokergb);
   }
}

static void Renderpath(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  UBYTE *d=XmlAttrValue(node,"d");
   struct Pathemit pe;
   if(!d) return;
   pe.dec=dec;
   pe.rs=rs;
   pe.M=&rs->M;
   pe.capacity=64;
   pe.pts=(struct Point32 *)AllocPooled(dec->pool,sizeof(struct Point32)*pe.capacity);
   if(!pe.pts) return;
   Pathreset(&pe);
   pe.subpathopen=FALSE;
   Parsepath(&pe,d);
}

/*--------------------------------------------------------------------*/
/* id -> node map and <use> support                                   */
/*--------------------------------------------------------------------*/

/* Case-sensitive byte equality - XML ids are case sensitive. */
static int Streq(const UBYTE *a, const UBYTE *b)
{  while(*a && *b)
   {  if(*a!=*b) return 0;
      a++; b++;
   }
   return *a==0 && *b==0;
}

/* Tiny djb2-style hash modulo IDTABLE_SIZE. */
static ULONG Hashid(const UBYTE *s)
{  ULONG h=5381;
   while(*s) { h=((h<<5)+h)+(ULONG)(*s); s++; }
   return h&(IDTABLE_SIZE-1);
}

/* Insert one node into the id table.  Called recursively on every
 * element after the DOM has been parsed.  Each entry is pool-allocated
 * so no explicit cleanup is needed. */
static void Indexids(struct Decoder *dec, struct XmlNode *node)
{  struct XmlNode *child;
   if(!node) return;
   if(node->type==XMLN_ELEMENT)
   {  UBYTE *id=XmlAttrValue(node,"id");
      if(id && *id)
      {  ULONG h=Hashid(id);
         struct Iddef *e=(struct Iddef *)AllocPooled(dec->pool,sizeof(*e));
         if(e)
         {  e->id=id;
            e->node=node;
            e->next=dec->idtable[h];
            dec->idtable[h]=e;
         }
      }
   }
   for(child=node->firstchild;child;child=child->nextsibling)
      Indexids(dec,child);
}

/* Look up a node by id.  Returns NULL if not found. */
static struct XmlNode *Findid(struct Decoder *dec, const UBYTE *id)
{  struct Iddef *e;
   ULONG h;
   if(!id || !*id) return NULL;
   h=Hashid(id);
   for(e=dec->idtable[h];e;e=e->next)
      if(Streq(e->id,id)) return e->node;
   return NULL;
}

/* Resolve a "#id" reference.  Returns the bare id (after stripping the
 * leading '#') or NULL if the reference is malformed or external. */
static UBYTE *Resolvehref(struct XmlNode *node)
{  UBYTE *href=XmlAttrValue(node,"xlink:href");
   if(!href) href=XmlAttrValue(node,"href");
   if(!href) return NULL;
   while(*href==' '||*href=='\t') href++;
   if(*href!='#') return NULL;
   return href+1;
}

/* Render a <use> element.  Resolves the referenced node, applies the
 * use element's own x/y/transform/style on top of the inherited state,
 * then renders the referenced subtree as if it were a child.  Cycles
 * are blocked by a depth counter. */
static void Renderuse(struct Decoder *dec, struct XmlNode *node, struct Renderstate *rs)
{  UBYTE *idref;
   struct XmlNode *target;
   LONG ux,uy;
   struct Matrix T;
   if(dec->usedepth>=USE_MAX_DEPTH) return;
   idref=Resolvehref(node);
   if(!idref) return;
   target=Findid(dec,idref);
   if(!target) return;
   /* Use's x and y attributes act as an additional translate that is
    * applied before the referenced subtree's own transform. */
   ux=Numattr(node,"x",0);
   uy=Numattr(node,"y",0);
   if(ux || uy)
   {  Midentity(&T);
      T.e=ux;
      T.f=uy;
      Mcompose(&rs->M,&rs->M,&T);
   }
   /* Render the target.  <symbol> is normally invisible during a
    * regular walk, so handle it explicitly: treat it as a <g> here so
    * its children inherit the use's transform/style cascade. */
   dec->usedepth++;
   if(XmlNameIs(target,"symbol"))
   {  struct XmlNode *child;
      struct Renderstate child_rs=*rs;
      Applytransform(target,&child_rs);
      Applystyle(dec,target,&child_rs);
      for(child=target->firstchild;child;child=child->nextsibling)
         Renderelement(dec,child,&child_rs);
   }
   else
   {  Renderelement(dec,target,rs);
   }
   dec->usedepth--;
}

/*--------------------------------------------------------------------*/
/* Recursive renderer                                                 */
/*--------------------------------------------------------------------*/

static void Renderelement(struct Decoder *dec, struct XmlNode *node, struct Renderstate *parent)
{  struct Renderstate rs;
   if(!node || node->type!=XMLN_ELEMENT) return;
   rs=*parent;
   Applytransform(node,&rs);
   Applystyle(dec,node,&rs);
   if(!rs.visible) return;     /* display:none */

   if(XmlNameIs(node,"g") || XmlNameIs(node,"svg") || XmlNameIs(node,"a"))
   {  struct XmlNode *child;
      for(child=node->firstchild;child;child=child->nextsibling)
      {  Renderelement(dec,child,&rs);
      }
      return;
   }
   if(XmlNameIs(node,"use"))          { Renderuse(dec,node,&rs); return; }
   if(XmlNameIs(node,"rect"))         { Renderrect(dec,node,&rs); return; }
   if(XmlNameIs(node,"circle"))       { Rendercircle(dec,node,&rs); return; }
   if(XmlNameIs(node,"ellipse"))      { Renderellipse(dec,node,&rs); return; }
   if(XmlNameIs(node,"line"))         { Renderline(dec,node,&rs); return; }
   if(XmlNameIs(node,"polygon"))      { Renderpoly(dec,node,&rs,TRUE); return; }
   if(XmlNameIs(node,"polyline"))     { Renderpoly(dec,node,&rs,FALSE); return; }
   if(XmlNameIs(node,"path"))         { Renderpath(dec,node,&rs); return; }
   /* defs, symbol, title, desc, metadata, style, script - never
    * rendered during a regular walk.  Symbol contents are reached
    * exclusively via <use>. */
}

/*--------------------------------------------------------------------*/
/* viewBox handling                                                   */
/*--------------------------------------------------------------------*/

/* Read width/height/viewBox from the root <svg> and decide our
 * bitmap dimensions and root transform. */
static void Setupviewport(struct Decoder *dec, struct XmlNode *root,
   long *bmw, long *bmh, struct Matrix *initial)
{  LONG svgw=0, svgh=0;
   LONG vbx=0, vby=0, vbw=0, vbh=0;
   BOOL havevb=FALSE;
   UBYTE *vbstr;
   LONG w,h;

   svgw=Numattr(root,"width",0);
   svgh=Numattr(root,"height",0);

   vbstr=XmlAttrValue(root,"viewBox");
   if(vbstr)
   {  UBYTE *p=vbstr;
      UBYTE *e=vbstr;
      BOOL ok;
      while(*e) e++;
      vbx=Parsefixed(&p,e,&ok);
      if(ok) { vby=Parsefixed(&p,e,&ok); }
      if(ok) { vbw=Parsefixed(&p,e,&ok); }
      if(ok) { vbh=Parsefixed(&p,e,&ok); }
      if(ok && vbw>0 && vbh>0) havevb=TRUE;
   }

   if(svgw<=0 && havevb) svgw=vbw;
   if(svgh<=0 && havevb) svgh=vbh;
   if(svgw<=0) svgw=DEFAULT_DIM<<16;
   if(svgh<=0) svgh=DEFAULT_DIM<<16;

   w=svgw>>16;
   h=svgh>>16;
   if(w<=0) w=DEFAULT_DIM;
   if(h<=0) h=DEFAULT_DIM;
   if(w>MAX_BITMAP_DIM)
   {  /* Preserve aspect ratio when clamping. */
      LONG nh=(LONG)(((double)h*(double)MAX_BITMAP_DIM)/(double)w);
      if(nh<1) nh=1;
      w=MAX_BITMAP_DIM;
      h=nh;
   }
   if(h>MAX_BITMAP_DIM)
   {  LONG nw=(LONG)(((double)w*(double)MAX_BITMAP_DIM)/(double)h);
      if(nw<1) nw=1;
      h=MAX_BITMAP_DIM;
      w=nw;
   }
   *bmw=w;
   *bmh=h;

   Midentity(initial);
   if(havevb)
   {  /* Map (vbx,vby)..(vbx+vbw,vby+vbh) to (0,0)..(w,h).
       * Use double through the divide to avoid trouble with
       * sub-unit viewBox dimensions (which underflow 16.16). */
      double dvbw=(double)vbw/65536.0;
      double dvbh=(double)vbh/65536.0;
      double sxf,syf;
      LONG sx,sy;
      if(dvbw<1.0/65536.0) dvbw=1.0;
      if(dvbh<1.0/65536.0) dvbh=1.0;
      sxf=((double)w/dvbw)*65536.0;
      syf=((double)h/dvbh)*65536.0;
      if(sxf>2147483000.0) sxf=2147483000.0;
      if(syf>2147483000.0) syf=2147483000.0;
      sx=(LONG)sxf;
      sy=(LONG)syf;
      initial->a=sx;
      initial->d=sy;
      initial->e=-fmul(sx,vbx);
      initial->f=-fmul(sy,vby);
   }
   /* No viewBox - SVG coords map 1:1 to raster pixels (identity). */
}

/*--------------------------------------------------------------------*/
/* The parser subtask                                                 */
/*--------------------------------------------------------------------*/

static void Parsertask(void *userdata)
{  struct Decoder dec;
   struct Svgsource *ss;
   struct Datablock *db;
   struct XmlNode *root;
   struct Renderstate rs;
   struct Matrix initial;
   long bmw=0, bmh=0;
   long offset;

   memset(&dec,0,sizeof(dec));
   ss=(struct Svgsource *)userdata;
   if(!ss)
   {  struct Task *t=FindTask(NULL);
      struct Aobject *to;
      if(t && t->tc_UserData)
      {  to=(struct Aobject *)t->tc_UserData;
         ss=(struct Svgsource *)Agetattr(to,AOTSK_Userdata);
      }
   }
   if(!ss) return;
   dec.source=ss;

   SVGLOG(("SVG: Parsertask start, ss=%p\n",ss));

   /* Wait for EOF.  Drain task messages along the way. */
   for(;;)
   {  struct Taskmsg *tm;
      ULONG cur;
      ObtainSemaphore(&ss->sema);
      cur=ss->flags;
      ReleaseSemaphore(&ss->sema);
      if(cur&SVGSF_EOF) break;
      if(dec.flags&DECOF_STOP) goto cleanup;
      while((tm=Gettaskmsg()))
      {  struct TagItem *tag,*tstate;
         if(tm->amsg && tm->amsg->method==AOM_SET)
         {  tstate=((struct Amset *)tm->amsg)->tags;
            while((tag=NextTagItem(&tstate)))
            {  if(tag->ti_Tag==AOTSK_Stop && tag->ti_Data)
                  dec.flags|=DECOF_STOP;
            }
         }
         Replytaskmsg(tm);
      }
      if(dec.flags&DECOF_STOP) goto cleanup;
      Waittask(0);
   }

   /* Concatenate all received data into one buffer. */
   dec.buflen=0;
   ObtainSemaphore(&ss->sema);
   for(db=ss->data.first;db && db->next;db=db->next) dec.buflen+=db->length;
   ReleaseSemaphore(&ss->sema);
   if(dec.buflen<=0) goto cleanup;
   if(dec.buflen>(2*1024*1024)) dec.buflen=2*1024*1024;
   dec.buffer=(UBYTE *)AllocVec(dec.buflen+1,MEMF_PUBLIC);
   if(!dec.buffer) goto cleanup;
   offset=0;
   ObtainSemaphore(&ss->sema);
   for(db=ss->data.first;db && db->next && offset<dec.buflen;db=db->next)
   {  long copy=db->length;
      if(offset+copy>dec.buflen) copy=dec.buflen-offset;
      memcpy(dec.buffer+offset,db->data,copy);
      offset+=copy;
   }
   ReleaseSemaphore(&ss->sema);
   dec.buffer[dec.buflen]=0;

   /* Create the parse pool.  Sized so most SVG icons fit one or two
    * 32KB puddles without a second allocator round-trip. */
   dec.pool=CreatePool(MEMF_PUBLIC,32768UL,4096UL);
   if(!dec.pool) goto cleanup;

   /* Parse the XML. */
   root=XmlParse(dec.pool,dec.buffer,dec.buflen);
   if(!root) { SVGLOG(("SVG: XML parse returned NULL root\n")); goto cleanup; }

   /* Pre-walk the DOM once to build the id-map used by <use> /
    * xlink:href resolution.  Doing this up front means lookups during
    * rendering are O(1) hash hits instead of O(n) tree searches. */
   Indexids(&dec,root);

   /* Compute viewport. */
   Setupviewport(&dec,root,&bmw,&bmh,&initial);
   dec.bmw=bmw;
   dec.bmh=bmh;
   SVGLOG(("SVG: bitmap %ldx%ld\n",bmw,bmh));

   /* Get screen + colormap. */
   if(Agetattr(Aweb(),AOAPP_Screenvalid))
   {  Agetattrs(Aweb(),
         AOAPP_Screen,&dec.screen,
         AOAPP_Colormap,&ss->colormap,
         TAG_END);
   }
   if(!dec.screen) goto cleanup;
   if(dec.screen->RastPort.BitMap)
      ss->friendbitmap=dec.screen->RastPort.BitMap;

   /* Allocate the output bitmap.  Prefer a Picasso96 true-colour surface
    * when the browser is running on one (same policy as PNG/GIF/JFIF);
    * otherwise fall back to an 8-bit palette bitmap. */
   {  ULONG depth=8;
      if(P96Base && ss->friendbitmap
      && p96GetBitMapAttr(ss->friendbitmap,P96BMA_ISP96))
      {  depth=p96GetBitMapAttr(ss->friendbitmap,P96BMA_DEPTH);
         dec.bitmap=p96AllocBitMap(bmw,bmh,depth,
            BMF_MINPLANES|BMF_CLEAR|BMF_DISPLAYABLE,ss->friendbitmap,RGBFB_NONE);
         if(dec.bitmap && p96GetBitMapAttr(dec.bitmap,P96BMA_ISP96))
         {  dec.decflags|=DECOF_P96MAP;
            if(depth>8) dec.decflags|=DECOF_P96DEEP;
         }
         else
         {  if(dec.bitmap) { p96FreeBitMap(dec.bitmap); dec.bitmap=NULL; }
         }
      }
      if(!dec.bitmap)
      {  if(dec.screen->RastPort.BitMap)
            depth=GetBitMapAttr(dec.screen->RastPort.BitMap,BMA_DEPTH);
         if(depth<1) depth=8;
         if(depth>8) depth=8;
         dec.bitmap=AllocBitMap(bmw,bmh,depth,BMF_CLEAR,dec.screen->RastPort.BitMap);
      }
   }
   if(!dec.bitmap) goto cleanup;
   InitRastPort(&dec.rp);
   dec.rp.BitMap=dec.bitmap;
   if(dec.decflags&DECOF_P96DEEP)
   {  ULONG y;
      UBYTE *p;
      dec.chunky=(UBYTE *)AllocVec(bmw*4,MEMF_PUBLIC);
      if(!dec.chunky) goto cleanup;
      dec.chunkybpr=bmw*4;
      dec.ri.Memory=dec.chunky;
      dec.ri.BytesPerRow=dec.chunkybpr;
      dec.ri.RGBFormat=RGBFB_R8G8B8;
      p=dec.chunky;
      for(y=0;y<(ULONG)bmh;y++)
      {  memset(p,0,(size_t)bmw*4);
         p96WritePixelArray(&dec.ri,0,0,&dec.rp,0,(LONG)y,bmw,1);
      }
   }
   else
   {  SetAPen(&dec.rp,0);
      RectFill(&dec.rp,0,0,bmw-1,bmh-1);
   }

   /* Render. */
   rs.M=initial;
   rs.fillpen=1;
   rs.strokepen=1;
   rs.fillvalid=TRUE;       /* SVG default fill is black */
   rs.strokevalid=FALSE;
   rs.visible=TRUE;
   rs.strokewidth=0x10000L;
   rs.fillrgb=0x000000UL;
   rs.strokergb=0x000000UL;
   /* Resolve default black pen up front so primitives without
    * explicit fill don't trigger a per-call ObtainBestPen. */
   rs.fillpen=Getpen(&dec,0x000000UL);

   if(XmlNameIs(root,"svg"))
   {  struct XmlNode *child;
      Applytransform(root,&rs);
      Applystyle(&dec,root,&rs);
      for(child=root->firstchild;child;child=child->nextsibling)
      {  Renderelement(&dec,child,&rs);
      }
   }
   else
   {  Renderelement(&dec,root,&rs);
   }

   /* Hand the bitmap to the source object. */
   ObtainSemaphore(&ss->sema);
   ss->width=bmw;
   ss->height=bmh;
   ss->bitmap=dec.bitmap;
   ss->flags|=SVGSF_IMAGEREADY;
   ReleaseSemaphore(&ss->sema);
   dec.bitmap=NULL; /* now owned by ss */

   Updatetaskattrs(
      AOSVG_Width,bmw,
      AOSVG_Height,bmh,
      AOSVG_Imgready,TRUE,
      AOSVG_Parseready,TRUE,
      TAG_END);

cleanup:
   SVGLOG(("SVG: Parsertask cleanup\n"));
   if(dec.chunky) FreeVec(dec.chunky);
   if(dec.bitmap)
   {  if(P96Base && p96GetBitMapAttr(dec.bitmap,P96BMA_ISP96))
         p96FreeBitMap(dec.bitmap);
      else
         FreeBitMap(dec.bitmap);
   }
   if(dec.pool) DeletePool(dec.pool);
   if(dec.buffer) FreeVec(dec.buffer);
   if(ss && !ss->bitmap)
   {  Updatetaskattrs(AOSVG_Error,TRUE,TAG_END);
   }
}

/*--------------------------------------------------------------------*/
/* AOM_ dispatch                                                      */
/*--------------------------------------------------------------------*/

static void Startparser(struct Svgsource *ss)
{  struct Screen *screen=NULL;
   if(!ss || ss->task) return;
   if(!Agetattr(Aweb(),AOAPP_Screenvalid)) return;
   Agetattrs(Aweb(),AOAPP_Screen,&screen,TAG_END);
   if(!screen) return;
   ss->task=Anewobject(AOTP_TASK,
      AOTSK_Entry,Parsertask,
      AOTSK_Name,"AWebSvg parser",
      AOTSK_Userdata,ss,
      AOBJ_Target,ss,
      TAG_END);
   if(ss->task)
   {  void *check=(void *)Agetattr(ss->task,AOTSK_Userdata);
      if(check!=ss) Asetattrs(ss->task,AOTSK_Userdata,ss,TAG_END);
      Asetattrs(ss->task,AOTSK_Start,TRUE,TAG_END);
   }
}

static ULONG Getsource(struct Svgsource *ss, struct Amset *amset)
{  struct TagItem *tag,*tstate;
   AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)ss,(struct Amessage *)amset);
   tstate=amset->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,ss->source);
            break;
         case AOSDV_Saveable:
            PUTATTR(tag,(ss->flags&SVGSF_EOF)?TRUE:FALSE);
            break;
      }
   }
   return 0;
}

static ULONG Setsource(struct Svgsource *ss, struct Amset *amset)
{  struct TagItem *tag,*tstate;
   Amethodas(AOTP_SOURCEDRIVER,(struct Aobject *)ss,AOM_SET,amset->tags);
   tstate=amset->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            ss->source=(struct Aobject *)tag->ti_Data;
            break;
         case AOSDV_Displayed:
            if(tag->ti_Data)
            {  struct Datablock *first;
               ss->flags|=SVGSF_DISPLAYED;
               ObtainSemaphore(&ss->sema);
               first=ss->data.first->next;
               ReleaseSemaphore(&ss->sema);
               if(first && !ss->bitmap && !ss->task) Startparser(ss);
            }
            else
            {  ss->flags&=~SVGSF_DISPLAYED;
            }
            break;
         case AOAPP_Screenvalid:
            if(tag->ti_Data)
            {  struct Datablock *first;
               ObtainSemaphore(&ss->sema);
               first=ss->data.first->next;
               ReleaseSemaphore(&ss->sema);
               if(first && (ss->flags&SVGSF_DISPLAYED) && !ss->task)
                  Startparser(ss);
            }
            else
            {  short i;
               if(ss->task) { Adisposeobject(ss->task); ss->task=NULL; }
               if(ss->bitmap)
               {  if(P96Base && p96GetBitMapAttr(ss->bitmap,P96BMA_ISP96))
                     p96FreeBitMap(ss->bitmap);
                  else
                     FreeBitMap(ss->bitmap);
                  ss->bitmap=NULL;
               }
               if(ss->mask) { FreeVec(ss->mask); ss->mask=NULL; }
               if(ss->colormap)
               {  for(i=0;i<256;i++)
                  {  while(ss->allocated[i])
                     {  ReleasePen(ss->colormap,i);
                        ss->allocated[i]--;
                     }
                  }
                  ss->colormap=NULL;
               }
               ss->width=0;
               ss->height=0;
               ss->memory=0;
               ss->flags&=~SVGSF_IMAGEREADY;
               Asetattrs(ss->source,AOSRC_Memory,0,TAG_END);
            }
            break;
      }
   }
   return 0;
}

static ULONG Updatesource(struct Svgsource *ss, struct Amset *amset)
{  struct TagItem *tag,*tstate;
   BOOL notify=FALSE;
   BOOL parseready=FALSE;
   tstate=amset->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOSVG_Width:    ss->width=tag->ti_Data; notify=TRUE; break;
         case AOSVG_Height:   ss->height=tag->ti_Data; notify=TRUE; break;
         case AOSVG_Bitmap:
            ObtainSemaphore(&ss->sema);
            if(tag->ti_Data) ss->bitmap=(struct BitMap *)tag->ti_Data;
            ReleaseSemaphore(&ss->sema);
            notify=TRUE;
            break;
         case AOSVG_Mask:     ss->mask=(UBYTE *)tag->ti_Data; notify=TRUE; break;
         case AOSVG_Imgready:
            if(tag->ti_Data) { ss->flags|=SVGSF_IMAGEREADY; notify=TRUE; }
            else ss->flags&=~SVGSF_IMAGEREADY;
            break;
         case AOSVG_Parseready:
            if(tag->ti_Data) { ss->flags|=SVGSF_IMAGEREADY; parseready=TRUE; notify=TRUE; }
            break;
         case AOSVG_Memory:
            ss->memory+=tag->ti_Data;
            Asetattrs(ss->source,AOSRC_Memory,ss->memory,TAG_END);
            break;
      }
   }
   if(notify && ss->bitmap)
   {  Anotifyset(ss->source,
         AOSVG_Bitmap,ss->bitmap,
         AOSVG_Mask,ss->mask,
         AOSVG_Width,ss->width,
         AOSVG_Height,ss->height,
         AOSVG_Imgready,(ss->flags&SVGSF_IMAGEREADY)?TRUE:FALSE,
         AOSVG_Jsready,parseready?TRUE:FALSE,
         TAG_END);
   }
   return 0;
}

static struct Svgsource *Newsource(struct Amset *amset)
{  struct Svgsource *ss;
   ss=(struct Svgsource *)Allocobject(PluginBase->sourcedriver,sizeof(struct Svgsource),amset);
   if(!ss) return NULL;
   InitSemaphore(&ss->sema);
   NEWLIST(&ss->data);
   Aaddchild(Aweb(),(struct Aobject *)ss,AOREL_APP_USE_SCREEN);
   ss->width=0;
   ss->height=0;
   ss->bitmap=NULL;
   ss->mask=NULL;
   ss->memory=0;
   ss->flags=0;
   ss->source=NULL;
   ss->task=NULL;
   ss->colormap=NULL;
   ss->friendbitmap=NULL;
   memset(ss->allocated,0,sizeof(ss->allocated));
   Setsource(ss,amset);
   if(!(ss->flags&SVGSF_DISPLAYED))
   {  if(Agetattr(ss->source,AOSRC_Displayed))
         Asetattrs((struct Aobject *)ss,AOSDV_Displayed,TRUE,TAG_END);
   }
   return ss;
}

static void Releasedata(struct Svgsource *ss)
{  struct Datablock *db;
   /* Single AllocVec for struct + payload (see Srcupdatesource);
    * one FreeVec releases everything. */
   while((db=REMHEAD(&ss->data)))
   {  FreeVec(db);
   }
}

static void Disposesource(struct Svgsource *ss)
{  short i;
   if(ss->task) { Adisposeobject(ss->task); ss->task=NULL; }
   if(ss->bitmap)
   {  if(P96Base && p96GetBitMapAttr(ss->bitmap,P96BMA_ISP96))
         p96FreeBitMap(ss->bitmap);
      else
         FreeBitMap(ss->bitmap);
   }
   if(ss->mask) FreeVec(ss->mask);
   ss->bitmap=NULL;
   ss->mask=NULL;
   ss->width=0;
   ss->height=0;
   ss->flags&=~SVGSF_IMAGEREADY;
   ss->memory=0;
   Asetattrs(ss->source,AOSRC_Memory,0,TAG_END);
   if(ss->colormap)
   {  for(i=0;i<256;i++)
      {  while(ss->allocated[i])
         {  ReleasePen(ss->colormap,i);
            ss->allocated[i]--;
         }
      }
      ss->colormap=NULL;
   }
   Releasedata(ss);
   Aremchild(Aweb(),(struct Aobject *)ss,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_SOURCEDRIVER,(struct Aobject *)ss,AOM_DISPOSE);
}

static ULONG Addchildsource(struct Svgsource *ss, struct Amadd *amadd)
{  if(amadd->relation==AOREL_SRC_COPY && ss->bitmap)
   {  Asetattrs(amadd->child,
         AOSVG_Bitmap,ss->bitmap,
         AOSVG_Mask,ss->mask,
         AOSVG_Width,ss->width,
         AOSVG_Height,ss->height,
         AOSVG_Imgready,ss->flags&SVGSF_IMAGEREADY,
         AOSVG_Jsready,ss->flags&SVGSF_IMAGEREADY,
         TAG_END);
   }
   return 0;
}

static ULONG Srcupdatesource(struct Svgsource *ss, struct Amsrcupdate *amsrcupdate)
{  struct TagItem *tag,*tstate;
   UBYTE *data=NULL;
   long datalength=0;
   struct Datablock *db;
   BOOL eof=FALSE;
   AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)ss,(struct Amessage *)amsrcupdate);
   tstate=amsrcupdate->tags;
   while((tag=NextTagItem(&tstate)))
   {  switch(tag->ti_Tag)
      {  case AOURL_Data:       data=(UBYTE *)tag->ti_Data; break;
         case AOURL_Datalength: datalength=tag->ti_Data; break;
         case AOURL_Eof:
            if(tag->ti_Data)
            {  eof=TRUE;
               ObtainSemaphore(&ss->sema);
               ss->flags|=SVGSF_EOF;
               ReleaseSemaphore(&ss->sema);
            }
            break;
      }
   }
   if(data && datalength>0)
   {  /* Combined Datablock + payload allocation, matching the
       * refactored GIF/PNG plugins. */
      if(datalength>1024*1024) datalength=1024*1024;
      db=(struct Datablock *)AllocVec(sizeof(struct Datablock)+datalength,
                                       MEMF_PUBLIC|MEMF_CLEAR);
      if(db)
      {  db->data=(UBYTE *)(db+1);
         memcpy(db->data,data,datalength);
         db->length=datalength;
         ObtainSemaphore(&ss->sema);
         ADDTAIL(&ss->data,db);
         ReleaseSemaphore(&ss->sema);
      }
      if(!ss->task) Startparser(ss);
   }
   if((data && datalength>0) || eof)
   {  if(ss->task) Asetattrsasync(ss->task,AOSVG_Data,TRUE,TAG_END);
   }
   return 0;
}

__asm __saveds ULONG Dispatchsource(register __a0 struct Aobject *obj,
                                    register __a1 struct Amessage *amsg)
{  struct Svgsource *ss=(struct Svgsource *)obj;
   ULONG result=0;
   switch(amsg->method)
   {  case AOM_NEW:       result=(ULONG)Newsource((struct Amset *)amsg); break;
      case AOM_SET:       result=Setsource(ss,(struct Amset *)amsg); break;
      case AOM_GET:       result=Getsource(ss,(struct Amset *)amsg); break;
      case AOM_DISPOSE:   Disposesource(ss); break;
      case AOM_SRCUPDATE: result=Srcupdatesource(ss,(struct Amsrcupdate *)amsg); break;
      case AOM_UPDATE:    result=Updatesource(ss,(struct Amset *)amsg); break;
      case AOM_ADDCHILD:  result=Addchildsource(ss,(struct Amadd *)amsg); break;
      default:            result=AmethodasA(AOTP_SOURCEDRIVER,(struct Aobject *)ss,amsg); break;
   }
   return result;
}
