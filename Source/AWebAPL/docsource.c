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

/* docsource.c - AWeb document source interpreter object */

#include "aweb.h"
#include "source.h"
#include "sourcedriver.h"
#include "copy.h"
#include "url.h"
#include "file.h"
#include "application.h"
#include "editor.h"
#include "docprivate.h"
#include <proto/utility.h>
#include <stdio.h>

#define DQID_RELOAD2    1  /* Queue-id: reload 2nd phase: srcupdate */

/*------------------------------------------------------------------------*/

/*------------------------------------------------------------------------*/

/* Set HTML, markdown, or plain according to content type */
static void Sethtmlflag(struct Docsource *dos,UBYTE *type)
{  UBYTE *p;
   UBYTE *type_copy;
   UBYTE *semicolon;
   
   if(!type || !*type)
   {  dos->flags&=~(DOSF_HTML|DOSF_MD);
      return;
   }
   
   /* Make a copy to modify (strip charset parameter) */
   type_copy=Dupstr(type,-1);
   if(!type_copy)
   {  dos->flags&=~(DOSF_HTML|DOSF_MD);
      return;
   }
   
   /* Remove charset parameter (e.g., "text/html; charset=UTF-8" -> "text/html") */
   semicolon=strchr(type_copy,';');
   if(semicolon) *semicolon='\0';
   
   /* Trim leading whitespace */
   p=type_copy;
   while(*p && isspace(*p)) p++;
   
   /* Trim trailing whitespace */
   {  long len;
      len=strlen(p);
      while(len > 0 && isspace(p[len-1]))
      {  p[len-1]='\0';
         len--;
      }
   }
   
   /* Case-insensitive comparison for HTML */
   if(STRNIEQUAL(p,"TEXT/HTML",9))
   {  dos->flags|=DOSF_HTML;
      dos->flags&=~DOSF_MD;
   }
   else if(STRNIEQUAL(p,"TEXT/MARKDOWN",13))
   {  dos->flags|=DOSF_MD;
      dos->flags&=~DOSF_HTML;
   }
   else
   {  dos->flags&=~(DOSF_HTML|DOSF_MD);
   }
   
   FREE(type_copy);
}

/* Save this document's source, but replace all \r by \n */
static void Savesource(struct Docsource *dos,void *file)
{  UBYTE *buf=Dupstr(dos->buf.buffer,dos->buf.length);
   UBYTE *p;
   if(buf)
   {  p=buf;
      while(p=strchr(p,'\r'))
      {  if(p[1]!='\n') *p='\n';
         p++;
      }
      Asetattrs(file,
         AOFIL_Data,buf,
         AOFIL_Datalength,dos->buf.length,
         TAG_END);
      FREE(buf);
   }
}

/* Re-do everything with the current source data. */
static void Redodocsource(struct Docsource *dos)
{  if(dos->spare)
   {  Adisposeobject(dos->spare);
      dos->spare=NULL;
   }
   Anotifyset(dos->source,AODOC_Reload,TRUE,TAG_END);
   /* Queue a
    * Anotifyset(dos->source,AODOC_Srcupdate,TRUE,TAG_END);
    * If it was done here, framed documents are parsed twice
    * (frameset reload/srcupdate creates new framed document,
    * then framed document receives reload/srcupdate).
    */
   Queuesetmsg(dos,DQID_RELOAD2);
}

/* Start editor for this source */
static void Editdocsource(struct Docsource *dos)
{  
#ifndef DEMOVERSION
   UBYTE *url=(UBYTE *)Agetattr((void *)Agetattr(dos->source,AOSRC_Url),AOURL_Url);
   UBYTE *name,*filename=NULL;
   if(dos->editor) Adisposeobject(dos->editor);
   if(url && STRNIEQUAL(url,"file://",7)
   && (name=strchr(url+7,'/')))
   {  name++;
      filename=Fullname(name);
   }
   if(filename && !Readonlyfile(filename))
   {  dos->editor=Anewobject(AOTP_EDITOR,
         AOBJ_Target,dos,
         AOEDT_Filename,filename,
         TAG_END);
   }
   else
   {  dos->editor=Anewobject(AOTP_EDITOR,
         AOBJ_Target,dos,
         AOEDT_Data,dos->buf.buffer,
         AOEDT_Datalength,dos->buf.length,
         TAG_END);
   }
   if(filename) FREE(filename);
#endif
}

/*------------------------------------------------------------------------*/

static long Setdocsource(struct Docsource *dos,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            dos->source=(void *)tag->ti_Data;
            break;
         case AOURL_Contenttype:
            Sethtmlflag(dos,(UBYTE *)tag->ti_Data);
            break;
         case AOSDV_Savesource:
            Savesource(dos,(void *)tag->ti_Data);
            break;
         case AOBJ_Application:
            if(!tag->ti_Data) dos->flags&=~DOSF_INREL;
            break;
         case AOAPP_Browsersettings:
            Redodocsource(dos);
            break;
         case AOSDV_Editsource:
            if(tag->ti_Data)
            {  Editdocsource(dos);
            }
            break;
         case AODOS_Spare:
            if(tag->ti_Data)
            {  if(dos->spare) Adisposeobject(dos->spare);
               dos->spare=(struct Document *)tag->ti_Data;
            }
            else
            {  dos->spare=NULL;
            }
            break;
         case AOBJ_Queueid:
            if(tag->ti_Data==DQID_RELOAD2)
            {  Anotifyset(dos->source,AODOC_Srcupdate,TRUE,TAG_END);
            }
            break;
      }
   }
   return 0;
}

static struct Docsource *Newdocsource(struct Amset *ams)
{  struct Docsource *dos;
   if(dos=Allocobject(AOTP_DOCSOURCE,sizeof(struct Docsource),ams))
   {  Aaddchild(Aweb(),dos,AOREL_APP_USE_BROWSER);
      dos->flags|=DOSF_INREL|DOSF_SCRIPTJS;
      Setdocsource(dos,ams);
   }
   return dos;
}

static long Getdocsource(struct Docsource *dos,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOSDV_Source:
            PUTATTR(tag,dos->source);
            break;
         case AOSDV_Saveable:
            PUTATTR(tag,TRUE);
            break;
         case AOSDV_Viewable:
            PUTATTR(tag,TRUE);
            break;
         case AOSDV_Getable:
            PUTATTR(tag,TRUE);
            break;
         case AOSDV_Getsource:
            PUTATTR(tag,dos->buf.buffer);
            break;
         case AODOS_Spare:
            if(dos->spare && dos->spare->htmlmode!=prefs.browser.htmlmode)
            {  Adisposeobject(dos->spare);
               dos->spare=NULL;
            }
            PUTATTR(tag,dos->spare);
            break;
      }
   }
   return 0;
}

static long Updatedocsource(struct Docsource *dos,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *data=NULL;
   long length=0;
   ULONG date=0;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOEDT_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOEDT_Datalength:
            length=tag->ti_Data;
            break;
         case AOEDT_Filedate:
            date=tag->ti_Data;
            break;
      }
   }
   if(data)
   {  if(date) Asetattrs(dos->source,AOSRC_Lastmodified,date,TAG_END);
      Freebuffer(&dos->buf);
      Addtobuffer(&dos->buf,data,length);
      Redodocsource(dos);
      Doupdateframes();
   }
   return 0;
}

static long Srcupdatedocsource(struct Docsource *dos,struct Amsrcupdate *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   long length=0;
   UBYTE *data=NULL;
   BOOL eof=FALSE;
   BOOL saw_reload=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOURL_Contenttype:
            Sethtmlflag(dos,(UBYTE *)tag->ti_Data);
            break;
         case AOURL_Contentlength:
            Expandbuffer(&dos->buf,tag->ti_Data-dos->buf.length);
            break;
         case AOURL_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOURL_Datalength:
            length=tag->ti_Data;
            break;
         case AOURL_Reload:
            saw_reload=TRUE;
            if(dos->spare)
            {  Adisposeobject(dos->spare);
               dos->spare=NULL;
            }
            Anotifyset(dos->source,AODOC_Reload,TRUE,TAG_END);
            Freebuffer(&dos->buf);
            dos->flags&=~DOSF_EOF;
            break;
         case AOURL_Eof:
            if(tag->ti_Data) dos->flags|=DOSF_EOF;
            eof=TRUE;
            break;
         case AOURL_Jsopen:
            if((dos->flags&DOSF_JSOPEN) && !tag->ti_Data) eof=TRUE;
            SETFLAG(dos->flags,DOSF_JSOPEN,tag->ti_Data);
            break;
         case AOURL_Contentscripttype:
            if(tag->ti_Data)
            {  SETFLAG(dos->flags,DOSF_SCRIPTJS,
                  STRIEQUAL((UBYTE *)tag->ti_Data,"text/javascript"));
            }
            break;
      }
   }
   if(data)
   {  Addtobuffer(&dos->buf,data,length);
      Asetattrs(dos->source,AOSRC_Memory,dos->buf.size,TAG_END);
   }
   if(data || eof) Anotifyset(dos->source,AODOC_Srcupdate,TRUE,TAG_END);
   return 0;
}

/* A new child was added; send it an initial update msg */
static long Addchilddocsource(struct Docsource *dos,struct Amadd *ama)
{  if(dos->buf.length || (dos->flags&DOSF_EOF))
   {  Asetattrs(ama->child,AODOC_Srcupdate,TRUE,TAG_END);
   }
   return 0;
}

static void Disposedocsource(struct Docsource *dos)
{  if(dos->flags&DOSF_INREL) Aremchild(Aweb(),dos,AOREL_APP_USE_BROWSER);
   Freebuffer(&dos->buf);
   Asetattrs(dos->source,AOSRC_Memory,0,TAG_END);
   if(dos->editor) Adisposeobject(dos->editor);
   if(dos->spare) Adisposeobject(dos->spare);
   Queuesetmsg(dos,0);
   Amethodas(AOTP_OBJECT,dos,AOM_DISPOSE);
}

static long Dispatch(struct Docsource *dos,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newdocsource((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setdocsource(dos,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getdocsource(dos,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updatedocsource(dos,(struct Amset *)amsg);
         break;
      case AOM_SRCUPDATE:
         result=Srcupdatedocsource(dos,(struct Amsrcupdate *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchilddocsource(dos,(struct Amadd *)amsg);
         break;
      case AOM_DISPOSE:
         Disposedocsource(dos);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installdocsource(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_DOCSOURCE,Dispatch)) return FALSE;
   return TRUE;
}

/* Find line # from source position */
long Docslinenrfrompos(struct Docsource *dos,long pos)
{  long lnr=0;
   long i;
   char *p=dos->buf.buffer;
   for(i=0;i<dos->buf.length && i<pos;i++)
   {  if(p[i]=='\n') lnr++;
      else if(i<dos->buf.length-1 && p[i]=='\r' && p[i+1]=='\n') lnr++;
   }
   return lnr;
}
      
      
