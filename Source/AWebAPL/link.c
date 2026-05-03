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

/* link.c - AWeb html link object */

#include "aweb.h"
#include "link.h"
#include "element.h"
#include "frame.h"
#include "frprivate.h"
#include "popup.h"
#include "url.h"
#include "window.h"
#include "linkprivate.h"
#include "jslib.h"
#include "docprivate.h"
#include "css.h"
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>

/*------------------------------------------------------------------------*/

/*------------------------------------------------------------------------*/

/* Build a popup menu */
static void Popupinquire(struct Link *lnk,void *pup)
{  struct Popupitem *pi;
   BOOL inmem=Agetattr(lnk->url,AOURL_Isinmem);
   for(pi=prefs.gui.popupmenu[PUPT_LINK].first;pi->next;pi=pi->next)
   {  if((inmem && (pi->flags&PUPF_INMEM))
      || (!inmem && (pi->flags&PUPF_NOTINMEM)))
      {  Asetattrs(pup,
            AOPUP_Title,pi->title,
            AOPUP_Command,pi->cmd,
            TAG_END);
      }
   }
}

/* User has selected this popup menu choice */
static void Popupselectlink(struct Link *lnk,UBYTE *cmd)
{  void *win;
   UBYTE *frameid=NULL,*target=NULL,*p;
   UBYTE *urlname;
   short i;
   if(cmd)
   {  win=(void *)Agetattr(lnk->frame,AOBJ_Window);
      if(!lnk->target)
      {  frameid=(UBYTE *)Agetattr(lnk->frame,AOFRM_Id);
         if(frameid && (target=ALLOCTYPE(UBYTE,4*strlen(frameid)+2,0)))
         {  for(i=0,p=target;frameid[i];i++)
            {  p+=sprintf(p,"%c%d",i?'.':'#',frameid[i]);
            }
         }
      }
      urlname=Urladdfragment((UBYTE *)Agetattr(lnk->url,AOURL_Url),lnk->fragment);
      Execarexxcmd(Agetattr(win,AOWIN_Key),cmd,"uit",
         urlname,
         frameid?target:lnk->target,
         lnk->title?lnk->title:urlname);
      if(target) FREE(target);
      if(urlname) FREE(urlname);
   }
   lnk->popup=NULL;
}

/* Returns dynamic string containing URL and fragment */
static UBYTE *Linkurl(struct Link *lnk,BOOL notitle)
{  UBYTE *urlname,*buf;
   long l1,l2;
   if(lnk->title && !notitle)
   {  buf=Dupstr(lnk->title,-1);
   }
   else if(lnk->url)
   {  urlname=(UBYTE *)Agetattr(lnk->url,AOURL_Url);
      l1=l2=strlen(urlname);
      if(lnk->fragment) l2+=strlen(lnk->fragment)+1;
      if(buf=ALLOCTYPE(UBYTE,l2+1,0))
      {  strcpy(buf,urlname);
         if(lnk->fragment)
         {  buf[l1]='#';
            strcpy(buf+l1+1,lnk->fragment);
         }
      }
   }
   else
   {  buf=Dupstr("",-1);
   }
   return buf;
}

/* GET or POST this link */
static void Followlink(struct Link *lnk)
{  void *target,*win,*url,*referer;
   UBYTE *frameid,*urlname,*p,*postmsg;
   if(lnk->url)
   {  target=Targetframe(lnk->frame,lnk->target);
      Agetattrs(target,
         AOBJ_Window,&win,
         AOFRM_Id,&frameid,
         TAG_END);
      if(lnk->flags&LNKF_POST)
      {  if(urlname=(UBYTE *)Agetattr(lnk->url,AOURL_Url))
         {  referer=(void *)Agetattr(lnk->frame,AOFRM_Url);
            if(p=strchr(urlname,'?'))
            {  postmsg=Dupstr(p+1,-1);
               urlname=Dupstr(urlname,(p-urlname));
               url=Findurl(urlname,"",-1);
               if(url)
               {  Auload(url,Agetattr(win,AOWIN_Noproxy)?AUMLF_NOPROXY:0,
                     referer,postmsg,lnk->frame);
               }
               FREE(urlname);
               FREE(postmsg);
            }
            else
            {  url=Findurl(urlname,"",-1);
               if(url)
               {  Auload(url,Agetattr(win,AOWIN_Noproxy)?AUMLF_NOPROXY:0,
                     referer,"",lnk->frame);
               }
            }
         }
      }
      else
      {  url=lnk->url;
      }
      Inputwindoc(win,url,lnk->fragment,frameid);
   }
}

/*------------------------------------------------------------------------*/

/* Get or set properties (JS) */
static BOOL Commonproperty(struct Varhookdata *vd,USHORT which)
{  BOOL result=FALSE;
   struct Link *lnk=vd->hookdata;
   UBYTE *part,*buf;
   long length;
   void *url;
   if(lnk)
   {  switch(vd->code)
      {  case VHC_SET:
            part=Jtostring(vd->jc,vd->value);
            if(lnk->url && (url=Repjspart(lnk->url,which,part)))
            {  /* Must duplicate fragment string because AOM_SET frees it */
               buf=Dupstr(lnk->fragment,-1);
               Asetattrs(lnk,
                  AOLNK_Url,url,
                  AOLNK_Fragment,buf,
                  TAG_END);
               if(buf) FREE(buf);
            }
            result=TRUE;
            break;
         case VHC_GET:
            if(lnk->url)
            {  Getjspart(lnk->url,which,&part,&length);
               if(buf=Dupstr(part,length))
               {  Jasgstring(vd->jc,vd->value,buf);
                  FREE(buf);
               }
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertyhash(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Link *lnk=vd->hookdata;
   UBYTE *hash;
   if(lnk)
   {  switch(vd->code)
      {  case VHC_SET:
            hash=Jtostring(vd->jc,vd->value);
            if(hash && *hash=='#') hash++;
            if(lnk->fragment) FREE(lnk->fragment);
            lnk->fragment=Dupstr(hash,-1);
            result=TRUE;
            break;
         case VHC_GET:
            if(lnk->fragment)
            {  if(hash=ALLOCTYPE(UBYTE,strlen(lnk->fragment)+2,0))
               {  strcpy(hash,"#");
                  strcat(hash,lnk->fragment);
                  Jasgstring(vd->jc,vd->value,hash);
                  FREE(hash);
               }
            }
            else
            {  Jasgstring(vd->jc,vd->value,"");
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertyhost(struct Varhookdata *vd)
{  return Commonproperty(vd,UJP_HOST);
}

static BOOL Propertyhostname(struct Varhookdata *vd)
{  return Commonproperty(vd,UJP_HOSTNAME);
}

static BOOL Propertyhref(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Link *lnk=vd->hookdata;
   UBYTE *href,*urlname,*fragment;
   long l1,l2;
   void *url;
   if(lnk)
   {  switch(vd->code)
      {  case VHC_SET:
            href=Jtostring(vd->jc,vd->value);
            if(href)
            {  fragment=Fragmentpart(href);
               if(url=Findurl(Getjscurrenturlname(vd->jc),href,0))
               {  Asetattrs(lnk,
                     AOLNK_Url,url,
                     AOLNK_Fragment,fragment,
                     TAG_END);
               }
            }
            result=TRUE;
            break;
         case VHC_GET:
            if(lnk->url)
            {  urlname=(UBYTE *)Agetattr(lnk->url,AOURL_Url);
               l1=l2=strlen(urlname);
               if(lnk->fragment) l2+=strlen(lnk->fragment)+1;
               if(href=ALLOCTYPE(UBYTE,l2+1,0))
               {  strcpy(href,urlname);
                  if(lnk->fragment)
                  {  href[l1]='#';
                     strcpy(href+l1+1,lnk->fragment);
                  }
                  Jasgstring(vd->jc,vd->value,href);
                  FREE(href);
               }
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertypathname(struct Varhookdata *vd)
{  return Commonproperty(vd,UJP_PATHNAME);
}

static BOOL Propertyport(struct Varhookdata *vd)
{  return Commonproperty(vd,UJP_PORT);
}

static BOOL Propertyprotocol(struct Varhookdata *vd)
{  return Commonproperty(vd,UJP_PROTOCOL);
}

static BOOL Propertysearch(struct Varhookdata *vd)
{  return Commonproperty(vd,UJP_SEARCH);
}

static BOOL Propertytarget(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Link *lnk=vd->hookdata;
   UBYTE *target;
   if(lnk)
   {  switch(vd->code)
      {  case VHC_SET:
            target=Jtostring(vd->jc,vd->value);
            if(lnk->target) FREE(lnk->target);
            lnk->target=Dupstr(target,-1);
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,lnk->target);
            result=TRUE;
            break;
      }
   }
   return result;
}

static void Methodtostring(struct Jcontext *jc)
{  struct Link *lnk=Jointernal(Jthis(jc));
   UBYTE *urlname=NULL;
   if(lnk && lnk->url)
   {  urlname=(UBYTE *)Agetattr(lnk->url,AOURL_Url);
   }
   Jasgstring(jc,NULL,urlname);
}

/*------------------------------------------------------------------------*/

static long Renderlink(struct Link *lnk,struct Amrender *amr)
{  struct Coords *coo;
   struct Component *comp;
   coo=Clipcoords(lnk->cframe,amr->coords);
   if(coo->rp)
   {  for(comp=lnk->components.first;comp->next;comp=comp->next)
      {  Arender(comp->object,coo,coo->minx,coo->miny,coo->maxx,coo->maxy,
            amr->flags,amr->text);
      }
   }
   Unclipcoords(coo);
   return 0;
}

static long Setlink(struct Link *lnk,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *newfragment=NULL;
   BOOL newurl=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Frame:
            lnk->frame=(void *)tag->ti_Data;
            if(!lnk->frame && lnk->jobject)
            {  Disposejobject(lnk->jobject);
               lnk->jobject=NULL;
            }
            break;
         case AOBJ_Cframe:
            lnk->cframe=(void *)tag->ti_Data;
            break;
         case AOBJ_Window:
            if(!tag->ti_Data && lnk->win)
            {  Asetattrs(lnk->win,AOWIN_Goinactive,lnk,TAG_END);
               lnk->flags&=~(LNKF_SELECTED|LNKF_CLIPDRAG);
               if(lnk->popup)
               {  Adisposeobject(lnk->popup);
                  lnk->popup=NULL;
               }
            }
            lnk->win=(void *)tag->ti_Data;
            break;
         case AOBJ_Pool:
            lnk->pool=(void *)tag->ti_Data;
            break;
         case AOLNK_Url:
            if(lnk->url) Aremchild(lnk->url,lnk,AOREL_URL_LINK);
            if(lnk->fragment)
            {  FREE(lnk->fragment);
               lnk->fragment=NULL;
            }
            lnk->url=(void *)tag->ti_Data;
            if(lnk->url)
            {  newurl=TRUE;
               Aaddchild(lnk->url,lnk,AOREL_URL_LINK);
            }
            break;
         case AOLNK_Fragment:
            newfragment=(UBYTE *)tag->ti_Data;
            break;
         case AOLNK_Text:
            lnk->text=(struct Buffer *)tag->ti_Data;
            break;
         case AOLNK_Target:
            if(lnk->target) FREE(lnk->target);
            lnk->target=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOLNK_Title:
            if(lnk->title) FREE(lnk->title);
            lnk->title=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOLNK_Onclick:
            if(lnk->onclick) FREE(lnk->onclick);
            lnk->onclick=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOLNK_Onmouseover:
            if(lnk->onmouseover) FREE(lnk->onmouseover);
            lnk->onmouseover=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOLNK_Onmouseout:
            if(lnk->onmouseout) FREE(lnk->onmouseout);
            lnk->onmouseout=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOLNK_Post:
            SETFLAG(lnk->flags,LNKF_POST,tag->ti_Data);
            break;
         case AOLNK_NoDecoration:
            SETFLAG(lnk->flags,LNKF_NODECORATION,tag->ti_Data);
            break;
         case AOPUP_Inquire:
            Popupinquire(lnk,(void *)tag->ti_Data);
            break;
         case AOPUP_Command:
            Popupselectlink(lnk,(UBYTE *)tag->ti_Data);
            break;
         case AOURL_Visited:
            if(!(lnk->flags&LNKF_SELECTED))
            {  Arender(lnk,NULL,0,0,AMRMAX,AMRMAX,AMRF_UPDATENORMAL,lnk->text);
            }
            break;
      }
   }
   if(newurl)
   {  if(newfragment) lnk->fragment=Dupstr(newfragment,-1);
   }
   return 0;
}

static void Disposelink(struct Link *lnk)
{  struct Component *comp;
   while(comp=REMHEAD(&lnk->components))
   {  Asetattrs(comp->object,AOELT_Link,NULL,TAG_END);
      FREE(comp);
   }
   if(lnk->popup) Adisposeobject(lnk->popup);
   if(lnk->win) Asetattrs(lnk->win,AOWIN_Goinactive,lnk,TAG_END);
   if(lnk->fragment) FREE(lnk->fragment);
   if(lnk->url) Aremchild(lnk->url,lnk,AOREL_URL_LINK);
   if(lnk->target) FREE(lnk->target);
   if(lnk->title) FREE(lnk->title);
   if(lnk->onclick) FREE(lnk->onclick);
   if(lnk->onmouseover) FREE(lnk->onmouseover);
   if(lnk->onmouseout) FREE(lnk->onmouseout);
   Amethodas(AOTP_OBJECT,lnk,AOM_DISPOSE);
}

static struct Link *Newlink(struct Amset *ams)
{  struct Link *lnk;
   if(lnk=Allocobject(AOTP_LINK,sizeof(struct Link),ams))
   {  NEWLIST(&lnk->components);
      Setlink(lnk,ams);
      if(!(lnk->url || lnk->onclick || lnk->onmouseover || lnk->onmouseout))
      {  Disposelink(lnk);
         lnk=NULL;
      }
   }
   return lnk;
}

static long Getlink(struct Link *lnk,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Frame:
            PUTATTR(tag,lnk->frame);
            break;
         case AOBJ_Clipdrag:
            PUTATTR(tag,lnk->flags&LNKF_CLIPDRAG);
            break;
         case AOLNK_Url:
            PUTATTR(tag,lnk->url);
            break;
         case AOLNK_Fragment:
            PUTATTR(tag,lnk->fragment);
            break;
         case AOLNK_Visited:
            PUTATTR(tag,Agetattr(lnk->url,AOURL_Visited));
            break;
         case AOLNK_Selected:
            PUTATTR(tag,BOOLVAL(lnk->flags&LNKF_SELECTED));
            break;
         case AOLNK_Target:
            PUTATTR(tag,lnk->target);
            break;
      }
   }
   return 0;
}

/* We are called from an element that has us as its link. */
static long Hittestlink(struct Link *lnk,struct Amhittest *amh)
{  long result;
   UBYTE *buf;
   if(amh->oldobject==lnk)
   {  result=AMHR_OLDHIT|AMHR_POPUP;
   }
   else
   {  result=AMHR_NEWHIT|AMHR_POPUP;
      if(amh->amhr)
      {  amh->amhr->object=lnk;
         amh->amhr->jonmouse=lnk;
         if(buf=Linkurl(lnk,FALSE))
         {  amh->amhr->text=buf;
         }
         if(prefs.browser.handpointer) amh->amhr->ptrtype=APTR_HAND;
      }
   }
   return result;
}

static long Goactivelink(struct Link *lnk,struct Amgoactive *amg)
{  struct Coords *coo;
   struct Component *comp;
   struct Document *doc;
   void *body;
   struct Frame *fr;
   struct Aobject *ao;
   
   lnk->flags|=LNKF_SELECTED;
   coo=Clipcoords(lnk->cframe,NULL);
   if(coo->rp)
   {  for(comp=lnk->components.first;comp->next;comp=comp->next)
      {  Arender(comp->object,coo,coo->minx,coo->miny,coo->maxx,coo->maxy,
            AMRF_UPDATESELECTED,lnk->text);
      }
   }
   Unclipcoords(coo);
   
   /* Track active state for CSS :active pseudo-class */
   if(lnk->cframe)
   {  fr = (struct Frame *)lnk->cframe;
      if(fr->copy)
      {  ao = (struct Aobject *)fr->copy;
         if(ao->objecttype == AOTP_COPYDRIVER)
         {  doc = (struct Document *)fr->copy;
            /* Find the body element that contains this link */
            body = NULL;
            if(lnk->components.first)
            {  /* Get parent body from first component */
               comp = lnk->components.first;
               body = (void *)Agetattr(comp->object, AOBJ_Layoutparent);
            }
            
            /* Update active state and re-apply CSS if changed */
            if(doc->activeElement != body)
            {  doc->activeElement = body;
               /* Re-apply CSS to all elements to update :active styles */
               if(doc->cssstylesheet)
               {  ReapplyCSSToAllElements(doc);
               }
            }
         }
      }
   }
   
   return AMR_ACTIVE;
}

static long Handleinputlink(struct Link *lnk,struct Aminput *ami)
{  struct Coords coords={0};
   long result=AMR_NOCARE;
   struct Component *comp;
   struct Amhresult amhr={0};
   long x,y;
   BOOL done;
   void *win=NULL;
   void *url;
   if(ami->imsg)
   {  switch(ami->imsg->Class)
      {  case IDCMP_MOUSEMOVE:
         case IDCMP_RAWKEY:
            /* Check if mouse is still over any component */
            Framecoords(lnk->cframe,&coords);
            x=ami->imsg->MouseX-coords.dx;
            y=ami->imsg->MouseY-coords.dy;
            done=FALSE;
            result=AMR_REUSE;
            for(comp=lnk->components.first;comp->next && !done;comp=comp->next)
            {  if(x>=comp->object->aox && x<comp->object->aox+comp->object->aow
               && y>=comp->object->aoy && y<comp->object->aoy+comp->object->aoh)
               {  amhr.text=NULL;
                  switch(AMHR_RESULTMASK & 
                     Ahittest(comp->object,&coords,
                        ami->imsg->MouseX,ami->imsg->MouseY,ami->flags,lnk,&amhr))
                  {  case AMHR_OLDHIT:
                        if(ami->amir) ami->amir->text=amhr.text;
                        result=AMR_ACTIVE;
                        done=TRUE;
                        break;
                     case AMHR_NEWHIT:
                        if(amhr.text) FREE(amhr.text);
                        done=TRUE;
                        break;
                     case AMHR_NOHIT:
                        /* Check other components */
                        break;
                  }
                  if(done)
                  {  SETFLAG(lnk->flags,LNKF_CLIPDRAG,Agetattr(comp->object,AOBJ_Clipdrag));
                  }
               }
            }
            
            /* If mouse moved off link while button is down, clear active state */
            if(!done && (lnk->flags & LNKF_SELECTED))
            {  struct Document *doc;
               struct Frame *fr;
               struct Aobject *ao;
               
               if(lnk->cframe)
               {  fr = (struct Frame *)lnk->cframe;
                  if(fr->copy)
                  {  ao = (struct Aobject *)fr->copy;
                     if(ao->objecttype == AOTP_COPYDRIVER)
                     {  doc = (struct Document *)fr->copy;
                        if(doc->activeElement)
                        {  doc->activeElement = NULL;
                           /* Re-apply CSS to all elements to update :active styles */
                           if(doc->cssstylesheet)
                           {  ReapplyCSSToAllElements(doc);
                           }
                        }
                     }
                  }
               }
            }
            
            break;
         case IDCMP_MOUSEBUTTONS:
            if(ami->flags&AMHF_POPUPREL)
            {  win=(void *)Agetattr(lnk->frame,AOBJ_Window);
               lnk->popup=Anewobject(AOTP_POPUP,
                  AOPUP_Target,lnk,
                  AOPUP_Left,ami->imsg->MouseX,
                  AOPUP_Top,ami->imsg->MouseY,
                  AOPUP_Window,win,
                  TAG_END);
            }
            else if(ami->imsg->Code==SELECTUP)
            {  struct Document *doc;
               struct Frame *fr;
               struct Aobject *ao;
               
               /* Clear active state when mouse button is released */
               if(lnk->cframe)
               {  fr = (struct Frame *)lnk->cframe;
                  if(fr->copy)
                  {  ao = (struct Aobject *)fr->copy;
                     if(ao->objecttype == AOTP_COPYDRIVER)
                     {  doc = (struct Document *)fr->copy;
                        if(doc->activeElement)
                        {  doc->activeElement = NULL;
                           /* Re-apply CSS to all elements to update :active styles */
                           if(doc->cssstylesheet)
                           {  ReapplyCSSToAllElements(doc);
                           }
                        }
                     }
                  }
               }
               
               if(ami->flags&AMHF_DOWNLOAD)
               {  win=(void *)Agetattr(lnk->frame,AOBJ_Window);
                  url=(void *)Agetattr(lnk->frame,AOFRM_Url);
                  Auload(lnk->url,AUMLF_DOWNLOAD|(Agetattr(win,AOWIN_Noproxy)?AUMLF_NOPROXY:0),
                     url,NULL,lnk->frame);
               }
               else
               {  if(Runjavascript(lnk->cframe,lnk->onclick,&lnk->jobject))
                  {  Followlink(lnk);
                  }
               }
            }
            else if(ami->imsg->Code==SELECTDOWN)
            {  /* Active state is set in Goactivelink, which is called separately */
               /* This is just for reference - SELECTDOWN triggers Goactivelink */
            }
            result=AMR_NOREUSE;
            break;
      }
   }
   return result;
}

static long Goinactivelink(struct Link *lnk)
{  struct Coords *coo;
   struct Component *comp;
   struct Document *doc;
   struct Frame *fr;
   struct Aobject *ao;
   
   lnk->flags&=~(LNKF_SELECTED|LNKF_CLIPDRAG);
   coo=Clipcoords(lnk->cframe,NULL);
   if(coo && coo->rp)
   {  for(comp=lnk->components.first;comp->next;comp=comp->next)
      {  Arender(comp->object,coo,coo->minx,coo->miny,coo->maxx,coo->maxy,
            AMRF_UPDATENORMAL,lnk->text);
      }
   }
   Unclipcoords(coo);
   
   /* Clear active state for CSS :active pseudo-class */
   if(lnk->cframe)
   {  fr = (struct Frame *)lnk->cframe;
      if(fr->copy)
      {  ao = (struct Aobject *)fr->copy;
         if(ao->objecttype == AOTP_COPYDRIVER)
         {  doc = (struct Document *)fr->copy;
            /* Clear active state and re-apply CSS if it was set */
            if(doc->activeElement)
            {  doc->activeElement = NULL;
               /* Re-apply CSS to all elements to update :active styles */
               if(doc->cssstylesheet)
               {  ReapplyCSSToAllElements(doc);
               }
            }
         }
      }
   }
   
   return 0;
}

static long Addchild(struct Link *lnk,struct Amadd *ama)
{  struct Component *comp=PALLOCSTRUCT(Component,1,MEMF_CLEAR,lnk->pool);
   if(comp)
   {  comp->object=(struct Element *)ama->child;
      ADDTAIL(&lnk->components,comp);
   }
   return 0;
}

static long Remchild(struct Link *lnk,struct Amadd *ama)
{  struct Component *comp;
   for(comp=lnk->components.first;comp->next;comp=comp->next)
   {  if(comp->object==(struct Element *)ama->child)
      {  REMOVE(comp);
         FREE(comp);
         break;
      }
   }
   return 0;
}

static long Notifylink(struct Link *lnk,struct Amnotify *amn)
{  if(amn->nmsg->method=AOM_GETREXX)
   {  struct Amgetrexx *amg=(struct Amgetrexx *)amn->nmsg;
      UBYTE *buf;
      if(amg->info==AMGRI_LINKS)
      {  if(buf=Linkurl(lnk,TRUE))
         {  amg->index++;
            Setstemvar(amg->ac,amg->stem,amg->index,"URL",buf);
            Setstemvar(amg->ac,amg->stem,amg->index,"TARGET",
               lnk->target?lnk->target:NULLSTRING);
            FREE(buf);
         }
      }
   }
   return 0;
}

static long Jsetuplink(struct Link *lnk,struct Amjsetup *amj)
{  struct Jvar *jv;
   struct Jobject *links;
   if(!lnk->jobject)
   {  if(lnk->jobject=Newjobject(amj->jc))
      {  Setjobject(lnk->jobject,NULL,lnk,NULL);
         Jkeepobject(lnk->jobject,TRUE);
         if(links=Jfindarray(amj->jc,amj->parent,"links"))
         {  if(jv=Jnewarrayelt(amj->jc,links))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgobject(amj->jc,jv,lnk->jobject);
            }
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"hash"))
         {  Setjproperty(jv,Propertyhash,lnk);
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"host"))
         {  Setjproperty(jv,Propertyhost,lnk);
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"hostname"))
         {  Setjproperty(jv,Propertyhostname,lnk);
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"href"))
         {  Setjproperty(jv,Propertyhref,lnk);
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"pathname"))
         {  Setjproperty(jv,Propertypathname,lnk);
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"port"))
         {  Setjproperty(jv,Propertyport,lnk);
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"protocol"))
         {  Setjproperty(jv,Propertyprotocol,lnk);
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"search"))
         {  Setjproperty(jv,Propertysearch,lnk);
         }
         if(jv=Jproperty(amj->jc,lnk->jobject,"target"))
         {  Setjproperty(jv,Propertytarget,lnk);
         }
         Addjfunction(amj->jc,lnk->jobject,"toString",Methodtostring,NULL);
      }
   }
   return 0;
}

static long Jonmouselink(struct Link *lnk,struct Amjonmouse *amj)
{  switch(amj->event)
   {  case AMJE_ONMOUSEOVER:
         Runjavascript(lnk->cframe,lnk->onmouseover,&lnk->jobject);
         break;
      case AMJE_ONMOUSEOUT:
         Runjavascript(lnk->cframe,lnk->onmouseout,&lnk->jobject);
         break;
   }
   return 0;
}

static long Dispatch(struct Link *lnk,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newlink((struct Amset *)amsg);
         break;
      case AOM_SET:
      case AOM_UPDATE:
         result=Setlink(lnk,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getlink(lnk,(struct Amset *)amsg);
         break;
      case AOM_RENDER:
         result=Renderlink(lnk,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestlink(lnk,(struct Amhittest *)amsg);
         break;
      case AOM_GOACTIVE:
         result=Goactivelink(lnk,(struct Amgoactive *)amsg);
         break;
      case AOM_HANDLEINPUT:
         result=Handleinputlink(lnk,(struct Aminput *)amsg);
         break;
      case AOM_GOINACTIVE:
         result=Goinactivelink(lnk);
         break;
      case AOM_ADDCHILD:
         result=Addchild(lnk,(struct Amadd *)amsg);
         break;
      case AOM_REMCHILD:
         result=Remchild(lnk,(struct Amadd *)amsg);
         break;
      case AOM_NOTIFY:
         result=Notifylink(lnk,(struct Amnotify *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetuplink(lnk,(struct Amjsetup *)amsg);
         break;
      case AOM_JONMOUSE:
         result=Jonmouselink(lnk,(struct Amjonmouse *)amsg);
         break;
      case AOM_DISPOSE:
         Disposelink(lnk);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installlink(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_LINK,Dispatch)) return FALSE;
   return TRUE;
}
