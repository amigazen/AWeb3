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

/* jsonparse.c - Minimal JSON parser for ARexx stem variable extraction.
 *
 * Supports:
 *   JSONGET DATA PATH [VAR name] [STEM name]
 *
 * PATH syntax:
 *   "key.subkey.subsubkey"     - Navigate nested objects
 *   "key.3"                    - Array index (1-based for ARexx)
 *   "key.*.subkey"             - Wildcard: first value in an object
 *   "key.#"                    - Return count of array elements or object keys
 *
 * When VAR is used: returns a single value into RESULT or named variable.
 * When STEM is used: returns array/object into stem variables.
 *   STEM.0 = count
 *   STEM.1 = first value (or STEM.1.KEY / STEM.1.VALUE for objects)
 */

#include "aweb.h"
#include "arexx.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <ctype.h>
#include <string.h>

/*-----------------------------------------------------------------------*/

/* Skip whitespace */
static UBYTE *Jskipws(UBYTE *p,UBYTE *end)
{  while(p<end && (*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')) p++;
   return p;
}

/* Skip a JSON value without extracting it. Returns pointer past the value.
 * Handles nested objects/arrays correctly by tracking brace depth. */
static UBYTE *Jskipvalue2(UBYTE *p,UBYTE *end)
{  long depth;
   p=Jskipws(p,end);
   if(p>=end) return NULL;
   switch(*p)
   {  case '"':
         p++;
         while(p<end)
         {  if(*p=='\\') { p+=2; continue; }
            if(*p=='"') { p++; return p; }
            p++;
         }
         return NULL;
      case '{': case '[':
         depth=1;
         p++;
         while(p<end && depth>0)
         {  if(*p=='"')
            {  p++;
               while(p<end)
               {  if(*p=='\\') { p+=2; continue; }
                  if(*p=='"') { p++; break; }
                  p++;
               }
               continue;
            }
            if(*p=='{' || *p=='[') depth++;
            else if(*p=='}' || *p==']') depth--;
            p++;
         }
         return (depth==0) ? p : NULL;
      case 't':
         if(p+4<=end && p[1]=='r' && p[2]=='u' && p[3]=='e') return p+4;
         return NULL;
      case 'f':
         if(p+5<=end && p[1]=='a' && p[2]=='l' && p[3]=='s' && p[4]=='e') return p+5;
         return NULL;
      case 'n':
         if(p+4<=end && p[1]=='u' && p[2]=='l' && p[3]=='l') return p+4;
         return NULL;
      default:
         if(*p=='-') p++;
         if(p>=end || !isdigit(*p)) return NULL;
         while(p<end && (isdigit(*p) || *p=='.' || *p=='e' || *p=='E' || *p=='+' || *p=='-')) p++;
         return p;
   }
}

/* Extract a JSON string value. Handles escape sequences.
 * p points to opening quote. Returns Dupstr'd string, advances *pp past closing quote. */
static UBYTE *Jextractstring(UBYTE **pp,UBYTE *end)
{  UBYTE *p=*pp;
   UBYTE *start,*out;
   long len,outlen;
   if(p>=end || *p!='"') return NULL;
   p++;  /* Skip opening quote */
   start=p;
   /* First pass: measure */
   len=0;
   while(p<end && *p!='"')
   {  if(*p=='\\')
      {  p++;
         if(p>=end) return NULL;
         switch(*p)
         {  case 'n': case 'r': case 't': case '"': case '\\': case '/':
               len++;
               break;
            case 'u':
               /* Unicode escape \uXXXX - emit as '?' for now */
               len++;
               if(p+4<end) p+=4; else return NULL;
               break;
            default:
               len++;
               break;
         }
      }
      else
      {  len++;
      }
      p++;
   }
   if(p>=end) return NULL;  /* Unterminated */
   
   /* Allocate and second pass: extract */
   out=ALLOCTYPE(UBYTE,len+1,0);
   if(!out) { *pp=p+1; return NULL; }
   p=start;
   outlen=0;
   while(p<end && *p!='"')
   {  if(*p=='\\')
      {  p++;
         switch(*p)
         {  case 'n': out[outlen++]='\n'; break;
            case 'r': out[outlen++]='\r'; break;
            case 't': out[outlen++]='\t'; break;
            case '"': out[outlen++]='"'; break;
            case '\\': out[outlen++]='\\'; break;
            case '/': out[outlen++]='/'; break;
            case 'u': out[outlen++]='?'; p+=4; break;
            default: out[outlen++]=*p; break;
         }
      }
      else
      {  out[outlen++]=*p;
      }
      p++;
   }
   out[outlen]='\0';
   *pp=p+1;  /* Skip closing quote */
   return out;
}

/* Extract a raw JSON value as string (for numbers, booleans, null) */
static UBYTE *Jextractraw(UBYTE *p,UBYTE *end,UBYTE **pend)
{  UBYTE *start=p;
   UBYTE *after;
   long len;
   after=Jskipvalue2(p,end);
   if(!after) return NULL;
   len=after-start;
   *pend=after;
   return Dupstr(start,len);
}

/* Navigate into a JSON object to find a key. 
 * p points to '{'. Returns pointer to value for the matching key. */
static UBYTE *Jfindinobject(UBYTE *p,UBYTE *end,UBYTE *key,long keylen,BOOL wildcard)
{  UBYTE *kstr;
   long klen;
   if(p>=end || *p!='{') return NULL;
   p++;  /* Skip { */
   p=Jskipws(p,end);
   if(p>=end) return NULL;
   if(*p=='}') return NULL;  /* Empty object */
   
   while(p<end)
   {  p=Jskipws(p,end);
      if(p>=end || *p=='}') return NULL;
      /* Expect string key */
      if(*p!='"') return NULL;
      kstr=Jextractstring(&p,end);
      if(!kstr) return NULL;
      klen=strlen(kstr);
      
      /* Skip colon */
      p=Jskipws(p,end);
      if(p>=end || *p!=':') { FREE(kstr); return NULL; }
      p++;
      p=Jskipws(p,end);
      
      /* Check if key matches (or wildcard) */
      if(wildcard || (klen==keylen && !strnicmp(kstr,key,keylen)))
      {  FREE(kstr);
         return p;  /* Points to the value */
      }
      FREE(kstr);
      
      /* Skip value */
      p=Jskipvalue2(p,end);
      if(!p) return NULL;
      p=Jskipws(p,end);
      if(p>=end) return NULL;
      if(*p==',') { p++; continue; }
      if(*p=='}') return NULL;  /* Key not found */
      return NULL;  /* Parse error */
   }
   return NULL;
}

/* Navigate into a JSON array by 1-based index.
 * p points to '['. Returns pointer to the value at that index. */
static UBYTE *Jfindinarray(UBYTE *p,UBYTE *end,long index)
{  long current=0;
   if(p>=end || *p!='[') return NULL;
   p++;
   p=Jskipws(p,end);
   if(p>=end) return NULL;
   if(*p==']') return NULL;  /* Empty array */
   
   while(p<end)
   {  current++;
      p=Jskipws(p,end);
      if(current==index) return p;  /* Found it */
      /* Skip this value */
      p=Jskipvalue2(p,end);
      if(!p) return NULL;
      p=Jskipws(p,end);
      if(p>=end) return NULL;
      if(*p==',') { p++; continue; }
      if(*p==']') return NULL;  /* Index out of range */
      return NULL;
   }
   return NULL;
}

/* Count elements in array or object.
 * p points to '[' or '{'. Returns count. */
static long Jcount(UBYTE *p,UBYTE *end)
{  long count=0;
   UBYTE closer;
   if(p>=end) return 0;
   if(*p=='[') closer=']';
   else if(*p=='{') closer='}';
   else return 0;
   p++;
   p=Jskipws(p,end);
   if(p>=end || *p==closer) return 0;  /* Empty */
   
   while(p<end)
   {  count++;
      if(closer=='}')
      {  /* Object: skip key */
         if(*p!='"') return count;  /* Parse error, return what we have */
         p=Jskipvalue2(p,end);
         if(!p) return count;
         p=Jskipws(p,end);
         if(p>=end || *p!=':') return count;
         p++;
         p=Jskipws(p,end);
      }
      /* Skip value */
      p=Jskipvalue2(p,end);
      if(!p) return count;
      p=Jskipws(p,end);
      if(p>=end) return count;
      if(*p==',') { p++; p=Jskipws(p,end); continue; }
      break;  /* End of container */
   }
   return count;
}

/*-----------------------------------------------------------------------*/

/* Main entry point: navigate JSON by dot-separated path and extract value(s).
 *
 * data     - JSON string
 * datalen  - length of JSON string
 * path     - dot-separated path (e.g., "query.pages.*.extract")
 * ac       - ARexx command for setting stem variables
 * varname  - if non-NULL, set single result here
 * stem     - if non-NULL, populate stem variables
 *
 * Returns Dupstr'd result string (caller must FREE), or NULL on error.
 */
UBYTE *Jsonget(UBYTE *data,long datalen,UBYTE *path,
               struct Arexxcmd *ac,UBYTE *varname,UBYTE *stem)
{  UBYTE *p,*end;
   UBYTE *pathp,*pathnext;
   UBYTE segment[128];
   long seglen;
   BOOL wildcard;
   long index;
   UBYTE *result=NULL;
   
   if(!data || !path || datalen<=0) return NULL;
   
   p=data;
   end=data+datalen;
   p=Jskipws(p,end);
   if(p>=end) return NULL;
   
   /* Navigate path segments */
   pathp=path;
   while(pathp && *pathp)
   {  /* Extract next segment (up to '.' or end) */
      pathnext=strchr(pathp,'.');
      if(pathnext)
      {  seglen=pathnext-pathp;
         if(seglen>=sizeof(segment)) seglen=sizeof(segment)-1;
         memcpy(segment,pathp,seglen);
         segment[seglen]='\0';
         pathnext++;  /* Skip the dot */
      }
      else
      {  seglen=strlen(pathp);
         if(seglen>=sizeof(segment)) seglen=sizeof(segment)-1;
         memcpy(segment,pathp,seglen);
         segment[seglen]='\0';
      }
      
      p=Jskipws(p,end);
      if(p>=end) return NULL;
      
      /* Handle special segments */
      if(segment[0]=='#' && segment[1]=='\0')
      {  /* Count elements */
         long count=Jcount(p,end);
         UBYTE buf[16];
         sprintf(buf,"%ld",count);
         result=Dupstr(buf,-1);
         /* If stem, also populate stem.0 */
         if(stem && ac)
         {  Setstemvar(ac,stem,0,NULL,buf);
         }
         return result;
      }
      
      wildcard=(segment[0]=='*' && segment[1]=='\0');
      
      /* Navigate based on current JSON type */
      if(*p=='{')
      {  /* Object: look up key */
         p=Jfindinobject(p,end,segment,seglen,wildcard);
         if(!p) return NULL;
      }
      else if(*p=='[')
      {  /* Array: use numeric index (1-based) */
         index=strtol(segment,NULL,10);
         if(index<=0) return NULL;  /* Invalid index */
         p=Jfindinarray(p,end,index);
         if(!p) return NULL;
      }
      else
      {  return NULL;  /* Can't navigate into a scalar */
      }
      
      pathp=pathnext;
   }
   
   /* We've navigated to the target value */
   p=Jskipws(p,end);
   if(p>=end) return NULL;
   
   if(stem && ac && (*p=='[' || *p=='{'))
   {  /* Populate stem variables from array or object */
      long count=Jcount(p,end);
      UBYTE buf[16];
      UBYTE *inner;
      long i;
      
      sprintf(buf,"%ld",count);
      Setstemvar(ac,stem,0,NULL,buf);
      
      if(*p=='[')
      {  /* Array - populate STEM.1, STEM.2, etc. */
         inner=p+1;
         for(i=1;i<=count;i++)
         {  UBYTE *val;
            inner=Jskipws(inner,end);
            if(inner>=end) break;
            if(*inner=='"')
            {  val=Jextractstring(&inner,end);
            }
            else if(*inner=='{' || *inner=='[')
            {  /* Nested object/array: return as raw JSON */
               UBYTE *vend=Jskipvalue2(inner,end);
               if(vend) { val=Dupstr(inner,vend-inner); inner=vend; }
               else break;
            }
            else
            {  val=Jextractraw(inner,end,&inner);
            }
            if(val)
            {  Setstemvar(ac,stem,i,NULL,val);
               FREE(val);
            }
            inner=Jskipws(inner,end);
            if(inner<end && *inner==',') inner++;
         }
      }
      else
      {  /* Object - populate STEM.1.KEY, STEM.1.VALUE, etc. */
         inner=p+1;
         for(i=1;i<=count;i++)
         {  UBYTE *key,*val;
            inner=Jskipws(inner,end);
            if(inner>=end || *inner!='"') break;
            key=Jextractstring(&inner,end);
            inner=Jskipws(inner,end);
            if(inner>=end || *inner!=':') { if(key) FREE(key); break; }
            inner++;
            inner=Jskipws(inner,end);
            if(*inner=='"')
            {  val=Jextractstring(&inner,end);
            }
            else if(*inner=='{' || *inner=='[')
            {  UBYTE *vend=Jskipvalue2(inner,end);
               if(vend) { val=Dupstr(inner,vend-inner); inner=vend; }
               else { if(key) FREE(key); break; }
            }
            else
            {  val=Jextractraw(inner,end,&inner);
            }
            if(key)
            {  Setstemvar(ac,stem,i,"KEY",key);
               FREE(key);
            }
            if(val)
            {  Setstemvar(ac,stem,i,"VALUE",val);
               FREE(val);
            }
            inner=Jskipws(inner,end);
            if(inner<end && *inner==',') inner++;
         }
      }
      
      /* Also return count as result */
      sprintf(buf,"%ld",count);
      result=Dupstr(buf,-1);
   }
   else
   {  /* Extract single value */
      UBYTE *pp=p;
      if(*p=='"')
      {  result=Jextractstring(&pp,end);
      }
      else if(*p=='{' || *p=='[')
      {  /* Return raw JSON */
         UBYTE *vend=Jskipvalue2(p,end);
         if(vend) result=Dupstr(p,vend-p);
      }
      else
      {  result=Jextractraw(p,end,&pp);
      }
   }
   
   return result;
}
