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

/* xmlparse.c - Small DOM-style XML parser for the SVG plugin.
 *
 * See xmlparse.h for the public API.
 *
 * The parser does a single linear pass over the buffer, mutating it in
 * place: separator bytes are replaced with NULs so that names can be
 * used directly as C strings.  Attribute values and text nodes are
 * entity-decoded into pool memory, which lets SVGs that use internal
 * DTD entities for long style strings expand correctly.
 *
 * No floating point, no library calls beyond exec.library memory pool
 * operations - everything else is plain ANSI C / C89 so the code is
 * easy to verify on the 68k target. */

#include "xmlparse.h"

#include <exec/memory.h>
#include <proto/exec.h>

/* Local helpers -----------------------------------------------------*/

static int isspace_x(UBYTE c)
{  return c==' '||c=='\t'||c=='\n'||c=='\r';
}

static int isnamestart_x(UBYTE c)
{  return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||c==':';
}

static int isnamechar_x(UBYTE c)
{  return isnamestart_x(c)||(c>='0'&&c<='9')||c=='-'||c=='.';
}

static int tolower_x(UBYTE c)
{  if(c>='A'&&c<='Z') return c-'A'+'a';
   return c;
}

static int stricmp_x(const UBYTE *a, const char *b)
{  while(*a && *b)
   {  int ca=tolower_x(*a);
      int cb=tolower_x((UBYTE)*b);
      if(ca!=cb) return ca-cb;
      a++;
      b++;
   }
   return tolower_x(*a)-tolower_x((UBYTE)*b);
}

static int strnicmp_x(const UBYTE *a, const char *b, LONG n)
{  LONG i;
   for(i=0;i<n;i++)
   {  int ca=tolower_x(a[i]);
      int cb=tolower_x((UBYTE)b[i]);
      if(ca!=cb) return ca-cb;
      if(!a[i] || !b[i]) return ca-cb;
   }
   return 0;
}

struct XmlEntity
{  struct XmlEntity *next;
   UBYTE *name;
   UBYTE *value;
   LONG namelen;
   LONG valuelen;
};

struct Decodebuf
{  APTR pool;
   UBYTE *buf;
   LONG len;
   LONG cap;
   BOOL failed;
};

/* Allocate a zeroed XmlNode from the pool. */
static struct XmlNode *Newnode(APTR pool, USHORT type)
{  struct XmlNode *n;
   n=(struct XmlNode *)AllocPooled(pool,sizeof(struct XmlNode));
   if(!n) return NULL;
   n->type=type;
   n->pad=0;
   n->name=NULL;
   n->text=NULL;
   n->namelen=0;
   n->textlen=0;
   n->attrs=NULL;
   n->parent=NULL;
   n->firstchild=NULL;
   n->lastchild=NULL;
   n->nextsibling=NULL;
   return n;
}

static struct XmlAttr *Newattr(APTR pool)
{  struct XmlAttr *a;
   a=(struct XmlAttr *)AllocPooled(pool,sizeof(struct XmlAttr));
   if(!a) return NULL;
   a->next=NULL;
   a->name=NULL;
   a->value=NULL;
   a->namelen=0;
   a->valuelen=0;
   return a;
}

static void Appendchild(struct XmlNode *parent, struct XmlNode *child)
{  child->parent=parent;
   if(parent->lastchild)
   {  parent->lastchild->nextsibling=child;
   }
   else
   {  parent->firstchild=child;
   }
   parent->lastchild=child;
}

/* Encode a Unicode scalar value as UTF-8.  Returns byte count written,
 * or 0 for invalid scalar values.  Numeric XML entities in SVG often
 * use punctuation above Latin-1 (e.g. &#8211; and &#8220;); emitting
 * UTF-8 here lets ttengine.library receive the same encoding that the
 * SVG text renderer asks for with TT_Encoding_UTF8. */
static LONG Utf8encode(ULONG val, UBYTE *dst)
{  if(val==0 || val>0x10FFFFUL) return 0;
   if(val>=0xD800UL && val<=0xDFFFUL) return 0;
   if(val<0x80UL)
   {  dst[0]=(UBYTE)val;
      return 1;
   }
   if(val<0x800UL)
   {  dst[0]=(UBYTE)(0xC0 | (val>>6));
      dst[1]=(UBYTE)(0x80 | (val&0x3FUL));
      return 2;
   }
   if(val<0x10000UL)
   {  dst[0]=(UBYTE)(0xE0 | (val>>12));
      dst[1]=(UBYTE)(0x80 | ((val>>6)&0x3FUL));
      dst[2]=(UBYTE)(0x80 | (val&0x3FUL));
      return 3;
   }
   dst[0]=(UBYTE)(0xF0 | (val>>18));
   dst[1]=(UBYTE)(0x80 | ((val>>12)&0x3FUL));
   dst[2]=(UBYTE)(0x80 | ((val>>6)&0x3FUL));
   dst[3]=(UBYTE)(0x80 | (val&0x3FUL));
   return 4;
}

static BOOL Dbinit(struct Decodebuf *db, APTR pool, LONG cap)
{  if(cap<16) cap=16;
   db->pool=pool;
   db->len=0;
   db->cap=cap;
   db->failed=FALSE;
   db->buf=(UBYTE *)AllocPooled(pool,(ULONG)(cap+1));
   if(!db->buf)
   {  db->cap=0;
      db->failed=TRUE;
      return FALSE;
   }
   db->buf[0]=0;
   return TRUE;
}

static BOOL Dbgrow(struct Decodebuf *db, LONG need)
{  LONG newcap;
   UBYTE *nbuf;
   if(db->failed) return FALSE;
   if(need<=db->cap) return TRUE;
   newcap=db->cap;
   while(newcap<need)
   {  if(newcap>0x3fffffffL)
      {  db->failed=TRUE;
         return FALSE;
      }
      newcap*=2;
   }
   nbuf=(UBYTE *)AllocPooled(db->pool,(ULONG)(newcap+1));
   if(!nbuf)
   {  db->failed=TRUE;
      return FALSE;
   }
   if(db->len>0) CopyMem(db->buf,nbuf,db->len);
   db->buf=nbuf;
   db->cap=newcap;
   return TRUE;
}

static BOOL Dbputc(struct Decodebuf *db, UBYTE c)
{  if(!Dbgrow(db,db->len+1)) return FALSE;
   db->buf[db->len++]=c;
   return TRUE;
}

static BOOL Dbputmem(struct Decodebuf *db, const UBYTE *src, LONG len)
{  if(len<=0) return TRUE;
   if(!Dbgrow(db,db->len+len)) return FALSE;
   CopyMem((APTR)src,db->buf+db->len,len);
   db->len+=len;
   return TRUE;
}

static struct XmlEntity *Findentity(struct XmlEntity *entities,
   const UBYTE *name, LONG namelen)
{  struct XmlEntity *e;
   for(e=entities;e;e=e->next)
   {  if(e->namelen==namelen)
      {  LONG i;
         BOOL same=TRUE;
         for(i=0;i<namelen;i++)
         {  if(e->name[i]!=name[i]) { same=FALSE; break; }
         }
         if(same) return e;
      }
   }
   return NULL;
}

/* Decode XML entities into pool memory.  The built-in XML entities and
 * numeric decimal/hex entities are handled directly; internal DTD
 * general entities are looked up in `entities`.  SVG authoring tools
 * often use those entities to share long style strings, so decoding
 * cannot be limited to in-place shrinking. */
static BOOL Decodeentities(APTR pool, UBYTE *str, LONG length,
   struct XmlEntity *entities, UBYTE **outstr, LONG *outlen)
{  UBYTE *src=str;
   UBYTE *end=str+length;
   UBYTE *p;
   UBYTE *semi;
   UBYTE *ename;
   UBYTE out[4];
   UBYTE c;
   ULONG val;
   LONG i;
   LONG j;
   LONG elen;
   LONG blen;
   LONG boutlen;
   BOOL ok;
   BOOL have_digit;
   struct Decodebuf db;
   struct XmlEntity *ent;

   *outstr=NULL;
   *outlen=0;
   if(!Dbinit(&db,pool,length)) return FALSE;
   while(src<end)
   {  if(*src!='&')
      {  if(!Dbputc(&db,*src++)) return FALSE;
         continue;
      }
      p=src+1;
      semi=NULL;
      for(i=0;i<64 && p<end;i++,p++)
      {  if(*p==';') { semi=p; break; }
      }
      if(!semi)
      {  if(!Dbputc(&db,*src++)) return FALSE;
         continue;
      }
      ename=src+1;
      elen=(LONG)(semi-ename);
      ent=Findentity(entities,ename,elen);
      if(ent)
      {  if(!Dbputmem(&db,ent->value,ent->valuelen)) return FALSE;
         src=semi+1;
         continue;
      }
      boutlen=0;
      ok=FALSE;
      if(elen==2 && ename[0]=='l' && ename[1]=='t') { out[0]='<'; boutlen=1; ok=TRUE; }
      else if(elen==2 && ename[0]=='g' && ename[1]=='t') { out[0]='>'; boutlen=1; ok=TRUE; }
      else if(elen==3 && ename[0]=='a' && ename[1]=='m' && ename[2]=='p') { out[0]='&'; boutlen=1; ok=TRUE; }
      else if(elen==4 && ename[0]=='q' && ename[1]=='u' && ename[2]=='o' && ename[3]=='t') { out[0]='"'; boutlen=1; ok=TRUE; }
      else if(elen==4 && ename[0]=='a' && ename[1]=='p' && ename[2]=='o' && ename[3]=='s') { out[0]='\''; boutlen=1; ok=TRUE; }
      else if(elen>=2 && ename[0]=='#')
      {  val=0;
         have_digit=FALSE;
         if(ename[1]=='x' || ename[1]=='X')
         {  for(j=2;j<elen;j++)
            {  c=ename[j];
               if(c>='0'&&c<='9') val=(val<<4)|(c-'0');
               else if(c>='a'&&c<='f') val=(val<<4)|(c-'a'+10);
               else if(c>='A'&&c<='F') val=(val<<4)|(c-'A'+10);
               else { val=0; have_digit=FALSE; break; }
               have_digit=TRUE;
            }
         }
         else
         {  for(j=1;j<elen;j++)
            {  c=ename[j];
               if(c<'0'||c>'9') { val=0; have_digit=FALSE; break; }
               val=val*10+(c-'0');
               have_digit=TRUE;
            }
         }
         if(have_digit)
         {  boutlen=Utf8encode(val,out);
            if(boutlen>0) ok=TRUE;
         }
      }
      if(ok)
      {  for(j=0;j<boutlen;j++)
         {  if(!Dbputc(&db,out[j])) return FALSE;
         }
         src=semi+1;
      }
      else
      {  blen=(LONG)(semi-src)+1;
         if(!Dbputmem(&db,src,blen)) return FALSE;
         src=semi+1;
      }
   }
   if(!Dbgrow(&db,db.len+1)) return FALSE;
   db.buf[db.len]=0;
   *outstr=db.buf;
   *outlen=db.len;
   return TRUE;
}

static BOOL Isdoctype(UBYTE *p, UBYTE *end)
{  if(p+9>=end) return FALSE;
   if(p[0]!='<' || p[1]!='!') return FALSE;
   return (BOOL)(strnicmp_x(p+2,"DOCTYPE",7)==0);
}

static UBYTE *Scandeclend(UBYTE *p, UBYTE *end,
   UBYTE **subsetstart, UBYTE **subsetend)
{  LONG depth=0;
   UBYTE quote=0;
   UBYTE *q=p+2;
   *subsetstart=NULL;
   *subsetend=NULL;
   while(q<end)
   {  if(quote)
      {  if(*q==quote) quote=0;
      }
      else if(*q=='"' || *q=='\'')
      {  quote=*q;
      }
      else if(*q=='[')
      {  if(depth==0) *subsetstart=q+1;
         depth++;
      }
      else if(*q==']' && depth>0)
      {  depth--;
         if(depth==0) *subsetend=q;
      }
      else if(*q=='>' && depth==0)
      {  return q+1;
      }
      q++;
   }
   return end;
}

static BOOL Addentity(APTR pool, struct XmlEntity **entities,
   UBYTE *name, LONG namelen, UBYTE *value, LONG valuelen)
{  struct XmlEntity *ent;
   UBYTE *ncopy;
   UBYTE *vcopy;
   LONG vlen;
   LONG i;
   if(namelen<=0) return TRUE;
   ncopy=(UBYTE *)AllocPooled(pool,(ULONG)(namelen+1));
   if(!ncopy) return FALSE;
   for(i=0;i<namelen;i++) ncopy[i]=name[i];
   ncopy[namelen]=0;
   if(!Decodeentities(pool,value,valuelen,*entities,&vcopy,&vlen))
      return FALSE;
   ent=(struct XmlEntity *)AllocPooled(pool,sizeof(struct XmlEntity));
   if(!ent) return FALSE;
   ent->name=ncopy;
   ent->namelen=namelen;
   ent->value=vcopy;
   ent->valuelen=vlen;
   ent->next=*entities;
   *entities=ent;
   return TRUE;
}

static void Parsedoctypeentities(APTR pool, UBYTE *decl, UBYTE *end,
   struct XmlEntity **entities)
{  UBYTE *subsetstart;
   UBYTE *subsetend;
   UBYTE *after;
   UBYTE *p;
   after=Scandeclend(decl,end,&subsetstart,&subsetend);
   (void)after;
   if(!subsetstart || !subsetend || subsetstart>=subsetend) return;
   p=subsetstart;
   while(p<subsetend)
   {  if(p+8<subsetend && p[0]=='<' && p[1]=='!'
      && strnicmp_x(p+2,"ENTITY",6)==0
      && isspace_x(p[8]))
      {  UBYTE *q=p+9;
         UBYTE *namestart;
         UBYTE *nameend;
         UBYTE *valuestart;
         UBYTE quote;
         while(q<subsetend && isspace_x(*q)) q++;
         if(q<subsetend && *q=='%')
         {  while(q<subsetend && *q!='>') q++;
            if(q<subsetend) q++;
            p=q;
            continue;
         }
         if(q>=subsetend || !isnamestart_x(*q))
         {  p++;
            continue;
         }
         namestart=q;
         while(q<subsetend && isnamechar_x(*q)) q++;
         nameend=q;
         while(q<subsetend && isspace_x(*q)) q++;
         if(q>=subsetend || (*q!='"' && *q!='\''))
         {  while(q<subsetend && *q!='>') q++;
            if(q<subsetend) q++;
            p=q;
            continue;
         }
         quote=*q++;
         valuestart=q;
         while(q<subsetend && *q!=quote) q++;
         if(q<subsetend)
         {  if(!Addentity(pool,entities,namestart,(LONG)(nameend-namestart),
               valuestart,(LONG)(q-valuestart)))
            {  return;
            }
            q++;
         }
         while(q<subsetend && *q!='>') q++;
         if(q<subsetend) q++;
         p=q;
      }
      else p++;
   }
}

/* Skip XML declaration <?xml ... ?>, processing instruction <?...?>,
 * comment <!-- ... -->, doctype <!DOCTYPE ...>, or CDATA <![CDATA[...]]>.
 * Returns pointer just past the construct, or NULL if not at one. */
static UBYTE *Skipprolog(UBYTE *p, UBYTE *end)
{  if(p+1>=end) return NULL;
   if(p[0]!='<') return NULL;
   if(p+3<end && p[1]=='!' && p[2]=='-' && p[3]=='-')
   {  /* Comment */
      p+=4;
      while(p+2<end)
      {  if(p[0]=='-' && p[1]=='-' && p[2]=='>') return p+3;
         p++;
      }
      return end;
   }
   if(p+1<end && p[1]=='?')
   {  /* PI or XML decl */
      p+=2;
      while(p+1<end)
      {  if(p[0]=='?' && p[1]=='>') return p+2;
         p++;
      }
      return end;
   }
   if(p+8<end && p[1]=='!' && p[2]=='[' && p[3]=='C' && p[4]=='D'
   && p[5]=='A' && p[6]=='T' && p[7]=='A' && p[8]=='[')
   {  /* CDATA - we don't preserve it as a separate node; the content
       * is left in the buffer with the wrapper marks still in place,
       * but the trailing ]]> ends our return here so the next call
       * resumes after it.  In practice SVG rarely contains CDATA. */
      p+=9;
      while(p+2<end)
      {  if(p[0]==']' && p[1]==']' && p[2]=='>') return p+3;
         p++;
      }
      return end;
   }
   if(p+1<end && p[1]=='!')
   {  UBYTE *subsetstart;
      UBYTE *subsetend;
      return Scandeclend(p,end,&subsetstart,&subsetend);
   }
   return NULL;
}

/* Parse a single attribute starting at *pp.  Returns TRUE on success
 * and advances *pp past the attribute; returns FALSE if no further
 * attribute is found (e.g. we hit '>' or '/').  On success the
 * attribute is appended to node->attrs. */
static BOOL Parseattr(APTR pool, struct XmlNode *node, UBYTE **pp,
   UBYTE *end, struct XmlEntity *entities)
{  UBYTE *p=*pp;
   UBYTE *namestart;
   UBYTE *valuestart;
   UBYTE quote;
   struct XmlAttr *a;
   struct XmlAttr **tail;

   while(p<end && isspace_x(*p)) p++;
   if(p>=end) { *pp=p; return FALSE; }
   if(*p=='>' || *p=='/') { *pp=p; return FALSE; }
   if(!isnamestart_x(*p))
   {  /* Skip unrecognised junk char to avoid infinite loop. */
      p++;
      *pp=p;
      return FALSE;
   }

   namestart=p;
   while(p<end && isnamechar_x(*p)) p++;
   if(p>=end) { *pp=p; return FALSE; }
   {  /* Terminate the name in place.  If the next char is '=' we
       * overwrite it later; otherwise we overwrite the whitespace. */
      UBYTE *nameend=p;
      LONG namelen=(LONG)(nameend-namestart);
      while(p<end && isspace_x(*p)) p++;
      if(p>=end || *p!='=')
      {  /* Attribute without value - rare in XML, common in HTML.
          * We accept it as empty-string value. */
         *nameend=0;
         a=Newattr(pool);
         if(!a) { *pp=p; return FALSE; }
         a->name=namestart;
         a->namelen=namelen;
         a->value=(UBYTE *)"";
         a->valuelen=0;
         goto append;
      }
      *nameend=0;
      p++; /* past '=' */
      while(p<end && isspace_x(*p)) p++;
      if(p>=end || (*p!='"' && *p!='\''))
      {  /* Unquoted value - read until whitespace or '>'.  We cannot
          * NUL-terminate in place because the terminator byte is
          * meaningful to the outer parse loop, so we duplicate the
          * value into pool memory. */
         valuestart=p;
         while(p<end && !isspace_x(*p) && *p!='>' && *p!='/') p++;
         {  UBYTE *valueend=p;
            LONG valuelen=(LONG)(valueend-valuestart);
            UBYTE *vcopy;
            UBYTE *decoded;
            LONG decodedlen;
            vcopy=(UBYTE *)AllocPooled(pool,(ULONG)(valuelen+1));
            if(!vcopy) { *pp=p; return FALSE; }
            if(valuelen>0) CopyMem(valuestart,vcopy,valuelen);
            vcopy[valuelen]=0;
            if(!Decodeentities(pool,vcopy,valuelen,entities,
               &decoded,&decodedlen))
            {  *pp=p;
               return FALSE;
            }
            a=Newattr(pool);
            if(!a) { *pp=p; return FALSE; }
            a->name=namestart;
            a->namelen=namelen;
            a->value=decoded;
            a->valuelen=decodedlen;
            goto append;
         }
      }
      quote=*p++;
      valuestart=p;
      while(p<end && *p!=quote) p++;
      if(p>=end) { *pp=p; return FALSE; }
      {  UBYTE *valueend=p;
         LONG valuelen=(LONG)(valueend-valuestart);
         UBYTE *decoded;
         LONG decodedlen;
         *valueend=0;
         if(!Decodeentities(pool,valuestart,valuelen,entities,
            &decoded,&decodedlen))
         {  *pp=p;
            return FALSE;
         }
         p++; /* past closing quote */
         a=Newattr(pool);
         if(!a) { *pp=p; return FALSE; }
         a->name=namestart;
         a->namelen=namelen;
         a->value=decoded;
         a->valuelen=decodedlen;
         goto append;
      }
   }

append:
   /* Append to the tail of node->attrs.  Order is preserved which is
    * useful for debugging though not strictly required. */
   tail=&node->attrs;
   while(*tail) tail=&((*tail)->next);
   *tail=a;
   *pp=p;
   return TRUE;
}

/* Parse one element starting at '<...'.  Returns the element node, or
 * NULL on out-of-memory.  *pp is advanced past the closing tag (or
 * past the '/>' for an empty element).  If this routine consumes an
 * end tag that does not match, it stops and returns whatever it has;
 * the parent recursion picks up. */
static struct XmlNode *Parseelement(APTR pool, UBYTE **pp, UBYTE *end,
   struct XmlEntity *entities);

/* Parse the children of an element until we see a matching </name>
 * or hit EOF.  parent is the open element. */
static void Parsechildren(APTR pool, struct XmlNode *parent, UBYTE **pp,
   UBYTE *end, struct XmlEntity *entities)
{  UBYTE *p=*pp;
   BOOL keep_ws_text=FALSE;
   if(parent && parent->name)
   {  if(stricmp_x(parent->name,"text")==0
      || stricmp_x(parent->name,"tspan")==0
      || stricmp_x(parent->name,"textPath")==0
      || stricmp_x(parent->name,"a")==0)
      {  keep_ws_text=TRUE;
      }
   }
   while(p<end)
   {  /* Text content until next '<'. */
      if(*p!='<')
      {  UBYTE *textstart=p;
         while(p<end && *p!='<') p++;
         {  LONG textlen=(LONG)(p-textstart);
            BOOL allspace=TRUE;
            LONG i;
            for(i=0;i<textlen;i++)
            {  if(!isspace_x(textstart[i])) { allspace=FALSE; break; }
            }
            if(!allspace || keep_ws_text)
            {  struct XmlNode *t=Newnode(pool,XMLN_TEXT);
               if(t)
               {  LONG newlen;
                  UBYTE *decoded;
                  if(!Decodeentities(pool,textstart,textlen,entities,
                     &decoded,&newlen))
                  {  *pp=p;
                     return;
                  }
                  t->text=decoded;
                  t->textlen=newlen;
                  Appendchild(parent,t);
               }
            }
         }
         continue;
      }
      /* Skip prolog/comment/PI/CDATA/DOCTYPE before treating as a tag. */
      {  UBYTE *after=Skipprolog(p,end);
         if(after)
         {  p=after;
            continue;
         }
      }
      /* End tag? */
      if(p+1<end && p[1]=='/')
      {  /* Consume </name> and return to caller. */
         p+=2;
         while(p<end && isnamechar_x(*p)) p++;
         while(p<end && *p!='>') p++;
         if(p<end) p++;
         *pp=p;
         return;
      }
      /* Start tag - recurse. */
      {  struct XmlNode *child=Parseelement(pool,&p,end,entities);
         if(child) Appendchild(parent,child);
         else break;
      }
   }
   *pp=p;
}

static struct XmlNode *Parseelement(APTR pool, UBYTE **pp, UBYTE *end,
   struct XmlEntity *entities)
{  UBYTE *p=*pp;
   struct XmlNode *node;
   UBYTE *namestart;
   UBYTE *nameend;
   UBYTE nameend_save;
   UBYTE tagclose;

   if(p>=end || *p!='<') return NULL;
   p++;
   while(p<end && isspace_x(*p)) p++;
   if(p>=end || !isnamestart_x(*p)) return NULL;

   namestart=p;
   while(p<end && isnamechar_x(*p)) p++;
   nameend=p;

   node=Newnode(pool,XMLN_ELEMENT);
   if(!node) return NULL;
   node->name=namestart;
   node->namelen=(LONG)(nameend-namestart);
   nameend_save=*nameend;

   /* Parse attributes. */
   while(p<end)
   {  while(p<end && isspace_x(*p)) p++;
      if(p>=end) break;
      if(*p=='/' || *p=='>') break;
      if(!Parseattr(pool,node,&p,end,entities)) break;
   }

   /* Now NUL-terminate the name.  If the element has no attributes
    * nameend is the actual tag closer ('>' or '/'), so keep its saved
    * value for the close handling below.  Without this, a tag like
    * <tspan> had its '>' overwritten before p advanced past it, and
    * the child text node began with a NUL byte.  ttengine rendered
    * that NUL as a missing-glyph box before every tspan. */
   *nameend=0;

   tagclose=(p==nameend)?nameend_save:*p;
   if(p<end && tagclose=='/')
   {  /* Empty element. */
      p++;
      while(p<end && *p!='>') p++;
      if(p<end) p++;
      *pp=p;
      return node;
   }
   if(p<end && tagclose=='>') p++;

   /* Recurse into children. */
   Parsechildren(pool,node,&p,end,entities);
   *pp=p;
   return node;
}

/* Public API --------------------------------------------------------*/

struct XmlNode *XmlParse(APTR pool, UBYTE *buffer, LONG length)
{  UBYTE *p=buffer;
   UBYTE *end=buffer+length;
   struct XmlNode *root=NULL;
   struct XmlEntity *entities=NULL;

   if(!pool || !buffer || length<=0) return NULL;

   while(p<end)
   {  while(p<end && isspace_x(*p)) p++;
      if(p>=end) break;
      if(*p!='<')
      {  /* Skip stray text outside any element. */
         while(p<end && *p!='<') p++;
         continue;
      }
      if(Isdoctype(p,end))
      {  UBYTE *subsetstart;
         UBYTE *subsetend;
         Parsedoctypeentities(pool,p,end,&entities);
         p=Scandeclend(p,end,&subsetstart,&subsetend);
         continue;
      }
      {  UBYTE *after=Skipprolog(p,end);
         if(after) { p=after; continue; }
      }
      if(p+1<end && p[1]=='/')
      {  /* Orphan end tag - skip past it. */
         while(p<end && *p!='>') p++;
         if(p<end) p++;
         continue;
      }
      root=Parseelement(pool,&p,end,entities);
      if(root) break;
      /* Failed to parse - advance one char and try again. */
      p++;
   }

   return root;
}

UBYTE *XmlAttrValueLen(struct XmlNode *node, const char *name, LONG *length)
{  struct XmlAttr *a;
   if(!node) { if(length) *length=0; return NULL; }
   for(a=node->attrs;a;a=a->next)
   {  if(stricmp_x(a->name,name)==0)
      {  if(length) *length=a->valuelen;
         return a->value;
      }
   }
   if(length) *length=0;
   return NULL;
}

UBYTE *XmlAttrValue(struct XmlNode *node, const char *name)
{  return XmlAttrValueLen(node,name,NULL);
}

BOOL XmlNameIs(struct XmlNode *node, const char *name)
{  if(!node || node->type!=XMLN_ELEMENT || !node->name) return FALSE;
   return (BOOL)(stricmp_x(node->name,name)==0);
}
