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
 * place: separator bytes are replaced with NULs so that name/value
 * pointers double as C strings.  Entity decoding (&amp; &lt; etc.) is
 * done in place inside attribute values and text nodes - it can only
 * shrink the string so this is always safe.
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

/* Decode XML entities in place.  Returns new length (always <= old).
 * Only the common five entities plus numeric &#nn; / &#xnn; are
 * handled - that is enough for SVG. */
static LONG Unescape(UBYTE *str, LONG length)
{  UBYTE *src=str;
   UBYTE *dst=str;
   UBYTE *end=str+length;
   while(src<end)
   {  if(*src!='&')
      {  *dst++=*src++;
         continue;
      }
      /* Look for ; within a reasonable distance. */
      {  UBYTE *p=src+1;
         UBYTE *semi=NULL;
         LONG i;
         for(i=0;i<8 && p<end;i++,p++)
         {  if(*p==';') { semi=p; break; }
         }
         if(!semi)
         {  *dst++=*src++;
            continue;
         }
         {  UBYTE *e=src+1;
            LONG elen=(LONG)(semi-e);
            UBYTE out=0;
            BOOL ok=FALSE;
            if(elen==2 && e[0]=='l' && e[1]=='t') { out='<'; ok=TRUE; }
            else if(elen==2 && e[0]=='g' && e[1]=='t') { out='>'; ok=TRUE; }
            else if(elen==3 && e[0]=='a' && e[1]=='m' && e[2]=='p') { out='&'; ok=TRUE; }
            else if(elen==4 && e[0]=='q' && e[1]=='u' && e[2]=='o' && e[3]=='t') { out='"'; ok=TRUE; }
            else if(elen==4 && e[0]=='a' && e[1]=='p' && e[2]=='o' && e[3]=='s') { out='\''; ok=TRUE; }
            else if(elen>=2 && e[0]=='#')
            {  ULONG val=0;
               LONG j;
               if(e[1]=='x' || e[1]=='X')
               {  for(j=2;j<elen;j++)
                  {  UBYTE c=e[j];
                     if(c>='0'&&c<='9') val=(val<<4)|(c-'0');
                     else if(c>='a'&&c<='f') val=(val<<4)|(c-'a'+10);
                     else if(c>='A'&&c<='F') val=(val<<4)|(c-'A'+10);
                     else { val=0; break; }
                  }
               }
               else
               {  for(j=1;j<elen;j++)
                  {  UBYTE c=e[j];
                     if(c<'0'||c>'9') { val=0; break; }
                     val=val*10+(c-'0');
                  }
               }
               if(val>0 && val<256) { out=(UBYTE)val; ok=TRUE; }
            }
            if(ok)
            {  *dst++=out;
               src=semi+1;
            }
            else
            {  *dst++=*src++;
            }
         }
      }
   }
   return (LONG)(dst-str);
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
   {  /* DOCTYPE or other declaration - scan forward to matching '>'
       * tracking [ ] depth so internal subsets are handled. */
      LONG depth=0;
      p+=2;
      while(p<end)
      {  if(*p=='[') depth++;
         else if(*p==']' && depth>0) depth--;
         else if(*p=='>' && depth==0) return p+1;
         p++;
      }
      return end;
   }
   return NULL;
}

/* Parse a single attribute starting at *pp.  Returns TRUE on success
 * and advances *pp past the attribute; returns FALSE if no further
 * attribute is found (e.g. we hit '>' or '/').  On success the
 * attribute is appended to node->attrs. */
static BOOL Parseattr(APTR pool, struct XmlNode *node, UBYTE **pp, UBYTE *end)
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
            vcopy=(UBYTE *)AllocPooled(pool,(ULONG)(valuelen+1));
            if(!vcopy) { *pp=p; return FALSE; }
            if(valuelen>0) CopyMem(valuestart,vcopy,valuelen);
            vcopy[valuelen]=0;
            valuelen=Unescape(vcopy,valuelen);
            vcopy[valuelen]=0;
            a=Newattr(pool);
            if(!a) { *pp=p; return FALSE; }
            a->name=namestart;
            a->namelen=namelen;
            a->value=vcopy;
            a->valuelen=valuelen;
            goto append;
         }
      }
      quote=*p++;
      valuestart=p;
      while(p<end && *p!=quote) p++;
      if(p>=end) { *pp=p; return FALSE; }
      {  UBYTE *valueend=p;
         LONG valuelen=(LONG)(valueend-valuestart);
         *valueend=0;
         valuelen=Unescape(valuestart,valuelen);
         valuestart[valuelen]=0;
         p++; /* past closing quote */
         a=Newattr(pool);
         if(!a) { *pp=p; return FALSE; }
         a->name=namestart;
         a->namelen=namelen;
         a->value=valuestart;
         a->valuelen=valuelen;
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
static struct XmlNode *Parseelement(APTR pool, UBYTE **pp, UBYTE *end);

/* Parse the children of an element until we see a matching </name>
 * or hit EOF.  parent is the open element. */
static void Parsechildren(APTR pool, struct XmlNode *parent, UBYTE **pp, UBYTE *end)
{  UBYTE *p=*pp;
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
            if(!allspace)
            {  struct XmlNode *t=Newnode(pool,XMLN_TEXT);
               if(t)
               {  LONG newlen;
                  /* Decode in place; terminator overwrites the '<'
                   * once we save it.  We need a NUL at the end of
                   * the text string for C-string compatibility, but
                   * we cannot overwrite the '<' because the outer
                   * loop needs it.  Solution: leave the buffer text
                   * un-NUL-terminated and rely on textlen + a NUL we
                   * write into newlen position only if the decoded
                   * string is strictly shorter. */
                  newlen=Unescape(textstart,textlen);
                  t->text=textstart;
                  t->textlen=newlen;
                  if(newlen<textlen) textstart[newlen]=0;
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
      {  struct XmlNode *child=Parseelement(pool,&p,end);
         if(child) Appendchild(parent,child);
         else break;
      }
   }
   *pp=p;
}

static struct XmlNode *Parseelement(APTR pool, UBYTE **pp, UBYTE *end)
{  UBYTE *p=*pp;
   struct XmlNode *node;
   UBYTE *namestart;
   UBYTE *nameend;

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

   /* Parse attributes. */
   while(p<end)
   {  while(p<end && isspace_x(*p)) p++;
      if(p>=end) break;
      if(*p=='/' || *p=='>') break;
      if(!Parseattr(pool,node,&p,end)) break;
   }

   /* Now NUL-terminate the name (we deferred until attrs were parsed
    * because the original char after the name might have been a
    * letter for the first attribute - no, actually it cannot be: the
    * only chars stopping a name are space, >, /. So we can terminate
    * the name before parsing attrs as well, but it is safer to do it
    * here so attribute parsing doesn't trip over it.) */
   *nameend=0;

   if(p<end && *p=='/')
   {  /* Empty element. */
      p++;
      while(p<end && *p!='>') p++;
      if(p<end) p++;
      *pp=p;
      return node;
   }
   if(p<end && *p=='>') p++;

   /* Recurse into children. */
   Parsechildren(pool,node,&p,end);
   *pp=p;
   return node;
}

/* Public API --------------------------------------------------------*/

struct XmlNode *XmlParse(APTR pool, UBYTE *buffer, LONG length)
{  UBYTE *p=buffer;
   UBYTE *end=buffer+length;
   struct XmlNode *root=NULL;

   if(!pool || !buffer || length<=0) return NULL;

   while(p<end)
   {  while(p<end && isspace_x(*p)) p++;
      if(p>=end) break;
      if(*p!='<')
      {  /* Skip stray text outside any element. */
         while(p<end && *p!='<') p++;
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
      root=Parseelement(pool,&p,end);
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
