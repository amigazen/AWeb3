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

/* form.c - AWeb html form object */

#include "aweb.h"
#include "form.h"
#include "field.h"
#include "button.h"
#include "radio.h"
#include "filefield.h"
#include "frame.h"
#include "url.h"
#include "window.h"
#include "jslib.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

/*------------------------------------------------------------------------*/

struct Form
{  struct Aobject object;
   void *pool;
   USHORT method;
   UBYTE *name;
   void *action;
   UBYTE *target;
   LIST(Formfld) fields;         /* Objects composing this form */
   void *win;
   void *frame;
   short type;
   void *autosubmit;             /* Submit button to include in automatic submit */
   struct Jobject *jobject;      /* Form's object */
   struct Jobject *jelements;    /* Elements array object */
   UBYTE *onreset;
   UBYTE *onsubmit;
   USHORT flags;
};

#define FORTP_EMPTY           0  /* No fields yet */
#define FORTP_1INPUT          1  /* Only 1 input field found */
#define FORTP_NORMAL          2  /* More fields found */

#define FORF_MULTIPART     0x0001   /* Encoding type is multipart/form-data */
#define FORF_NOSUBMIT      0x0002   /* Form does not have a submit button */

struct Formfld
{  NODE(Formfld);
   struct Aobject *object;
};

/*------------------------------------------------------------------------*/

/* Allocate a new Multipartdata */
static struct Multipartdata *Newmultipartdata(void)
{  struct Multipartdata *mpd;
   UBYTE boundary[41];
   static UBYTE bchars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
   short i,l=strlen(bchars);
   if(mpd=ALLOCSTRUCT(Multipartdata,1,MEMF_PUBLIC))
   {  NEWLIST(&mpd->parts);
      srand(Today());
      for(i=0;i<sizeof(boundary)-1;i++)
      {  boundary[i]=bchars[(rand()>>6)%l];
      }
      boundary[i]='\0';
      Addtobuffer(&mpd->buf,boundary,41);
   }
   return mpd;
}

/* Add textual field value to multipart data. Add to last part if that was
 * a textual part, else create a new part. */
static void Addmpdtext(struct Multipartdata *mpd,UBYTE *name,UBYTE *value)
{  struct Multipartpart *mpp;
   mpp=mpd->parts.last;
   if(!mpp->prev || mpp->lock)
   {  mpp=ALLOCSTRUCT(Multipartpart,1,MEMF_PUBLIC);
      if(mpp)
      {  ADDTAIL(&mpd->parts,mpp);
         mpp->start=mpd->buf.length;
      }
   }
   if(mpp)
   {  Addtobuffer(&mpd->buf,"\r\n--",4);
      Addtobuffer(&mpd->buf,mpd->buf.buffer,40);
      Addtobuffer(&mpd->buf,"\r\nContent-Disposition: form-data; name=\"",-1);
      if(name)
      {  Addtobuffer(&mpd->buf,name,-1);
      }
      Addtobuffer(&mpd->buf,"\"\r\n\r\n",5);
      if(value)
      {  Addtobuffer(&mpd->buf,value,-1);
      }
      mpd->length+=mpd->buf.length-(mpp->start+mpp->length);
      mpp->length=mpd->buf.length-mpp->start;
   }
}

/* Add a file lock to multipart data. First add boundary and headers
 * to the last textual part, or create a new part for that. */
static void Addmpdfile(struct Multipartdata *mpd,UBYTE *name,UBYTE *filename)
{  struct Multipartpart *mpp;
   UBYTE *fname,*mtype;
   long lock;
   struct FileInfoBlock *fib;
   BOOL unlock=TRUE,ok=FALSE;
   if(filename && (lock=Lock(filename,SHARED_LOCK)))
   {  if(fib=AllocDosObjectTags(DOS_FIB,TAG_END))
      {  if(Examine(lock,fib) && fib->fib_DirEntryType<0)
         {  mpp=mpd->parts.last;
            if(!mpp->prev || mpp->lock)
            {  mpp=ALLOCSTRUCT(Multipartpart,1,MEMF_PUBLIC);
               if(mpp)
               {  ADDTAIL(&mpd->parts,mpp);
                  mpp->start=mpd->buf.length;
               }
            }
            if(mpp)
            {  Addtobuffer(&mpd->buf,"\r\n--",4);
               Addtobuffer(&mpd->buf,mpd->buf.buffer,40);
               Addtobuffer(&mpd->buf,"\r\nContent-Disposition: form-data; name=\"",-1);
               if(name)
               {  Addtobuffer(&mpd->buf,name,-1);
               }
               Addtobuffer(&mpd->buf,"\"; filename=\"",-1);
               if(fname=FilePart(filename))
               {  Addtobuffer(&mpd->buf,fname,-1);
               }
               Addtobuffer(&mpd->buf,"\"\r\nContent-Type: ",-1);
               if(!(mtype=Mimetypefromext(filename))) mtype="application/octet-stream";
               Addtobuffer(&mpd->buf,mtype,-1);
               Addtobuffer(&mpd->buf,"\r\n",-1);
               if(!STRNIEQUAL(mtype,"TEXT/",5))
               {  Addtobuffer(&mpd->buf,"Content-Transfer-Encoding: Binary\r\n",-1);
               }
               Addtobuffer(&mpd->buf,"\r\n",2);
               mpd->length+=mpd->buf.length-(mpp->start+mpp->length);
               mpp->length=mpd->buf.length-mpp->start;
            }
            if(mpp=ALLOCSTRUCT(Multipartpart,1,MEMF_PUBLIC))
            {  ADDTAIL(&mpd->parts,mpp);
               mpp->lock=lock;
               mpp->length=fib->fib_Size;
               mpd->length+=fib->fib_Size;
               unlock=FALSE;
            }
            ok=TRUE;
         }
         FreeDosObject(DOS_FIB,fib);
      }
      if(unlock) UnLock(lock);
   }
   if(!ok)
   {  Addmpdtext(mpd,name,filename);
   }
}

/* Add the last closing boundary to multipart data */
static void Closempd(struct Multipartdata *mpd)
{  struct Multipartpart *mpp;
   mpp=mpd->parts.last;
   if(!mpp->prev || mpp->lock)
   {  mpp=ALLOCSTRUCT(Multipartpart,1,MEMF_PUBLIC);
      if(mpp)
      {  ADDTAIL(&mpd->parts,mpp);
         mpp->start=mpd->buf.length;
      }
   }
   if(mpp)
   {  Addtobuffer(&mpd->buf,"\r\n--",4);
      Addtobuffer(&mpd->buf,mpd->buf.buffer,40);
      Addtobuffer(&mpd->buf,"--\r\n",4);
      mpd->length+=mpd->buf.length-(mpp->start+mpp->length);
      mpp->length=mpd->buf.length-mpp->start;
   }
}

/* Dispose multipartdata */
void Freemultipartdata(struct Multipartdata *mpd)
{  struct Multipartpart *mpp;
   if(mpd)
   {  while(mpp=REMHEAD(&mpd->parts))
      {  if(mpp->lock) UnLock(mpp->lock);
         FREE(mpp);
      }
      Freebuffer(&mpd->buf);
      FREE(mpd);
   }
}

/*------------------------------------------------------------------------*/

/* Add a name and a value, optionally preceded by ampersand.
 * Return TRUE if something was added. */
static BOOL Addnamevalue(struct Buffer *buf,BOOL amp,UBYTE *name,UBYTE *value)
{  if(amp) Addtobuffer(buf,"&",1);
   if(name)
   {  Urlencode(buf,name,strlen(name));
      Addtobuffer(buf,"=",1);
   }
   if(value)
   {  Urlencode(buf,value,strlen(value));
   }
   return (BOOL)(name || value);
}

/* Submit the form. */
static long Dosubmitform(struct Form *frm,struct Afosubmit *afms,BOOL runonsubmit)
{  struct Buffer buf={0};
   struct Formfld *fld;
   BOOL amp=FALSE;
   UBYTE *name,*value,*mvalue,*p;
   long i,max;
   void *url,*referer,*target,*window=NULL;
   UBYTE *frameid=NULL;
   UBYTE mapbuf[16];
   UBYTE *mapname=NULL;
   ULONG loadflags=0;
   struct Multipartdata *mpd=NULL;
   /* Only submit if actor was SUBMIT button
    * -or- the only text input field
    * -or- any text input field if there is no submit button
    * -or- NULL (submit from image icon) */
   if(frm->action
   && (!afms->submit || afms->submit->objecttype!=AOTP_INPUT || frm->type==FORTP_1INPUT
      || (afms->submit->objecttype==AOTP_INPUT && (frm->flags&FORF_NOSUBMIT))))
   {  if(frm->method==FORMTH_POST)
      {  if(frm->flags&FORF_MULTIPART)
         {  mpd=Newmultipartdata();
         }
      }
      else Addtobuffer(&buf,"?",1);
      for(fld=frm->fields.first;fld->next;fld=fld->next)
      {  if(fld->object->objecttype!=AOTP_BUTTON
         || fld->object==afms->submit
         || fld->object==frm->autosubmit)
         {  name=value=NULL;
            Agetattrs(fld->object,
               AOFLD_Name,&name,
               AOFLD_Value,&value,
               AOFLD_Multivalue,&mvalue,
               TAG_END);
            if(name || frm->method==FORMTH_INDEX)
            {  if(mvalue)
               {  max=*(long *)mvalue;
                  p=mvalue+4;
                  for(i=0;i<max;i++)
                  {  if(mpd) Addmpdtext(mpd,name,p);
                     else amp=Addnamevalue(&buf,amp,name,p);
                     p+=strlen(p)+1;
                  }
               }
               else if(fld->object->objecttype==AOTP_COPY)
               {  /* Image (map) field, only include if it was submitted from here */
                  if(fld->object==afms->submit)
                  {  if(mapname=ALLOCTYPE(UBYTE,strlen(name)+4,0))
                     {  strcpy(mapname,name);
                        strcat(mapname,".x");
                        sprintf(mapbuf,"%d",afms->x);
                        if(mpd) Addmpdtext(mpd,mapname,mapbuf);
                        else amp=Addnamevalue(&buf,amp,mapname,mapbuf);
                        strcpy(mapname,name);
                        strcat(mapname,".y");
                        sprintf(mapbuf,"%d",afms->y);
                        if(mpd) Addmpdtext(mpd,mapname,mapbuf);
                        else amp=Addnamevalue(&buf,amp,mapname,mapbuf);
                        FREE(mapname);
                     }
                  }
               }
               else if(fld->object->objecttype==AOTP_FILEFIELD)
               {  if(mpd) Addmpdfile(mpd,name,value);
                  else amp=Addnamevalue(&buf,amp,name,value);
               }
               else
               {  if(mpd) Addmpdtext(mpd,name,value);
                  else amp=Addnamevalue(&buf,amp,name,value);
               }
            }
         }
      }
      if(mpd) Closempd(mpd);
      else Addtobuffer(&buf,"",1);
      if(!runonsubmit || Runjavascript(frm->frame,frm->onsubmit,&frm->jobject))
      {
         referer=(void *)Agetattr(frm->frame,AOFRM_Url);
         target=Targetframe(frm->frame,frm->target);
         Agetattrs(target,
            AOFRM_Id,&frameid,
            AOBJ_Window,&window,
            TAG_END);
         if(prefs.network.formwarn) loadflags|=AUMLF_FORMWARN;
         if(Agetattr(window,AOWIN_Noproxy)) loadflags|=AUMLF_NOPROXY;
         if(frm->method==FORMTH_POST)
         {  url=Findurl((UBYTE *)Agetattr(frm->action,AOURL_Url),"",-1);
            if(url)
            {  if(mpd) loadflags|=AUMLF_MULTIPART;
               Auload(url,loadflags,referer,mpd?(UBYTE *)mpd:buf.buffer,frm->frame);
               Inputwindoc(window,url,NULL,frameid);
            }
            else if(mpd) Freemultipartdata(mpd);
         }
         else
         {  url=Findurl((UBYTE *)Agetattr(frm->action,AOURL_Url),buf.buffer,0);
            if(url)
            {  Auload(url,loadflags,referer,NULL,frm->frame);
               Inputwindoc(window,url,NULL,frameid);
            }
         }
      }
      else
      {  if(mpd) Freemultipartdata(mpd);
      }
      Freebuffer(&buf);
   }
   return 0;
}
/*------------------------------------------------------------------------*/

/* Get or set action property (JS) */
static BOOL Propertyaction(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Form *frm=vd->hookdata;
   UBYTE *action;
   void *url;
   if(frm)
   {  switch(vd->code)
      {  case VHC_SET:
            action=Jtostring(vd->jc,vd->value);
            if(action) url=Findurl(Getjscurrenturlname(vd->jc),action,0);
            if(url) frm->action=url;
            result=TRUE;
            break;
         case VHC_GET:
            action=(UBYTE *)Agetattr(frm->action,AOURL_Url);
            Jasgstring(vd->jc,vd->value,action);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Get or set encoding property (JS) */
static BOOL Propertyencoding(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Form *frm=vd->hookdata;
   UBYTE *enctype;
   if(frm)
   {  switch(vd->code)
      {  case VHC_SET:
            if(enctype=Jtostring(vd->jc,vd->value))
            {  SETFLAG(frm->flags,FORF_MULTIPART,
                  STRIEQUAL(enctype,"multipart/form-data"));
            }
            result=TRUE;
            break;
         case VHC_GET:
            if(frm->flags&FORF_MULTIPART) enctype="multipart/form-data";
            else enctype="application/x-www-form-urlencoded";
            Jasgstring(vd->jc,vd->value,enctype);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Get length property (JS) */
static BOOL Propertylength(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Form *frm=vd->hookdata;
   struct Jvar *jv;
   long length;
   if(frm)
   {  switch(vd->code)
      {  case VHC_SET:
            /* read-only property */
            result=TRUE;
            break;
         case VHC_GET:
            if(jv=Jproperty(vd->jc,frm->jelements,"length"))
            {  length=Jtonumber(vd->jc,jv);
               Jasgnumber(vd->jc,vd->value,length);
            }
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Get or set method property (JS) */
static BOOL Propertymethod(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Form *frm=vd->hookdata;
   UBYTE *method;
   if(frm)
   {  switch(vd->code)
      {  case VHC_SET:
            if(method=Jtostring(vd->jc,vd->value))
            {  if(STRIEQUAL(method,"GET")) frm->method=FORMTH_GET;
               else if(STRIEQUAL(method,"POST")) frm->method=FORMTH_POST;
            }
            result=TRUE;
            break;
         case VHC_GET:
            if(frm->method==FORMTH_POST) method="POST";
            else method="GET";
            Jasgstring(vd->jc,vd->value,method);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* Get or set target property (JS) */
static BOOL Propertytarget(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Form *frm=vd->hookdata;
   UBYTE *target;
   if(frm)
   {  switch(vd->code)
      {  case VHC_SET:
            if(target=Jtostring(vd->jc,vd->value))
            {  if(frm->target) FREE(frm->target);
               frm->target=Dupstr(target,-1);
            }
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,frm->target);
            result=TRUE;
            break;
      }
   }
   return result;
}

/* JS method reset, should run onReset too */
static void Methodreset(struct Jcontext *jc)
{  struct Form *frm=Jointernal(Jthis(jc));
   Amethod(frm,AFO_RESET);
}

/* JS method submit, must not run onSubmit */
static void Methodsubmit(struct Jcontext *jc)
{  struct Afosubmit afms={0};
   struct Form *frm=Jointernal(Jthis(jc));
   afms.method=AFO_SUBMIT;
   if(frm) Dosubmitform(frm,&afms,FALSE);
}

/*------------------------------------------------------------------------*/

static long Setform(struct Form *frm,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOBJ_Pool:
            frm->pool=(void *)tag->ti_Data;
            break;
         case AOBJ_Window:
            frm->win=(void *)tag->ti_Data;
            break;
         case AOBJ_Frame:
            frm->frame=(void *)tag->ti_Data;
            if(!frm->frame)
            {  if(frm->jobject) Disposejobject(frm->jobject);
               if(frm->jelements) Disposejobject(frm->jelements);
               frm->jobject=NULL;
               frm->jelements=NULL;
            }
            break;
         case AOFOR_Method:
            frm->method=tag->ti_Data;
            break;
         case AOFOR_Action:
            frm->action=(void *)tag->ti_Data;
            break;
         case AOFOR_Target:
            if(frm->target) FREE(frm->target);
            frm->target=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFOR_Complete:
            break;
         case AOFOR_Name:
            if(frm->name) FREE(frm->name);
            frm->name=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFOR_Onreset:
            if(frm->onreset) FREE(frm->onreset);
            frm->onreset=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFOR_Onsubmit:
            if(frm->onsubmit) FREE(frm->onsubmit);
            frm->onsubmit=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFOR_Multipart:
            SETFLAG(frm->flags,FORF_MULTIPART,tag->ti_Data);
            break;
      }
   }
   return 0;
}

static struct Form *Newform(struct Amset *ams)
{  struct Form *frm;
   if(frm=Allocobject(AOTP_FORM,sizeof(struct Form),ams))
   {  NEWLIST(&frm->fields);
      Setform(frm,ams);
      frm->flags|=FORF_NOSUBMIT;
   }
   return frm;
}

static long Getform(struct Form *frm,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *radioname=NULL;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFOR_Action:
            PUTATTR(tag,frm->action);
            break;
         case AOFOR_Jelements:
            PUTATTR(tag,frm->jelements);
            break;
         case AOFOR_Radioname:
            radioname=(UBYTE *)tag->ti_Data;
            break;
         case AOFOR_Radioselected:
            if(radioname)
            {  struct Formfld *fld;
               UBYTE *name;
               for(fld=frm->fields.first;fld->next;fld=fld->next)
               {  if(fld->object->objecttype==AOTP_RADIO
                  && (name=(UBYTE *)Agetattr(fld->object,AOFLD_Name))
                  && STRIEQUAL(radioname,name)
                  && Agetattr(fld->object,AORAD_Checked))
                  {  PUTATTR(tag,fld->object);
                     break;
                  }
               }
            }
            break;
         case AOBJ_Jobject:
            PUTATTR(tag,frm->jobject);
            break;
      }
   }
   return 0;
}

static long Addchild(struct Form *frm,struct Amadd *ama)
{  struct Formfld *fld=PALLOCSTRUCT(Formfld,1,MEMF_CLEAR,frm->pool);
   USHORT buttype;
   if(fld)
   {  fld->object=ama->child;
      ADDTAIL(&frm->fields,fld);
      switch(ama->child->objecttype)
      {  case AOTP_BUTTON:
            buttype=Agetattr(ama->child,AOBUT_Type);
            if(frm->type!=FORTP_NORMAL)
            {  if(buttype==BUTTP_SUBMIT)
               {  if(!frm->autosubmit) frm->autosubmit=ama->child;
                  else
                  {  frm->type=FORTP_NORMAL;
                     frm->autosubmit=NULL;
                  }
               }
               else if(buttype==BUTTP_BUTTON)
               {  frm->type=FORTP_NORMAL;
                  frm->autosubmit=NULL;
               }
            }
            if(buttype==BUTTP_SUBMIT)
            {  frm->flags&=~FORF_NOSUBMIT;
            }
            break;
         case AOTP_INPUT:
            if(frm->type==FORTP_EMPTY) frm->type=FORTP_1INPUT;
            else
            {  frm->type=FORTP_NORMAL;
               frm->autosubmit=NULL;
            }
            break;
         case AOTP_HIDDEN:
            /* hidden, ignore for autosubmit */
            break;
         default:
            frm->type=FORTP_NORMAL;
            frm->autosubmit=NULL;
      }
   }
   return 0;
}

static long Remchild(struct Form *frm,struct Amadd *ama)
{  struct Formfld *fld;
   for(fld=frm->fields.first;fld->next;fld=fld->next)
   {  if(fld->object==ama->child)
      {  REMOVE(fld);
         FREE(fld);
         break;
      }
   }
   return 0;
}

static long Jsetupform(struct Form *frm,struct Amjsetup *amj)
{  struct Jvar *jv;
   struct Jobject *forms;
   struct Formfld *fld;
   if(!frm->jobject)
   {  if(frm->jobject=Newjobject(amj->jc))
      {  Jkeepobject(frm->jobject,TRUE);
         Setjobject(frm->jobject,NULL,frm,NULL);
         if(jv=Jproperty(amj->jc,amj->parent,frm->name))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,frm->jobject);
         }
         if(forms=Jfindarray(amj->jc,amj->parent,"forms"))
         {  if(jv=Jnewarrayelt(amj->jc,forms))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgobject(amj->jc,jv,frm->jobject);
            }
            if(frm->name)
            {  if(jv=Jproperty(amj->jc,forms,frm->name))
               {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                  Jasgobject(amj->jc,jv,frm->jobject);
               }
            }
         }
         if(jv=Jproperty(amj->jc,frm->jobject,"elements"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            if(frm->jelements=Newjarray(amj->jc))
            {  Jkeepobject(frm->jelements,TRUE);
               Jasgobject(amj->jc,jv,frm->jelements);
               Jsetobjasfunc(frm->jelements,TRUE);
            }
         }
         if(jv=Jproperty(amj->jc,frm->jobject,"action"))
         {  Setjproperty(jv,Propertyaction,frm);
         }
         if(jv=Jproperty(amj->jc,frm->jobject,"encoding"))
         {  Setjproperty(jv,Propertyencoding,frm);
         }
         if(jv=Jproperty(amj->jc,frm->jobject,"length"))
         {  Setjproperty(jv,Propertylength,frm);
         }
         if(jv=Jproperty(amj->jc,frm->jobject,"method"))
         {  Setjproperty(jv,Propertymethod,frm);
         }
         if(jv=Jproperty(amj->jc,frm->jobject,"name"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgstring(amj->jc,jv,frm->name);
         }
         if(jv=Jproperty(amj->jc,frm->jobject,"target"))
         {  Setjproperty(jv,Propertytarget,frm);
         }
         Addjfunction(amj->jc,frm->jobject,"reset",Methodreset,NULL);
         Addjfunction(amj->jc,frm->jobject,"submit",Methodsubmit,NULL);
      }
   }
   if(frm->jobject)
   {  for(fld=frm->fields.first;fld->next;fld=fld->next)
      {  Ajsetup(fld->object,amj->jc,frm->jobject,amj->parentframe);
      }
   }
   return 0;
}

static void Disposeform(struct Form *frm)
{  struct Formfld *fld;
   while(fld=REMHEAD(&frm->fields))
   {  Asetattrs(fld->object,AOFLD_Form,NULL,TAG_END);
      FREE(fld);
   }
   if(frm->name) FREE(frm->name);
   Amethodas(AOTP_OBJECT,frm,AOM_DISPOSE);
}

/*
static long Submitform(struct Form *frm,struct Afosubmit *afms)
{  if(frm->action
   && (!afms->submit || afms->submit->objecttype!=AOTP_INPUT || frm->type==FORTP_1INPUT))
   {  /*if(Runjavascript(frm->frame,frm->onsubmit,&frm->jobject))*/
      {  Dosubmitform(frm,afms,TRUE);
      }
   }
   return 0;
}
*/

static long Resetform(struct Form *frm)
{  struct Formfld *fld;
   Runjavascript(frm->frame,frm->onreset,&frm->jobject);
   for(fld=frm->fields.first;fld->next;fld=fld->next)
   {  Asetattrs(fld->object,AOFLD_Reset,TRUE,TAG_END);
   }
   return 0;
}

static long Radioselectform(struct Form *frm,struct Aforadioselect *afor)
{  struct Formfld *fld;
   UBYTE *name;
   for(fld=frm->fields.first;fld->next;fld=fld->next)
   {  if(fld->object->objecttype==AOTP_RADIO
      && fld->object!=afor->select
      && afor->name
      && (name=(UBYTE *)Agetattr(fld->object,AOFLD_Name))
      && STRIEQUAL(afor->name,name))
      {  Asetattrs(fld->object,AORAD_Checked,FALSE,TAG_END);
         Arender(fld->object,NULL,0,0,AMRMAX,AMRMAX,0,NULL);
      }
   }
   return 0;
}

static long Nextinputform(struct Form *frm,struct Afonextinput *afon)
{  struct Formfld *fld,*next;
   long result=0;
   for(fld=frm->fields.first;fld->next && fld->object!=afon->current;fld=fld->next);
   if(fld->next)
   {  if(afon->direction<0)
      {  for(next=fld->prev;;next=next->prev)
         {  if(!next->prev) next=frm->fields.last;
            if(next==fld) break;
            if(next->object->objecttype==AOTP_INPUT)
            {  result=(long)next->object;
               break;
            }
            if(next->object->objecttype==AOTP_FILEFIELD)
            {  result=(long)Agetattr(next->object,AOFUF_Input);
               break;
            }
         }
      }
      else
      {  for(next=fld->next;;next=next->next)
         {  if(!next->next) next=frm->fields.first;
            if(next==fld) break;
            if(next->object->objecttype==AOTP_INPUT)
            {  result=(long)next->object;
               break;
            }
            if(next->object->objecttype==AOTP_FILEFIELD)
            {  result=(long)Agetattr(next->object,AOFUF_Input);
               break;
            }
         }
      }
   }
   return result;
}

static long Dispatch(struct Form *frm,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newform((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setform(frm,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getform(frm,(struct Amset *)amsg);
         break;
      case AOM_ADDCHILD:
         result=Addchild(frm,(struct Amadd *)amsg);
         break;
      case AOM_REMCHILD:
         result=Remchild(frm,(struct Amadd *)amsg);
         break;
      case AOM_JSETUP:
         result=Jsetupform(frm,(struct Amjsetup *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeform(frm);
         break;
      case AFO_SUBMIT:
         result=Dosubmitform(frm,(struct Afosubmit *)amsg,TRUE);
         break;
      case AFO_RESET:
         result=Resetform(frm);
         break;
      case AFO_RADIOSELECT:
         result=Radioselectform(frm,(struct Aforadioselect *)amsg);
         break;
      case AFO_NEXTINPUT:
         result=Nextinputform(frm,(struct Afonextinput *)amsg);
         break;
      case AOM_DEINSTALL:
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installform(void)
{  if(!Amethod(NULL,AOM_INSTALL,AOTP_FORM,Dispatch)) return FALSE;
   return TRUE;
}

/* Copy (p) to (buf), URLencoded if necessary */
void Urlencode(struct Buffer *buf,UBYTE *p,long len)
{  UBYTE *q=p+len;
   UBYTE enc[4];
   while(p<q)
   {  if(Isurlchar(*p)) Addtobuffer(buf,p,1);
      else if(*p==' ') Addtobuffer(buf,"+",1);
      else
      {  sprintf(enc,"%%%02X",*p);
         Addtobuffer(buf,enc,3);
      }
      p++;
   }
}

