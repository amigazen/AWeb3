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

/* support.c - AWeb awebpluginsupport.library */

#include "aweb.h"
#include "object.h"
#include "task.h"
#include "source.h"
#include "cidregistry.h"
#include <proto/exec.h>
#include <proto/utility.h>

/*-----------------------------------------------------------------------*/

__asm struct Library *apsOpen(register __a6 struct Library *libptr,
   register __d0 long version)
{  libptr->lib_OpenCnt++;
   return libptr;
}

__asm long apsClose(register __a6 struct Library *libptr)
{  libptr->lib_OpenCnt--;
   return 0;
}

__asm long apsExpunge(register __a6 struct Library *libptr)
{  if(libptr->lib_OpenCnt==0)
   {  Remove(libptr);
   }
   return 0;
}

__asm long apsExtfunc(void)
{  return 0;
}

__asm __saveds ULONG apsAmethodA(register __a0 void *object,
   register __a1 void *amessage)
{  return AmethodA(object,amessage);
}

__asm __saveds ULONG apsAmethodasA(register __d0 long objtype,
   register __a0 void *object,
   register __a1 void *amessage)
{  return AmethodasA(objtype,object,amessage);
}

__asm __saveds void *apsAnewobjectA(register __d0 long objtype,
   register __a0 struct TagItem *taglist)
{  return (void *)Amethodas(objtype,NULL,AOM_NEW,taglist);
}

__asm __saveds void apsAdisposeobject(register __a0 void *object)
{  Adisposeobject(object);
}

__asm __saveds ULONG apsAsetattrsA(register __a0 void *object,
   register __a1 struct TagItem *taglist)
{  return Amethod(object,AOM_SET,taglist);
}

__asm __saveds ULONG apsAgetattrsA(register __a0 void *object,
   register __a1 struct TagItem *taglist)
{  return Amethod(object,AOM_GET,taglist);
}

__asm __saveds ULONG apsAgetattr(register __a0 void *object,
   register __d0 ULONG attrid)
{  return Agetattr(object,attrid);
}

__asm __saveds ULONG apsAupdateattrsA(register __a0 void *object,
   register __a1 struct TagItem *maplist,
   register __a2 struct TagItem *taglist)
{  if(maplist)
   {  MapTags(taglist,maplist,MAP_KEEP_NOT_FOUND);
   }
   return Amethod(object,AOM_UPDATE,taglist);
}

__asm __saveds ULONG apsArender(register __a0 void *object,
   register __a1 struct Coords *coords,
   register __d0 long minx,
   register __d1 long miny,
   register __d2 long maxx,
   register __d3 long maxy,
   register __d4 USHORT flags,
   register __a2 void *text)
{  return Arender(object,coords,minx,miny,maxx,maxy,flags,text);
}

__asm __saveds ULONG apsAaddchild(register __a0 void *object,
   register __a1 void *child,
   register __d0 long relation)
{  return Aaddchild(object,child,relation);
}

__asm __saveds ULONG apsAremchild(register __a0 void *object,
   register __a1 void *child,
   register __d0 long relation)
{  return Aremchild(object,child,relation);
}

__asm __saveds ULONG apsAnotify(register __a0 void *object,
   register __a1 void *amessage)
{  return Anotify(object,amessage);
}

__asm __saveds void *apsAllocobject(register __d0 long objtype,
   register __d1 long size,
   register __a0 void *amset)
{  return Allocobject(objtype,size,amset);
}

__asm __saveds ULONG apsAnotifysetA(register __a0 void *object,
   register __a1 struct TagItem *taglist)
{  struct Amset ams;
   struct Amnotify amn={0};
   ams.method=AOM_SET;
   ams.tags=taglist;
   amn.method=AOM_NOTIFY;
   amn.nmsg=&ams;
   return AmethodA(object,&amn);
}

__asm __saveds void *apsAllocmem(register __d0 long size,
   register __d1 ULONG flags)
{  return Allocmem(size,flags);
}

__asm __saveds UBYTE *apsDupstr(register __a0 UBYTE *string,
   register __d0 long length)
{  return Dupstr(string,length);
}

__asm __saveds void apsFreemem(register __a0 void *mem)
{  Freemem(mem);
}

__asm __saveds struct Coords *apsClipcoords(register __a0 void *frame,
   register __a1 struct Coords *coords)
{  return Clipcoords(frame,coords);
}

__asm __saveds void apsUnclipcoords(register __a0 struct Coords *coords)
{  Unclipcoords(coords);
}

__asm __saveds void apsErasebg(register __a0 void *frame,
   register __a1 struct Coords *coords,
   register __d0 long xmin,
   register __d1 long ymin,
   register __d2 long xmax,
   register __d3 long ymax)
{  Erasebg(frame,coords,xmin,ymin,xmax,ymax);
}

__asm __saveds void *apsAweb(void)
{  return Aweb();
}

__asm __saveds void apsAsetattrsasyncA(register __a0 void *task,
   register __a1 struct TagItem *taglist)
{  AsetattrsasyncA(task,taglist);
}

__asm __saveds ULONG apsWaittask(register __d0 ULONG signals)
{  return Waittask(signals);
}

__asm __saveds struct Taskmsg *apsGettaskmsg(void)
{  return Gettaskmsg();
}

__asm __saveds void apsReplytaskmsg(register __a0 struct Taskmsg *taskmsg)
{  Replytaskmsg(taskmsg);
}

__asm __saveds ULONG apsChecktaskbreak(void)
{  return Checktaskbreak();
}

__asm __saveds ULONG apsUpdatetask(register __a0 void *amessage)
{  return (ULONG)Updatetask(amessage);
}

__asm __saveds ULONG apsUpdatetaskattrsA(register __a0 struct TagItem *taglist)
{  return (ULONG)UpdatetaskattrsA(taglist);
}

__asm __saveds ULONG apsObtaintasksemaphore(register __a0 struct SignalSemaphore *semaphore)
{  return Obtaintasksemaphore(semaphore);
}

__asm __saveds long apsAvprintf(register __a0 char *format,
   register __a1 va_list args)
{  return vprintf(format,args);
}

__asm __saveds UBYTE *apsAwebstr(register __d0 ULONG id)
{  return AWEBSTR(id);
}

__asm __saveds void apsTcperrorA(register __a0 struct Fetchdriver *fd,
   register __d0 ULONG err,
   register __a1 ULONG *args)
{  TcperrorA(fd,err,args);
}

__asm __saveds void apsTcpmessageA(register __a0 struct Fetchdriver *fd,
   register __d0 ULONG err,
   register __a1 ULONG *args)
{  TcpmessageA(fd,err,args);
}

__asm __saveds struct Library *apsOpentcp(register __a0 struct Library **base,
   register __a1 struct Fetchdriver *fd,
   register __d0 BOOL autocon)
{
#ifndef LOCALONLY
   return Opentcp(base,fd,autocon);
#else
   return NULL;
#endif
}

__asm __saveds struct hostent *apsLookup(register __a0 UBYTE *name,
   register __a1 struct Library *base)
{  return Lookup(name,base);
}

__asm __saveds BOOL apsExpandbuffer(register __a0 struct Buffer *buf,
   register __d0 long size)
{  return Expandbuffer(buf,size);
}

__asm __saveds BOOL apsInsertinbuffer(register __a0 struct Buffer *buf,
   register __a1 UBYTE *text,
   register __d0 long length,
   register __d1 long pos)
{  return Insertinbuffer(buf,text,length,pos);
}

__asm __saveds BOOL apsAddtobuffer(register __a0 struct Buffer *buf,
   register __a1 UBYTE *text,
   register __d0 long length)
{  return Addtobuffer(buf,text,length);
}

__asm __saveds void apsDeleteinbuffer(register __a0 struct Buffer *buf,
   register __d0 long pos,
   register __d1 long length)
{  Deleteinbuffer(buf,pos,length);
}

__asm __saveds void apsFreebuffer(register __a0 struct Buffer *buf)
{  Freebuffer(buf);
}

__asm __saveds struct Locale *apsLocale(void)
{  return locale;
}

__asm __saveds long apsLprintdate(register __a0 UBYTE *buffer,
   register __a1 UBYTE *fmt,
   register __a2 struct DateStamp *ds)
{  return Lprintdate(buffer,fmt,ds);
}

__asm __saveds struct RastPort *apsObtainbgrp(register __a0 void *frame,
   register __a1 struct Coords *coords,
   register __d0 long xmin,
   register __d1 long ymin,
   register __d2 long xmax,
   register __d3 long ymax)
{  return Obtainbgrp(frame,coords,xmin,ymin,xmax,ymax);
}

__asm __saveds void apsReleasebgrp(register __a0 struct RastPort *bgrp)
{  Releasebgrp(bgrp);
}

__asm __saveds void apsSetstemvar(register __a0 struct Arexxcmd *ac,
   register __a1 UBYTE *stem,
   register __d0 long index,
   register __a2 UBYTE *field,
   register __a3 UBYTE *value)
{  Setstemvar(ac,stem,index,field,value);
}

__asm __saveds void apsCopyprefs(register __a0 struct Prefs *from,
   register __a1 struct Prefs *to)
{  Copyprefs(from,to);
}

__asm __saveds void apsSaveprefs(register __a0 struct Prefs *prefs)
{  Saveprefs(prefs);
}

__asm __saveds void apsDisposeprefs(register __a0 struct Prefs *prefs)
{  Disposeprefs(prefs);
}

__asm __saveds void apsFreemenuentry(register __a0 struct Menuentry *me)
{  Freemenuentry(me);
}

__asm __saveds struct Menuentry *apsAddmenuentry(register __a0 void *list,
   register __d0 USHORT type,
   register __a1 UBYTE *title,
   register __d1 UBYTE scut,
   register __a2 UBYTE *cmd)
{  return Addmenuentry(list,type,title,scut,cmd);
}

__asm __saveds void apsFreemimeinfo(register __a0 struct Mimeinfo *mi)
{  Freemimeinfo(mi);
}

__asm __saveds struct Mimeinfo *apsAddmimeinfo(register __a0 LIST(Mimeinfo) *list,
   register __a1 UBYTE *type,
   register __a2 UBYTE *subtype,
   register __a3 UBYTE *extensions,
   register __d0 USHORT driver,
   register __a4 UBYTE *cmd,
   register __a5 UBYTE *args)
{  return Addmimeinfo(list,type,subtype,extensions,driver,cmd,args);
}

__asm __saveds void apsFreenocache(register __a0 struct Nocache *nc)
{  Freenocache(nc);
}

__asm __saveds struct Nocache *apsAddnocache(register __a0 void *list,
   register __a1 UBYTE *name)
{  return Addnocache(list,name);
}

__asm __saveds UBYTE *apsGetstemvar(register __a0 struct Arexxcmd *ac,
   register __a1 UBYTE *stem,
   register __d0 long index,
   register __a2 UBYTE *field)
{  return Getstemvar(ac,stem,index,field);
}

__asm __saveds void apsFreeuserbutton(register __a0 struct Userbutton *ub)
{  Freeuserbutton(ub);
}

__asm __saveds struct Userbutton *apsAdduserbutton(register __a0 void *list,
   register __a1 UBYTE *label,
   register __a2 UBYTE *cmd)
{  return Adduserbutton(list,label,cmd);
}

__asm __saveds void apsFreepopupitem(register __a0 struct Popupitem *pi)
{  Freepopupitem(pi);
}

__asm __saveds struct Popupitem *apsAddpopupitem(register __a0 void *list,
   register __d0 USHORT flags,
   register __a1 UBYTE *title,
   register __a2 UBYTE *cmd)
{  return Addpopupitem(list,flags,title,cmd);
}

__asm __saveds UBYTE *apsAwebversion(void)
{  return awebversion;
}

__asm __saveds long apsSyncrequest(register __a0 UBYTE *title,
   register __a1 UBYTE *text,
   register __a2 UBYTE *labels)
{  return Syncrequest(title,text,labels,0);
}

__asm __saveds UBYTE *apsPprintfA(register __a0 UBYTE *fmt,
   register __a1 UBYTE *argspec,
   register __a2 UBYTE **params)
{  UBYTE *p;
   long len;
   len=Pformatlength(fmt,argspec,params);
   if(p=ALLOCTYPE(UBYTE,len+1,MEMF_PUBLIC))
   {  Pformat(p,fmt,argspec,params,FALSE);
   }
   return p;
}

__asm __saveds ULONG apsToday(void)
{  return Today();
}

__asm __saveds void apsFreeuserkey(register __a0 struct Userkey *uk)
{  Freeuserkey(uk);
}

__asm __saveds struct Userkey *apsAdduserkey(register __a0 void *list,
   register __d0 USHORT key,
   register __a1 UBYTE *cmd)
{  return Adduserkey(list,key,cmd);
}

__asm __saveds long apsLprintfA(register __a0 UBYTE *buffer,
   register __a1 UBYTE *fmt,
   register __a2 void *params)
{  return LprintfA(buffer,fmt,params);
}

__asm __saveds BOOL apsFullversion(void)
{
#ifdef DEMOVERSION
   return FALSE;
#else
   return TRUE;
#endif
}

__asm __saveds void apsSetfiltertype(register __a0 void *handle,
   register __a1 UBYTE *type)
{  Srcsetfiltertype(handle,type);
}

__asm __saveds void apsWritefilter(register __a0 void *handle,
   register __a1 UBYTE *data,
   register __d0 long length)
{  Srcwritefilter(handle,data,length);
}

__asm __saveds long apsAwebcommand(register __d0 long portnr,
   register __a0 UBYTE *cmd,
   register __a1 UBYTE *resultbuf,
   register __d1 long length)
{
   return Supportarexxcmd(portnr,cmd,resultbuf,length);
}

__asm __saveds long apsRegistercidpart(register __a0 UBYTE *referer_url,
   register __a1 UBYTE *content_id,
   register __a2 UBYTE *content_type,
   register __a3 UBYTE *data,
   register __d0 long datalen)
{  return (long)Registercidpart(referer_url,content_id,content_type,data,datalen);
}

__asm __saveds void apsUnregistercidparts(register __a0 UBYTE *referer_url)
{  Unregistercidparts(referer_url);
}

__asm __saveds BOOL apsAwebactive(void)
{  return Awebactive();
}

__asm __saveds void apsFreefontalias(register __a0 struct Fontalias *fa)
{  Freefontalias(fa);
}

__asm __saveds struct Fontalias *apsAddfontalias(register __a0 void *list,
   register __a1 UBYTE *alias)
{  return Addfontalias(list,alias);
}

/*-----------------------------------------------------------------------*/

#define APSVERSION      2
#define APSREVISION     3
#define APSVERSIONSTR   "2.3"

static UBYTE version[]="awebplugin.library";
static UBYTE idstring[]="awebplugin " APSVERSIONSTR " " __AMIGADATE__;

struct Jumptab
{  UWORD jmp;
   void *function;
};
#define JMP 0x4ef9

struct Jumptab jumptab[]=
{  
   JMP,apsUnregistercidparts,
   JMP,apsRegistercidpart,
   JMP,apsAddfontalias,
   JMP,apsFreefontalias,
   JMP,apsAwebactive,
   JMP,apsAwebcommand,
   JMP,apsWritefilter,
   JMP,apsSetfiltertype,
   JMP,apsFullversion,
   JMP,apsLprintfA,
   JMP,apsAdduserkey,
   JMP,apsFreeuserkey,
   JMP,apsToday,
   JMP,apsPprintfA,
   JMP,apsSyncrequest,
   JMP,apsAwebversion,
   JMP,apsAddpopupitem,
   JMP,apsFreepopupitem,
   JMP,apsAdduserbutton,
   JMP,apsFreeuserbutton,
   JMP,apsGetstemvar,
   JMP,apsAddnocache,
   JMP,apsFreenocache,
   JMP,apsAddmimeinfo,
   JMP,apsFreemimeinfo,
   JMP,apsAddmenuentry,
   JMP,apsFreemenuentry,
   JMP,apsDisposeprefs,
   JMP,apsSaveprefs,
   JMP,apsCopyprefs,
   JMP,apsSetstemvar,
   JMP,apsReleasebgrp,
   JMP,apsObtainbgrp,
   JMP,apsLprintdate,
   JMP,apsLocale,
   JMP,apsFreebuffer,
   JMP,apsDeleteinbuffer,
   JMP,apsAddtobuffer,
   JMP,apsInsertinbuffer,
   JMP,apsExpandbuffer,
   JMP,apsLookup,
   JMP,apsOpentcp,
   JMP,apsTcpmessageA,
   JMP,apsTcperrorA,
   JMP,apsAwebstr,
   JMP,apsAvprintf,
   JMP,apsObtaintasksemaphore,
   JMP,apsUpdatetaskattrsA,
   JMP,apsUpdatetask,
   JMP,apsChecktaskbreak,
   JMP,apsReplytaskmsg,
   JMP,apsGettaskmsg,
   JMP,apsWaittask,
   JMP,apsAsetattrsasyncA,
   JMP,apsAweb,
   JMP,apsErasebg,
   JMP,apsUnclipcoords,
   JMP,apsClipcoords,
   JMP,apsFreemem,
   JMP,apsDupstr,
   JMP,apsAllocmem,
   JMP,apsAnotifysetA,
   JMP,apsAllocobject,
   JMP,apsAnotify,
   JMP,apsAremchild,
   JMP,apsAaddchild,
   JMP,apsArender,
   JMP,apsAupdateattrsA,
   JMP,apsAgetattr,
   JMP,apsAgetattrsA,
   JMP,apsAsetattrsA,
   JMP,apsAdisposeobject,
   JMP,apsAnewobjectA,
   JMP,apsAmethodasA,
   JMP,apsAmethodA,
   JMP,apsExtfunc,
   JMP,apsExpunge,
   JMP,apsClose,
   JMP,apsOpen,
};

struct Library libbase=
{  {  NULL,NULL,NT_LIBRARY,0,version }, /* Node */
   0,                      /* Flags */
   0,                      /* pad */
   sizeof(jumptab),        /* NegSize */
   sizeof(struct Library), /* PosSize */
   APSVERSION,             /* Version */
   APSREVISION,            /* Revision */
   idstring,               /* IdString */
   0,                      /* Sum */
   1,                      /* OpenCnt. Initially 1 to prevent expunging. */
};

/*-----------------------------------------------------------------------*/

BOOL Initsupport(void)
{  AddLibrary(&libbase);
   return TRUE;
}

void Freesupport(void)
{  if(libbase.ln_Succ)
   {  Forbid();
      while(libbase.lib_OpenCnt>1)
      {  Permit();
         Lowlevelreq(AWEBSTR(MSG_ERROR_CANTQUIT),"awebplugin.library");
         Forbid();
      }
      libbase.lib_OpenCnt--;
      RemLibrary(&libbase);
      Permit();
   }
}
