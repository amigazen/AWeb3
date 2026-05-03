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

/* docjs.c - AWeb document JavaScript interface */

#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include "aweb.h"
#include "docprivate.h"
#include "frame.h"
#include "application.h"
#include "source.h"
#include "copy.h"
#include "url.h"
#include "jslib.h"

struct Jprotkey
{  NODE(Jprotkey);
   UBYTE *domain;
};

static LIST(Jprotkey) jprotkeys;

/*------------------------------------------------------------------------*/

static ULONG Getjprotkeydomain(UBYTE *domain)
{  struct Jprotkey *jk;
   for(jk=jprotkeys.first;jk->next;jk=jk->next)
   {  if(jk->domain && STRIEQUAL(jk->domain,domain))
      {  return (ULONG)jk;
      }
   }
   if(jk=ALLOCSTRUCT(Jprotkey,1,0))
   {  jk->domain=Dupstr(domain,-1);
      ADDHEAD(&jprotkeys,jk);
      return (ULONG)jk;
   }
   return 0;
}

static ULONG Getjprotkey(struct Document *doc)
{  UBYTE *start,*domain;
   long length;
   ULONG key=0;
   Getjspart((void *)Agetattr(doc->source->source,AOSRC_Url),UJP_HOST,&start,&length);
   if(length>0)
   {  if(domain=Dupstr(start,length))
      {  key=Getjprotkeydomain(domain);
         FREE(domain);
      }
   }
   return key;
}

/*------------------------------------------------------------------------*/

static void Getjcolor(struct Varhookdata *vd,struct Colorinfo *ci,struct Colorprefs *cp)
{  ULONG rgb;
   UBYTE buf[10];
   if(prefs.browser.docolors && ci && ci->pen>=0)
   {  rgb=ci->rgb;
   }
   else
   {  rgb=((cp->red>>8)&0xff0000) | ((cp->green>>16)&0xff00) | ((cp->blue>>24)&0xff);
   }
   sprintf(buf,"#%06x",rgb);
   Jasgstring(vd->jc,vd->value,buf);
}

static void Setjcolor(struct Varhookdata *vd,struct Document *doc,struct Colorinfo **cip)
{  UBYTE *color;
   if(color=Jtostring(vd->jc,vd->value))
   {  if(Setbodycolor(doc,cip,color))
      {  Registerdoccolors(doc);
         Asetattrs(doc->copy,AOBJ_Changedchild,doc,TAG_END);
         Changedlayout();
      }
   }
}

static BOOL Propertybgcolor(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Document *doc=vd->hookdata;
   if(doc)
   {  switch(vd->code)
      {  case VHC_SET:
            Setjcolor(vd,doc,&doc->bgcolor);
            result=TRUE;
            break;
         case VHC_GET:
            Getjcolor(vd,doc->bgcolor,&prefs.browser.background);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertyfgcolor(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Document *doc=vd->hookdata;
   if(doc)
   {  switch(vd->code)
      {  case VHC_SET:
            Setjcolor(vd,doc,&doc->textcolor);
            result=TRUE;
            break;
         case VHC_GET:
            Getjcolor(vd,doc->textcolor,&prefs.browser.text);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertylinkcolor(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Document *doc=vd->hookdata;
   if(doc)
   {  switch(vd->code)
      {  case VHC_SET:
            Setjcolor(vd,doc,&doc->linkcolor);
            result=TRUE;
            break;
         case VHC_GET:
            Getjcolor(vd,doc->linkcolor,&prefs.browser.newlink);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertyalinkcolor(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Document *doc=vd->hookdata;
   if(doc)
   {  switch(vd->code)
      {  case VHC_SET:
            Setjcolor(vd,doc,&doc->alinkcolor);
            result=TRUE;
            break;
         case VHC_GET:
            Getjcolor(vd,doc->alinkcolor,&prefs.browser.selectlink);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertyvlinkcolor(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Document *doc=vd->hookdata;
   if(doc)
   {  switch(vd->code)
      {  case VHC_SET:
            Setjcolor(vd,doc,&doc->vlinkcolor);
            result=TRUE;
            break;
         case VHC_GET:
            Getjcolor(vd,doc->vlinkcolor,&prefs.browser.oldlink);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertycookie(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Document *doc=vd->hookdata;
   UBYTE *url,*cookie;
   if(doc)
   {  url=(UBYTE *)Agetattr((void *)Agetattr(doc->source->source,AOSRC_Url),AOURL_Url);
      switch(vd->code)
      {  case VHC_SET:
            if(cookie=Dupstr(Jtostring(vd->jc,vd->value),-1))
            {  Storecookie(url,cookie,Today());
               FREE(cookie);
            }
            result=TRUE;
            break;
         case VHC_GET:
            if(cookie=Getjcookies(url))
            {  Jasgstring(vd->jc,vd->value,cookie);
               FREE(cookie);
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

static BOOL Propertydomain(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Document *doc=vd->hookdata;
   UBYTE *newdomain;
   long l0,l1;
   struct Jvar *jv;
   ULONG jprotkey;
   if(doc)
   {  switch(vd->code)
      {  case VHC_SET:
            /* Only allow setting of domain if done from the same frame */
            if(Getjsframe(vd->jc)==doc->frame)
            {  if(doc->jdomain && (newdomain=Jtostring(vd->jc,vd->value)))
               {  l0=strlen(doc->jdomain);
                  l1=strlen(newdomain);
                  /* New domain must be a right-hand substring of current domain
                   * directly following a dot, and containing a dot. */
                  if(l1<l0 && strchr(newdomain,'.') && doc->jdomain[l0-l1-1]=='.'
                  && STRIEQUAL(doc->jdomain+l0-l1,newdomain))
                  {  FREE(doc->jdomain);
                     doc->jdomain=Dupstr(newdomain,l1);
                     jprotkey=Getjprotkeydomain(newdomain);
                     Asetattrs(doc->frame,AOFRM_Jprotect,jprotkey,TAG_END);
                     if(jv=Jproperty(vd->jc,doc->jobject,"domain")) Jpprotect(jv,jprotkey);
                     if(jv=Jproperty(vd->jc,doc->jobject,"cookie")) Jpprotect(jv,jprotkey);
                     if(jv=Jproperty(vd->jc,doc->jobject,"forms")) Jpprotect(jv,jprotkey);
                     if(jv=Jproperty(vd->jc,doc->jobject,"lastModified")) Jpprotect(jv,jprotkey);
                     if(jv=Jproperty(vd->jc,doc->jobject,"links")) Jpprotect(jv,jprotkey);
                     if(jv=Jproperty(vd->jc,doc->jobject,"referrer")) Jpprotect(jv,jprotkey);
                     if(jv=Jproperty(vd->jc,doc->jobject,"title")) Jpprotect(jv,jprotkey);
                     if(jv=Jproperty(vd->jc,doc->jobject,"URL")) Jpprotect(jv,jprotkey);
                  }
               }
            }
            result=TRUE;
            break;
         case VHC_GET:
            Jasgstring(vd->jc,vd->value,doc->jdomain);
            result=TRUE;
            break;
      }
   }
   return result;
}

static BOOL Propertytitle(struct Varhookdata *vd)
{  BOOL result=FALSE;
   struct Document *doc=vd->hookdata;
   UBYTE *title;
   if(doc)
   {  switch(vd->code)
      {  case VHC_SET:
            /* title is read-only */
            result=TRUE;
            break;
         case VHC_GET:
            if(doc->dflags&DDF_TITLEVALID)
            {  title=doc->text.buffer+doc->titlepos;
            }
            else title="";
            Jasgstring(vd->jc,vd->value,title);
            result=TRUE;
            break;
      }
   }
   return result;
}

static void Domethodwrite(struct Jcontext *jc,BOOL ln)
{  struct Jvar *jv;
   UBYTE *s;
   long n;
   struct Document *doc=Jointernal(Jthis(jc));
   if(doc)
   {  for(n=0;jv=Jfargument(jc,n);n++)
      {  s=Jtostring(jc,jv);
         if(s) Addtobuffer(&doc->jout,s,strlen(s));
      }
      if(ln) Addtobuffer(&doc->jout,"\n",1);
      if(doc->pflags&DPF_JRUN)
      {  /* Called while parsing. Parse generated text now as far as it goes. */
         Parsehtml(doc,&doc->jout,FALSE,&doc->joutpos);
         Asetattrs(doc->copy,AOBJ_Changedchild,doc,TAG_END);
         Changedlayout();
         /* Set up JS again, since new elements may have been added that are
          * referenced in following source code */
         Ajsetup(Aweb(),NULL,NULL,NULL);
      }
      else if(doc->source->flags&DOSF_JSOPEN)
      {  /* If called while the JS generated source is still open,
          * send the output to our source. First make a copy of the
          * output, then clear the jout buffer just in case a
          * JS script was written. */
         s=Dupstr(doc->jout.buffer,n=doc->jout.length);
         Freebuffer(&doc->jout);
         if(s)
         {  Asrcupdatetags(doc->source,NULL,
               AOURL_Data,s,
               AOURL_Datalength,n,
               TAG_END);
            FREE(s);
            Changedlayout();
         }
      }
      else
      {  /* Else store the output and let frame start a fetch for
          * this output afterwards. */
         Asetattrs(doc->frame,
            AOFRM_Jgenerated,&doc->jout,
            AOFRM_Jsopen,TRUE,
            TAG_END);
      }
   }
}

static void Methodwrite(struct Jcontext *jc)
{  Domethodwrite(jc,FALSE);
}

static void Methodwriteln(struct Jcontext *jc)
{  Domethodwrite(jc,TRUE);
}

static void Methodclose(struct Jcontext *jc)
{  struct Document *doc=Jointernal(Jthis(jc));
   if(doc)
   {  if(doc->pflags&DPF_JRUN)
      {  /* Meaningless. */
      }
      else if(doc->source->flags&DOSF_JSOPEN)
      {  Asrcupdatetags(doc->source,NULL,
            AOURL_Jsopen,FALSE,
            TAG_END);
         Changedlayout();
      }
      else
      {  Asetattrs(doc->frame,AOFRM_Jsopen,FALSE,TAG_END);
      }
   }
}

static void Methodopen(struct Jcontext *jc)
{  struct Document *doc=Jointernal(Jthis(jc));
   if(doc)
   {  /* Close any open js generated document */
      Methodclose(jc);
      Freebuffer(&doc->jout);
      if(!(doc->pflags&DPF_JRUN))
      {  Asetattrs(doc->frame,AOFRM_Jsopen,TRUE,TAG_END);
      }
   }
}

/*------------------------------------------------------------------------*/

long Jsetupdocument(struct Document *doc,struct Amjsetup *amj)
{  struct Jvar *jv;
   struct Aobject *p;
   ULONG jprotkey;
   if(!doc->jobject)
   {  if(doc->jobject=Newjobject(amj->jc))
      {  Jkeepobject(doc->jobject,TRUE);
         jprotkey=Getjprotkey(doc);
         Asetattrs(doc->frame,
            AOFRM_Jdocument,doc->jobject,
            AOFRM_Jprotect,jprotkey,
            TAG_END);
         Setjobject(doc->jobject,NULL,doc,NULL);
         if(jv=Jproperty(amj->jc,amj->parent,"document"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(amj->jc,jv,doc->jobject);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"forms"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jpprotect(jv,jprotkey);
            if(doc->jforms=Newjarray(amj->jc))
            {  Jasgobject(amj->jc,jv,doc->jforms);
               Jsetobjasfunc(doc->jforms,TRUE);
            }
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"links"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jpprotect(jv,jprotkey);
            if(doc->jlinks=Newjarray(amj->jc))
            {  Jasgobject(amj->jc,jv,doc->jlinks);
               Jsetobjasfunc(doc->jlinks,TRUE);
            }
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"images"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            if(doc->jimages=Newjarray(amj->jc))
            {  Jasgobject(amj->jc,jv,doc->jimages);
               Jsetobjasfunc(doc->jimages,TRUE);
            }
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"anchors"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            if(doc->janchors=Newjarray(amj->jc))
            {  Jasgobject(amj->jc,jv,doc->janchors);
               Jsetobjasfunc(doc->janchors,TRUE);
            }
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"applets"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            if(doc->japplets=Newjarray(amj->jc))
            {  Jasgobject(amj->jc,jv,doc->japplets);
               Jsetobjasfunc(doc->japplets,TRUE);
            }
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"embeds"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            if(doc->jembeds=Newjarray(amj->jc))
            {  Jasgobject(amj->jc,jv,doc->jembeds);
               Jsetobjasfunc(doc->jembeds,TRUE);
            }
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"bgColor"))
         {  Setjproperty(jv,Propertybgcolor,doc);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"fgColor"))
         {  Setjproperty(jv,Propertyfgcolor,doc);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"linkColor"))
         {  Setjproperty(jv,Propertylinkcolor,doc);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"alinkColor"))
         {  Setjproperty(jv,Propertyalinkcolor,doc);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"vlinkColor"))
         {  Setjproperty(jv,Propertyvlinkcolor,doc);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"cookie"))
         {  Setjproperty(jv,Propertycookie,doc);
            Jpprotect(jv,jprotkey);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"title"))
         {  Setjproperty(jv,Propertytitle,doc);
            Jpprotect(jv,jprotkey);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"URL"))
         {  UBYTE *s;
            Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jpprotect(jv,jprotkey);
            s=(UBYTE *)Agetattr((void *)Agetattr(doc->source->source,AOSRC_Url),AOURL_Url);
            Jasgstring(amj->jc,jv,s);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"domain"))
         {  Setjproperty(jv,Propertydomain,doc);
            Jpprotect(jv,jprotkey);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"lastModified"))
         {  UBYTE buf[30];
            long date=Agetattr(doc->source->source,AOSRC_Lastmodified);
            Makedate(date,buf);
            Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jpprotect(jv,jprotkey);
            Jasgstring(amj->jc,jv,buf);
         }
         if(jv=Jproperty(amj->jc,doc->jobject,"referrer"))
         {  UBYTE *s;
            Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jpprotect(jv,jprotkey);
            s=(UBYTE *)Agetattr((void *)Agetattr(doc->copy,AOCPY_Referer),AOURL_Url);
            Jasgstring(amj->jc,jv,s);
         }
         Addjfunction(amj->jc,doc->jobject,"close",Methodclose,NULL);
         Addjfunction(amj->jc,doc->jobject,"open",Methodopen,NULL);
         Addjfunction(amj->jc,doc->jobject,"write",Methodwrite,"string",NULL);
         Addjfunction(amj->jc,doc->jobject,"writeln",Methodwriteln,"string",NULL);
         Jaddeventhandler(amj->jc,amj->parent,"onload",doc->onload);
         Jaddeventhandler(amj->jc,amj->parent,"onunload",doc->onunload);
      }
   }
   if(doc->jobject) 
   {  /* .location is actually a reference to frame.location. Since that is
       * renewed every time, assign the new object here. */
      if(jv=Jproperty(amj->jc,doc->jobject,"location"))
      {  Setjproperty(jv,JPROPHOOK_SYNONYM,Jproperty(amj->jc,amj->parent,"location"));
         Jpprotect(jv,jprotkey);
      }
      for(p=doc->forms.first;p->next;p=p->next)
      {  Ajsetup(p,amj->jc,doc->jobject,amj->parentframe);
      }
      for(p=doc->links.first;p->next;p=p->next)
      {  Ajsetup(p,amj->jc,doc->jobject,amj->parentframe);
      }
      Ajsetup(doc->body,amj->jc,doc->jobject,amj->parentframe);
   }
   return 0;
}

/* Called when inline or external script is run during parse (e.g. </script> or
 * when external script load completes via AODOC_Docextready). Sync XHR is
 * implemented by blocking in send() until the fetch task signals completion. */
void Docjexecute(struct Document *doc,UBYTE *source)
{  struct Jcontext *jc=(struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext);
   if(jc)
   {  Jsetlinenumber(jc,doc->jsrcline+1);
      Runjsnobanners(doc->frame,source,NULL);
      /* GC after each Runjprogram is done in Runjavascriptwith (framejs.c). */
   }

/*** OR: show JS instead of execute
      {  long jpos=0;
         UBYTE *p;
         p="<hr><b>JavaScript:</b><listing>";
         Addtobuffer(&doc->jout,p,strlen(p));
         Addtobuffer(&doc->jout,doc->jsrc.buffer,doc->jsrc.length-1);
         p="</listing><hr>";
         Addtobuffer(&doc->jout,p,strlen(p));
      }
***/
}

void Freejdoc(struct Document *doc)
{  if(doc->frame)
   {  Asetattrs(doc->frame,
         AOFRM_Jdocument,NULL,
         AOFRM_Onfocus,NULL,
         AOFRM_Onblur,NULL,
         TAG_END);
   }
   if(doc->jobject)
   {  Disposejobject(doc->jobject);
      doc->jobject=NULL;
   }
   if(doc->jforms)
   {  Disposejobject(doc->jforms);
      doc->jforms=NULL;
   }
   if(doc->jlinks)
   {  Disposejobject(doc->jlinks);
      doc->jlinks=NULL;
   }
   if(doc->jimages)
   {  Disposejobject(doc->jimages);
      doc->jimages=NULL;
   }
   if(doc->janchors)
   {  Disposejobject(doc->janchors);
      doc->janchors=NULL;
   }
   if(doc->japplets)
   {  Disposejobject(doc->japplets);
      doc->japplets=NULL;
   }
   if(doc->jembeds)
   {  Disposejobject(doc->jembeds);
      doc->jembeds=NULL;
   }
   if(AWebJSBase)
   {  void *jc=(void *)Agetattr(Aweb(),AOAPP_Jcontext);
      Jgarbagecollect(jc);
   }
}

void Initdocjs(void)
{  NEWLIST(&jprotkeys);
}

void Exitdocjs(void)
{  struct Jprotkey *jk;
   if(jprotkeys.first)
   {  while(jk=REMHEAD(&jprotkeys))
      {  FREE(jk->domain);
         FREE(jk);
      }
   }
}
