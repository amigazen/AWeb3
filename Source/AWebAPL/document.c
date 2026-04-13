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

/* document.c - AWeb document object */

#include "aweb.h"
#include "docprivate.h"
#include "document.h"
#include "frame.h"
#include "winhis.h"
#include "url.h"
#include "source.h"
#include "copy.h"
#include "application.h"
#include "window.h"
#include "body.h"
#include "table.h"
#include "info.h"
#include "map.h"
#include "jslib.h"
#include "xhrjs.h"
#include "css.h"
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#define DQID_ONLOAD     1     /* Queueid: run onLoad JavaScript */

/*------------------------------------------------------------------------*/

static ULONG Rgbexpand(UBYTE c)
{  ULONG a=c|(c<<8);
   return a|(a<<16);
}

static void Obtaincolorinfo(struct Colorinfo *ci)
{  ULONG r,g,b;
   struct ColorMap *cmap;
   if(ci->pen<=0)
   {  r=Rgbexpand((ci->rgb>>16)&0xff);
      g=Rgbexpand((ci->rgb>>8)&0xff);
      b=Rgbexpand(ci->rgb&0xff);
      cmap=(struct ColorMap *)Agetattr(Aweb(),AOAPP_Colormap);
      ci->pen=ObtainBestPen(cmap,r,g,b,TAG_END);
   }
}

static void Releasecolorinfo(struct Colorinfo *ci)
{  struct ColorMap *cmap;
   if(ci->pen>=0)
   {  cmap=(struct ColorMap *)Agetattr(Aweb(),AOAPP_Colormap);
      ReleasePen(cmap,ci->pen);
      ci->pen=-1;
   }
}

static void Disposecolorinfo(struct Colorinfo *ci)
{  if(ci)
   {  if(ci->pen>=0) Releasecolorinfo(ci);
      FREE(ci);
   }
}

/*------------------------------------------------------------------------*/

static void Disposefragment(struct Fragment *f)
{  if(f)
   {  if(f->name) FREE(f->name);
      FREE(f);
   }
}

/* Find fragment position */
static long Fragmentpos(struct Document *doc,UBYTE *name)
{  struct Fragment *frag;
   for(frag=doc->fragments.first;frag->next;frag=frag->next)
   {  if(STRIEQUAL(frag->name,name))
      {  if(frag->elt->eltflags&ELTF_ALIGNED) return frag->elt->aoy;
         else break;
      }
   }
   return -1;
}

/*------------------------------------------------------------------------*/

static void Disposebguser(struct Bguser *bgu)
{  if(bgu)
   {  FREE(bgu);
   }
}

static void Disposebgimage(struct Bgimage *bgi)
{  void *p;
   if(bgi)
   {  if(bgi->copy) Adisposeobject(bgi->copy);
      while(p=REMHEAD(&bgi->bgusers)) Disposebguser(p);
      FREE(bgi);
   }
}

static void Disposeinfotext(struct Infotext *it)
{  if(it)
   {  if(it->text) FREE(it->text);
      FREE(it);
   }
}

/* Send info text lines */
static void Makeinfo(struct Document *doc,void *inf)
{  struct Infotext *it;
   BOOL meta=FALSE,link=FALSE;
   for(it=doc->infotexts.first;it->next;it=it->next)
   {  if(it->link) link=TRUE;
      else meta=TRUE;
   }
   if(meta)
   {  Asetattrs(inf,
         AOINF_Text,AWEBSTR(MSG_INFO_META),
         AOINF_Header,TRUE,
         TAG_END);
      for(it=doc->infotexts.first;it->next;it=it->next)
      {  if(!it->link)
         {  Asetattrs(inf,AOINF_Text,it->text,TAG_END);
         }
      }
   }
   if(link)
   {  Asetattrs(inf,
         AOINF_Text,AWEBSTR(MSG_INFO_LINK),
         AOINF_Header,TRUE,
         TAG_END);
      for(it=doc->infotexts.first;it->next;it=it->next)
      {  if(it->link)
         {  Asetattrs(inf,
               AOINF_Text,it->text,
               AOINF_Link,it->link,
               TAG_END);
         }
      }
   }
}

/* Get the base URL (dynamic string) */
static UBYTE *Getbaseurl(struct Document *doc)
{  void *url=(void *)Agetattr(doc->source->source,AOSRC_Url);
   UBYTE *urlname=(UBYTE *)Agetattr(url,AOURL_Url);
   if(STRNEQUAL(urlname,"x-jsgenerated:",14))
   {  UBYTE *p=strchr(urlname+14,'/');
      if(p)
      {  urlname=p+1;
      }
   }
   return Dupstrp(urlname,-1,doc->pool);
}

/*------------------------------------------------------------------------*/

/* Reload, dispose everything and re-initialize. */
static void Reloaddocument(struct Document *doc)
{  void *p,*url;
   UBYTE *newbase,*start;
   long length;
   extern BOOL httpdebug;
   if(httpdebug)
   {  void *docurl;
      UBYTE *urlstr;
      docurl = (void *)Agetattr(doc->source->source,AOSRC_Url);
      urlstr = docurl ? (UBYTE *)Agetattr(docurl,AOURL_Url) : NULL;
      printf("[RELOAD] Reloaddocument: Starting reload for document, URL=%s, stylesheet=%p, body=%p\n",
             urlstr ? (char *)urlstr : "NULL", doc->cssstylesheet, doc->body);
   }
   Asetattrs(doc->frame,
      AOFRM_Bgcolor,-1,
      AOFRM_Textcolor,-1,
      AOFRM_Linkcolor,-1,
      AOFRM_Vlinkcolor,-1,
      AOFRM_Alinkcolor,-1,
      AOFRM_Bgimage,NULL,
      TAG_END);
   Freebuffer(&doc->text);
   Freebuffer(&doc->args);
   Freebuffer(&doc->jsrc);
   Freebuffer(&doc->jout);
   Freebuffer(&doc->csssrc);
   FreeCSSStylesheet(doc);
   if(doc->body)
   {  Adisposeobject(doc->body);
      doc->body=NULL;
   }
   while(p=REMHEAD(&doc->tables)) FREE(p);
   while(p=REMHEAD(&doc->frames)) FREE(p);
   while(p=REMHEAD(&doc->framesets)) FREE(p);
   while(p=REMHEAD(&doc->bgimages)) Disposebgimage(p);
   while(p=REMHEAD(&doc->colors)) Disposecolorinfo(p);
   while(p=REMHEAD(&doc->links)) Adisposeobject(p);
   while(p=REMHEAD(&doc->maps)) Adisposeobject(p);
   while(p=REMHEAD(&doc->forms)) Adisposeobject(p);
   while(p=REMHEAD(&doc->fragments)) Disposefragment(p);
   while(p=REMHEAD(&doc->infotexts)) Disposeinfotext(p);
   doc->bgimage=NULL;
   if(doc->bgsound)
   {  Adisposeobject(doc->bgsound);
      doc->bgsound=NULL;
      if(doc->win) Asetattrs(doc->win,AOWIN_Bgsound,FALSE,TAG_END);
   }
   if(doc->target)
   {  FREE(doc->target);
      doc->target=NULL;
   }
   if(doc->clientpull)
   {  FREE(doc->clientpull);
      doc->clientpull=NULL;
   }
   if(doc->onload)
   {  FREE(doc->onload);
      doc->onload=NULL;
   }
   if(doc->onunload)
   {  FREE(doc->onunload);
      doc->onunload=NULL;
   }
   if(doc->onfocus)
   {  FREE(doc->onfocus);
      doc->onfocus=NULL;
   }
   if(doc->onblur)
   {  FREE(doc->onblur);
      doc->onblur=NULL;
   }
   if(newbase=Getbaseurl(doc))
   {  if(doc->base) FREE(doc->base);
      doc->base=newbase;
   }
   if(doc->jdomain) FREE(doc->jdomain);
   url=(void *)Agetattr(doc->source->source,AOSRC_Url);
   Getjspart(url,UJP_HOST,&start,&length);
   doc->jdomain=Dupstrp(start,length,doc->pool);
   Addtobuffer(&doc->text," ",1);
   doc->srcpos=0;
   doc->htmlmode=prefs.htmlmode;
   /* Preserve DPF_RELOADVERIFY flag during reload so CSS and other external
    * resources are properly reloaded instead of using cached versions */
   {  ULONG savedReloadVerify = (doc->pflags & DPF_RELOADVERIFY);
      doc->pflags=0;
      if(savedReloadVerify) doc->pflags|=DPF_RELOADVERIFY;
   }
   doc->pmode=0;
   if(doc->currentlistclass) FREE(doc->currentlistclass);
   doc->currentlistclass=NULL;
   doc->gridcolgap=0;
   doc->currentdivinline=FALSE;
   doc->charcount=0;
   doc->frameseqnr=0;
   doc->select=NULL;
   doc->textarea=NULL;
   doc->gotbreak=2;
   doc->wantbreak=0;
   doc->doctype=DOCTP_NONE;
   doc->dflags=0;
   doc->viewportwidth=0;  /* Reset viewport width to default (use window inner width) */
   doc->bgcolor=NULL;
   doc->textcolor=NULL;
   doc->linkcolor=NULL;
   doc->vlinkcolor=NULL;
   doc->alinkcolor=NULL;
   Freejdoc(doc);
   if(doc->frame) doc->dflags|=DDF_DISPTITLE;
   SETFLAG(doc->pflags,DPF_SCRIPTJS,(doc->source->flags&DOSF_SCRIPTJS));
   if(httpdebug)
   {  printf("[RELOAD] Reloaddocument: Reload complete, stylesheet=%p (should be NULL), body=%p (should be NULL), pflags=0x%lx (reloadverify=%d)\n",
             doc->cssstylesheet, doc->body, (ULONG)doc->pflags, (doc->pflags & DPF_RELOADVERIFY) ? 1 : 0);
   }
}

static void Forwardtomap(struct Document *doc,UBYTE *name,struct Amset *ams)
{  struct Aobject *map;
   for(map=doc->maps.first;map->next;map=map->next)
   {  if(STRIEQUAL((UBYTE *)Agetattr(map,AOMAP_Name),name))
      {  AmethodA(map,ams);
      }
   }
}

/*------------------------------------------------------------------------*/

static long Parsedocument(struct Document *doc)
{  BOOL eof=(doc->source->flags&DOSF_EOF) && !(doc->source->flags&DOSF_JSOPEN);
   struct Buffer *src=&doc->source->buf;
   if(Agetattr(doc->source->source,AOSRC_Foreign)) doc->dflags|=DDF_FOREIGN;
   if(src && !(doc->dflags&DDF_DONE))
   {  if(doc->source->flags&DOSF_HTML)
      {  Parsehtml(doc,src,eof,&doc->srcpos);
      }
      else if(doc->source->flags&DOSF_MD)
      {  Parsemarkdown(doc,src,eof,&doc->srcpos);
      }
      else
      {  Parseplain(doc,src,eof,&doc->srcpos);
      }
      if(eof)
      {  doc->dflags|=DDF_DONE;
         if(doc->onload) Queuesetmsg(doc,DQID_ONLOAD);
      }
   }
   return 0;
}

static long Measuredocument(struct Document *doc,struct Ammeasure *amm)
{  if(doc->body)
   {  Ameasure(doc->body,amm->width,amm->height,0,amm->flags,&doc->text,amm->ammr);
   }
   return 0;
}

static long Layoutdocument(struct Document *doc,struct Amlayout *aml)
{  long result=0;
   if(doc->body)
   {  result=Alayout(doc->body,aml->width,aml->height,aml->flags,&doc->text,0,aml->amlr);
      Agetattrs(doc->body,
         AOBJ_Width,&doc->aow,
         AOBJ_Height,&doc->aoh,
         TAG_END);
   }
   return result;
}

static long Renderdocument(struct Document *doc,struct Amrender *amr)
{  BOOL clip=FALSE;
   ULONG clipkey=0;
   struct Coords coords={0},*coo=NULL;
   ULONG frameid=0;
   void *whis=NULL;
   if(doc->dflags&DDF_DISPTITLE)
   {  if(doc->dflags&DDF_TITLEVALID)
      {  Agetattrs(doc->frame,
            AOFRM_Id,&frameid,
            AOBJ_Winhis,&whis,
            TAG_END);
         Asetattrs(whis,
            AOWHS_Frameid,frameid,
            AOWHS_Title,doc->text.buffer+doc->titlepos,
            TAG_END);
      }
      if(amr->flags&AMRF_DISPTITLE)
      {  UBYTE *title=NULL;
         if(doc->dflags&DDF_TITLEVALID)
         {  title=doc->text.buffer+doc->titlepos;
         }
         else
         {  void *url=(void *)Agetattr(doc->source->source,AOSRC_Url);
            title=(UBYTE *)Agetattr(url,AOURL_Url);
         }
         if(title) Asetattrs(doc->win,AOWIN_Title,title,TAG_END);
      }
      if(doc->dflags&DDF_TITLEVALID) doc->dflags&=~DDF_DISPTITLE;
   }
   coo=amr->coords;
   if(!coo)
   {  Framecoords(doc->frame,&coords);
      coo=&coords;
      clip=TRUE;
   }
   if(coo->rp)
   {  if(clip) clipkey=Clipto(coo->rp,coo->minx,coo->miny,coo->maxx,coo->maxy);
      if(doc->body)
      {  Arender(doc->body,coo,amr->rect.minx,amr->rect.miny,amr->rect.maxx,amr->rect.maxy,
            amr->flags,&doc->text);
      }
      else if(amr->flags&AMRF_CLEAR)
      {  Erasebg(doc->frame,coo,amr->rect.minx,amr->rect.miny,amr->rect.maxx,amr->rect.maxy);
      }
      if(clip) Unclipto(clipkey);
   }
   return 0;
}

/* Global counter for background image updates */
ULONG bgupdate = 0;

/* Changebackground: When a background image changes, only re-render the
 * specific bodies/elements that use it, rather than forcing a complete
 * document redraw. This provides dramatic speed improvements. */
static long Changebackground(struct Document *doc, struct Bgimage *bgimage)
{  struct Bgimage *bgi;
   struct Body *bd;
   void *tc;
   struct Aobject *tab;
   long tabx,taby;
   /* find the Bgimage for this background.  */
   /* If the document has no frame, it's not displayed so don't try and render */
   /* This prevents background images from rendering into document copies that have no frame */
   if(doc->frame)
   {  for(bgi=doc->bgimages.first;bgi->next;bgi=bgi->next)
      {  if(bgi->copy==bgimage)
         {  struct Bguser *bgu;
            /* increment the bgupdate key */
            bgupdate++;
            /* Work through the list of elements using this background */
            /* Render as required*/
            for(bgu=bgi->bgusers.first;bgu->next;bgu=bgu->next)
            {  struct Coords *coo = NULL;
               long minx,miny,maxx,maxy;
               long bx,by,bw,bh;
               coo = Clipcoords(doc->cdv.cframe,coo);
               if(coo)
               {  bd = (struct Body *)bgu->user;
                  if(((struct Aobject *)bd)->objecttype == AOTP_BODY)
                  {  if(bgupdate > Agetattr((struct Aobject *)bd,AOBDY_Bgupdate))
                     {  tab = (struct Aobject *)Agetattr((struct Aobject *)bd,AOBJ_Layoutparent);
                        /* In order to clear the background for the body properly */
                        /* we need the dimensions of the cell or frame. */
                        if((tc = (void *)Agetattr((struct Aobject *)bd,AOBDY_Tcell)))
                        {  /* We're a table cell so get our dims from cell structure */
                           Agetattrs(tab,
                              AOBJ_Left,(Tag)&tabx,
                              AOBJ_Top,(Tag)&taby,
                              TAG_END);
                           /* Get cell coordinates and size using Aobject attributes */
                           Agetattrs((struct Aobject *)bd,
                              AOBJ_Left,(Tag)&bx,
                              AOBJ_Top,(Tag)&by,
                              AOBJ_Width,(Tag)&bw,
                              AOBJ_Height,(Tag)&bh,
                              TAG_END);
                           /* The coords are relative to the table which is our layout parent */
                           bx += tabx;
                           by += taby;
                        }
                        else if(tab && tab->objecttype == AOTP_TABLE)
                        {  /* Not a cell but a caption */
                           Agetattrs((struct Aobject *)bd,
                              AOBJ_Left,(Tag)&bx,
                              AOBJ_Top,(Tag)&by,
                              AOBJ_Width,(Tag)&bw,
                              AOBJ_Height,(Tag)&bh,
                              TAG_END);
                        }
                        else
                        {  /* Must be a frame body - render the whole frame */
                           bx = coo->minx - coo->dx;
                           by = coo->miny - coo->dy;
                           bw = coo->maxx - coo->minx + 1;
                           bh = coo->maxy - coo->miny + 1;
                        }
                        minx = MAX(coo->minx - coo->dx,bx);
                        miny = MAX(coo->miny - coo->dy,by);
                        maxx = MIN(coo->maxx - coo->dx,bx + bw - 1);
                        maxy = MIN(coo->maxy - coo->dy,by + bh - 1);
                        Anotifyset((struct Aobject *)bd,AOBJ_Bgchanged,TRUE,TAG_END);
                        Arender((struct Aobject *)bd,coo,minx,miny,maxx,maxy,AMRF_CLEAR,NULL);
                     }
                  }
                  else
                  {  /* For other element types, get their coordinates */
                     Agetattrs((struct Aobject *)bd,
                        AOBJ_Left,&bx,
                        AOBJ_Top,&by,
                        AOBJ_Width,&bw,
                        AOBJ_Height,&bh,
                        TAG_END);
                     minx = MAX(coo->minx - coo->dx,bx);
                     miny = MAX(coo->miny - coo->dy,by);
                     maxx = MIN(coo->maxx - coo->dx,bx + bw - 1);
                     maxy = MIN(coo->maxy - coo->dy,by + bh - 1);
                     Arender((struct Aobject *)bd,coo,minx,miny,maxx,maxy,AMRF_CLEAR,NULL);
                  }
                  Unclipcoords(coo);
               }
            }
            /* Notify the background image copy object itself if it's an animated GIF.
             * This ensures animated GIF backgrounds update their animation frames
             * when the background changes. */
            if(bgi->copy)
            {  Asetattrs(bgi->copy,AOCPY_Onimganim,TRUE,TAG_END);
            }
         }
      }
   }
   return 0;
}

static void Srcupdatedocument(struct Document *doc)
{  if(!(doc->pflags&DPF_SUSPEND))
   {  if(doc->frame)
      {  /* Only parse if we have data AND (content type is determined OR EOF reached).
           * This prevents Parseplain() from being called prematurely for HTML content
           * (e.g., about:blank) before DOSF_HTML flag is set. Wait for content type
           * to be determined before parsing to avoid incorrect plain text parsing. */
         BOOL eof;
         BOOL hasContentType;
         long srcpos_before;

         eof=(doc->source->flags&DOSF_EOF) && !(doc->source->flags&DOSF_JSOPEN);
         hasContentType=(doc->source->flags&(DOSF_HTML|DOSF_MD))!=0;
         if(doc->source->buf.length>0 && (eof || hasContentType))
         {  /* Only ask the frame for measure/layout/render when new input was actually
             * consumed. A separate EOF Srcupdate after the last data chunk does not move
             * srcpos; notifying AOBJ_Changedchild then repainted the whole viewport twice. */
            srcpos_before=doc->srcpos;
            Parsedocument(doc);
            if(doc->srcpos!=srcpos_before)
            {  /* While an external stylesheet is still loading (Dolink suspended after
                * <link rel=stylesheet>), skip notifying the copy so we do not lay out
                * the tree without merged CSS; one refresh runs after merge or on resume. */
               if(!(doc->pflags&DPF_EXTCSSEXPECT))
               {  Asetattrs(doc->copy,AOBJ_Changedchild,doc,TAG_END);
               }
            }
         }
      }
      else if(doc->dflags&DDF_MAPDOCUMENT)
      {  Parsedocument(doc);
      }
   }
}

static long Setdocument(struct Document *doc,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   struct Colorinfo *ci;
   struct Aobject *link,*form,*map;
   BOOL setwin=FALSE,setframe=FALSE,setwhis=FALSE,setjscancel=FALSE;
   void *whis;
   struct Bgimage *bgi;
   UBYTE *start;
   long length;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Copy:
            doc->copy=(void *)tag->ti_Data;
            break;
         case AOCDV_Sourcedriver:
            doc->source=(struct Docsource *)tag->ti_Data;
            Getjspart((void *)Agetattr(doc->source->source,AOSRC_Url),UJP_HOST,&start,&length);
            if(doc->jdomain) FREE(doc->jdomain);
            doc->jdomain=Dupstr(start,length);
            break;
         case AOCDV_Mapdocument:
            SETFLAG(doc->dflags,DDF_MAPDOCUMENT,tag->ti_Data);
            break;
         case AOBJ_Window:
            doc->win=(void *)tag->ti_Data;
            if(doc->win)
            {  for(ci=doc->colors.first;ci->next;ci=ci->next)
               {  if(ci->pen<0 && !(doc->dflags&DDF_NOBACKGROUND)) Obtaincolorinfo(ci);
               }
               if(doc->bgsound) Asetattrs(doc->win,AOWIN_Bgsound,TRUE,TAG_END);
            }
            else
            {  for(ci=doc->colors.first;ci->next;ci=ci->next)
               {  if(ci->pen>=0) Releasecolorinfo(ci);
               }
               doc->dflags|=DDF_DISPTITLE;
            }
            setwin=TRUE;
            break;
         case AOBJ_Frame:
            if(!doc->frame && tag->ti_Data)  /* set new frame from NULL */
            {  doc->dflags|=DDF_DISPTITLE;
               SETFLAG(doc->dflags,DDF_PLAYBGSOUND,prefs.dobgsound);
               if(doc->clientpull)
               {  Asetattrs(doc->copy,AOURL_Clientpull,doc->clientpull,TAG_END);
               }
            }
            else if(doc->frame && !tag->ti_Data)   /* clear frame */
            {  
               if(doc->onunload || AWebJSBase)
               {  Runjsnobanners(doc->frame,awebonunload,NULL);               
               }
               Freejdoc(doc);
/*
               doc->batch=NULL;
*/
            }
            doc->frame=(void *)tag->ti_Data;
            if(doc->frame)
            {  Asetattrs(doc->frame,
                  AOFRM_Onfocus,doc->onfocus,
                  AOFRM_Onblur,doc->onblur,
                  TAG_END);
            }
            setframe=TRUE;
            break;
         case AOBJ_Winhis:
            whis=(void *)tag->ti_Data;
            setwhis=TRUE;
            break;
         case AODOC_Srcupdate:
            Srcupdatedocument(doc);
            break;
         case AODOC_Reload:
            Reloaddocument(doc);
            break;
         case AOBJ_Changedchild:
            if(doc->frame) Asetattrs(doc->frame,AOBJ_Changedchild,doc->copy,TAG_END);
            break;
         case AOBJ_Nobackground:
            SETFLAG(doc->dflags,DDF_NOBACKGROUND,tag->ti_Data);
            /* When set, notify our children (when spare was reused) */
            if(tag->ti_Data && doc->body)
            {  Anotifyset(doc->body,AOBJ_Nobackground,TRUE,TAG_END);
            }
            break;
         case AOBJ_Changedbgimage:
            if(tag->ti_Data) Changebackground(doc,(struct Bgimage *)tag->ti_Data);
            break;
         case AOCDV_Bgchanged:
            if(tag->ti_Data)
            {  Anotifyset(doc->body,AOBJ_Bgchanged,TRUE,TAG_END);
            }
            break;
         case AOCDV_Reloadverify:
            SETFLAG(doc->pflags,DPF_RELOADVERIFY,tag->ti_Data);
            break;
         case AOCDV_Marginwidth:
            /* Frame margins are applied as body left margin. This creates visual
             * spacing within the viewport rather than reducing the viewport width.
             * The viewport width remains the full window inner width, and margins
             * are applied as content area reduction. */
            if(!(doc->dflags&DDF_HMARGINSET))
            {  doc->hmargin=tag->ti_Data;
               if(doc->body) Asetattrs(doc->body,AOBDY_Leftmargin,doc->hmargin,TAG_END);
            }
            break;
         case AOCDV_Marginheight:
            /* Frame margins applied as body top margin (see comment above) */
            if(!(doc->dflags&DDF_VMARGINSET))
            {  doc->vmargin=tag->ti_Data;
               if(doc->body) Asetattrs(doc->body,AOBDY_Topmargin,doc->vmargin,TAG_END);
            }
            break;
         case AOCDV_Viewportwidth:
            /* Viewport width from meta viewport tag. If > 0, overrides default
             * viewport width (window inner width). Value of 0 means use default. */
            doc->viewportwidth=tag->ti_Data;
            break;
         case AOINF_Inquire:
            Makeinfo(doc,(void *)tag->ti_Data);
            break;
         case AOBJ_Queueid:
            if(tag->ti_Data==DQID_ONLOAD && doc->frame)
            {  Runjsnobanners(doc->frame,awebonload,NULL);
            }
            break;
         case AOBJ_Jscancel:
            setjscancel|=tag->ti_Data;
            break;
         case AODOC_Docextready:
            {  void *url;
               UBYTE *extcss;
               UBYTE *urlstr;
               UBYTE *contenttype;
               long urllen;
               BOOL isCSS;
               BOOL hasContentType;
               /* Check if this is a CSS file and merge it */
               url = (void *)tag->ti_Data;
               isCSS = FALSE;
               hasContentType = FALSE;
               if(url)
               {  /* Check content-type first */
                  contenttype = (UBYTE *)Agetattr(url,AOURL_Contenttype);
                  if(contenttype)
                  {  hasContentType = TRUE;
                     if(strnicmp((char *)contenttype,"text/css",8) == 0)
                     {  isCSS = TRUE;
                     }
                  }
                  /* Only fall back to ".css" URL extension when content-type is missing.
                   * Some servers (or error paths) return HTML for a .css URL; treating that as CSS
                   * results in 0 parsed rules and "sometimes styled" behavior depending on timing. */
                  if(!isCSS && !hasContentType)
                  {  UBYTE *query;
                     urlstr = (UBYTE *)Agetattr(url,AOURL_Url);
                     if(urlstr)
                     {  /* Find query string (?) if present */
                        query = (UBYTE *)strchr((char *)urlstr,'?');
                        if(query)
                        {  urllen = (long)(query - urlstr);
                        }
                        else
                        {  urllen = strlen((char *)urlstr);
                        }
                        /* Check if URL ends with .css (case insensitive) before query string */
                        if(urllen >= 4 && strnicmp((char *)&urlstr[urllen-4],".css",4) == 0)
                        {  isCSS = TRUE;
                        }
                     }
                  }
                  if(isCSS)
                  {  extern BOOL httpdebug;
                     UBYTE *urlstr;
                     urlstr = (UBYTE *)Agetattr(url,AOURL_Url);
                     if(httpdebug)
                     {  printf("[STYLE] AODOC_Docextready: CSS file ready, URL=%s, existing stylesheet=%p\n",
                               urlstr ? (char *)urlstr : "NULL", doc->cssstylesheet);
                     }
                     /* Try to get the CSS content */
                     extcss = Finddocext(doc,url,FALSE);
                     if(extcss && extcss != (UBYTE *)~0)
                     {  BOOL payloadIsCss;
                        payloadIsCss = TRUE;
                        if(httpdebug)
                        {  printf("[STYLE] AODOC_Docextready: CSS file loaded, calling MergeCSSStylesheet\n");
                        }
                        /* Defensive: if payload looks like HTML, don't attempt to parse as CSS.
                         * This commonly happens when a .css URL returns an error page. */
                        if(extcss[0] == '<')
                        {  if(httpdebug)
                           {  printf("[STYLE] AODOC_Docextready: Content looks like HTML, skipping CSS merge\n");
                           }
                           payloadIsCss = FALSE;
                        }
                        if(payloadIsCss)
                        {  /* Check if this CSS was already merged via Dolink.
                            * If DPF_NORLDOCEXT is set AND stylesheet exists, it means Dolink already processed it.
                            * If stylesheet is NULL, the flag might be from a previous document, so merge anyway. */
                           if(!(doc->pflags&DPF_NORLDOCEXT) || !doc->cssstylesheet)
                           {  /* Merge external CSS with existing stylesheet */
                              MergeCSSStylesheet(doc,extcss);
                              /* Set DPF_NORLDOCEXT to prevent Dolink from merging this CSS again
                               * if it's called later (e.g., during parsing resume after suspend) */
                              doc->pflags|=DPF_NORLDOCEXT;
                           }
                           else if(httpdebug)
                           {  printf("[STYLE] AODOC_Docextready: CSS already merged via Dolink, skipping duplicate merge\n");
                           }
                           /* Apply link colors from CSS (a:link, a:visited) */
                           ApplyCSSToLinkColors(doc);
                           /* Always re-apply CSS to body when external CSS loads */
                           if(doc->body && doc->cssstylesheet)
                           {  if(httpdebug)
                              {  printf("[STYLE] AODOC_Docextready: Re-applying CSS to body, body=%p, stylesheet=%p\n",
                                       doc->body, doc->cssstylesheet);
                              }
                              ApplyCSSToBody(doc,doc->body,NULL,NULL,"BODY");
                              /* Ensure CSS is applied to the full existing tree */
                           ApplyDocCssIfReady(doc);
                              /* Re-register colors to ensure link colors are updated */
                              if(doc->win && doc->frame)
                              {  Registerdoccolors(doc);
                              }
                           }
                           else if(httpdebug)
                           {  printf("[STYLE] AODOC_Docextready: Cannot apply CSS - body=%p, stylesheet=%p\n",
                                     doc->body, doc->cssstylesheet);
                           }
                        }
                     }
                     else if(extcss == (UBYTE *)~0)
                     {  if(httpdebug)
                        {  printf("[STYLE] AODOC_Docextready: CSS file load error\n");
                        }
                     }
                     else
                     {  if(httpdebug)
                        {  printf("[STYLE] AODOC_Docextready: CSS file not yet available\n");
                        }
                     }
                  }
               }
               if(doc->pflags&DPF_SUSPEND)
               {  long srcpos_before;
                  doc->pflags&=~(DPF_SUSPEND|DPF_EXTCSSEXPECT);
                  doc->dflags&=~DDF_DONE;
                  srcpos_before=doc->srcpos;
                  Srcupdatedocument(doc);
                  /* If the HTML side had nothing left to consume, srcpos does not move
                   * and Srcupdatedocument would not notify the copy; still repaint once
                   * now that CSS (or failure) is known. */
                  if(doc->frame && doc->copy && doc->srcpos==srcpos_before)
                  {  Asetattrs(doc->copy,AOBJ_Changedchild,doc,TAG_END);
                  }
               }
            }
            break;
      }
   }
   Amethodas(AOTP_COPYDRIVER,doc,AOM_SET,ams->tags);
   /* If window was set or reset, or frame was set, register our pens with our frame. */
   if(doc->frame && (setwin || setframe))
   {  Registerdoccolors(doc);
   }
   /* If window or frame was set or reset, pass to child */
   if(doc->body && (setwin || setframe || setwhis || setjscancel))
   {  Asetattrs(doc->body,
         setwin?AOBJ_Window:TAG_IGNORE,doc->win,
         setframe?AOBJ_Frame:TAG_IGNORE,doc->frame,
         setframe?AOBJ_Cframe:TAG_IGNORE,doc->frame,
         setwhis?AOBJ_Winhis:TAG_IGNORE,whis,
         setjscancel?AOBJ_Jscancel:TAG_IGNORE,TRUE,
         TAG_END);
   }
   if(setwin || setframe)
   {  for(form=doc->forms.first;form->next;form=form->next)
      {  Asetattrs(form,
            setwin?AOBJ_Window:TAG_IGNORE,doc->win,
            setframe?AOBJ_Frame:TAG_IGNORE,doc->frame,
            setframe?AOBJ_Cframe:TAG_IGNORE,doc->frame,
            TAG_END);
      }
      for(bgi=doc->bgimages.first;bgi->next;bgi=bgi->next)
      {  Asetattrs(bgi->copy,
            setwin?AOBJ_Window:TAG_IGNORE,doc->win,
            setframe?AOBJ_Frame:TAG_IGNORE,doc->frame,
            setframe?AOBJ_Cframe:TAG_IGNORE,doc->frame,
            TAG_END);
      }
      if(doc->bgsound && (doc->dflags&DDF_PLAYBGSOUND))
      {  Asetattrs(doc->bgsound,
            setwin?AOBJ_Window:TAG_IGNORE,doc->win,
            setframe?AOBJ_Frame:TAG_IGNORE,doc->frame,
            setframe?AOBJ_Cframe:TAG_IGNORE,doc->frame,
            TAG_END);
      }
      for(link=doc->links.first;link->next;link=link->next)
      {  Asetattrs(link,
            setwin?AOBJ_Window:TAG_IGNORE,doc->win,
            setframe?AOBJ_Frame:TAG_IGNORE,doc->frame,
            setframe?AOBJ_Cframe:TAG_IGNORE,doc->cframe,
            TAG_END);
      }
   }
   if(setwin)
   {  for(map=doc->maps.first;map->next;map=map->next)
      {  Asetattrs(map,
            AOBJ_Window,doc->win,
            TAG_END);
      }
   }
   return 0;
}

static void Disposedocument(struct Document *doc)
{  void *p;
   struct Aobject *lnk;
   struct Colorinfo *ci;
   struct Bgimage *bgi;
   short i;
   for(i=0;i<doc->divancsp && i<DIV_ANCESTOR_STACK_MAX;i++)
   {  if(doc->divanc[i].tagname) FREE(doc->divanc[i].tagname);
      if(doc->divanc[i].class) FREE(doc->divanc[i].class);
      if(doc->divanc[i].id) FREE(doc->divanc[i].id);
      doc->divanc[i].tagname=NULL;
      doc->divanc[i].class=NULL;
      doc->divanc[i].id=NULL;
   }
   doc->divancsp=0;
   Queuesetmsg(doc,0);
   Remwaitingdoc(doc);
   if(!doc->source || (doc->dflags&(DDF_ISSPARE|DDF_MAPDOCUMENT|DDF_NOSPARE)))
   {        Freebuffer(&doc->text);
      Freebuffer(&doc->args);
      Freebuffer(&doc->jsrc);
      Freebuffer(&doc->jout);
      Freebuffer(&doc->csssrc);
      if(doc->base) FREE(doc->base);
      FreeCSSStylesheet(doc);
      if(doc->body) Adisposeobject(doc->body);
      while(p=REMHEAD(&doc->tables)) FREE(p);
      while(p=REMHEAD(&doc->framesets)) FREE(p);
      while(p=REMHEAD(&doc->frames)) FREE(p);
      while(p=REMHEAD(&doc->bgimages)) Disposebgimage(p);
      while(p=REMHEAD(&doc->colors)) Disposecolorinfo(p);
      while(p=REMHEAD(&doc->links)) Adisposeobject(p);
      while(p=REMHEAD(&doc->maps)) Adisposeobject(p);
      while(p=REMHEAD(&doc->forms)) Adisposeobject(p);
      while(p=REMHEAD(&doc->fragments)) Disposefragment(p);
      while(p=REMHEAD(&doc->infotexts)) Disposeinfotext(p);
      if(doc->bgsound) Adisposeobject(doc->bgsound);
      if(doc->target) FREE(doc->target);
      if(doc->clientpull) FREE(doc->clientpull);
      if(doc->onload) FREE(doc->onload);
      if(doc->onunload) FREE(doc->onunload);
      if(doc->onfocus) FREE(doc->onfocus);
      if(doc->onblur) FREE(doc->onblur);
      if(doc->jdomain) FREE(doc->jdomain);
      if(doc->pool) DeletePool(doc->pool);
      if(doc->frame) Asetattrs(doc->frame,AOFRM_Bgimage,NULL,TAG_END);
      if(doc->win) Asetattrs(doc->win,AOWIN_Bgsound,FALSE,TAG_END);
      Freejdoc(doc);
      Amethodas(AOTP_COPYDRIVER,doc,AOM_DISPOSE);
   }
   else
   {  for(ci=doc->colors.first;ci->next;ci=ci->next)
      {  if(ci->pen>=0) Releasecolorinfo(ci);
      }
      if(doc->frame)
      {  long embedded=0,background=0;
         Agetattrs(doc->copy,
            AOCPY_Embedded,&embedded,
            AOCPY_Background,&background,
            TAG_END);
         if(!embedded && !background)
         {  Asetattrs(doc->frame,
               AOFRM_Bgcolor,-1,
               AOFRM_Textcolor,-1,
               AOFRM_Linkcolor,-1,
               AOFRM_Vlinkcolor,-1,
               AOFRM_Alinkcolor,-1,
               AOFRM_Bgimage,NULL,
               TAG_END);
         }
      }
      doc->frame=NULL;
      Asetattrs(doc->body,
         AOBJ_Frame,NULL,
         AOBJ_Window,NULL,
         AOBJ_Cframe,NULL,
         TAG_END);
      for(lnk=doc->links.first;lnk->next;lnk=lnk->next)
      {  Asetattrs(lnk,
            AOBJ_Frame,NULL,
            AOBJ_Window,NULL,
            AOBJ_Cframe,NULL,
            TAG_END);
      }
      for(bgi=doc->bgimages.first;bgi->next;bgi=bgi->next)
      {  Asetattrs(bgi->copy,
            AOBJ_Frame,NULL,
            AOBJ_Window,NULL,
            AOBJ_Cframe,NULL,
            TAG_END);
      }
      if(doc->bgsound)
      {  Asetattrs(doc->bgsound,
            AOBJ_Frame,NULL,
            AOBJ_Window,NULL,
            AOBJ_Cframe,NULL,
            TAG_END);
      }
      doc->win=NULL;
      doc->copy=NULL;
      doc->jobject=NULL;
      doc->dflags|=DDF_ISSPARE;
      Freebuffer(&doc->jout);
      Freejdoc(doc);
      Asetattrs(doc->source,AODOS_Spare,doc,TAG_END);
   }
}

static struct Document *Newdocument(struct Amset *ams)
{  struct Document *doc;
   struct Docsource *dos=(struct Docsource *)GetTagData(AOCDV_Sourcedriver,NULL,ams->tags);
   if(doc=(struct Document *)Agetattr(dos,AODOS_Spare))
   {  Asetattrs(dos,AODOS_Spare,NULL,TAG_END);
      doc->dflags&=~(DDF_ISSPARE|DDF_NOBACKGROUND);
      /* Clear all parse flags when reusing spare document for new page load.
       * Preserve only DPF_RELOADVERIFY if it was set (for CSS reload verification).
       * This prevents leftover flags like DPF_PREFORMAT from causing incorrect
       * parsing behavior (e.g., PRE tag detection when no PRE tag exists). */
      {  ULONG savedReloadVerify = (doc->pflags & DPF_RELOADVERIFY);
         doc->pflags=0;
         if(savedReloadVerify) doc->pflags|=DPF_RELOADVERIFY;
      }
      SETFLAG(doc->pflags,DPF_SCRIPTJS,(dos->flags&DOSF_SCRIPTJS));
      /* Reset source position to start parsing from beginning */
      doc->srcpos=0;
      doc->pmode=0;
      doc->charcount=0;
      doc->hoveredElement=NULL;
      doc->activeElement=NULL;
      doc->divancsp=0;
      Anotifyset(doc->body,AOBJ_Nobackground,FALSE,TAG_END);
      Setdocument(doc,ams);
      if(doc->bgsound && doc->win) Asetattrs(doc->win,AOWIN_Bgsound,TRUE,TAG_END);
      if((doc->dflags&DDF_DONE) && doc->onload) Queuesetmsg(doc,DQID_ONLOAD);
   }
   else
   {  if(!(doc=Allocobject(AOTP_DOCUMENT,sizeof(struct Document),ams))) goto err;
      doc->pool=CreatePool(MEMF_PUBLIC|MEMF_CLEAR,PUDDLESIZE,TRESHSIZE);
      NEWLIST(&doc->tables);
      NEWLIST(&doc->frames);
      NEWLIST(&doc->framesets);
      NEWLIST(&doc->bgimages);
      NEWLIST(&doc->colors);
      NEWLIST(&doc->links);
      NEWLIST(&doc->maps);
      NEWLIST(&doc->forms);
      NEWLIST(&doc->fragments);
      NEWLIST(&doc->infotexts);
      doc->htmlmode=prefs.htmlmode;
      doc->gotbreak=2;
      doc->hoveredElement=NULL;
      doc->activeElement=NULL;
      doc->divancsp=0;
      /* Initialize parse flags for new document. Clear all flags first,
       * then set only the ones we need. This prevents garbage values
       * from uninitialized memory (if Allocobject doesn't clear memory)
       * from causing incorrect parsing behavior (e.g., PRE tag detection
       * when no PRE tag exists). */
      doc->pflags=0;
      SETFLAG(doc->pflags,DPF_SCRIPTJS,(dos->flags&DOSF_SCRIPTJS));
      /* Initialize source position to start parsing from beginning */
      doc->srcpos=0;
      doc->pmode=0;
      doc->charcount=0;
      Setdocument(doc,ams);
      if(!doc->source) goto err;
      if(!(doc->base=Getbaseurl(doc))) goto err;
      if(!Addtobuffer(&doc->text," ",1)) goto err;
   }
   return doc;

err:
   if(doc)
   {  doc->dflags|=DDF_ISSPARE;  /* Prevent from being used as spare */
      Disposedocument(doc);
   }
   return NULL;
}

static long Getdocument(struct Document *doc,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *fragname=NULL;
   AmethodasA(AOTP_COPYDRIVER,doc,ams);
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOCDV_Fragmentname:
            fragname=(UBYTE *)tag->ti_Data;
            break;
         case AOCDV_Fragmentpos:
            PUTATTR(tag,Fragmentpos(doc,fragname));
            break;
         case AOCDV_Title:
            if(doc->dflags&DDF_TITLEVALID)
            {  PUTATTR(tag,doc->text.buffer+doc->titlepos);
            }
            else
            {  PUTATTR(tag,NULL);
            }
            break;
         case AOCDV_Ready:
            PUTATTR(tag,TRUE);
            break;
         case AOCDV_Shapes:
            PUTATTR(tag,TRUE);
            break;
         case AOCDV_Hlayout:
            PUTATTR(tag,TRUE);
            break;
         case AOCDV_Vlayout:
            PUTATTR(tag,doc->doctype==DOCTP_FRAMESET);
            break;
         case AOCDV_Extendhit:
            PUTATTR(tag,TRUE);
            break;
         case AODOC_Mapname:
            Forwardtomap(doc,(UBYTE *)tag->ti_Data,ams);
            break;
         case AODOC_Base:
            PUTATTR(tag,doc->base);
            break;
         case AOBJ_Isframeset:
            PUTATTR(tag,doc->doctype==DOCTP_FRAMESET);
            break;
      }
   }
   return 0;
}

static long Hittestdocument(struct Document *doc,struct Amhittest *amh)
{  long result=0;
   if(doc->body)
   {  result=AmethodA(doc->body,amh);
      if(!result && doc->bgimage)
      {  result=AmethodA(doc->bgimage,amh);
      }
   }
   return result;
}

static long Notifydocument(struct Document *doc,struct Amnotify *amn)
{  /* If document has a body, then see if the notify message is a AOM_SET
    * with only AOFRM_Updatecopy. If so, only forward it to this document's
    * frames.
    * Otherwise forward it to our body. */
   long result=0;
   BOOL all=TRUE,frames=TRUE;
   struct TagItem *tag,*tstate;
   struct Frameref *fref;
   struct Fragment *frag;
   if(doc->doctype==DOCTP_BODY && amn->nmsg->method==AOM_SET)
   {  all=FALSE;
      tstate=((struct Amset *)amn->nmsg)->tags;
      while(tag=NextTagItem(&tstate))
      {  switch(tag->ti_Tag)
         {  case AOFRM_Updatecopy:
               break;
            case AOCDV_Playsound:
               if(tag->ti_Data)
               {  doc->dflags|=DDF_PLAYBGSOUND;
                  if(doc->bgsound)
                  {  Anotifycload(doc->bgsound,ACMLF_BGSOUND);
                     Asetattrs(doc->bgsound,
                        AOBJ_Frame,doc->frame,
                        AOBJ_Cframe,doc->frame,
                        AOBJ_Window,doc->win,
                        TAG_END);
                  }
               }
               else
               {  doc->dflags&=~DDF_PLAYBGSOUND;
                  if(doc->bgsound)
                  {  Asetattrs(doc->bgsound,
                        AOBJ_Frame,NULL,
                        AOBJ_Cframe,NULL,
                        AOBJ_Window,NULL,
                        TAG_END);
                  }
               }
               /* and forward only to our frames */
               break;
            default:
               all=TRUE;
         }
      }
   }
   else if(amn->nmsg->method==AOM_GETREXX)
   {  struct Amgetrexx *amg=(struct Amgetrexx *)amn->nmsg;
      UBYTE *name,*buf,*urlname,*title;
      void *url;
      struct Aobject *link;
      struct Infotext *it;
      switch(amg->info)
      {  case AMGRI_FRAMES:
         case AMGRI_ALLFRAMES:
            for(fref=doc->frames.first;fref->next;fref=fref->next)
            {  Agetattrs(fref->frame,
                  AOFRM_Name,&name,
                  AOFRM_Url,&url,
                  AOFRM_Title,&title,
                  TAG_END);
               if(buf=Rexxframeid(fref->frame))
               {  urlname=(UBYTE *)Agetattr(url,AOURL_Url);
                  if(!urlname) urlname=NULLSTRING;
                  amg->index++;
                  Setstemvar(amg->ac,amg->stem,amg->index,"NAME",name?name:NULLSTRING);
                  Setstemvar(amg->ac,amg->stem,amg->index,"ID",buf);
                  Setstemvar(amg->ac,amg->stem,amg->index,"URL",urlname);
                  Setstemvar(amg->ac,amg->stem,amg->index,"TITLE",title?title:urlname);
                  FREE(buf);
               }
            }
            all=frames=FALSE;
            if(amg->info==AMGRI_ALLFRAMES) frames=TRUE;
            break;
         case AMGRI_LINKS:
            for(link=doc->links.first;link->next;link=link->next)
            {  AmethodA(link,amn);
            }
            all=frames=FALSE;
            break;
         case AMGRI_IMAGES:
            all=TRUE;
            frames=FALSE;
            break;
         case AMGRI_NAMES:
            for(frag=doc->fragments.first;frag->next;frag=frag->next)
            {  amg->index++;
               Setstemvar(amg->ac,amg->stem,amg->index,"NAME",frag->name?frag->name:NULLSTRING);
            }
            break;
         case AMGRI_INFO:
            for(it=doc->infotexts.first;it->next;it=it->next)
            {  amg->index++;
               Setstemvar(amg->ac,amg->stem,amg->index,"TYPE",it->link?"LINK":"META");
               Setstemvar(amg->ac,amg->stem,amg->index,"VALUE",it->text);
               if(it->link)
               {  urlname=(UBYTE *)Agetattr(it->link,AOURL_Url);
                  Setstemvar(amg->ac,amg->stem,amg->index,"URL",urlname?urlname:NULLSTRING);
               }
            }
            all=FALSE;
            frames=FALSE;
            break;
      }
   }
   if(all)
   {  struct Bgimage *bgi;
      if(doc->body) result=AmethodA(doc->body,amn);
      for(bgi=doc->bgimages.first;bgi->next;bgi=bgi->next)
      {  result=AmethodA(bgi->copy,amn);
      }
   }
   else if(frames)
   {  for(fref=doc->frames.first;fref->next;fref=fref->next)
      {  result=AmethodA(fref->frame,amn);
      }
   }
   return result;
}

static long Searchposdocument(struct Document *doc,struct Amsearch *ams)
{  if(doc->doctype==DOCTP_BODY)
   {  ams->text=&doc->text;
      if(doc->dflags&DDF_TITLEVALID)
      {  ams->startpos=doc->titlepos+strlen(doc->text.buffer+doc->titlepos);
      }
      else
      {  ams->startpos=0;
      }
      if(ams->flags&AMSF_CURRENTPOS)
      {  AmethodA(doc->body,ams);
      }
   }
   return 0;
}

static long Searchsetdocument(struct Document *doc,struct Amsearch *ams)
{  if(doc->doctype==DOCTP_BODY)
   {  ams->text=&doc->text;
      AmethodA(doc->body,ams);
   }
   return 0;
}

static long Dragtestdocument(struct Document *doc,struct Amdragtest *amd)
{  long result=0;
   if(doc->body)
   {  result=AmethodA(doc->body,amd);
   }
   return result;
}

static long Dragrenderdocument(struct Document *doc,struct Amdragrender *amd)
{  long result=0;
   if(doc->body)
   {  result=AmethodA(doc->body,amd);
   }
   return result;
}

static long Dragcopydocument(struct Document *doc,struct Amdragcopy *amd)
{  long result=0;
   if(doc->body)
   {  result=AmethodA(doc->body,amd);
   }
   return result;
}

static void Deinstalldocument(void)
{  Exitxhrjs();
   Exitdocjs();
}

static long Dispatch(struct Document *doc,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newdocument((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setdocument(doc,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getdocument(doc,(struct Amset *)amsg);
         break;
      case AOM_MEASURE:
         result=Measuredocument(doc,(struct Ammeasure *)amsg);
         break;
      case AOM_LAYOUT:
         result=Layoutdocument(doc,(struct Amlayout *)amsg);
         break;
      case AOM_RENDER:
         result=Renderdocument(doc,(struct Amrender *)amsg);
         break;
      case AOM_HITTEST:
         result=Hittestdocument(doc,(struct Amhittest *)amsg);
         break;
      case AOM_NOTIFY:
         result=Notifydocument(doc,(struct Amnotify *)amsg);
         break;
      case AOM_SEARCHPOS:
         result=Searchposdocument(doc,(struct Amsearch *)amsg);
         break;
      case AOM_SEARCHSET:
         result=Searchsetdocument(doc,(struct Amsearch *)amsg);
         break;
      case AOM_DRAGTEST:
         result=Dragtestdocument(doc,(struct Amdragtest *)amsg);
         break;
      case AOM_DRAGRENDER:
         result=Dragrenderdocument(doc,(struct Amdragrender *)amsg);
         break;
      case AOM_DRAGCOPY:
         result=Dragcopydocument(doc,(struct Amdragcopy *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupdocument(doc,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposedocument(doc);
         break;
      case AOM_DEINSTALL:
         Deinstalldocument();
         break;
      default:
         result=AmethodasA(AOTP_COPYDRIVER,doc,amsg);
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installdocument(void)
{  Initdocjs();
   Initxhrjs();
   if(!Amethod(NULL,AOM_INSTALL,AOTP_DOCUMENT,Dispatch)) return FALSE;
   return TRUE;
}

struct Colorinfo *Finddoccolor(struct Document *doc,ULONG rgb)
{  struct Colorinfo *ci;
   for(ci=doc->colors.first;ci->next;ci=ci->next)
   {  if(ci->rgb==rgb) return ci;
   }
   if(ci=PALLOCSTRUCT(Colorinfo,1,MEMF_PUBLIC|MEMF_CLEAR,doc->pool))
   {  ADDTAIL(&doc->colors,ci);
      ci->rgb=rgb;
      ci->pen=-1;
      if(doc->win && !(doc->dflags&DDF_NOBACKGROUND)) Obtaincolorinfo(ci);
   }
   return ci;
}

/* Register our colors with our frame and body */
#define COLOR(ci) ((ci)?(ci->pen):-1)
void Registerdoccolors(struct Document *doc)
{  long embedded=0,background=0;
   Agetattrs(doc->copy,
      AOCPY_Embedded,&embedded,
      AOCPY_Background,&background,
      TAG_END);
   if(!embedded && !background)
   {  Asetattrs(doc->frame,
         AOFRM_Bgcolor,COLOR(doc->bgcolor),
         AOFRM_Textcolor,COLOR(doc->textcolor),
         AOFRM_Linkcolor,COLOR(doc->linkcolor),
         AOFRM_Vlinkcolor,COLOR(doc->vlinkcolor),
         AOFRM_Alinkcolor,COLOR(doc->alinkcolor),
         AOFRM_Bgimage,doc->bgimage,
         TAG_END);
      Asetattrs(doc->body,
         AOBDY_Bgcolor,COLOR(doc->bgcolor),
         AOBDY_Bgimage,doc->bgimage,
         TAG_END);
   }
}

