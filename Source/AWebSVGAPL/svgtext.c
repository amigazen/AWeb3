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

/* svgtext.c - Optional ttengine.library based text rendering.
 *
 * See svgtext.h for the architectural overview and the rationale for
 * the deferred-render design.  In short: <text> elements are parsed
 * during the SVG DOM walk in svgsource.c and appended to a list of
 * SvgTextRun structs; once the vector layer has been flushed to the
 * output BitMap, svgsource.c calls SvgTextRenderAll() which emits one
 * TT_Text call per run.
 *
 * Implementation notes
 * --------------------
 *   - This translation unit is the ONLY place in the SVG plugin that
 *     touches ttengine.library.  Every other file in the plugin
 *     remains buildable and linkable even when ttengine.library is
 *     missing at runtime: the SAS/C pragma link statements are
 *     compile-time-only (they generate inline JSR-via-library-base
 *     code that is never reached when TTEngineBase is NULL because
 *     SvgTextIsAvailable() short-circuits each entry point).
 *
 *   - All parsing is done in-place on the XML attribute values, which
 *     the XML parser (xmlparse.c) has already entity-decoded and NUL
 *     terminated.  We never modify the parser buffer here because the
 *     same attribute string may be referenced multiple times when
 *     the SVG uses <use> to instantiate the same <text> at multiple
 *     positions.
 *
 *   - C89 / ANSI C only: every local is declared at the top of its
 *     enclosing function or fresh `{}` block.  No mixed declarations
 *     and statements; no // line comments. */

#include "pluginlib.h"
#include "awebsvg.h"
#include "svgtext.h"
#include "xmlparse.h"

#include <exec/memory.h>
#include <graphics/text.h>
#include <intuition/intuitionbase.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/awebplugin.h>

#include <libraries/ttengine.h>
#include <clib/ttengine_protos.h>
#include <pragma/ttengine_lib.h>

#include <string.h>

/* ------------------------------------------------------------------ */
/* Diagnostic logging                                                  */
/* ------------------------------------------------------------------ */

/* Aprintf hits the same debug stream AWeb's own logging uses (the
 * "SVG[...]" facility is just a string prefix; AWeb passes the line
 * through to whatever the user has set up - launchpad CLI, sashimono
 * trace file, console, etc.).  Gated on DEBUG_PLUGINS so release
 * builds stay quiet - early <text> rendering was noisy by default
 * while we were chasing silent ttengine/font failure modes; that
 * work has stabilised so the diagnostic is now opt-in.  Flip the
 * define in awebsvg.h (or the smakefile) to bring the noise back. */
#ifdef DEBUG_PLUGINS
#define SVGT_LOG(args) do { if(AwebPluginBase) Aprintf args; } while(0)
#else
#define SVGT_LOG(args)
#endif

/* ------------------------------------------------------------------ */
/* Small ANSI helpers                                                  */
/* ------------------------------------------------------------------ */

/* Case-insensitive ASCII compare.  We don't lean on stricmp() because
 * SAS/C's prototype lives in <string.h> as the OS-side _stricmp and
 * its name varies between toolchains.  Inlining keeps the code
 * stand-alone. */
static int Strieq(const UBYTE *a, const char *b)
{  UBYTE ca, cb;
   while(*a && *b)
   {  ca=*a;
      cb=(UBYTE)*b;
      if(ca>='A' && ca<='Z') ca=(UBYTE)(ca-'A'+'a');
      if(cb>='A' && cb<='Z') cb=(UBYTE)(cb-'A'+'a');
      if(ca!=cb) return 0;
      a++; b++;
   }
   return (*a==0 && *b==0);
}

/* Case-insensitive token match: TRUE iff `a` (length >= n) starts
 * with `n` bytes that equal `b` (which is exactly `n` chars long).
 * Used by the in-place font-family list parser where the candidate
 * is not NUL terminated. */
static int StrieqN(const UBYTE *a, long alen, const char *b)
{  long i;
   long blen=0;
   while(b[blen]) blen++;
   if(alen!=blen) return 0;
   for(i=0;i<alen;i++)
   {  UBYTE ca=a[i], cb=(UBYTE)b[i];
      if(ca>='A' && ca<='Z') ca=(UBYTE)(ca-'A'+'a');
      if(cb>='A' && cb<='Z') cb=(UBYTE)(cb-'A'+'a');
      if(ca!=cb) return 0;
   }
   return 1;
}

/* Parse an optionally-signed decimal number with optional fractional
 * part and optional CSS unit suffix (px, pt, em, %, ...).  Used for
 * x/y and font-size attribute values.  Result is in raster-equivalent
 * units (px assumed for all suffixes - SVG plugin policy elsewhere).
 *
 * Sets *out to the integer value (rounded half-down) and returns the
 * advanced pointer.  If no digits were seen *out is left alone and
 * the input pointer is returned unchanged. */
static UBYTE *ParseLongUnits(UBYTE *p, LONG *out)
{  LONG sign=1;
   LONG ipart=0;
   LONG fpart=0;
   LONG fscale=1;
   UBYTE *save=p;
   int   any=0;
   if(!p) return p;
   while(*p==' '||*p=='\t'||*p==',') p++;
   if(*p=='+') p++;
   else if(*p=='-') { sign=-1; p++; }
   while(*p>='0' && *p<='9') { ipart=ipart*10+(*p-'0'); p++; any=1; }
   if(*p=='.')
   {  p++;
      while(*p>='0' && *p<='9' && fscale<10000)
      {  fpart=fpart*10+(*p-'0');
         fscale*=10;
         p++;
         any=1;
      }
      while(*p>='0' && *p<='9') p++;
   }
   if(!any) return save;
   /* Round to nearest integer using the fractional part. */
   if(fscale>1 && fpart*2>=fscale) ipart++;
   /* Swallow unit suffix without interpreting - %, px, pt, em, etc. */
   while((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || *p=='%') p++;
   *out = sign*ipart;
   return p;
}

/* Read the first number out of a possibly-multi-valued attribute (SVG
 * permits "x" / "y" on <text> to be a space-separated list applying
 * to successive glyphs; we always take the first). */
static LONG NumAttr1(struct XmlNode *node, const char *name, LONG def)
{  UBYTE *v=XmlAttrValue(node,name);
   LONG out=def;
   if(!v) return def;
   ParseLongUnits(v,&out);
   return out;
}

/* ------------------------------------------------------------------ */
/* Fixed-point matrix transform                                        */
/* ------------------------------------------------------------------ */

/* Match the inner workings of svgsource.c's fmul()/Mxform(): six
 * 16.16 LONGs ordered (a,b,c,d,e,f); maps (x,y) ->
 *      (a*x + c*y + e, b*x + d*y + f) and returns integer raster
 * pixel coordinates.  We inline the multiply rather than link the
 * caller's static helper. */
static LONG Fmul1616(LONG a, LONG b)
{  LONG  ah, bh;
   ULONG al, bl;
   ah = a >> 16;
   bh = b >> 16;
   al = (ULONG)a & 0xffffUL;
   bl = (ULONG)b & 0xffffUL;
   return ((ah * bh) << 16)
        + ah * (LONG)bl
        + (LONG)al * bh
        + (LONG)((al * bl) >> 16);
}

static void Mxform(const LONG *M, LONG x16, LONG y16,
   LONG *rx, LONG *ry)
{  LONG nx, ny;
   if(M[1]==0 && M[2]==0)
   {  nx = Fmul1616(M[0],x16) + M[4];
      ny = Fmul1616(M[3],y16) + M[5];
   }
   else
   {  nx = Fmul1616(M[0],x16) + Fmul1616(M[2],y16) + M[4];
      ny = Fmul1616(M[1],x16) + Fmul1616(M[3],y16) + M[5];
   }
   *rx = nx>>16;
   *ry = ny>>16;
}

/* Approximate the effective uniform scale of M.  Used to convert
 * font-size from user units to raster pixels under the current CTM.
 * Returns a 16.16 fixed-point factor that is the average of the X and
 * Y axis scales (sqrt of determinant would be more correct but
 * requires floating-point; the geometric mean shortcut is fine for
 * the orthogonal/near-orthogonal CTMs SVG files actually emit). */
static LONG MScale1616(const LONG *M)
{  LONG sx, sy;
   /* Column 0 length ~= |a| when matrix is axis-aligned.  For general
    * matrices we approximate with the larger of |a|,|c| (resp.
    * |b|,|d|).  Underestimates skewed scales but never reports zero
    * for a usable matrix, which matters because font-size 0 makes
    * TT_OpenFontA refuse the open. */
   sx = M[0]; if(sx<0) sx=-sx;
   {  LONG sc = M[2]; if(sc<0) sc=-sc;
      if(sc>sx) sx=sc;
   }
   sy = M[3]; if(sy<0) sy=-sy;
   {  LONG sc = M[1]; if(sc<0) sc=-sc;
      if(sc>sy) sy=sc;
   }
   return (sx+sy)>>1;
}

/* ------------------------------------------------------------------ */
/* Style cascade for font-related properties                           */
/* ------------------------------------------------------------------ */

/* Cumulative style state derived from the <text>'s presentation
 * attributes and inline style="..." declarations.
 *
 * Lifetime note: family_raw points either at an entity-decoded XML
 * attribute value (lives for the life of the decoder pool) or at a
 * pool-allocated copy made when the value came from an inline
 * style="..." declaration.  The inline-style helper destructively
 * NUL-terminates substrings of the attribute value and restores the
 * separator on the way out; keeping a raw pointer into that buffer
 * would alias the post-restore characters into the family list, so
 * for inline style we always make a stable pool copy first. */
struct FontStyle
{  UBYTE *family_raw;         /* unparsed font-family list           */
   LONG   size_units;         /* font-size in SVG user units, 0=auto */
   UBYTE  bold;
   UBYTE  italic;
   UBYTE  anchor;
   UBYTE  pad;
};

static void FsInit(struct FontStyle *fs)
{  fs->family_raw=NULL;
   fs->size_units=0;
   fs->bold=0;
   fs->italic=0;
   fs->anchor=SVGT_ANCHOR_START;
}

/* Pool-allocated NUL-terminated copy of an arbitrary byte buffer.
 * Used to stabilise the family-name pointer captured from an inline
 * style="..." declaration (see lifetime note above). */
static UBYTE *PoolStrdup(APTR pool, const UBYTE *src, long n)
{  UBYTE *dup;
   if(!src || n<0) return NULL;
   dup=(UBYTE *)AllocPooled(pool,(ULONG)(n+1));
   if(!dup) return NULL;
   if(n>0) memcpy(dup,src,(size_t)n);
   dup[n]=0;
   return dup;
}

/* Translate a font-weight value (CSS) to bold/normal.  Numeric
 * weights >= 600 count as bold (SVG/CSS bold thresholds). */
static UBYTE WeightIsBold(const UBYTE *v)
{  long n=0;
   const UBYTE *p=v;
   int any=0;
   if(!v) return 0;
   while(*p==' '||*p=='\t') p++;
   if(Strieq(p,"bold"))  return 1;
   if(Strieq(p,"bolder")) return 1;
   if(Strieq(p,"normal")) return 0;
   if(Strieq(p,"lighter")) return 0;
   while(*p>='0' && *p<='9') { n=n*10+(*p-'0'); p++; any=1; }
   if(!any) return 0;
   return (UBYTE)(n>=600);
}

static UBYTE StyleIsItalic(const UBYTE *v)
{  if(!v) return 0;
   while(*v==' '||*v=='\t') v++;
   if(Strieq(v,"italic")) return 1;
   if(Strieq(v,"oblique")) return 1;
   return 0;
}

static UBYTE ParseAnchor(const UBYTE *v)
{  if(!v) return SVGT_ANCHOR_START;
   while(*v==' '||*v=='\t') v++;
   if(Strieq(v,"middle")) return SVGT_ANCHOR_MIDDLE;
   if(Strieq(v,"end"))    return SVGT_ANCHOR_END;
   return SVGT_ANCHOR_START;
}

/* Apply one style declaration to fs.  `value` is NUL-terminated but
 * the terminator may be a temporary substitution into a longer
 * buffer (see FsApplyInlineStyle); when `stable_pool` is non-NULL the
 * helper makes a permanent pool copy of any string we need to keep
 * alive beyond the caller's restore.  When called with a NULL pool
 * the caller is guaranteeing the storage is already stable (i.e. the
 * value was returned by XmlAttrValue and lives for the life of the
 * decoder pool). */
static void FsApplyPair(struct FontStyle *fs, UBYTE *key, UBYTE *value,
   APTR stable_pool)
{  if(Strieq(key,"font-family"))
   {  if(stable_pool)
      {  long vlen=0;
         while(value[vlen]) vlen++;
         fs->family_raw=PoolStrdup(stable_pool,value,vlen);
      }
      else
      {  fs->family_raw=value;
      }
   }
   else if(Strieq(key,"font-size"))
   {  LONG n=0;
      ParseLongUnits(value,&n);
      if(n>0) fs->size_units=n;
   }
   else if(Strieq(key,"font-weight"))
   {  fs->bold=WeightIsBold(value);
   }
   else if(Strieq(key,"font-style"))
   {  fs->italic=StyleIsItalic(value);
   }
   else if(Strieq(key,"text-anchor"))
   {  fs->anchor=ParseAnchor(value);
   }
   else if(Strieq(key,"font"))
   {  /* CSS shorthand `font: italic bold 12px Family` - we parse just
       * enough to pick out the bold/italic keywords and the trailing
       * family.  Doing a proper CSS shorthand parser here is more
       * complexity than the realistic SVG content asks for. */
      UBYTE *p=value;
      UBYTE *tok;
      LONG nval=0;
      while(*p)
      {  while(*p==' '||*p=='\t') p++;
         if(!*p) break;
         tok=p;
         while(*p && *p!=' ' && *p!='\t' && *p!=',') p++;
         {  UBYTE save=*p;
            *p=0;
            if(Strieq(tok,"italic")||Strieq(tok,"oblique")) fs->italic=1;
            else if(Strieq(tok,"bold")) fs->bold=1;
            else if(*tok>='0' && *tok<='9')
            {  /* Likely the size token. */
               nval=0;
               ParseLongUnits(tok,&nval);
               if(nval>0) fs->size_units=nval;
            }
            else if(*tok>0)
            {  /* Any remaining tail is treated as the family list.
                * Same stable-pool rationale as above. */
               if(stable_pool)
               {  long tlen=0;
                  UBYTE *q=tok;
                  while(q[tlen]) tlen++;
                  fs->family_raw=PoolStrdup(stable_pool,tok,tlen);
               }
               else
               {  fs->family_raw=tok;
               }
            }
            *p=save;
            if(*p) p++;
         }
      }
   }
}

/* Tiny destructive splitter for `key: value; ...` declarations, in
 * the same shape as svgsource.c's Parsestyle() but inline so we don't
 * need to expose that helper across translation units.  `pool` is
 * passed through to FsApplyPair so captured family strings survive
 * the temporary NUL substitutions this routine performs. */
static void FsApplyInlineStyle(struct FontStyle *fs, UBYTE *str, APTR pool)
{  UBYTE *p=str;
   if(!p) return;
   while(*p)
   {  UBYTE *key, *val, *pend;
      while(*p==' '||*p=='\t'||*p==';'||*p=='\n'||*p=='\r') p++;
      if(!*p) break;
      key=p;
      while(*p && *p!=':' && *p!=';') p++;
      if(*p!=':') { while(*p && *p!=';') p++; continue; }
      pend=p;
      while(pend>key && (pend[-1]==' '||pend[-1]=='\t')) pend--;
      {  UBYTE saveK=*pend;
         *pend=0;
         p++;
         while(*p==' '||*p=='\t') p++;
         val=p;
         while(*p && *p!=';') p++;
         {  UBYTE *vend=p;
            UBYTE saveV;
            while(vend>val && (vend[-1]==' '||vend[-1]=='\t')) vend--;
            saveV=*vend;
            *vend=0;
            FsApplyPair(fs,key,val,pool);
            *vend=saveV;
         }
         *pend=saveK;
      }
      if(*p==';') p++;
   }
}

/* Read every font-related presentation attribute off `node` and the
 * in-line style="...".  Order matters: presentation attributes apply
 * first, then style="..." overrides (matches the SVG/CSS cascade for
 * the cases the SVG plugin actually encounters - see Applystyle() in
 * svgsource.c).  `pool` is used only for the inline-style path. */
static void FsReadFromNode(struct FontStyle *fs, struct XmlNode *node,
   APTR pool)
{  UBYTE *v;
   v=XmlAttrValue(node,"font-family"); if(v) fs->family_raw=v;
   v=XmlAttrValue(node,"font-size");
   if(v)
   {  LONG n=0;
      ParseLongUnits(v,&n);
      if(n>0) fs->size_units=n;
   }
   v=XmlAttrValue(node,"font-weight"); if(v) fs->bold=WeightIsBold(v);
   v=XmlAttrValue(node,"font-style");  if(v) fs->italic=StyleIsItalic(v);
   v=XmlAttrValue(node,"text-anchor"); if(v) fs->anchor=ParseAnchor(v);
   v=XmlAttrValue(node,"style");       if(v) FsApplyInlineStyle(fs,v,pool);
}

/* ------------------------------------------------------------------ */
/* Font-family list -> primary family + generic fallback               */
/* ------------------------------------------------------------------ */

/* Pick the first concrete (non-generic) family name from a CSS list
 * and decide an appropriate ttengine generic fallback based on any
 * generic seen later in the list.  Both returned strings are pool-
 * allocated copies, NUL terminated.
 *
 * Recognised CSS generics: serif, sans-serif, monospace, cursive,
 * fantasy.  Map "monospace" to ttengine's "monospaced" (this is the
 * spelling AWeb's HTML renderer uses, see ttengine.c in AWebAPL).
 *
 * If the list is entirely empty or contains only generics, the
 * primary family ends up being the generic itself - which is what
 * ttengine wants anyway. */
static void FamilyPick(APTR pool, const UBYTE *raw,
   UBYTE **out_primary, UBYTE **out_generic)
{  const UBYTE *p=raw;
   const UBYTE *tok_start;
   const UBYTE *tok_end;
   const char *generic_seen=NULL;
   const UBYTE *primary_start=NULL;
   long primary_len=0;
   *out_primary=NULL;
   *out_generic=NULL;
   if(!raw) return;
   while(*p)
   {  long len;
      UBYTE quote=0;
      /* Skip separators / whitespace */
      while(*p==',' || *p==' ' || *p=='\t' || *p=='\n' || *p=='\r') p++;
      if(!*p) break;
      /* Strip optional quoting */
      if(*p=='"' || *p=='\'') { quote=*p; p++; }
      tok_start=p;
      if(quote)
      {  while(*p && *p!=quote) p++;
         tok_end=p;
         if(*p==quote) p++;
      }
      else
      {  while(*p && *p!=',') p++;
         tok_end=p;
         while(tok_end>tok_start
            && (tok_end[-1]==' '||tok_end[-1]=='\t')) tok_end--;
      }
      len=(long)(tok_end-tok_start);
      if(len<=0) continue;
      /* Detect generics. */
      if(StrieqN(tok_start,len,"serif"))
      {  if(!generic_seen) generic_seen="serif";
         if(!primary_start) { primary_start=(const UBYTE *)"serif"; primary_len=5; }
         continue;
      }
      if(StrieqN(tok_start,len,"sans-serif"))
      {  if(!generic_seen) generic_seen="sans-serif";
         if(!primary_start) { primary_start=(const UBYTE *)"sans-serif"; primary_len=10; }
         continue;
      }
      if(StrieqN(tok_start,len,"monospace"))
      {  /* AWeb / ttengine spelling. */
         if(!generic_seen) generic_seen="monospaced";
         if(!primary_start) { primary_start=(const UBYTE *)"monospaced"; primary_len=10; }
         continue;
      }
      if(StrieqN(tok_start,len,"monospaced"))
      {  if(!generic_seen) generic_seen="monospaced";
         if(!primary_start) { primary_start=(const UBYTE *)"monospaced"; primary_len=10; }
         continue;
      }
      if(StrieqN(tok_start,len,"cursive")
      || StrieqN(tok_start,len,"fantasy"))
      {  if(!generic_seen) generic_seen="default";
         continue;
      }
      /* Any other token: the first non-generic wins as the primary
       * family.  Subsequent tokens still influence the generic
       * fallback if they happen to be generic. */
      if(!primary_start || primary_start==(const UBYTE *)"serif"
                       || primary_start==(const UBYTE *)"sans-serif"
                       || primary_start==(const UBYTE *)"monospaced")
      {  primary_start=tok_start;
         primary_len=len;
      }
   }
   if(primary_start && primary_len>0)
   {  UBYTE *dup=(UBYTE *)AllocPooled(pool,primary_len+1);
      if(dup)
      {  memcpy(dup,primary_start,primary_len);
         dup[primary_len]=0;
         *out_primary=dup;
      }
   }
   if(generic_seen)
   {  long gl=(long)strlen(generic_seen);
      UBYTE *dup=(UBYTE *)AllocPooled(pool,gl+1);
      if(dup)
      {  memcpy(dup,generic_seen,gl);
         dup[gl]=0;
         *out_generic=dup;
      }
   }
}

/* ------------------------------------------------------------------ */
/* Whitespace-collapsing UTF-8 buffer                                  */
/* ------------------------------------------------------------------ */

/* Append `n` bytes from `src` to `*buf` (capacity *cap, length *len),
 * growing the buffer geometrically.  Returns FALSE on alloc failure -
 * the caller should treat the run as truncated and bail out. */
static BOOL TbufAppend(APTR pool, UBYTE **buf, long *cap, long *len,
   const UBYTE *src, long n)
{  long need=*len+n+1;
   if(need>*cap)
   {  long ncap=*cap? *cap*2 : 64;
      UBYTE *nb;
      while(ncap<need) ncap*=2;
      nb=(UBYTE *)AllocPooled(pool,(ULONG)ncap);
      if(!nb) return FALSE;
      if(*len>0) memcpy(nb,*buf,(size_t)*len);
      *buf=nb;
      *cap=ncap;
   }
   memcpy(*buf+*len,src,(size_t)n);
   *len+=n;
   (*buf)[*len]=0;
   return TRUE;
}

/* Append a single space if the buffer's last byte was not already a
 * space.  Used for XML whitespace collapsing at element boundaries. */
static BOOL TbufAppendSpace(APTR pool, UBYTE **buf, long *cap, long *len)
{  const UBYTE sp=(UBYTE)' ';
   if(*len>0 && (*buf)[*len-1]==' ') return TRUE;
   return TbufAppend(pool,buf,cap,len,&sp,1);
}

/* Append normalised text from an XMLN_TEXT node: runs of whitespace
 * collapse to a single space, leading/trailing whitespace is kept as
 * a single space so that adjacent text/tspan/text sequences stay
 * separated.  Matches the default `xml:space` ("default") which is
 * what virtually every SVG icon uses; "preserve" support would mean
 * walking up the XML tree which is over-engineering for v1. */
static BOOL TbufAppendTextCollapsed(APTR pool, UBYTE **buf, long *cap,
   long *len, const UBYTE *src, long n)
{  long i;
   long start;
   int prev_was_ws;
   if(!src || n<=0) return TRUE;
   prev_was_ws=0;
   start=-1;
   for(i=0;i<n;i++)
   {  UBYTE c=src[i];
      int is_ws=(c==' '||c=='\t'||c=='\n'||c=='\r');
      if(is_ws)
      {  if(start>=0)
         {  if(!TbufAppend(pool,buf,cap,len,src+start,i-start)) return FALSE;
            start=-1;
         }
         if(!prev_was_ws)
         {  if(!TbufAppendSpace(pool,buf,cap,len)) return FALSE;
         }
         prev_was_ws=1;
      }
      else
      {  if(start<0) start=i;
         prev_was_ws=0;
      }
   }
   if(start>=0)
   {  if(!TbufAppend(pool,buf,cap,len,src+start,n-start)) return FALSE;
   }
   return TRUE;
}

/* Recurse into a <text>/<tspan> subtree, accumulating UTF-8 text into
 * *buf.  XML element kinds other than text and tspan are skipped so
 * embedded <title>/<desc> / <a> wrappers do not corrupt the visible
 * content. */
static BOOL TbufCollect(APTR pool, UBYTE **buf, long *cap, long *len,
   struct XmlNode *node)
{  struct XmlNode *child;
   if(!node) return TRUE;
   for(child=node->firstchild; child; child=child->nextsibling)
   {  if(child->type==XMLN_TEXT)
      {  if(child->text && child->textlen>0)
         {  if(!TbufAppendTextCollapsed(pool,buf,cap,len,
               child->text,child->textlen)) return FALSE;
         }
      }
      else if(child->type==XMLN_ELEMENT)
      {  if(XmlNameIs(child,"tspan") || XmlNameIs(child,"textPath")
          || XmlNameIs(child,"a"))
         {  if(!TbufCollect(pool,buf,cap,len,child)) return FALSE;
         }
         /* title/desc/metadata are non-rendering per SVG spec.
          * Anything unknown we ignore - safer than guessing it might
          * be text content. */
      }
   }
   return TRUE;
}

/* ------------------------------------------------------------------ */
/* UTF-8 glyph counter                                                 */
/* ------------------------------------------------------------------ */

/* Count Unicode codepoints in a UTF-8 byte string.  Counts every
 * non-continuation byte (continuation bytes start with bit pattern
 * 10xxxxxx).  Tolerates malformed sequences by counting them as
 * glyphs of their own; ttengine renders the U+FFFD replacement glyph
 * for any decode it can't make sense of. */
static LONG Utf8CountGlyphs(const UBYTE *s, LONG n)
{  LONG i;
   LONG g=0;
   for(i=0;i<n;i++)
   {  if((s[i] & 0xC0) != 0x80) g++;
   }
   return g;
}

/* ------------------------------------------------------------------ */
/* Public: SvgTextListInit / SvgTextIsAvailable                        */
/* ------------------------------------------------------------------ */

void SvgTextListInit(struct SvgTextList *list)
{  if(!list) return;
   list->head=NULL;
   list->tail=NULL;
   list->count=0;
}

BOOL SvgTextIsAvailable(void)
{  return TTEngineAvail;
}

/* ------------------------------------------------------------------ */
/* Public: SvgTextAccumulate                                           */
/* ------------------------------------------------------------------ */

void SvgTextAccumulate(APTR pool, struct SvgTextList *list,
   struct XmlNode *node, const LONG *M,
   ULONG fillrgb, BOOL fillvalid)
{  struct FontStyle fs;
   UBYTE *buf=NULL;
   long   buflen=0;
   long   bufcap=0;
   UBYTE *primary=NULL;
   UBYTE *generic=NULL;
   LONG   x_user=0, y_user=0;
   LONG   raster_x=0, raster_y=0;
   LONG   pix_size;
   LONG   scale16;
   struct SvgTextRun *run;
   LONG   glyphs;
   /* Cheap exits first. */
   if(!pool || !list || !node) return;
   if(!TTEngineAvail)
   {  SVGT_LOG(("SVG[text]: Accumulate skipped, ttengine unavailable\n"));
      return;
   }
   if(!fillvalid)
   {  SVGT_LOG(("SVG[text]: Accumulate skipped, fill disabled\n"));
      return;
   }
   /* Style + position. */
   FsInit(&fs);
   FsReadFromNode(&fs,node,pool);
   x_user = (NumAttr1(node,"x",0)) << 16;
   y_user = (NumAttr1(node,"y",0)) << 16;
   /* Map font-size from SVG user units to raster pixels through the
    * CTM scale.  When size is missing fall back to CSS medium (16px)
    * because returning early on every styleless <text> would erase
    * far too much real-world content. */
   if(fs.size_units<=0) fs.size_units=16;
   scale16=MScale1616(M);
   if(scale16<=0) scale16=0x10000L;
   pix_size = Fmul1616((fs.size_units<<16),scale16) >> 16;
   /* ttengine reasonable bounds; copied from pdffont.c. */
   if(pix_size<6) pix_size=6;
   if(pix_size>1024) pix_size=1024;
   /* Position in raster pixels (SVG y is baseline for <text>). */
   Mxform(M,x_user,y_user,&raster_x,&raster_y);
   /* Resolve family list -> primary + generic. */
   FamilyPick(pool,fs.family_raw,&primary,&generic);
   if(!primary)
   {  /* Default to the same generic the SVG anchor implies, falling
       * back to sans-serif when nothing was specified. */
      const char *def="sans-serif";
      long dl=(long)strlen(def);
      primary=(UBYTE *)AllocPooled(pool,dl+1);
      if(primary) { memcpy(primary,def,dl); primary[dl]=0; }
   }
   /* Collect text content (collapsed). */
   if(!TbufCollect(pool,&buf,&bufcap,&buflen,node))
   {  SVGT_LOG(("SVG[text]: Accumulate alloc failure\n"));
      return;
   }
   if(buflen<=0 || !buf)
   {  SVGT_LOG(("SVG[text]: Accumulate empty content\n"));
      return;
   }
   /* Trim trailing whitespace produced by the collapse rule. */
   while(buflen>0 && buf[buflen-1]==' ')
   {  buflen--;
      buf[buflen]=0;
   }
   if(buflen<=0) return;
   /* Trim leading whitespace too. */
   {  long lead=0;
      while(lead<buflen && buf[lead]==' ') lead++;
      if(lead>0)
      {  memmove(buf,buf+lead,(size_t)(buflen-lead));
         buflen-=lead;
         buf[buflen]=0;
      }
      if(buflen<=0) return;
   }
   glyphs=Utf8CountGlyphs(buf,buflen);
   if(glyphs<=0) return;
   /* Allocate the run. */
   run=(struct SvgTextRun *)AllocPooled(pool,sizeof(struct SvgTextRun));
   if(!run) return;
   run->next=NULL;
   run->family=primary;
   run->family2=generic;
   run->utf8=buf;
   run->utf8len=buflen;
   run->glyphs=glyphs;
   run->raster_x=raster_x;
   run->raster_y=raster_y;
   run->pix_size=pix_size;
   run->rgb=fillrgb;
   run->bold=fs.bold;
   run->italic=fs.italic;
   run->anchor=fs.anchor;
   run->pad=0;
   /* Append to list tail. */
   if(list->tail) list->tail->next=run;
   else           list->head=run;
   list->tail=run;
   list->count++;
   SVGT_LOG(("SVG[text]: queued #%ld at (%ld,%ld) size=%ld bold=%ld italic=%ld "
      "anchor=%ld family='%s' fb='%s' rgb=%06lx glyphs=%ld text='%s'\n",
      (long)list->count,(long)raster_x,(long)raster_y,(long)pix_size,
      (long)run->bold,(long)run->italic,(long)run->anchor,
      run->family?(char *)run->family:"(null)",
      run->family2?(char *)run->family2:"(null)",
      (unsigned long)run->rgb,(long)run->glyphs,
      (char *)run->utf8));
}

/* ------------------------------------------------------------------ */
/* Public: SvgTextRenderAll                                            */
/* ------------------------------------------------------------------ */

/* Open a ttengine font matching the run's family/weight/style/size.
 * Returns the TT handle or NULL on failure.  The caller is
 * responsible for TT_CloseFont. */
static UBYTE *MapFamilyAlias(UBYTE *family)
{  if(!family) return NULL;
   if(Strieq(family,"serif")) return (UBYTE *)"Times New Roman";
   if(Strieq(family,"sans-serif")) return (UBYTE *)"Arial";
   if(Strieq(family,"sans")) return (UBYTE *)"Arial";
   if(Strieq(family,"monospace")) return (UBYTE *)"Courier New";
   if(Strieq(family,"monospaced")) return (UBYTE *)"Courier New";
   if(Strieq(family,"courier")) return (UBYTE *)"Courier New";
   return family;
}

static APTR TryOpenFontFamilyTable(UBYTE **familytable, LONG pix_size,
   ULONG weight, ULONG style)
{  struct TagItem tags[5];
   tags[0].ti_Tag=TT_FamilyTable;
   tags[0].ti_Data=(ULONG)familytable;
   tags[1].ti_Tag=TT_FontSize;
   tags[1].ti_Data=(ULONG)pix_size;
   tags[2].ti_Tag=TT_FontWeight;
   tags[2].ti_Data=(ULONG)weight;
   tags[3].ti_Tag=TT_FontStyle;
   tags[3].ti_Data=(ULONG)style;
   tags[4].ti_Tag=TAG_END;
   return TT_OpenFontA(tags);
}

static APTR OpenFontForRun(struct SvgTextRun *run, struct Screen *screen)
{  UBYTE *familytable[8];
   UBYTE *mapped_primary;
   UBYTE *mapped_fallback;
   ULONG weight;
   ULONG style;
   APTR font;
   long fi=0;
   (void)screen;
   if(!TTEngineBase || !run) return NULL;
   /* Match AWeb's HTML ttengine wrapper more closely: TT_OpenFontA is
    * given only font-selection tags.  TT_Screen / TT_Encoding are
    * applied later with TT_SetAttrsA on the RastPort. */
   mapped_primary=MapFamilyAlias(run->family);
   mapped_fallback=MapFamilyAlias(run->family2);
   if(mapped_primary) familytable[fi++]=mapped_primary;
   if(mapped_fallback && (!mapped_primary
      || strcmp((char *)mapped_primary,(char *)mapped_fallback)!=0))
      familytable[fi++]=mapped_fallback;
   /* Generic and concrete fallbacks: ttengine DBs vary in what names
    * they register.  Keep both spellings so a CSS generic-only SVG can
    * still find the user's configured default family. */
   if(!mapped_fallback && run->family2) familytable[fi++]=run->family2;
   if(fi<7) familytable[fi++]=(UBYTE *)"Arial";
   if(fi<7) familytable[fi++]=(UBYTE *)"Times New Roman";
   if(fi<7) familytable[fi++]=(UBYTE *)"Courier New";
   if(fi<7) familytable[fi++]=(UBYTE *)"default";
   familytable[fi]=NULL;
   weight=(ULONG)(run->bold? TT_FontWeight_Bold : TT_FontWeight_Normal);
   style=(ULONG)(run->italic? TT_FontStyle_Italic : TT_FontStyle_Regular);
   font=TryOpenFontFamilyTable(familytable,run->pix_size,weight,style);
   if(font) return font;
   /* Some ttengine family DBs do not carry separate bold/italic
    * faces.  Retry regular-normal before giving up; SVG text is more
    * useful drawn with a regular face than not drawn at all. */
   if(weight!=TT_FontWeight_Normal || style!=TT_FontStyle_Regular)
      font=TryOpenFontFamilyTable(familytable,run->pix_size,
         TT_FontWeight_Normal,TT_FontStyle_Regular);
   return font;
}

/* Apply the standard rastport attribute set used during draw
 * (matches ApplyTtAttrs in pdffont.c).  TT_Screen is mandatory on
 * palette screens or ttengine writes invisible pixels; the antialias
 * and disk-font-metrics flags match what AWeb's HTML renderer asks
 * for. */
static void ApplyTtAttrs(struct RastPort *rp, struct Screen *scr)
{  struct TagItem posttags[6];
   long pidx=0;
   if(scr)
   {  posttags[pidx].ti_Tag=TT_Screen;
      posttags[pidx].ti_Data=(ULONG)scr;
      pidx++;
   }
   posttags[pidx].ti_Tag=TT_Antialias;
   posttags[pidx].ti_Data=(ULONG)TT_Antialias_Auto;
   pidx++;
   posttags[pidx].ti_Tag=TT_DiskFontMetrics;
   posttags[pidx].ti_Data=(ULONG)FALSE;
   pidx++;
   posttags[pidx].ti_Tag=TT_SoftStyle;
   posttags[pidx].ti_Data=(ULONG)TT_SoftStyle_None;
   pidx++;
   /* TT_Encoding is not an OpenFont tag in ttengine.library; the SDK
    * marks it as get/set/pixmap only.  Set it after TT_SetFont on the
    * RastPort, the same phase AWeb's HTML ttengine wrapper uses for
    * all dynamic raster-port attributes.  Passing TT_Encoding to
    * TT_OpenFontA made the library reject every SVG text font open. */
   posttags[pidx].ti_Tag=TT_Encoding;
   posttags[pidx].ti_Data=(ULONG)TT_Encoding_UTF8;
   pidx++;
   posttags[pidx].ti_Tag=TAG_END;
   TT_SetAttrsA(rp,posttags);
}

/* Apply text-anchor adjustment by measuring the rendered width with
 * TT_TextExtent and shifting the baseline left.  This MUST happen
 * after TT_SetFont so the font under measurement matches the one we
 * are about to draw. */
static void ApplyAnchorOffset(struct RastPort *rp, struct SvgTextRun *run,
   LONG *bx)
{  struct TextExtent te;
   LONG w;
   if(run->anchor==SVGT_ANCHOR_START) return;
   te.te_Width=0;
   te.te_Height=0;
   te.te_Extent.MinX=0;
   te.te_Extent.MinY=0;
   te.te_Extent.MaxX=0;
   te.te_Extent.MaxY=0;
   TT_TextExtent(rp,run->utf8,(WORD)run->glyphs,&te);
   w=(LONG)te.te_Width;
   if(w<=0) return;
   if(run->anchor==SVGT_ANCHOR_MIDDLE) *bx -= w>>1;
   else if(run->anchor==SVGT_ANCHOR_END) *bx -= w;
}

BOOL SvgTextRenderAll(struct RastPort *rp, struct Screen *screen,
   struct ColorMap *colormap, struct SvgTextList *list,
   UBYTE (*pen_obtain)(void *ctx, ULONG rgb), void *ctx)
{  struct SvgTextRun *run;
   BOOL any=FALSE;
   long n_open_fail=0;
   long n_setfont_fail=0;
   long n_rendered=0;
   (void)colormap;     /* presence reserved for future pen accounting */
   if(!TTEngineAvail)
   {  SVGT_LOG(("SVG[text]: RenderAll skipped, ttengine unavailable\n"));
      return FALSE;
   }
   if(!rp || !list || list->count<=0)
   {  SVGT_LOG(("SVG[text]: RenderAll nothing to draw rp=%lx count=%ld\n",
         (unsigned long)rp, list?(long)list->count:-1L));
      return FALSE;
   }
   SVGT_LOG(("SVG[text]: RenderAll start, %ld runs, screen=0x%lx\n",
      (long)list->count,(unsigned long)screen));
   for(run=list->head; run; run=run->next)
   {  APTR tthandle;
      UBYTE pen=1;
      LONG bx;
      LONG by;
      if(!run->utf8 || run->utf8len<=0 || run->glyphs<=0) continue;
      tthandle=OpenFontForRun(run,screen);
      if(!tthandle)
      {  n_open_fail++;
         SVGT_LOG(("SVG[text]: TT_OpenFontA failed family='%s' fb='%s' size=%ld\n",
            run->family?(char *)run->family:"(null)",
            run->family2?(char *)run->family2:"(null)",
            (long)run->pix_size));
         continue;
      }
      if(!TT_SetFont(rp,tthandle))
      {  n_setfont_fail++;
         SVGT_LOG(("SVG[text]: TT_SetFont failed tthandle=0x%lx rp=0x%lx\n",
            (unsigned long)tthandle,(unsigned long)rp));
         TT_CloseFont(tthandle);
         continue;
      }
      ApplyTtAttrs(rp,screen);
      bx=run->raster_x;
      by=run->raster_y;
      ApplyAnchorOffset(rp,run,&bx);
      if(pen_obtain) pen=pen_obtain(ctx,run->rgb);
      SetAPen(rp,pen);
      SetDrMd(rp,JAM1);
      Move(rp,(WORD)bx,(WORD)by);
      TT_Text(rp,run->utf8,(ULONG)run->glyphs);
      TT_CloseFont(tthandle);
      any=TRUE;
      n_rendered++;
   }
   SVGT_LOG(("SVG[text]: RenderAll done, rendered=%ld open_fail=%ld setfont_fail=%ld\n",
      n_rendered,n_open_fail,n_setfont_fail));
   return any;
}
