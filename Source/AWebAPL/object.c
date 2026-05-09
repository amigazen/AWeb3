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

/* object.c - AWeb re-usable objects */

#include "aweb.h"
#include "jslib.h"
#include "object.h"
#include <proto/utility.h>

typedef ULONG dispatcher(void *,struct Amessage *);
static ULONG Dispatch(struct Aobject *ao,struct Amessage *amsg);

#define NR_OBJECTTYPES  256
static dispatcher *dispatchers[NR_OBJECTTYPES]={ Dispatch };

/*------------------------------------------------------------------------*/

#ifdef NODEBUG
#undef DEVELOPER
#endif

#ifdef DEVELOPER

static UBYTE oodebug[NR_OBJECTTYPES];
static UBYTE oomethod[101];
static UBYTE oodelay;
BOOL ookdebug;

extern void KPrintF(UBYTE *,...);

static UBYTE *classname[NR_OBJECTTYPES]=
{  "OBJECT","APPLICATION","URL","CACHE","FETCH","SOURCE","COPY","TASK",
   "08","09","0A","0B","0C","0D","0E","0F",
   "SCROLLER","FILE","FILEREQ","EDITOR","TIMER","15","16","17",
   "18","19","1A","1B","1C","1D","1E","1F",
   "WINDOW","WINHIS","WHISWINDOW","NETSTATWIN","CABROWSE","HOTLIST","POPUP","SEARCH",
   "PRINTWINDOW","PRINT","INFO","AUTHEDIT","SAVEIFF","2D","2E","2F",
   "DOCSOURCE","DOCUMENT","FRAMESET","BODY","LINK","MAP","FORM","AREA",
   "ELEMENT","39","3A","3B","3C","3D","3E","3F",
   "BREAK","TEXT","RULER","BULLET","TABLE","NAME","FRAME","FIELD",
   "INPUT","CHECKBOX","RADIO","SELECT","TEXTAREA","BUTTON","HIDDEN","FILEFIELD",
   "50","51","52","53","54","55","56","57",
   "58","59","5A","5B","5C","5D","5E","5F",
   "IMGSOURCE","IMGCOPY","EXTPROG","SAVEAS","SOUNDSOURCE","SOUNDCOPY","DOCEXT","67",
   "68","69","6A","6B","6C","6D","6E","6F",
   "70","SOURCEDRIVER","COPYDRIVER",
};

static UBYTE *methodname[101]=
{  "","NEW","SET","GET","DISPOSE","RENDER","HITTEST","GOACTIVE","HANDLEINPUT",
   "GOINACTIVE","UPDATE","MEASURE","LAYOUT","ALIGN","SRCUPDATE","ADDCHILD",
   "REMCHILD","NOTIFY","MOVE","SEARCHPOS","SEARCHSET","GETREXX",
   "DRAGTEST","DRAGRENDER","DRAGCOPY","JSETUP","JONMOUSE",
};

void Setoodebug(UBYTE *types)
{  UBYTE *p,*q;
   short i;
   BOOL end=FALSE;
   methodname[98]="INSTALL";
   methodname[99]="DEINSTALL";
   methodname[100]="OTHER";
   for(p=types;!end && *p;p=q+1)
   {  q=strchr(p,',');
      if(!q)
      {  q=p+strlen(p);
         end=TRUE;
      }
      for(i=0;i<NR_OBJECTTYPES;i++)
      {  if(classname[i] && (STRNIEQUAL(classname[i],p,q-p) || STRNIEQUAL("ALL",p,q-p)))
         {  oodebug[i]=1;
         }
         else if(i>=AOTP_PLUGIN && (STRNIEQUAL("ALL",p,q-p) || STRNIEQUAL("OTHER",p,q-p)))
         {  oodebug[i]=1;
         }
      }
   }
}

void Setoomethod(UBYTE *types)
{  UBYTE *p,*q;
   short i,v;
   BOOL end=FALSE;
   methodname[98]="INSTALL";
   methodname[99]="DEINSTALL";
   methodname[100]="OTHER";
   for(p=types;!end && *p;p=q+1)
   {  q=strchr(p,',');
      if(!q)
      {  q=p+strlen(p);
         end=TRUE;
      }
      if(*p=='~')
      {  p++;
         v=0;
      }
      else v=1;
      for(i=0;i<101;i++)
      {  if(methodname[i] && (STRNIEQUAL(methodname[i],p,q-p) || STRNIEQUAL("ALL",p,q-p)))
         {  oomethod[i]=v;
         }
      }
   }
}

void Setoodelay(void)
{  oodelay=1;
}

typedef void pfunc(UBYTE *,...);
   
static void Tagdebug(pfunc *prf,struct TagItem *tags)
{  struct TagItem *tag,*tstate=tags;
   while(tag=NextTagItem(&tstate))
   {  prf("%08lx=%08lx ",tag->ti_Tag,tag->ti_Data);
   }
}   

static void Oodebug(short type,struct Aobject *ao,struct Amessage *amsg)
{  pfunc *prf;
   if(ookdebug) prf=(pfunc *)KPrintF;
   else prf=(pfunc *)printf;
   if(type>=0 && type<NR_OBJECTTYPES && classname[type])
   {  prf(">%-8.8s %08lx: %s ",classname[type],ao,
         methodname[amsg->method<100?amsg->method:100]);
   }
   else
   {  prf("?%08lx %08lx: %s ",type,ao,
         methodname[amsg->method<100?amsg->method:100]);
   }
   switch(amsg->method)
   {  case AOM_NEW:
      case AOM_SET:
      case AOM_GET:
      case AOM_UPDATE:
         Tagdebug(prf,((struct Amset *)amsg)->tags);
         break;
      case AOM_RENDER:
         {  struct Amrender *amr=(struct Amrender *)amsg;
            prf("%08lx[%+ld,%+ld] (%ld,%ld)-(%ld,%ld) %04lx %08lx",
               amr->coords,amr->coords?amr->coords->dx:0,amr->coords?amr->coords->dy:0,
               amr->minx,amr->miny,amr->maxx,amr->maxy,amr->flags,amr->text);
         }
         break;
      case AOM_MEASURE:
         {  struct Ammeasure *amm=(struct Ammeasure *)amsg;
            prf("(%ld,%ld) %+ld %04lx %08lx %08lx",
               amm->width,amm->height,amm->addwidth,amm->flags,amm->text,amm->ammr);
         }
         break;
      case AOM_LAYOUT:
         {  struct Amlayout *aml=(struct Amlayout *)amsg;
            prf("(%ld,%ld) %04lx %08lx %ld %08lx",
               aml->width,aml->height,aml->flags,aml->text,aml->startx,aml->amlr);
         }
         break;
      case AOM_ALIGN:
         {  struct Amalign *ama=(struct Amalign *)amsg;
            prf("%ld %ld %ld %ld",ama->dx,ama->y,ama->baseline,ama->height);
         }
         break;
      case AOM_ADDCHILD:
      case AOM_REMCHILD:
         {  struct Amadd *ama=(struct Amadd *)amsg;
            struct Aobject *ao=ama->child;
            prf("%08lx %ls (%lx)",ao,ao?classname[ao->objecttype]:NULLSTRING,ama->relation);
         }
         break;
      case AOM_MOVE:
         {  struct Ammove *amm=(struct Ammove *)amsg;
            prf("(%ld,%ld)",amm->dx,amm->dy);
         }
         break;
      case AOM_SRCUPDATE:
         {  struct Amsrcupdate *ams=(struct Amsrcupdate *)amsg;
            prf("%08lx ",ams->fetch);
            Tagdebug(prf,ams->tags);
         }
         break;
      case AOM_NOTIFY:
         {  struct Amnotify *amn=(struct Amnotify *)amsg;
            prf(" ->\n");
            Oodebug(type,ao,amn->nmsg);
         }
         break;
      case AOM_HITTEST:
         {  struct Amhittest *amh=(struct Amhittest *)amsg;
            prf("%08lx[%+ld,%+ld] (%ld,%ld) %04lx %08lx %08lx",
               amh->coords,amh->coords?amh->coords->dx:0,amh->coords?amh->coords->dy:0,
               amh->xco,amh->yco,amh->flags,amh->oldobject,amh->amhr);
         }
         break;
      case AOM_HANDLEINPUT:
         {  struct Aminput *ami=(struct Aminput *)amsg;
            prf("%08lx (%08lx) %04lx %08lx",ami->imsg,ami->imsg?ami->imsg->Class:0,
               ami->flags,ami->amir);
         }
         break;
      case AOM_SEARCHPOS:
      case AOM_SEARCHSET:
         {  struct Amsearch *ams=(struct Amsearch *)amsg;
            prf("%04lx %ld,%ld %08lx %ld %ld",ams->flags,ams->left,ams->top,
               ams->text,ams->pos,ams->startpos);
         }
         break;
      case AOM_DRAGTEST:
         {  struct Amdragtest *amd=(struct Amdragtest *)amsg;
            prf("%08lx[%+ld,%+ld] (%ld,%ld) %08lx",
               amd->coords,amd->coords?amd->coords->dx:0,amd->coords?amd->coords->dy:0,
               amd->xco,amd->yco,amd->amdr);
         }
         break;
      case AOM_DRAGRENDER:
         {  struct Amdragrender *amd=(struct Amdragrender *)amsg;
            prf("%08lx[%+ld,%+ld] %08lx(%ld) %08lx(%ld) %04lx",
               amd->coords,amd->coords?amd->coords->dx:0,amd->coords?amd->coords->dy:0,
               amd->startobject,amd->startobjpos,amd->endobject,amd->endobjpos,amd->state);
         }
         break;
      case AOM_DRAGCOPY:
         {  struct Amdragcopy *amd=(struct Amdragcopy *)amsg;
            prf("%08lx(%ld) %08lx(%ld) %04lx %08lx",
               amd->startobject,amd->startobjpos,amd->endobject,amd->endobjpos,
               amd->state,amd->clip);
         }
         break;
      case AOM_JSETUP:
         {  struct Amjsetup *amj=(struct Amjsetup *)amsg;
            prf("%08lx %08lx %08lx",amj->jc,amj->parent,amj->parentframe);
         }
         break;
      case AOM_JONMOUSE:
         {  struct Amjonmouse *amj=(struct Amjonmouse *)amsg;
            prf("%ld",amj->event);
         }
         break;
      default:
         if(amsg->method>=100)
         {  ULONG *p=(ULONG *)&amsg->method;
            prf("%ld %08lx %08lx %08lx %08lx",p[0],p[1],p[2],p[3],p[4]);
         }
   }
   prf("\n");
   if(oodelay) Delay(10);
}

#endif /* DEVELOPER */

/*------------------------------------------------------------------------*/

static void Disposeao(struct Aobject *ao)
{  FREE(ao);
}

static ULONG Install(struct Aminstall *ami)
{  ULONG result=0;
   if(ami->objecttype>0 && ami->objecttype<NR_OBJECTTYPES)
   {  dispatchers[ami->objecttype]=ami->dispatcher;
      result=ami->objecttype;
   }
   else if(ami->dispatcher)
   {  long id;
      for(id=AOTP_PLUGIN;id<NR_OBJECTTYPES;id++)
      {  if(!dispatchers[id])
         {  result=id;
            dispatchers[id]=ami->dispatcher;
            break;
         }
      }
   }
   return result;
}

static ULONG Deinstall(void)
{  short i;
   struct Amessage amsg={ AOM_DEINSTALL };
   for(i=1;i<NR_OBJECTTYPES;i++)
   {  if(dispatchers[i])
      {  dispatchers[i](NULL,&amsg);
      }
   }
   return 0;
}

static ULONG Dispatch(struct Aobject *ao,struct Amessage *amsg)
{  ULONG result=0;
   switch(amsg->method)
   {  case AOM_DISPOSE:
         if(ao) Disposeao(ao);
         break;
      case AOM_INSTALL:
         result=Install((struct Aminstall *)amsg);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

// Emergency dispatcher in case of stack overflow
static ULONG Emergencydisp(struct Amessage *amsg)
{  switch(amsg->method)
   {  case AOM_LAYOUT:
         {  struct Amlayout *aml=(struct Amlayout *)amsg;
            if(aml->amlr)
            {  aml->amlr->result=AMLR_OK;
            }
         }
         break;
   }
   return 0;
}

/*------------------------------------------------------------------------*/

#ifdef DEVELOPER
static void Oodebugtest(long tp,void *ao,struct Amessage *amsg)
{  if(oodebug[tp]
   && ((amsg->method<100 && oomethod[amsg->method])
         || (amsg->method>=100 && oomethod[100])))
   {  Oodebug(tp,ao,amsg);
   }
}
#define OODEBUG(tp,ao,msg) Oodebugtest(tp,ao,(struct Amessage *)(msg))
#else
#define OODEBUG(tp,ao,msg)
#endif

ULONG AmethodasA(short objtype,struct Aobject *ao,struct Amessage *amsg)
{  dispatcher *disp=dispatchers[objtype];
   if(Stackoverflow()) return Emergencydisp(amsg);
   OODEBUG(objtype,ao,amsg);
   return disp?disp(ao,amsg):0;
}

ULONG Amethodas(short objtype,struct Aobject *ao,ULONG method,...)
{  dispatcher *disp=dispatchers[objtype];
   if(Stackoverflow()) return Emergencydisp((struct Amessage *)&method);
   OODEBUG(objtype,ao,&method);
   return disp?disp(ao,(struct Amessage *)&method):0;
}

ULONG AmethodA(struct Aobject *ao,struct Amessage *amsg)
{  dispatcher *disp=dispatchers[ao?ao->objecttype:0];
   if(Stackoverflow()) return Emergencydisp(amsg);
   OODEBUG(ao?ao->objecttype:0,ao,amsg);
   return disp?disp(ao,amsg):0;
}

ULONG Amethod(struct Aobject *ao,ULONG method,...)
{  dispatcher *disp=dispatchers[ao?ao->objecttype:0];
   if(Stackoverflow()) return Emergencydisp((struct Amessage *)&method);
   OODEBUG(ao?ao->objecttype:0,ao,&method);
   return disp?disp(ao,(struct Amessage *)&method):0;
}

void *Anewobject(short type,...)
{  return (void *)Amethodas(type,NULL,AOM_NEW,VARARG(type));
}

ULONG Asetattrs(struct Aobject *ao,...)
{  dispatcher *disp=dispatchers[ao?ao->objecttype:0];
   struct Amset ams;
   ams.method=AOM_SET;
   ams.tags=VARARG(ao);
   OODEBUG(ao?ao->objecttype:0,ao,&ams);
   return disp?disp(ao,&ams):0;
}

ULONG Agetattrs(struct Aobject *ao,...)
{  dispatcher *disp=dispatchers[ao?ao->objecttype:0];
   struct Amset ams;
   ams.method=AOM_GET;
   ams.tags=VARARG(ao);
   OODEBUG(ao?ao->objecttype:0,ao,&ams);
   return disp?disp(ao,&ams):0;
}

ULONG Agetattr(struct Aobject *ao,ULONG attrid)
{  dispatcher *disp=dispatchers[ao?ao->objecttype:0];
   ULONG value=0;
   struct TagItem tags[2];
   struct Amset ams;
   tags[0].ti_Tag=attrid;
   tags[0].ti_Data=(ULONG)&value;
   tags[1].ti_Tag=TAG_END;
   ams.method=AOM_GET;
   ams.tags=tags;
   OODEBUG(ao?ao->objecttype:0,ao,&ams);
   if(disp) disp(ao,&ams);
   return value;
}

ULONG Aupdateattrs(struct Aobject *ao,struct TagItem *maplist,...)
{  struct TagItem *tags=VARARG(maplist);
   if(maplist)
   {  MapTags(tags,maplist,MAP_KEEP_NOT_FOUND);
   }
   return Amethod(ao,AOM_UPDATE,tags);
}

void Adisposeobject(struct Aobject *ao)
{  Amethod(ao,AOM_DISPOSE);
}

void Freeobject(void)
{  Deinstall();
}

long Gettagsdata(struct TagItem *tags,...)
{  struct TagItem *storetags=VARARG(tags);
   struct TagItem *tag,*tstate=storetags,*vtag;
   ULONG *p;
   while(tag=NextTagItem(&tstate))
   {  p=(ULONG *)tag->ti_Data;
      if(vtag=FindTagItem(tag->ti_Tag,tags))
      {  *p=vtag->ti_Data;
      }
   }
   return 0;
}

ULONG Arender(struct Aobject *ao,struct Coords *coo,
   long minx,long miny,long maxx,long maxy,ULONG flags,struct Buffer *text)
{  struct Amrender amr;
   amr.method=AOM_RENDER;
   amr.coords=coo;
   amr.minx=minx;
   amr.maxx=maxx;
   amr.miny=miny;
   amr.maxy=maxy;
   amr.flags=flags;
   amr.text=text;
   return AmethodA(ao,&amr);
}

ULONG Ahittest(struct Aobject *ao,struct Coords *coo,
   long xco,long yco,USHORT flags,void *oldobject,struct Amhresult *amhr)
{  struct Amhittest amh;
   amh.method=AOM_HITTEST;
   amh.coords=coo;
   amh.xco=xco;
   amh.yco=yco;
   amh.flags=flags;
   amh.oldobject=oldobject;
   amh.amhr=amhr;
   return AmethodA(ao,&amh);
}

ULONG Agoactive(struct Aobject *ao,struct IntuiMessage *imsg,USHORT flags)
{  struct Amgoactive amg;
   amg.method=AOM_GOACTIVE;
   amg.imsg=imsg;
   amg.flags=flags;
   return AmethodA(ao,&amg);
}

ULONG Ahandleinput(struct Aobject *ao,struct IntuiMessage *imsg,
   USHORT flags,struct Amiresult *amir)
{  struct Aminput ami;
   ami.method=AOM_HANDLEINPUT;
   ami.imsg=imsg;
   ami.flags=flags;
   ami.amir=amir;
   return AmethodA(ao,&ami);
}

ULONG Agoinactive(struct Aobject *ao)
{  struct Amessage am;
   am.method=AOM_GOINACTIVE;
   return AmethodA(ao,&am);
}

ULONG Ameasure(struct Aobject *ao,long width,long height,long addwidth,
   USHORT flags,struct Buffer *text,struct Ammresult *ammr)
{  struct Ammeasure amm;
   amm.method=AOM_MEASURE;
   amm.width=width;
   amm.height=height;
   amm.addwidth=addwidth;
   amm.flags=flags;
   amm.text=text;
   amm.ammr=ammr;
   return AmethodA(ao,&amm);
}

ULONG Alayout(struct Aobject *ao,long width,long height,
   USHORT flags,struct Buffer *text,long startx,struct Amlresult *amlr)
{  struct Amlayout aml;
   aml.method=AOM_LAYOUT;
   aml.width=width;
   aml.height=height;
   aml.flags=flags;
   aml.text=text;
   aml.startx=startx;
   aml.amlr=amlr;
   return AmethodA(ao,&aml);
}

ULONG Aalign(struct Aobject *ao,long dx,long y,long baseline,long height)
{  struct Amalign ama;
   ama.method=AOM_ALIGN;
   ama.dx=dx;
   ama.y=y;
   ama.baseline=baseline;
   ama.height=height;
   return AmethodA(ao,&ama);
}

ULONG Asrcupdate(struct Aobject *ao,struct Aobject *fch,struct TagItem *tags)
{  struct Amsrcupdate ams;
   ams.method=AOM_SRCUPDATE;
   ams.fetch=fch;
   ams.tags=tags;
   return AmethodA(ao,&ams);
}

ULONG Aaddchild(struct Aobject *ao,struct Aobject *child,ULONG relation)
{  struct Amadd ama;
   ama.method=AOM_ADDCHILD;
   ama.child=child;
   ama.relation=relation;
   return AmethodA(ao,&ama);
}

ULONG Aremchild(struct Aobject *ao,struct Aobject *child,ULONG relation)
{  struct Amadd ama;
   ama.method=AOM_REMCHILD;
   ama.child=child;
   ama.relation=relation;
   return AmethodA(ao,&ama);
}

ULONG Amove(struct Aobject *ao,long dx,long dy)
{  struct Ammove amm;
   amm.method=AOM_MOVE;
   amm.dx=dx;
   amm.dy=dy;
   return AmethodA(ao,&amm);
}

ULONG Adragtest(struct Aobject *ao,struct Coords *coo,long xco,long yco,
   struct Amdresult *amdr)
{  struct Amdragtest amd;
   amd.method=AOM_DRAGTEST;
   amd.coords=coo;
   amd.xco=xco;
   amd.yco=yco;
   amd.amdr=amdr;
   return AmethodA(ao,&amd);
}

ULONG Adragrender(struct Aobject *ao,struct Coords *coo,void *startobject,
   ULONG startobjpos,void *endobject,ULONG endobjpos,ULONG state)
{  struct Amdragrender amd;
   amd.method=AOM_DRAGRENDER;
   amd.coords=coo;
   amd.startobject=startobject;
   amd.startobjpos=startobjpos;
   amd.endobject=endobject;
   amd.endobjpos=endobjpos;
   amd.state=state;
   return AmethodA(ao,&amd);
}

ULONG Adragcopy(struct Aobject *ao,void *startobject,ULONG startobjpos,
   void *endobject,ULONG endobjpos,ULONG state,struct Buffer *clip)
{  struct Amdragcopy amd;
   amd.method=AOM_DRAGCOPY;
   amd.startobject=startobject;
   amd.startobjpos=startobjpos;
   amd.endobject=endobject;
   amd.endobjpos=endobjpos;
   amd.state=state;
   amd.clip=clip;
   return AmethodA(ao,&amd);
}

ULONG Ajsetup(struct Aobject *ao,struct Jcontext *jc,struct Jobject *parent,
   struct Jobject *parentframe)
{  struct Amjsetup amj;
   ULONG result;
   static long ajsetup_nest = 0;
   /* Ajsetup() can be triggered re-entrantly (e.g. document.write() while parsing calls Parsehtml()
    * which causes object setup). The old global Ajsetup(Aweb()) path was guarded in higher layers,
    * but now we call Ajsetup() more locally and can hit recursion. Avoid deadlocks by skipping
    * nested Ajsetup calls; callers will re-run setup at the next safe point. */
   if(ajsetup_nest > 0)
   {
      return 0;
   }
   ajsetup_nest++;
   amj.amsg.method=AOM_JSETUP;
   amj.jc=jc;
   amj.parent=parent;
   amj.parentframe=parentframe;
   /* Only call Jallowgc if jc is valid and AWebJSBase is initialized */
   if(jc && AWebJSBase)
   {  Jallowgc(jc,FALSE);
   }
   result=AmethodA(ao,(struct Amessage *)&amj);
   if(jc && AWebJSBase)
   {  Jallowgc(jc,TRUE);
   }
   ajsetup_nest--;
   return result;
}

ULONG Ajonmouse(struct Aobject *ao,USHORT event)
{  struct Amjonmouse amj;
   amj.amsg.method=AOM_JONMOUSE;
   amj.event=event;
   return AmethodA(ao,(struct Amessage *)&amj);
}

ULONG Anotify(struct Aobject *ao,struct Amessage *nmsg)
{  struct Amnotify amn;
   amn.amsg.method=AOM_NOTIFY;
   amn.nmsg=nmsg;
   return AmethodA(ao,(struct Amessage *)&amn);
}

void *Allocobject(short type,short size,struct Amset *ams)
{  struct Aobject *ao;
   void *pool=(void *)GetTagData(AOBJ_Pool,NULL,ams->tags);
   if(pool)
   {  ao=(struct Aobject *)Pallocmem(size,MEMF_CLEAR|MEMF_PUBLIC,pool);
   }
   else
   {  ao=(struct Aobject *)Allocmem(size,MEMF_CLEAR|MEMF_PUBLIC);
   }
   if(ao)
   {  ao->objecttype=type;
   }
   return ao;
}

ULONG Asrcupdatetags(struct Aobject *ao,struct Aobject *fch,...)
{  struct Amsrcupdate ams;
   ams.method=AOM_SRCUPDATE;
   ams.fetch=fch;
   ams.tags=VARARG(fch);
   return AmethodA(ao,&ams);
}

ULONG Anotifyset(struct Aobject *ao,...)
{  struct Amset ams;
   struct Amnotify amn={0};
   ams.method=AOM_SET;
   ams.tags=VARARG(ao);
   amn.amsg.method=AOM_NOTIFY;
   amn.nmsg=&ams;
   return AmethodA(ao,(struct Amessage *)&amn);
}

