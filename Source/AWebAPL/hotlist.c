/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2025 amigazen project
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

/* hotlist.c - AWeb hotlist manager */

#include "aweb.h"
#include "url.h"
#include "file.h"
#include "application.h"
#include "awebprefs.h"
#include "window.h"
#include "task.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#define HOTLISTNAME "AWeb:aweb.hotlist"
static UBYTE *hotlistname;

#include "hotlist.h"

static LIST(Hotbase) hotbase;
static LIST(Hotitem) hotlist;

/* Max size of URL. An I/O buffer will be allocated to allow this plus some extra. */
#define MAXURLSIZE   4096

static struct SignalSemaphore hotsema;

static BOOL hotchanged;

#ifdef AWEB4
/* Forward declaration: the dispatcher (AOHOT_Changed etc.) requests a
 * Hotlist menu rebuild, but the helper is defined further down with the
 * rest of the AWEB4 dynamic-menu code. */
static void Hotlistmenus_refresh(void);
#endif

/* m is the maintenance window, v is the view window */
static struct Hotwindow *hotwindowm=NULL,*hotwindowv=NULL;

enum LIB_ENTRIES
{  LIBENTRY_VIEWER=0,LIBENTRY_MANAGER,
};

/*-----------------------------------------------------------------------*/

/* Find the base for this entry, create a new one of not found */
static struct Hotbase *Findbase(UBYTE *title,UBYTE *url)
{  struct Hotbase *hb,*newhb;
   long c,l;
   if(!title || !url) return NULL;
   for(hb=hotbase.first;hb->next;hb=hb->next)
   {  c=stricmp(hb->title,title);
      if(c==0 && STREQUAL(hb->url,url))
      {  return hb;
      }
      if(c>0) break;
   }
   /* If not found, create a new one and insert before (hb) */
   hb=hb->prev;
   if(newhb=ALLOCSTRUCT(Hotbase,1,MEMF_CLEAR|MEMF_PUBLIC))
   {  newhb->nodetype=HNT_BASE;
      l=strlen(url);
      if(l>MAXURLSIZE) l=MAXURLSIZE;
      if((newhb->title=Dupstr(title,-1))
      && (newhb->url=Dupstr(url,l)))
      {  INSERT(&hotbase,newhb,hb);
         return newhb;
      }
      if(newhb->url) FREE(newhb->url);
      if(newhb->title) FREE(newhb->title);
      FREE(newhb);
   }
   return NULL;
}

static void Readhotgroup(struct Hotitem *owner,void *hlist,long fh,
   UBYTE *buffer)
{  
#ifndef DEMOVERSION
   struct Hotitem *hi;
   struct Hotbase *hb;
   UBYTE *url;
   long l;
   BOOL makeitem;
   ULONG date=0;
   for(;;)
   {  if(!FGets(fh,buffer,MAXURLSIZE+1)) return;
      l=strlen(buffer);
      if(buffer[l-1]=='\n') buffer[l-1]='\0';
      if(STRIEQUAL(buffer,"@ENDGROUP")) return;
      hb=NULL;
      makeitem=TRUE;
      if(STRNIEQUAL(buffer,"@DATE ",6))
      {  sscanf(buffer+6,"%x",&date);
      }
      else
      {  if(buffer[0]!='@')
         {  url=Dupstr(buffer,-1);
            if(FGets(fh,buffer,MAXURLSIZE+1))
            {  l=strlen(buffer);
               if(buffer[l-1]=='\n') buffer[l-1]='\0';
               hb=Findbase(buffer,url);
               if(hb) hb->date=date;
               date=0;
            }
            if(url) FREE(url);
            if(!owner) makeitem=FALSE;
         }
         if(makeitem && (hi=ALLOCSTRUCT(Hotitem,1,MEMF_PUBLIC|MEMF_CLEAR)))
         {  hi->nodetype=HNT_ITEM;
            NEWLIST(&hi->subitems);
            if(STRNIEQUAL(buffer,"@GROUP_HIDE ",12))
            {  hi->type=HITEM_HGROUP;
               hi->name=Dupstr(buffer+12,-1);
               Readhotgroup(hi,&hi->subitems,fh,buffer);
            }
            else if(STRNIEQUAL(buffer,"@GROUP ",7))
            {  hi->type=HITEM_GROUP;
               hi->name=Dupstr(buffer+7,-1);
               Readhotgroup(hi,&hi->subitems,fh,buffer);
            }
            else if(hb)
            {  hi->type=HITEM_ENTRY;
               hi->base=hb;
               hb->used++;
            }
            hi->owner=owner;
            ADDTAIL(hlist,hi);
            date=0;
         }
      }
   }
#endif
}

/* Case-insensitive character comparison */
static BOOL charequal(UBYTE a,UBYTE b)
{  if(a==b) return TRUE;
   if(a>='A' && a<='Z') a=a-'A'+'a';
   if(b>='A' && b<='Z') b=b-'A'+'a';
   return BOOLVAL(a==b);
}

/* Extract attribute value from HTML tag line (case-insensitive attribute name) */
static UBYTE *Gethtmlattr(UBYTE *line,UBYTE *attrname)
{  UBYTE *p,*q,*val;
   long len,attrlen;
   long i;
   BOOL match;
   if(!line || !attrname) return NULL;
   p=line;
   attrlen=strlen(attrname);
   while(*p)
   {  /* Case-insensitive search for attribute name */
      match=TRUE;
      for(i=0;i<attrlen && p[i];i++)
      {  if(!charequal(p[i],attrname[i]))
         {  match=FALSE;
            break;
         }
      }
      if(match && (p[attrlen]==' ' || p[attrlen]=='\t' || p[attrlen]=='='))
      {  p+=attrlen;
         while(*p==' ' || *p=='\t') p++;
         if(*p=='=')
         {  p++;
            while(*p==' ' || *p=='\t') p++;
            if(*p=='"')
            {  p++;
               q=p;
               while(*q && *q!='"' && *q!='>') q++;
               if(*q=='"')
               {  len=q-p;
                  if(len>0 && len<MAXURLSIZE)
                  {  if(val=ALLOCTYPE(UBYTE,len+1,MEMF_PUBLIC))
                     {  strncpy(val,p,len);
                        val[len]='\0';
                        return val;
                     }
                  }
               }
            }
            else if(*p!='>' && *p!=' ' && *p!='\t')
            {  q=p;
               while(*q && *q!=' ' && *q!='\t' && *q!='>') q++;
               len=q-p;
               if(len>0 && len<MAXURLSIZE)
               {  if(val=ALLOCTYPE(UBYTE,len+1,MEMF_PUBLIC))
                  {  strncpy(val,p,len);
                     val[len]='\0';
                     return val;
                  }
               }
            }
         }
      }
      p++;
   }
   return NULL;
}

/* Extract text content from HTML tag, removing HTML entities */
static UBYTE *Gethtmltext(UBYTE *line)
{  UBYTE *p,*q,*result,*out;
   long len,outlen;
   BOOL intag;
   if(!line) return NULL;
   len=strlen(line);
   if(result=ALLOCTYPE(UBYTE,len+1,MEMF_PUBLIC))
   {  p=line;
      out=result;
      outlen=0;
      intag=FALSE;
      while(*p && outlen<len)
      {  if(*p=='<')
         {  intag=TRUE;
            p++;
            while(*p && *p!='>') p++;
            if(*p=='>') p++;
            intag=FALSE;
         }
         else if(*p=='&')
         {  if(STRNIEQUAL(p,"&lt;",4))
            {  *out++='<';
               outlen++;
               p+=4;
            }
            else if(STRNIEQUAL(p,"&gt;",4))
            {  *out++='>';
               outlen++;
               p+=4;
            }
            else if(STRNIEQUAL(p,"&amp;",5))
            {  *out++='&';
               outlen++;
               p+=5;
            }
            else if(STRNIEQUAL(p,"&quot;",6))
            {  *out++='"';
               outlen++;
               p+=6;
            }
            else if(STRNIEQUAL(p,"&#",2))
            {  p+=2;
               while(*p>='0' && *p<='9') p++;
               if(*p==';') p++;
            }
            else
            {  q=p+1;
               while(*q && *q!=';' && *q!=' ' && *q!='\t' && *q!='<') q++;
               if(*q==';') p=q+1;
               else p=q;
            }
         }
         else if(!intag)
         {  *out++=*p++;
            outlen++;
         }
         else
         {  p++;
         }
      }
      *out='\0';
      /* Trim whitespace */
      p=result;
      while(*p==' ' || *p=='\t' || *p=='\n' || *p=='\r') p++;
      if(p>result)
      {  q=result;
         while(*p) *q++=*p++;
         *q='\0';
      }
      p=result+strlen(result)-1;
      while(p>=result && (*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')) *p--='\0';
      if(*result)
      {  return result;
      }
      FREE(result);
   }
   return NULL;
}

/* Parse Mozilla/Netscape bookmarks HTML format */
static void Readmozillahotlist(long fh,UBYTE *buffer)
{  
#ifndef DEMOVERSION
   struct Hotitem *hi,*currentgroup;
   struct Hotbase *hb;
   UBYTE *url,*title,*adddate;
   UBYTE *p,*q;
   long l;
   ULONG date=0;
   BOOL indl=FALSE;
   currentgroup=NULL;
   Seek(fh,0,OFFSET_BEGINNING);
   while(FGets(fh,buffer,MAXURLSIZE+1))
   {  l=strlen(buffer);
      if(l>0 && buffer[l-1]=='\n') buffer[l-1]='\0';
      if(l>0 && buffer[l-1]=='\r') buffer[l-1]='\0';
      /* Skip whitespace lines */
      p=buffer;
      while(*p==' ' || *p=='\t') p++;
      if(!*p) continue;
      /* Check for <DL><p> - start of folder */
      if(STRNIEQUAL(p,"<DL><p>",7) || STRNIEQUAL(p,"<DL>",4))
      {  indl=TRUE;
         continue;
      }
      /* Check for </DL><p> - end of folder */
      if(STRNIEQUAL(p,"</DL><p>",8) || STRNIEQUAL(p,"</DL>",5))
      {  if(currentgroup)
         {  currentgroup=currentgroup->owner;
         }
         indl=FALSE;
         continue;
      }
      /* Check for <DT><H3> - folder/group */
      if(STRNIEQUAL(p,"<DT><H3",7))
      {  title=Gethtmltext(p);
         adddate=Gethtmlattr(p,"ADD_DATE");
         if(adddate)
         {  sscanf(adddate,"%lu",&date);
            FREE(adddate);
         }
         if(title && *title)
         {  if(hi=ALLOCSTRUCT(Hotitem,1,MEMF_PUBLIC|MEMF_CLEAR))
            {  hi->nodetype=HNT_ITEM;
               hi->type=HITEM_GROUP;
               hi->name=title;
               hi->owner=currentgroup;
               NEWLIST(&hi->subitems);
               if(currentgroup)
               {  ADDTAIL(&currentgroup->subitems,hi);
               }
               else
               {  ADDTAIL(&hotlist,hi);
               }
               currentgroup=hi;
            }
            else
            {  FREE(title);
            }
         }
         continue;
      }
      /* Check for <DT><A HREF=...> - bookmark entry */
      if(STRNIEQUAL(p,"<DT><A",6))
      {  url=Gethtmlattr(p,"HREF");
         adddate=Gethtmlattr(p,"ADD_DATE");
         if(adddate)
         {  sscanf(adddate,"%lu",&date);
            FREE(adddate);
         }
         /* Get title from text content after the tag */
         q=strchr(p,'>');
         if(q)
         {  q++;
            title=Gethtmltext(q);
            /* If title not found on same line, try next line */
            if((!title || !*title) && FGets(fh,buffer,MAXURLSIZE+1))
            {  l=strlen(buffer);
               if(l>0 && buffer[l-1]=='\n') buffer[l-1]='\0';
               if(l>0 && buffer[l-1]=='\r') buffer[l-1]='\0';
               q=buffer;
               while(*q==' ' || *q=='\t') q++;
               /* Check if this line contains </A> - if so, extract title before it */
               {  UBYTE *endtag;
                  endtag=strstr(q,"</A>");
                  if(endtag)
                  {  *endtag='\0';
                     title=Gethtmltext(q);
                  }
                  else
                  {  title=Gethtmltext(q);
                  }
               }
            }
         }
         if(url && *url)
         {  if(!title || !*title) title=Dupstr(url,-1);
            hb=Findbase(title,url);
            if(hb && date) hb->date=date;
            if(hb && (hi=ALLOCSTRUCT(Hotitem,1,MEMF_PUBLIC|MEMF_CLEAR)))
            {  hi->nodetype=HNT_ITEM;
               hi->type=HITEM_ENTRY;
               hi->base=hb;
               hi->owner=currentgroup;
               NEWLIST(&hi->subitems);
               hb->used++;
               if(currentgroup)
               {  ADDTAIL(&currentgroup->subitems,hi);
               }
               else
               {  ADDTAIL(&hotlist,hi);
               }
            }
         }
         if(url) FREE(url);
         if(title) FREE(title);
         date=0;
         continue;
      }
   }
#endif
}

/* Detect if file is Mozilla bookmarks format */
static BOOL Ismozillabookmarks(long fh)
{  UBYTE buffer[512];
   long pos;
   BOOL result=FALSE;
   long i;
   pos=Seek(fh,0,OFFSET_CURRENT);
   Seek(fh,0,OFFSET_BEGINNING);
   /* Check first few lines for DOCTYPE or DL tag */
   for(i=0;i<10 && !result && FGets(fh,buffer,sizeof(buffer));i++)
   {  /* Check for DOCTYPE declaration */
      if(STRNIEQUAL(buffer,"<!DOCTYPE NETSCAPE-Bookmark-file-1",34))
      {  result=TRUE;
         break;
      }
      /* Check for <DL> tag (case-insensitive) */
      {  UBYTE *p;
         p=buffer;
         while(*p)
         {  if((*p=='<' || *p==' ') && STRNIEQUAL(p+1,"DL>",3))
            {  result=TRUE;
               break;
            }
            p++;
         }
      }
   }
   Seek(fh,pos,OFFSET_BEGINNING);
   return result;
}

static void Readawebhotlist(void)
{  
#ifndef DEMOVERSION
   long fh;
   UBYTE *iobuffer;
   if(iobuffer=ALLOCTYPE(UBYTE,MAXURLSIZE+512,MEMF_PUBLIC))
   {  if(fh=Open(hotlistname,MODE_OLDFILE))
      {  if(Ismozillabookmarks(fh))
         {  Readmozillahotlist(fh,iobuffer);
         }
         else if(FGets(fh,iobuffer,MAXURLSIZE+1) && iobuffer[0]=='@')
         {  Readhotgroup(NULL,&hotlist,fh,iobuffer);
         }
         Close(fh);
      }
      FREE(iobuffer);
   }
#endif
}

static void Savehotgroup(LIST(Hotitem) *hlist,long fh,UBYTE *buffer)
{  
#ifndef DEMOVERSION
   struct Hotitem *hi;
   for(hi=hlist->first;hi->next;hi=hi->next)
   {  if(hi->type==HITEM_ENTRY && hi->base)
      {  if(hi->base->date)
         {  sprintf(buffer,"@DATE %012x\n",hi->base->date);
            Write(fh,buffer,strlen(buffer));
         }
         strcpy(buffer,hi->base->url);
         strcat(buffer,"\n");
         Write(fh,buffer,strlen(buffer));
         strcpy(buffer,hi->base->title);
         strcat(buffer,"\n");
         Write(fh,buffer,strlen(buffer));
      }
      else
      {  if(hi->type==HITEM_HGROUP) strcpy(buffer,"@GROUP_HIDE ");
         else strcpy(buffer,"@GROUP ");
         strcat(buffer,hi->name);
         strcat(buffer,"\n");
         Write(fh,buffer,strlen(buffer));
         Savehotgroup(&hi->subitems,fh,buffer);
         strcpy(buffer,"@ENDGROUP\n");
         Write(fh,buffer,strlen(buffer));
      }
   }
#endif
}

static void Saveawebhotlist(void)
{  
#ifndef DEMOVERSION
   long fh;
   struct Hotbase *hb;
   UBYTE *iobuffer;
   if(iobuffer=ALLOCTYPE(UBYTE,MAXURLSIZE+512,MEMF_PUBLIC))
   {  if(fh=Open(hotlistname,MODE_NEWFILE))
      {  strcpy(iobuffer,"@AWeb hotlist\n");
         Write(fh,iobuffer,strlen(iobuffer));
         Savehotgroup(&hotlist,fh,iobuffer);
         for(hb=hotbase.first;hb->next;hb=hb->next)
         {  if(!hb->used)
            {  if(hb->date)
               {  sprintf(iobuffer,"@DATE %012x\n",hb->date);
                  Write(fh,iobuffer,strlen(iobuffer));
               }
               strcpy(iobuffer,hb->url);
               strcat(iobuffer,"\n");
               Write(fh,iobuffer,strlen(iobuffer));
               strcpy(iobuffer,hb->title);
               strcat(iobuffer,"\n");
               Write(fh,iobuffer,strlen(iobuffer));
            }
         }
         Close(fh);
      }
      FREE(iobuffer);
   }
#endif
}

static void Disposehotgroup(void *hlist)
{  struct Hotitem *hi;
   while(hi=REMHEAD(hlist))
   {  if(hi->name) FREE(hi->name);
      Disposehotgroup(&hi->subitems);
      FREE(hi);
   }
}

static void Disposehotlist(void)
{  struct Hotbase *hb;
   Disposehotgroup(&hotlist);
   while(hb=REMHEAD(&hotbase))
   {  if(hb->title) FREE(hb->title);
      if(hb->url) FREE(hb->url);
      FREE(hb);
   }
}

/* Get a group in this stem variable. */
static void Gethotlistgroup(LIST (Hotitem) *list,UBYTE *owner,
   struct Arexxcmd *ac,UBYTE *stem,long *stemnr,BOOL groupsonly)
{  UBYTE buf[12];
   struct Hotitem *hi;
   for(hi=list->first;hi->next;hi=hi->next)
   {  if(hi->type==HITEM_ENTRY)
      {  if(!groupsonly)
         {  (*stemnr)++;
            Setstemvar(ac,stem,*stemnr,"GROUP","0");
            Setstemvar(ac,stem,*stemnr,"TITLE",hi->base->title);
            Setstemvar(ac,stem,*stemnr,"URL",hi->base->url);
            Setstemvar(ac,stem,*stemnr,"OWNER",owner);
         }
      }
      else
      {  (*stemnr)++;
         if(!groupsonly)
         {  Setstemvar(ac,stem,*stemnr,"GROUP","1");
         }
         Setstemvar(ac,stem,*stemnr,"TITLE",hi->name);
         Setstemvar(ac,stem,*stemnr,"OWNER",owner);
         sprintf(buf,"%d",*stemnr);
         Gethotlistgroup(&hi->subitems,buf,ac,stem,stemnr,groupsonly);
      }
   }
}

static void Buildfromstem(struct Arexxcmd *ac,UBYTE *stem)
{  long stemnr,maxnr=0;
   UBYTE *value,*title,*url;
   BOOL group;
   long owner;
   struct Hotbase *hb;
   struct Hotitem *hi;
   struct Hotitem **grouptab;
   if(value=Getstemvar(ac,stem,0,NULL))
   {  sscanf(value," %ld",&maxnr);
   }
   if(maxnr)
   {  if(grouptab=ALLOCTYPE(struct Hotitem *,maxnr+1,MEMF_CLEAR))
      {  for(stemnr=1;stemnr<=maxnr;stemnr++)
         {  if(value=Getstemvar(ac,stem,stemnr,"GROUP"))
            {  sscanf(value," %hd",&group);
            }
            else
            {  group=FALSE;
            }
            if(value=Getstemvar(ac,stem,stemnr,"OWNER"))
            {  sscanf(value," %ld",&owner);
               if(owner<1 || owner>maxnr || !grouptab[owner]) owner=0;
            }
            else
            {  owner=0;
            }
            title=Getstemvar(ac,stem,stemnr,"TITLE");
            if(!group)
            {  url=Getstemvar(ac,stem,stemnr,"URL");
            }
            if(title && (group || url))
            {  if(!group)
               {  if(!(hb=Findbase(title,url))) return;
               }
               if(group || owner)
               {  if(!(hi=ALLOCSTRUCT(Hotitem,1,MEMF_PUBLIC|MEMF_CLEAR))) return;
                  NEWLIST(&hi->subitems);
                  if(owner)
                  {  hi->owner=grouptab[owner];
                     ADDTAIL(&hi->owner->subitems,hi);
                  }
                  else
                  {  ADDTAIL(&hotlist,hi);
                  }
                  if(group)
                  {  hi->type=HITEM_GROUP;
                     if(!(hi->name=Dupstr(title,-1)))
                     {  FREE(hi);
                        return;
                     }
                     grouptab[stemnr]=hi;
                  }
                  else
                  {  hi->base=hb;
                     hb->used++;
                  }
               }
            }
         }
         FREE(grouptab);
      }
   }
}

/*-----------------------------------------------------------------------*/

/* Open aweblib and start task */
static BOOL Starttask(struct Hotwindow *how)
{  if(how->libbase=Openaweblib("AWeb:aweblib/hotlist.aweblib"))
   {  how->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,AWEBLIBENTRY(how->libbase,how->libentry),
         AOTSK_Name,"AWeb hotlist",
         AOTSK_Userdata,how,
         AOBJ_Target,how,
         TAG_END);
      if(how->task)
      {  Asetattrs(how->task,AOTSK_Start,TRUE,TAG_END);
      }
      else
      {  CloseLibrary(how->libbase);
         how->libbase=NULL;
      }
   }
   return BOOLVAL(how->task);
}

/* Dispose task and close aweblib */
static void Stoptask(struct Hotwindow *how)
{  if(how->libbase)
   {  if(how->task)
      {  Adisposeobject(how->task);
         how->task=NULL;
      }
      CloseLibrary(how->libbase);
      how->libbase=NULL;
   }
}

/*-----------------------------------------------------------------------*/

static long Updatehotwindow(struct Hotwindow *how,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   BOOL close=FALSE,dim=FALSE;
   void *url,*win;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOHOT_Close:
            if(tag->ti_Data) close=TRUE;
            break;
         case AOHOT_Dimx:
            how->x=tag->ti_Data;
            dim=TRUE;
            break;
         case AOHOT_Dimy:
            how->y=tag->ti_Data;
            break;
         case AOHOT_Dimw:
            how->w=tag->ti_Data;
            break;
         case AOHOT_Dimh:
            how->h=tag->ti_Data;
            break;
         case AOHOT_Follow:
            if(tag->ti_Data)
            {  if(url=Findurl("",(UBYTE *)tag->ti_Data,0))
               {  if(!(win=Findwindow(how->windowkey)))
                  {  win=Activewindow();
                  }
                  if(win)
                  {  Inputwindoc(win,url,NULL,0);
                  }
               }
            }
            break;
         case AOHOT_Changed:
            hotchanged|=tag->ti_Data;
#ifndef DEMOVERSION
            if(hotwindowv && hotwindowv->task)
            {  Asetattrsasync(hotwindowv->task,AOHOT_Addentry,TRUE,TAG_END);
            }
#endif
#ifdef AWEB4
            Hotlistmenus_refresh();
#endif
            break;
         case AOHOT_Save:
            Savehotlist();
            break;
         case AOHOT_Restore:
            Restorehotlist();
            break;
         case AOHOT_Manager:
            Hotlistmanager(how->windowkey);
            break;
         case AOHOT_Vchanged:
            hotchanged|=tag->ti_Data;
#ifdef AWEB4
            Hotlistmenus_refresh();
#endif
            break;
      }
   }
   if(dim)
   {  if(how->libentry==LIBENTRY_VIEWER)
      {  prefs.window.hotx=how->x;
         prefs.window.hoty=how->y;
         prefs.window.hotw=how->w;
         prefs.window.hoth=how->h;
      }
      else
      {  prefs.window.homx=how->x;
         prefs.window.homy=how->y;
         prefs.window.homw=how->w;
         prefs.window.homh=how->h;
      }
   }
   if(close && !(how->flags&HOTF_BREAKING))
   {  Stoptask(how);
   }
   return 0;
}

static long Sethotwindow(struct Hotwindow *how,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOAPP_Screenvalid:
            if(!tag->ti_Data)
            {  if(how->task)
               {  how->flags|=HOTF_BREAKING|HOTF_WASOPEN;
                  Stoptask(how);
                  how->flags&=~HOTF_BREAKING;
               }
            }
            else
            {  if((how->flags&HOTF_WASOPEN) && !how->task)
               {  how->screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
                  Starttask(how);
               }
            }
            break;
      }
   }
   return 0;
}

static struct Hotwindow *Newhotwindow(struct Amset *ams)
{  struct Hotwindow *how;
   if(how=Allocobject(AOTP_HOTLIST,sizeof(struct Hotwindow),ams))
   {  Aaddchild(Aweb(),how,AOREL_APP_USE_SCREEN);
      how->windowkey=GetTagData(AOHOT_Windowkey,0,ams->tags);
      how->libentry=GetTagData(AOHOT_Libentry,0,ams->tags);
      if(Agetattr(Aweb(),AOAPP_Screenvalid))
      {  how->hotsema=&hotsema;
         how->hotbase=&hotbase;
         how->hotlist=&hotlist;
         if(how->libentry==LIBENTRY_VIEWER)
         {  how->x=prefs.window.hotx;
            how->y=prefs.window.hoty;
            how->w=prefs.window.hotw;
            how->h=prefs.window.hoth;
            how->autoclose=prefs.program.hlautoclose;
         }
         else
         {  how->x=prefs.window.homx;
            how->y=prefs.window.homy;
            how->w=prefs.window.homw;
            how->h=prefs.window.homh;
         }
         how->screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
         if(!Starttask(how))
         {  Adisposeobject(how);
            how=NULL;
         }
      }
   }
   return how;
}

static void Disposehotwindow(struct Hotwindow *how)
{  if(how->task)
   {  how->flags|=HOTF_BREAKING;
      Stoptask(how);
   }
   Aremchild(Aweb(),how,AOREL_APP_USE_SCREEN);
   Amethodas(AOTP_OBJECT,how,AOM_DISPOSE);
}

static void Deinstallhotwindow(void)
{  if(hotwindowv) Adisposeobject(hotwindowv);
   if(hotwindowm) Adisposeobject(hotwindowm);
   if(hotchanged) Saveawebhotlist();
   if(hotbase.first) Disposehotlist();
   if(hotlistname) FREE(hotlistname);
}

static long Dispatch(struct Hotwindow *how,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newhotwindow((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Sethotwindow(how,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatehotwindow(how,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposehotwindow(how);
         break;
      case AOM_DEINSTALL:
         Deinstallhotwindow();
         break;
   }
   return result;
}

/*-----------------------------------------------------------------------*/

#ifdef AWEB4

/* Dynamic Hotlist menu strip entries (see application.c Makemenus). */
static LIST(Menuentry) hotlistmenus;

static void Hotlistmenus_refresh(void)
{  if(Aweb() && Agetattr(Aweb(),AOAPP_Screenvalid))
   {  Asetattrs(Aweb(),AOAPP_Menus,NULL,TAG_END);
   }
}

void Freehotlistmenus(void)
{  struct Menuentry *me;
   while((me=(struct Menuentry *)REMHEAD(&hotlistmenus)))
   {  Freemenuentry(me);
   }
}

struct Menuentry *Hotlistmenuentryfromnum(USHORT menunum)
{  struct Menuentry *me;
   for(me=hotlistmenus.first;me->next;me=me->next)
   {  if(me->menunum==menunum) return me;
   }
   return NULL;
}

/* TRUE if this Menuentry is one of our dynamic Hotlist bookmark rows.
 * Used by Processmenu() to load me->cmd (a raw URL) into the picked
 * window rather than treating it as an ARexx command. */
BOOL Ishotlistmenuentry(struct Menuentry *me)
{  struct Menuentry *e;
   if(!me) return FALSE;
   for(e=hotlistmenus.first;e->next;e=e->next)
   {  if(e==me) return TRUE;
   }
   return FALSE;
}

/* Maximum visible characters of a bookmark label in the Hotlist menu.
 * Longer labels are truncated in the MIDDLE with an ellipsis so both the
 * start (often a group prefix) and the end (often the distinctive part of
 * a title) stay readable. */
#define HOTLIST_MENU_MAXLABEL  18
#define HOTLIST_MENU_ELLIPSIS  "..."

/* Copy src to dest, truncating in the middle with HOTLIST_MENU_ELLIPSIS when
 * src is longer than maxlen characters. dest must hold maxlen+1 bytes.
 * "..." is used rather than a single ellipsis glyph because the Amiga
 * default (topaz / ISO-8859-1) menu font has no reliable ellipsis code. */
static void Truncatemiddle(UBYTE *dest,UBYTE *src,long maxlen)
{  long len=strlen((char *)src);
   long elen=strlen(HOTLIST_MENU_ELLIPSIS);
   long head,tail;
   if(len<=maxlen || maxlen<=elen+1)
   {  strncpy((char *)dest,(char *)src,maxlen);
      dest[(len<maxlen)?len:maxlen]='\0';
      return;
   }
   head=(maxlen-elen+1)/2;
   tail=(maxlen-elen)/2;
   strncpy((char *)dest,(char *)src,head);
   dest[head]='\0';
   strcat((char *)dest,HOTLIST_MENU_ELLIPSIS);
   strcat((char *)dest,(char *)(src+len-tail));
}

/* TRUE if this hotlist entry is a selectable bookmark (has a title and url). */
static BOOL Is_hotlist_bookmark(struct Hotitem *hi)
{  return (BOOL)(hi->type==HITEM_ENTRY && hi->base
      && hi->base->title && hi->base->title[0]
      && hi->base->url && hi->base->url[0]);
}

/* Count the direct (one level deep) bookmarks inside a group. Bookmarks in
 * nested sub-groups are not counted: the Hotlist menu shows at most two
 * levels (a group title item and its sub-items). */
static long Count_direct_entries(LIST(Hotitem) *list)
{  struct Hotitem *hi;
   long n=0;
   for(hi=list->first;hi->next;hi=hi->next)
   {  if(Is_hotlist_bookmark(hi)) n++;
   }
   return n;
}

/* Count the NewMenu rows we will place for the root hotlist: one row per
 * top level bookmark, and for each non-empty top level group one parent row
 * plus one row per direct child bookmark. Must match Hotlistmenuaddentries. */
static long Count_hotlist_list(LIST(Hotitem) *list)
{  struct Hotitem *hi;
   long n=0,sub;
   for(hi=list->first;hi->next;hi=hi->next)
   {  if(Is_hotlist_bookmark(hi))
      {  n++;
      }
      else if(hi->type>=HITEM_GROUP && hi->name && hi->name[0])
      {  sub=Count_direct_entries(&hi->subitems);
         if(sub>0) n+=1+sub;
      }
   }
   return n;
}

long Hotlistmenuentrycount(void)
{  struct Hotbase *hb;
   long n=0;
   ObtainSemaphore(&hotsema);
   n=Count_hotlist_list(&hotlist);
   for(hb=hotbase.first;hb->next;hb=hb->next)
   {  if(!hb->used && hb->title && hb->title[0] && hb->url && hb->url[0]) n++;
   }
   ReleaseSemaphore(&hotsema);
   /* +1 for the separator placed between the management items and the live
    * bookmarks (see Hotlistmenuaddentries). */
   return n+1;
}

/* Place a single bookmark NewMenu row of the given type (NM_ITEM or NM_SUB)
 * plus a matching Menuentry whose cmd holds the raw URL for IDCMP dispatch.
 * menunum is already encoded by the caller following the GadTools convention
 * used by the prefs menus (the item/sub index is incremented BEFORE use, so
 * our numbers line up with the values Intuition reports on a menu pick). The
 * visible label is truncated in the middle so long titles stay narrow. */
static void Add_hotlist_row(struct NewMenu *newmenus,long *index,UBYTE type,
   USHORT menunum,UBYTE *label,UBYTE *url)
{  struct Menuentry *me;
   UBYTE shortlabel[HOTLIST_MENU_MAXLABEL+1];
   if(!(me=Addmenuentry(&hotlistmenus,AMENU_ITEM,label,0,url))) return;
   Truncatemiddle(shortlabel,label,HOTLIST_MENU_MAXLABEL);
   newmenus[*index].nm_Type=type;
   newmenus[*index].nm_Label=Dupstr(shortlabel,-1);
   me->menunum=menunum;
   (*index)++;
}

/* Place a non-selectable group title row (an NM_ITEM that owns sub-items, so
 * it gets no Menuentry of its own). */
static void Add_hotlist_grouptitle(struct NewMenu *newmenus,long *index,
   UBYTE *name)
{  UBYTE shortlabel[HOTLIST_MENU_MAXLABEL+1];
   Truncatemiddle(shortlabel,name,HOTLIST_MENU_MAXLABEL);
   newmenus[*index].nm_Type=NM_ITEM;
   newmenus[*index].nm_Label=Dupstr(shortlabel,-1);
   (*index)++;
}

void Hotlistmenuaddentries(struct NewMenu *newmenus,long *index,
   short mnum,short *inum,short *snum)
{  struct Hotitem *hi,*chi;
   struct Hotbase *hb;
   ObtainSemaphore(&hotsema);
   /* Separator dividing the management items from the live bookmarks. It is a
    * plain bar-label item (not selectable) so it gets no Menuentry, but it
    * does consume an item index. */
   (*inum)++;
   *snum=-1;
   newmenus[*index].nm_Type=NM_ITEM;
   newmenus[*index].nm_Label=NM_BARLABEL;
   (*index)++;
   for(hi=hotlist.first;hi->next;hi=hi->next)
   {  if(Is_hotlist_bookmark(hi))
      {  (*inum)++;
         *snum=-1;
         Add_hotlist_row(newmenus,index,NM_ITEM,
            (USHORT)(SHIFTMENU(mnum)|SHIFTITEM(*inum)|SHIFTSUB(NOSUB)),
            hi->base->title,hi->base->url);
      }
      else if(hi->type>=HITEM_GROUP && hi->name && hi->name[0]
         && Count_direct_entries(&hi->subitems)>0)
      {  (*inum)++;
         *snum=-1;
         Add_hotlist_grouptitle(newmenus,index,hi->name);
         for(chi=hi->subitems.first;chi->next;chi=chi->next)
         {  if(Is_hotlist_bookmark(chi))
            {  (*snum)++;
               Add_hotlist_row(newmenus,index,NM_SUB,
                  (USHORT)(SHIFTMENU(mnum)|SHIFTITEM(*inum)|SHIFTSUB(*snum)),
                  chi->base->title,chi->base->url);
            }
            /* Bookmarks in deeper sub-groups are intentionally ignored. */
         }
      }
   }
   for(hb=hotbase.first;hb->next;hb=hb->next)
   {  if(!hb->used && hb->title && hb->title[0] && hb->url && hb->url[0])
      {  (*inum)++;
         *snum=-1;
         Add_hotlist_row(newmenus,index,NM_ITEM,
            (USHORT)(SHIFTMENU(mnum)|SHIFTITEM(*inum)|SHIFTSUB(NOSUB)),
            hb->title,hb->url);
      }
   }
   ReleaseSemaphore(&hotsema);
}

#endif /* AWEB4 */

/*-----------------------------------------------------------------------*/

BOOL Installhotlist(void)
{  InitSemaphore(&hotsema);
   NEWLIST(&hotbase);
   NEWLIST(&hotlist);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_HOTLIST,Dispatch)) return FALSE;
   if(!hotlistname)
   {  if(!(hotlistname=Dupstr(HOTLISTNAME,-1))) return FALSE;
   }
   Readawebhotlist();
   return TRUE;
}

void Hotlistviewer(ULONG windowkey)
{  if(hotwindowv && hotwindowv->task)
   {  Asetattrsasync(hotwindowv->task,
         AOHOT_Tofront,TRUE,
         TAG_END);
   }
   else
   {  if(hotwindowv) Adisposeobject(hotwindowv);
      hotwindowv=Anewobject(AOTP_HOTLIST,
         AOHOT_Libentry,LIBENTRY_VIEWER,
         AOHOT_Windowkey,windowkey,
         TAG_END);
   }
}

void Hotlistmanager(ULONG windowkey)
{  if(hotwindowm && hotwindowm->task)
   {  Asetattrsasync(hotwindowm->task,
         AOHOT_Tofront,TRUE,
         TAG_END);
   }
   else
   {  if(hotwindowm) Adisposeobject(hotwindowm);
      hotwindowm=Anewobject(AOTP_HOTLIST,
         AOHOT_Libentry,LIBENTRY_MANAGER,
         AOHOT_Windowkey,windowkey,
         TAG_END);
   }
}

void Closehotlistviewer(void)
{  if(hotwindowv)
   {  Adisposeobject(hotwindowv);
      hotwindowv=NULL;
   }
}

void Closehotlistmanager(void)
{  if(hotwindowm)
   {  Adisposeobject(hotwindowm);
      hotwindowm=NULL;
   }
}

static void Addhtmltext(UBYTE *line,void *tf)
{  UBYTE *p,*q;
   p=line;
   for(;;)
   {  for(q=p;*q && *q!='<' && *q!='&';q++);
      if(q>p)
      {  Asetattrs(tf,
            AOFIL_Data,p,
            AOFIL_Datalength,q-p,
            TAG_END);
      }
      if(!*q) break;
      if(*q=='<') Asetattrs(tf,AOFIL_Stringdata,"&lt;",TAG_END);
      else Asetattrs(tf,AOFIL_Stringdata,"&amp;",TAG_END);
      p=q+1;
   }
}

static void Addhtml(LIST(Hotitem) *list,void *tf)
{  struct Hotitem *hi;
   for(hi=list->first;hi->next;hi=hi->next)
   {  Asetattrs(tf,AOFIL_Stringdata,"<li>",TAG_END);
      if(hi->type==HITEM_ENTRY && hi->base)
      {  Asetattrs(tf,
            AOFIL_Stringdata,"<a href=\"",
            AOFIL_Stringdata,hi->base->url,
            AOFIL_Stringdata,"\">",
            TAG_END);
         Addhtmltext(hi->base->title,tf);
         Asetattrs(tf,AOFIL_Stringdata,"</a>\n",TAG_END);
      }
      else
      {  Asetattrs(tf,AOFIL_Stringdata,"<strong>",TAG_END);
         Addhtmltext(hi->name,tf);
         Asetattrs(tf,AOFIL_Stringdata,"</strong><ul>\n",TAG_END);
         Addhtml(&hi->subitems,tf);
         Asetattrs(tf,AOFIL_Stringdata,"\n</ul>\n",TAG_END);
      }
   }
}

void Buildhtmlhotlist(void *tf)
{  UBYTE *title=AWEBSTR(MSG_AWEB_HOTLISTTITLE);
   struct Hotbase *hb;
   ObtainSemaphore(&hotsema);
   Asetattrs(tf,
      AOFIL_Stringdata,"<html><head><title>AWeb ",
      AOFIL_Stringdata,title,
      AOFIL_Stringdata,"</title></head><body><h1>AWeb ",
      AOFIL_Stringdata,title,
      AOFIL_Stringdata,"</h1>\n",
      AOFIL_Stringdata,"<ul>\n",
      TAG_END);
   Addhtml(&hotlist,tf);
   for(hb=hotbase.first;hb->next;hb=hb->next)
   {  if(!hb->used)
      {  Asetattrs(tf,
            AOFIL_Stringdata,"<li>",
            AOFIL_Stringdata,"<a href=\"",
            AOFIL_Stringdata,hb->url,
            AOFIL_Stringdata,"\">",
            TAG_END);
         Addhtmltext(hb->title,tf);
         Asetattrs(tf,AOFIL_Stringdata,"</a>\n",TAG_END);
      }
   }
   Asetattrs(tf,
      AOFIL_Stringdata,"</ul></body></html>\n",
      TAG_END);
   ReleaseSemaphore(&hotsema);
}

void Addtohotlist(void *url,UBYTE *title,UBYTE *group)
{  UBYTE *p,*q,*urlp,*path;
   long len;
   struct Hotbase *hb;
   struct Hotitem *hi,*hg;
   LIST(Hotitem) *hl;
   ULONG tag=AOHOT_Addentry;
   if(url)
   {  urlp=(UBYTE *)Agetattr(url,AOURL_Url);
      ObtainSemaphore(&hotsema);
      if(title && *title) p=title;
      else p=urlp;
      len=strlen(p);
      p=Dupstr(p,MIN(len,128));
      hb=Findbase(p,urlp);
      if(hb) hb->date=Today();
      if(p) FREE(p);
      hotchanged=TRUE;
      if(group
      && (path=Dupstr(group,-1)))
      {  /* Follow path through groups and subgroups of hotlist */
         hl=&hotlist;
         hg=NULL;
         p=path;
         while(*p)
         {  if(q=strchr(p,'\n')) *q++='\0';
            for(hi=hl->first;hi->next;hi=hi->next)
            {  if(hi->type>=HITEM_GROUP && hi->name
               && STRIEQUAL(hi->name,p))
               {  hg=hi;
                  hl=&hi->subitems;
                  if(q) p=q;
                  else p=p+strlen(p);
                  break;
               }
            }
            if(!hi->next)  /* Not found */
            {  break;
            }
         }
         if(hg && !*p)           /* Path found upto last segment, ok */
         {  /* See if this hotbase already is included in this group */
            for(hi=hg->subitems.first;hi->next;hi=hi->next)
            {  if(hi->base==hb) break;
            }
            if(!hi->next)  /* Not found, add new entry */
            {  if(hi=ALLOCSTRUCT(Hotitem,1,MEMF_CLEAR|MEMF_PUBLIC))
               {  hi->nodetype=HNT_ITEM;
                  hi->owner=hg;
                  hi->type=HITEM_ENTRY;
                  hi->base=hb;
                  NEWLIST(&hi->subitems);
                  ADDTAIL(&hg->subitems,hi);
                  hb->used++;
                  tag=AOHOT_Newlist;
               }
            }
         }
         FREE(path);
      }
      ReleaseSemaphore(&hotsema);
#ifdef AWEB4
      Hotlistmenus_refresh();
#endif
      if(hotwindowv && hotwindowv->task)
      {  Asetattrsasync(hotwindowv->task,tag,TRUE,TAG_END);
      }
      if(hotwindowm && hotwindowm->task)
      {  Asetattrsasync(hotwindowm->task,tag,TRUE,TAG_END);
      }
      Savehotlist();
   }
}

void Sethotlistname(UBYTE *name)
{  UBYTE *newname=Dupstr(name,-1);
   if(newname)
   {  if(hotlistname) FREE(hotlistname);
      hotlistname=newname;
   }
}

void Savehotlist(void)
{  ObtainSemaphore(&hotsema);
   if(hotchanged) Saveawebhotlist();
   hotchanged=FALSE;
   ReleaseSemaphore(&hotsema);
}

void Restorehotlist(void)
{  if(hotwindowv && hotwindowv->task)
   {  Asetattrssync(hotwindowv->task,AOHOT_Dispose,TRUE,TAG_END);
   }
   if(hotwindowm && hotwindowm->task)
   {  Asetattrssync(hotwindowm->task,AOHOT_Dispose,TRUE,TAG_END);
   }
   ObtainSemaphore(&hotsema);
   Disposehotlist();
   Readawebhotlist();
   hotchanged=FALSE;
   ReleaseSemaphore(&hotsema);
#ifdef AWEB4
   Hotlistmenus_refresh();
#endif
   if(hotwindowv && hotwindowv->task)
   {  Asetattrsasync(hotwindowv->task,AOHOT_Newlist,TRUE,TAG_END);
   }
   if(hotwindowm && hotwindowm->task)
   {  Asetattrsasync(hotwindowm->task,AOHOT_Newlist,TRUE,TAG_END);
   }
}

void Gethotlistcontents(struct Arexxcmd *ac,UBYTE *stem,BOOL groupsonly)
{  long stemnr=0;
   UBYTE buf[12];
   struct Hotbase *hb;
   Gethotlistgroup(&hotlist,"0",ac,stem,&stemnr,groupsonly);
   if(!groupsonly)
   {  for(hb=hotbase.first;hb->next;hb=hb->next)
      {  if(!hb->used)
         {  stemnr++;
            Setstemvar(ac,stem,stemnr,"GROUP","0");
            Setstemvar(ac,stem,stemnr,"TITLE",hb->title);
            Setstemvar(ac,stem,stemnr,"URL",hb->url);
            Setstemvar(ac,stem,stemnr,"OWNER","0");
         }
      }
   }
   sprintf(buf,"%d",stemnr);
   Setstemvar(ac,stem,0,NULL,buf);
}

void Sethotlistcontents(struct Arexxcmd *ac,UBYTE *stem)
{  if(hotwindowv && hotwindowv->task)
   {  Asetattrssync(hotwindowv->task,AOHOT_Dispose,TRUE,TAG_END);
   }
   if(hotwindowm && hotwindowm->task)
   {  Asetattrssync(hotwindowm->task,AOHOT_Dispose,TRUE,TAG_END);
   }
   ObtainSemaphore(&hotsema);
   Disposehotlist();
   Buildfromstem(ac,stem);
   hotchanged=TRUE;
   ReleaseSemaphore(&hotsema);
   if(hotwindowv && hotwindowv->task)
   {  Asetattrsasync(hotwindowv->task,AOHOT_Newlist,TRUE,TAG_END);
   }
   if(hotwindowm && hotwindowm->task)
   {  Asetattrsasync(hotwindowm->task,AOHOT_Newlist,TRUE,TAG_END);
   }
}

BOOL Isopenhotlistviewer(void)
{  return (BOOL)(hotwindowv && hotwindowv->task);
}

BOOL Isopenhotlistmanager(void)
{  return (BOOL)(hotwindowm && hotwindowm->task);
}