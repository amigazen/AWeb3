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

/* prefs.c - AWeb preference task */

#include "aweb.h"
#include "window.h"
#include "cache.h"
#include "application.h"
#include "jslib.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/utility.h>

#define PCMD_OVERLAP       0x00000001
#define PCMD_LOADIMG       0x00000002
#define PCMD_CACHE         0x00000004
#define PCMD_NEWSCREEN     0x00000008
#define PCMD_NEWLINKPENS   0x00000010
#define PCMD_NEWSAVEPATH   0x00000020
#define PCMD_BLINKRATE     0x00000040
#define PCMD_NEWBUTTONS    0x00000080
#define PCMD_NEWMENUS      0x00000100
#define PCMD_DOCOLORS      0x00000200
#define PCMD_DOBGSOUND     0x00000400
#define PCMD_BROWSER       0x00000800
#define PCMD_SHOWBUTTONS   0x00001000
#define PCMD_NEWMIME       0x00002000
#define PCMD_CONTANIM      0x00004000
#define PCMD_SAVEPREFS     0x10000000

struct Fontprefs *fonts;

struct Prefs prefs;
struct SignalSemaphore prefssema;

static UBYTE brprefsname[64],prprefsname[64],uiprefsname[64],nwprefsname[64],ncprefsname[64];
static struct MsgPort *notifyport;
static struct NotifyRequest nfreqbr,nfreqpr,nfrequi,nfreqnw,nfreqnc;

#define NFCODE_BROWSER  0x0001
#define NFCODE_PROGRAM  0x0002
#define NFCODE_NETWORK  0x0004
#define NFCODE_GUI      0x0008

static struct Jobject *jmimetypes;
static struct Jobject *jplugins;

/*-----------------------------------------------------------------------*/

static void Methodpluginsrefresh(struct Jcontext *jc)
{  /* Everything is automatically refreshed */
}

/* Rebuild the JS mimetypes and plugin array objects */
static void Jmimetypes(struct Jcontext *jc)
{  struct Jvar *jv;
   struct Jobject *jo,*jpo;
   struct Mimeinfo *mi,*mj;
   long mlength=0,plength=0;
   UBYTE buf[64],namebuf[32];
   UBYTE *p,*q,*name;
   /* First create all MIME types */
   for(mi=prefs.browser.mimelist.first;mi->next;mi=mi->next)
   {  if(jo=Newjobject(jc))
      {  if(jv=Jproperty(jc,jo,"description"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            if(STREQUAL(mi->subtype,"*")) strcpy(buf,mi->type);
            else if(STRNIEQUAL(mi->subtype,"X-",2)) strcpy(buf,mi->subtype+2);
            else strcpy(buf,mi->subtype);
            for(p=buf;*p;p++) *p=toupper(*p);
            strcat(buf," file");
            Jasgstring(jc,jv,buf);
         }
         if(jv=Jproperty(jc,jo,"enabledPlugin"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(jc,jv,NULL);
         }
         if(jv=Jproperty(jc,jo,"type"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            sprintf(buf,"%s/%s",mi->type,mi->subtype);
            for(p=buf;*p;p++) *p=tolower(*p);
            Jasgstring(jc,jv,buf);
         }
         if(jv=Jproperty(jc,jo,"suffixes"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            for(p=buf;*p;p++) *p=tolower(*p);
            Jasgstring(jc,jv,mi->extensions);
         }
         if(jmimetypes)
         {  sprintf(buf,"%d",mlength++);
            if(jv=Jproperty(jc,jmimetypes,buf))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgobject(jc,jv,jo);
            }
            sprintf(buf,"%s/%s",mi->type,mi->subtype);
            for(p=buf;*p;p++) *p=tolower(*p);
            if(jv=Jproperty(jc,jmimetypes,buf))
            {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
               Jasgobject(jc,jv,jo);
            }
         }
         Freejobject(jo);
      }
   }
   if(jv=Jproperty(jc,jmimetypes,"length"))
   {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
      Jasgnumber(jc,jv,mlength);
   }
   /* Scan the mime list for plugins. */
   for(mi=prefs.browser.mimelist.first;mi->next;mi=mi->next)
   {  if(mi->driver==MDRIVER_PLUGIN && mi->cmd)
      {  name=mi->cmd+strlen(mi->cmd)-1;
         for(;*name && *name!='/' && *name!=':';name--);
         name++;
         for(p=name,q=namebuf;*p && *p!='.';*q++=*p++);
         *q='\0';
         /* See if we haven't got this plugin already */
         if(jv=Jproperty(jc,jplugins,namebuf))
         {  if(Jtoobject(jc,jv)==NULL)
            {  /* New plugin */
               if(jpo=Newjobject(jc))
               {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                  Jasgobject(jc,jv,jpo);
                  if(jv=Jproperty(jc,jpo,"description"))
                  {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                     Jasgstring(jc,jv,name);
                  }
                  if(jv=Jproperty(jc,jpo,"filename"))
                  {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                     Jasgstring(jc,jv,mi->cmd);
                  }
                  if(jv=Jproperty(jc,jpo,"name"))
                  {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                     Jasgstring(jc,jv,namebuf);
                  }
                  if(jv=Jproperty(jc,jpo,"length"))
                  {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                     Jasgnumber(jc,jv,0);
                  }
                  sprintf(buf,"%d",plength++);
                  if(jv=Jproperty(jc,jplugins,buf))
                  {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                     Jasgobject(jc,jv,jpo);
                  }
                  
                  /* Scan the mime list and add all MIME types for this plugin,
                   * and add this plugin to these MIME types. */
                  mlength=0;
                  for(mj=mi;mj->next;mj=mj->next)
                  {  if(mj->driver==MDRIVER_PLUGIN && mi->cmd && STRIEQUAL(mi->cmd,mj->cmd))
                     {  sprintf(buf,"%s/%s",mj->type,mj->subtype);
                        for(p=buf;*p;p++) *p=tolower(*p);
                        if(jv=Jproperty(jc,jmimetypes,buf))
                        {  jo=Jtoobject(jc,jv);
                           if(jv=Jproperty(jc,jo,"enabledPlugin"))
                           {  Jasgobject(jc,jv,jpo);
                           }
                           sprintf(buf,"%d",mlength++);
                           if(jv=Jproperty(jc,jpo,buf))
                           {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                              Jasgobject(jc,jv,jo);
                           }
                        }
                     }
                  }
                  if(jv=Jproperty(jc,jpo,"length"))
                  {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
                     Jasgnumber(jc,jv,mlength);
                  }
                  Freejobject(jpo);
               }
            }
         }
      }
   }
   if(jv=Jproperty(jc,jplugins,"length"))
   {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
      Jasgnumber(jc,jv,plength);
   }
   Addjfunction(jc,jplugins,"refresh",Methodpluginsrefresh,"reload",NULL);
}

/* Re-build the JS mimetypes and plugins arrays */
static void Rebuildjmime(void)
{  if(jmimetypes && jplugins)
   {  Clearjobject(jmimetypes,NULL);
      Clearjobject(jplugins,NULL);
      Jmimetypes((struct Jcontext *)Agetattr(Aweb(),AOAPP_Jcontext));
   }
}

/*-----------------------------------------------------------------------*/

static void Copyfile(UBYTE *from,UBYTE *to)
{  UBYTE *block;
   long fh1,fh2,l;
   if(block=ALLOCTYPE(UBYTE,INPUTBLOCKSIZE,MEMF_PUBLIC))
   {  if(fh1=Open(from,MODE_OLDFILE))
      {  if(fh2=Open(to,MODE_NEWFILE))
         {  while((l=Read(fh1,block,INPUTBLOCKSIZE))>0)
            {  Write(fh2,block,l);
            }
            Close(fh2);
         }
         Close(fh1);
      }
      FREE(block);
   }
}

static void Makepatterns(void *listp)
{  LIST(Nocache) *list=listp;
   struct Nocache *nc;
   long len;
   for(nc=list->first;nc->next;nc=nc->next)
   {  if(nc->pattern) FREE(nc->pattern);
      len=2*strlen(nc->name)+4;
      nc->pattern=ALLOCTYPE(UBYTE,len,0);
      if(nc->pattern)
      {  if(ParsePatternNoCase(nc->name,nc->pattern,len)<=0)
         {  FREE(nc->pattern);
            nc->pattern=NULL;
         }
      }
   }
}

static BOOL Openfonts(void)
{  short i,j;
   struct TextAttr ta={0};
   for(j=0;j<2;j++)
   {  for(i=0;i<NRFONTS;i++)
      {  ta.ta_Name=prefs.browser.font[j][i].fontname;
         ta.ta_YSize=prefs.browser.font[j][i].fontsize;
         if(!(prefs.browser.font[j][i].font=OpenDiskFont(&ta)))
         {  /* Fallback to bitmap fonts if scalable fonts not available */
            if(j==0)
            {  ta.ta_Name="times.font";
            }
            else
            {  ta.ta_Name="courier.font";
            }
            if(!(prefs.browser.font[j][i].font=OpenDiskFont(&ta)))
            {  ta.ta_Name="topaz.font";
               if(!(prefs.browser.font[j][i].font=OpenDiskFont(&ta))) return FALSE;
            }
         }
      }
      Setloadreqlevel(j+1,2);
   }
   return TRUE;
}

static void Opennewfont(struct Fontprefs *newf)
{  struct TextAttr ta={0};
   ta.ta_Name=newf->fontname;
   ta.ta_YSize=newf->fontsize;
   newf->font=OpenDiskFont(&ta);
}

static ULONG Comparefontprefsarray(struct Fontprefs *oldfa,struct Fontprefs *newfa,BOOL open)
{  ULONG pcmd=0;
   short i;
   struct Fontprefs *oldf,*newf;
   for(i=0;i<NRFONTS;i++)
   {  oldf=&oldfa[i];
      newf=&newfa[i];
      if(!STREQUAL(oldf->fontname,newf->fontname)
      || oldf->fontsize!=newf->fontsize)
      {  pcmd|=PCMD_BROWSER;
         if(open)
         {  Opennewfont(newf);
            if(!newf->font)
            {  newf->font=oldf->font;  /* keep old one */
               oldf->font=NULL;        /* and prevent closing by Dispose below */
            }
         }
         else
         {  newf->font=NULL;           /* open when required */
         }
      }
      else
      {  oldf->font=NULL;              /* don't close, still in use */
      }
   }
   return pcmd;
}

static void Installmimetypes(void)
{  struct Mimeinfo *mi;
   UBYTE buffer[32];
   UBYTE *p;
   Reinitmime();
   for(mi=prefs.browser.mimelist.first;mi->next;mi=mi->next)
   {  if(*mi->type)
      {  strcpy(buffer,mi->type);
         strcat(buffer,"/");
         if(*mi->subtype) p=mi->subtype;
         else p="*";
         strncat(buffer,p,31-strlen(buffer));
         Addmimetype(buffer,mi->extensions,mi->driver,mi->cmd,mi->args);
      }
   }
}

static ULONG Changedbrowser(void)
{  struct Browserprefs oldp={0};
   short i,j;
   ULONG pcmd=0;
   struct Fontalias *fao,*fan;
   struct Styleprefs *olds,*news;
   struct Mimeinfo *min,*mio;
   Copybrowserprefs(&prefs.browser,&oldp);
   Loadbrowserprefs(&prefs.browser,FALSE,NULL);
   Busypointer(TRUE);
   for(j=0;j<2;j++)
   {  pcmd|=Comparefontprefsarray(oldp.font[j],prefs.browser.font[j],TRUE);
   }
   Busypointer(FALSE);
   for(fan=prefs.browser.aliaslist.first;fan->next;fan=fan->next)
   {  for(fao=oldp.aliaslist.first;fao->next;fao=fao->next)
      {  if(STRIEQUAL(fan->alias,fao->alias))
         {  pcmd|=Comparefontprefsarray(fao->fp,fan->fp,FALSE);
            break;
         }
      }
      if(!fao->next) pcmd|=PCMD_BROWSER;
   }
   /* Also check if no alias was deleted */
   for(fao=oldp.aliaslist.first;fao->next;fao=fao->next)
   {  for(fan=prefs.browser.aliaslist.first;fan->next;fan=fan->next)
      {  if(STRIEQUAL(fan->alias,fao->alias)) break;
      }
      if(!fan->next)
      {  pcmd|=PCMD_BROWSER;
         break;
      }
   }
   for(i=0;i<NRSTYLES;i++)
   {  olds=&oldp.styles[i];
      news=&prefs.browser.styles[i];
      if(olds->fonttype!=news->fonttype
      || olds->fontsize!=news->fontsize
      || olds->style!=news->style)
         pcmd|=PCMD_BROWSER;
   }
   if(prefs.browser.ullink!=oldp.ullink) pcmd|=PCMD_BROWSER;
   if(memcmp(&prefs.browser.newlink,&oldp.newlink,sizeof(prefs.browser.newlink)))
      pcmd|=PCMD_NEWLINKPENS;
   if(memcmp(&prefs.browser.oldlink,&oldp.oldlink,sizeof(prefs.browser.oldlink)))
      pcmd|=PCMD_NEWLINKPENS;
   if(memcmp(&prefs.browser.selectlink,&oldp.selectlink,sizeof(prefs.browser.selectlink)))
      pcmd|=PCMD_NEWLINKPENS;
   if(memcmp(&prefs.browser.background,&oldp.background,sizeof(prefs.browser.background)))
      pcmd|=PCMD_NEWLINKPENS;
   if(memcmp(&prefs.browser.text,&oldp.text,sizeof(prefs.browser.text)))
      pcmd|=PCMD_NEWLINKPENS;
   if(prefs.browser.htmlmode!=oldp.htmlmode) pcmd|=PCMD_BROWSER;
   if(prefs.browser.docolors!=oldp.docolors) pcmd|=PCMD_DOCOLORS;
   if(prefs.browser.dobgsound!=oldp.dobgsound) pcmd|=PCMD_DOBGSOUND;
   if(prefs.browser.blinkrate!=oldp.blinkrate) pcmd|=PCMD_BLINKRATE;
   if(prefs.browser.screenpens!=oldp.screenpens) pcmd|=PCMD_NEWLINKPENS;
   if(prefs.browser.dojs!=oldp.dojs) pcmd|=PCMD_BROWSER;
   if(prefs.browser.doframes!=oldp.doframes) pcmd|=PCMD_BROWSER;
   if(prefs.browser.nominalframe!=oldp.nominalframe) pcmd|=PCMD_BROWSER;
   for(min=prefs.browser.mimelist.first;min->next && !(pcmd&PCMD_NEWMIME);min=min->next)
   {  for(mio=oldp.mimelist.first;mio->next;mio=mio->next)
      {  if(STRIEQUAL(min->type,mio->type) && STRIEQUAL(min->subtype,mio->subtype))
         {  if((min->driver==MDRIVER_PLUGIN || mio->driver==MDRIVER_PLUGIN)
            && (min->driver!=mio->driver))
            {  pcmd|=PCMD_NEWMIME;
            }
            else if(min->driver==MDRIVER_PLUGIN && !STREQUAL(min->cmd,mio->cmd))
            {  pcmd|=PCMD_NEWMIME;
            }
            else if(!STRIEQUAL(min->extensions,mio->extensions))
            {  pcmd|=PCMD_NEWMIME;
            }
            break;
         }
      }
      if(!mio->next) pcmd|=PCMD_NEWMIME;
   }
   Installmimetypes();
   Disposebrowserprefs(&oldp);
   return pcmd;
}

static ULONG Changedprogram(void)
{  struct Programprefs oldp={0};
   ULONG pcmd=0;
   Copyprogramprefs(&prefs.program,&oldp);
   Loadprogramprefs(&prefs.program,FALSE,NULL);
   if(prefs.program.screentype!=oldp.screentype) pcmd|=PCMD_NEWSCREEN;
   if(prefs.program.screentype==SCRTYPE_NAMED)
   {  if(!STREQUAL(prefs.program.screenname,oldp.screenname)) pcmd|=PCMD_NEWSCREEN;
   }
   if(prefs.program.screentype==SCRTYPE_OWN)
   {  if(prefs.program.screenmode!=oldp.screenmode
      || prefs.program.screenwidth!=oldp.screenwidth
      || prefs.program.screenheight!=oldp.screenheight
      || prefs.program.screendepth!=oldp.screendepth
      || prefs.program.loadpalette!=oldp.loadpalette) pcmd|=PCMD_NEWSCREEN;
   }
   if(prefs.program.screentype==SCRTYPE_OWN
   && memcmp(prefs.program.scrdrawpens,oldp.scrdrawpens,sizeof(prefs.program.scrdrawpens)))
      pcmd|=PCMD_NEWSCREEN;
   if(!STRIEQUAL(prefs.program.savepath,oldp.savepath)) pcmd|=PCMD_NEWSAVEPATH;
   if(prefs.program.overlap!=oldp.overlap) pcmd|=PCMD_OVERLAP;
   Disposeprogramprefs(&oldp);
   return pcmd;
}

static ULONG Changedgui(void)
{  struct Guiprefs oldp={0};
   struct Menuentry *me;
   struct Userbutton *ub,*uc;
   struct Popupitem *pi;
   struct Userkey *uk;
   short i;
   ULONG pcmd=0;
   Copyguiprefs(&prefs.gui,&oldp);
   /* Delete all existing definitions, so "no entry" in the config file won't
    * leave the exiting definitions */
#ifndef DEMOVERSION
   while(me=REMHEAD(&prefs.gui.menus)) Freemenuentry(me);
   while(uk=REMHEAD(&prefs.gui.keys)) Freeuserkey(uk);
#endif
   while(ub=REMHEAD(&prefs.gui.buttons)) Freeuserbutton(ub);
   for(i=0;i<NRPOPUPMENUS;i++)
   {  while(pi=REMHEAD(&prefs.gui.popupmenu[i])) Freepopupitem(pi);
   }
   Loadguiprefs(&prefs.gui,FALSE,NULL);
   if(prefs.gui.showbuttons!=oldp.showbuttons) pcmd|=PCMD_SHOWBUTTONS;
   if(prefs.gui.shownav!=oldp.shownav) pcmd|=PCMD_SHOWBUTTONS;
/*
   for(me=oldp.menus.first,mf=prefs.gui.menus.first;
      me->next && mf->next;me=me->next,mf=mf->next)
   {  if(me->type!=mf->type
      || !STREQUAL(me->title,mf->title)
      || me->scut[0]!=mf->scut[0]) break;
   }
   if(me->next || mf->next) pcmd|=PCMD_NEWMENUS;   /* old and new menus are different */
*/
   pcmd|=PCMD_NEWMENUS; /* Always rebuild the menus and the menunums */
   for(ub=oldp.buttons.first,uc=prefs.gui.buttons.first;
      ub->next && uc->next;ub=ub->next,uc=uc->next)
   {  if(!STREQUAL(ub->label,uc->label)) break;
   }
   if(ub->next || uc->next) pcmd|=PCMD_NEWBUTTONS; /* old and new labels are different */
   Disposeguiprefs(&oldp);
   return pcmd;
}

static ULONG Changednetwork(void)
{  struct Networkprefs oldp={0};
   ULONG pcmd=0;
   Copynetworkprefs(&prefs.network,&oldp);
   Disposenetworkprefs(&prefs.network);
   Loadnetworkprefs(&prefs.network,FALSE,NULL);
   if(prefs.network.loadimg!=oldp.loadimg) pcmd|=PCMD_LOADIMG;
   if(prefs.network.camemsize<oldp.camemsize) pcmd|=PCMD_CACHE;
   if(prefs.network.cadisksize<oldp.cadisksize) pcmd|=PCMD_CACHE;
   if(prefs.network.minfreechip<oldp.minfreechip) pcmd|=PCMD_CACHE;
   if(prefs.network.minfreefast<oldp.minfreefast) pcmd|=PCMD_CACHE;
   if(prefs.network.contanim!=oldp.contanim) pcmd|=PCMD_CONTANIM;
   if(prefs.network.restrictimages!=oldp.restrictimages) pcmd|=PCMD_LOADIMG;
   Makepatterns(&prefs.network.nocookie);
   Makepatterns(&prefs.network.noproxy);
   Makepatterns(&prefs.network.nocache);
   Disposenetworkprefs(&oldp);
   return pcmd;
}

static void Processprefs(void)
{  struct NotifyMessage *msg;
   USHORT changed=0;
   ULONG pcmd=0;
   ObtainSemaphore(&prefssema);
   while(msg=(struct NotifyMessage *)GetMsg(notifyport))
   {  changed|=msg->nm_NReq->nr_UserData;
      ReplyMsg(msg);
   }
   if(changed&NFCODE_BROWSER)
   {  pcmd|=Changedbrowser();
   }
   if(changed&NFCODE_PROGRAM)
   {  pcmd|=Changedprogram();
   }
   if(changed&NFCODE_GUI)
   {  pcmd|=Changedgui();
   }
   if(changed&NFCODE_NETWORK)
   {  pcmd|=Changednetwork();
   }
   ReleaseSemaphore(&prefssema);
   if(pcmd&PCMD_NEWMIME)
   {  Rebuildjmime();
   }
   if(pcmd&PCMD_NEWSCREEN)
   {  Asetattrs(Aweb(),AOAPP_Newprefs,PREFSF_SCREEN,TAG_END);
      pcmd&=~(PCMD_NEWLINKPENS|PCMD_SHOWBUTTONS|PCMD_NEWBUTTONS);
      pcmd|=PCMD_LOADIMG;
   }
   if(pcmd&PCMD_SHOWBUTTONS)
   {  Asetattrs(Aweb(),AOAPP_Newprefs,PREFSF_SHOWBUTTONS,TAG_END);
      pcmd&=~(PCMD_NEWBUTTONS);
   }
   if(pcmd&PCMD_NEWBUTTONS)
   {  Asetattrs(Aweb(),AOAPP_Newprefs,PREFSF_BUTTONS,TAG_END);
   }
   if(pcmd&PCMD_BROWSER)
   {  Asetattrs(Aweb(),AOAPP_Browsersettings,TRUE,TAG_END);
   }
   if(pcmd&PCMD_NEWLINKPENS)
   {  Asetattrs(Aweb(),AOAPP_Browserpens,TRUE,TAG_END);
   }
   if(pcmd&PCMD_BLINKRATE)
   {  Asetattrs(Aweb(),AOAPP_Blink,TRUE,TAG_END);
   }
   if(pcmd&PCMD_NEWMENUS) Asetattrs(Aweb(),AOAPP_Menus,NULL,TAG_END);
   if(pcmd&PCMD_NEWSAVEPATH) Asetattrs(Aweb(),AOAPP_Savepath,NULL,TAG_END);
   if(pcmd&PCMD_CACHE) Flushcache(CACFT_EXCESS);
   if(pcmd&PCMD_LOADIMG) Setloadimg();
   if(pcmd&PCMD_DOCOLORS) Setdocolors();
   if(pcmd&PCMD_OVERLAP) Asetattrs(Aweb(),AOAPP_Overlapsetting,TRUE,TAG_END);
   if(pcmd&PCMD_CONTANIM) Asetattrs(Aweb(),AOAPP_Animgadsetting,TRUE,TAG_END);
   if(pcmd&PCMD_DOBGSOUND) Setdobgsound();
}

/*-----------------------------------------------------------------------*/

BOOL Initprefs(void)
{  UBYTE nfname[64]="ENV:" DEFAULTCFG;
   InitSemaphore(&prefssema);
   Copybrowserprefs(&defprefs.browser,&prefs.browser);
   Copyprogramprefs(&defprefs.program,&prefs.program);
   Copyguiprefs(&defprefs.gui,&prefs.gui);
   Copynetworkprefs(&defprefs.network,&prefs.network);
   Copywindowprefs(&defprefs.window,&prefs.window);
   Loadbrowserprefs(&prefs.browser,FALSE,NULL);
   Loadprogramprefs(&prefs.program,FALSE,NULL);
   Loadguiprefs(&prefs.gui,FALSE,NULL);
   Loadnetworkprefs(&prefs.network,FALSE,NULL);
   Loadwindowprefs(&prefs.window,TRUE,NULL);
   Installmimetypes();
   Makepatterns(&prefs.network.nocookie);
   Makepatterns(&prefs.network.noproxy);
   Makepatterns(&prefs.network.nocache);
   if(!(notifyport=CreateMsgPort())) return FALSE;
   if(*configname)
   {  AddPart(nfname,configname,64);
      /* Ensure the config directory exists before setting up notify requests */
      {  long lock;
         lock=Lock(nfname,SHARED_LOCK);
         if(!lock) lock=CreateDir(nfname);
         if(lock) UnLock(lock);
      }
   }
   sprintf(brprefsname,"%s/browser",nfname);
   nfreqbr.nr_Name=brprefsname;
   nfreqbr.nr_Port=notifyport;
   nfreqbr.nr_Flags=NRF_SEND_MESSAGE;
   nfreqbr.nr_UserData=NFCODE_BROWSER;
   StartNotify(&nfreqbr);
   sprintf(prprefsname,"%s/program",nfname);
   nfreqpr.nr_Name=prprefsname;
   nfreqpr.nr_Port=notifyport;
   nfreqpr.nr_Flags=NRF_SEND_MESSAGE;
   nfreqpr.nr_UserData=NFCODE_PROGRAM;
   StartNotify(&nfreqpr);
   sprintf(uiprefsname,"%s/gui",nfname);
   nfrequi.nr_Name=uiprefsname;
   nfrequi.nr_Port=notifyport;
   nfrequi.nr_Flags=NRF_SEND_MESSAGE;
   nfrequi.nr_UserData=NFCODE_GUI;
   StartNotify(&nfrequi);
   sprintf(nwprefsname,"%s/network",nfname);
   nfreqnw.nr_Name=nwprefsname;
   nfreqnw.nr_Port=notifyport;
   nfreqnw.nr_Flags=NRF_SEND_MESSAGE;
   nfreqnw.nr_UserData=NFCODE_NETWORK;
   StartNotify(&nfreqnw);
   sprintf(ncprefsname,"%s/nocookie",nfname);
   nfreqnc.nr_Name=ncprefsname;
   nfreqnc.nr_Port=notifyport;
   nfreqnc.nr_Flags=NRF_SEND_MESSAGE;
   nfreqnc.nr_UserData=NFCODE_NETWORK;
   StartNotify(&nfreqnc);
   Setprocessfun(notifyport->mp_SigBit,Processprefs);
   return TRUE;
}

BOOL Initprefs2(void)
{  if(!Openfonts()) return FALSE;
   return TRUE;
}

void Freeprefs(void)
{  if(nfreqbr.nr_Name) EndNotify(&nfreqbr);
   if(nfreqpr.nr_Name) EndNotify(&nfreqpr);
   if(nfrequi.nr_Name) EndNotify(&nfrequi);
   if(nfreqnw.nr_Name) EndNotify(&nfreqnw);
   if(nfreqnc.nr_Name) EndNotify(&nfreqnc);
   if(notifyport)
   {  Setprocessfun(notifyport->mp_SigBit,NULL);
      DeleteMsgPort(notifyport);
   }
   if(jmimetypes) Freejobject(jmimetypes);
   if(jplugins) Freejobject(jplugins);
   Disposebrowserprefs(&prefs.browser);
   Disposeprogramprefs(&prefs.program);
   Disposeguiprefs(&prefs.gui);
   Disposenetworkprefs(&prefs.network);
}

BOOL Setprefsname(UBYTE *name)
{  strncpy(configname,name,31);
   return TRUE;
}

void Startsettings(USHORT type)
{  struct Cfgmsg msg={0};
   struct MsgPort *port,*cfgport;
   UBYTE *p,*q;
   if(port=CreateMsgPort())
   {  Forbid();
      if(cfgport=FindPort(AWEBCFGPORTNAME))
      {  msg.mn_ReplyPort=port;
         msg.cfgclass=type;
         PutMsg(cfgport,&msg);
         Permit();
         WaitPort(port);
      }
      else
      {  Permit();
         switch(type)
         {  case CFGCLASS_BROWSER:p="BROWSER CONFIG %c PUBSCREEN %n";break;
            case CFGCLASS_PROGRAM:p="PROGRAM CONFIG %c PUBSCREEN %n";break;
            case CFGCLASS_GUI:p="GUI CONFIG %c PUBSCREEN %n";break;
            case CFGCLASS_NETWORK:p="NETWORK CONFIG %c PUBSCREEN %n";break;
            default:p=NULL;
         }
         if(p)
         {  if(q=Fullname("AWeb:AWebCfg"))
            {  Spawn(FALSE,q,p,"cn",configname,Agetattr(Aweb(),AOAPP_Screenname));
               FREE(q);
            }
         }
      }
      DeleteMsgPort(port);
   }
}

void Closesettings(void)
{  struct Cfgmsg msg={0};
   struct MsgPort *port,*cfgport;
   if(port=CreateMsgPort())
   {  Forbid();
      if(cfgport=FindPort(AWEBCFGPORTNAME))
      {  msg.mn_ReplyPort=port;
         msg.cfgclass=CFGCLASS_QUIT;
         PutMsg(cfgport,&msg);
         Permit();
         WaitPort(port);
      }
      else Permit();
      DeleteMsgPort(port);
   }
}

void Snapshotwindows(void *window)
{  struct IBox *box=NULL,*zoombox=NULL;
   ObtainSemaphore(&prefssema);
   Agetattrs(window,
      AOWIN_Box,&box,
      AOWIN_Zoombox,&zoombox,
      TAG_END);
   if(box)
   {  prefs.window.winx=box->Left;
      prefs.window.winy=box->Top;
      prefs.window.winw=box->Width;
      prefs.window.winh=box->Height;
   }
   if(zoombox)
   {  prefs.window.wiax=zoombox->Left;
      prefs.window.wiay=zoombox->Top;
      prefs.window.wiaw=zoombox->Width;
      prefs.window.wiah=zoombox->Height;
   }
   Getnetstatdim(&prefs.window.nwsx,&prefs.window.nwsy,&prefs.window.nwsw,&prefs.window.nwsh);
   Getinfodim(&prefs.window.infx,&prefs.window.infy,&prefs.window.infw,&prefs.window.infh);
   Savewindowprefs(&prefs.window,TRUE,NULL);
   ReleaseSemaphore(&prefssema);
}

void Prefsloadimg(long loadimg)
{  ObtainSemaphore(&prefssema);
   if(loadimg!=prefs.network.loadimg)
   {  prefs.network.loadimg=loadimg;
      Savenetworkprefs(&prefs.network,FALSE,NULL);
      Setloadimg();
   }
   ReleaseSemaphore(&prefssema);
}

void Prefsdocolors(BOOL docolors)
{  ObtainSemaphore(&prefssema);
   if(docolors!=prefs.browser.docolors)
   {  prefs.browser.docolors=docolors;
      Savebrowserprefs(&prefs.browser,FALSE,NULL);
      Setdocolors();
   }
   ReleaseSemaphore(&prefssema);
}

void Prefsdobgsound(BOOL dobgsound)
{  ObtainSemaphore(&prefssema);
   if(dobgsound!=prefs.browser.dobgsound)
   {  prefs.browser.dobgsound=dobgsound;
      Savebrowserprefs(&prefs.browser,FALSE,NULL);
      Setdobgsound();
   }
   ReleaseSemaphore(&prefssema);
}

void Saveallsettings(void)
{  ObtainSemaphore(&prefssema);
   Savebrowserprefs(&prefs.browser,TRUE,NULL);
   Saveprogramprefs(&prefs.program,TRUE,NULL);
   Saveguiprefs(&prefs.gui,TRUE,NULL);
   Savenetworkprefs(&prefs.network,TRUE,NULL);
   Savenocookieprefs(&prefs.network,TRUE,NULL);
   ReleaseSemaphore(&prefssema);
}

void Savesettingsas(UBYTE *path)
{  UBYTE *name;
   ObtainSemaphore(&prefssema);
   if(name=ALLOCTYPE(UBYTE,STRINGBUFSIZE,MEMF_CLEAR))
   {  strncpy(name,path,STRINGBUFSIZE-1);
      if(AddPart(name,"browser",STRINGBUFSIZE-1))
      {  Savebrowserprefs(&prefs.browser,TRUE,name);
      }
      strncpy(name,path,STRINGBUFSIZE-1);
      if(AddPart(name,"program",STRINGBUFSIZE-1))
      {  Saveprogramprefs(&prefs.program,TRUE,name);
      }
      strncpy(name,path,STRINGBUFSIZE-1);
      if(AddPart(name,"gui",STRINGBUFSIZE-1))
      {  Saveguiprefs(&prefs.gui,TRUE,name);
      }
      strncpy(name,path,STRINGBUFSIZE-1);
      if(AddPart(name,"network",STRINGBUFSIZE-1))
      {  Savenetworkprefs(&prefs.network,TRUE,name);
      }
      strncpy(name,path,STRINGBUFSIZE-1);
      if(AddPart(name,"nocookie",STRINGBUFSIZE-1))
      {  Savenocookieprefs(&prefs.network,TRUE,name);
      }
      FREE(name);
   }
   ReleaseSemaphore(&prefssema);
}

void Loadsettings(UBYTE *path)
{  UBYTE *name1,*name2;
   UBYTE *config=(UBYTE *)Agetattr(Aweb(),AOAPP_Configname);
   UBYTE *file[]={ "browser","program","gui","network" };
   short i;
   long lock;
   if(name1=ALLOCTYPE(UBYTE,2*STRINGBUFSIZE,MEMF_CLEAR))
   {  name2=name1+STRINGBUFSIZE;
      /* If a custom config name is provided, ensure the directory exists */
      if(config && *config)
      {  strcpy(name2,"ENV:" DEFAULTCFG);
         if(AddPart(name2,config,STRINGBUFSIZE-1))
         {  lock=Lock(name2,SHARED_LOCK);
            if(!lock) lock=CreateDir(name2);
            if(lock) UnLock(lock);
         }
      }
      for(i=0;i<4;i++)
      {  strncpy(name1,path,STRINGBUFSIZE-1);
         if(!AddPart(name1,file[i],STRINGBUFSIZE-1)) break;
         strcpy(name2,"ENV:" DEFAULTCFG);
         if(!AddPart(name2,config,STRINGBUFSIZE-1)) break;
         if(!AddPart(name2,file[i],STRINGBUFSIZE-1)) break;
         Copyfile(name1,name2);
      }
      FREE(name1);
   }
}

/* Add name to network nocookie list and save */
void Addtonocookie(UBYTE *name)
{  struct Nocache *nc;
   BOOL found=FALSE;
   long len;
   ObtainSemaphore(&prefssema);
   for(nc=prefs.network.nocookie.first;nc->next;nc=nc->next)
   {  if(STRIEQUAL(nc->name,name))
      {  found=TRUE;
         break;
      }
   }
   if(!found)
   {  nc=Addnocookie(&prefs.network.nocookie,name);
      len=2*strlen(nc->name)+4;
      nc->pattern=ALLOCTYPE(UBYTE,len,0);
      if(nc->pattern)
      {  if(ParsePatternNoCase(nc->name,nc->pattern,len)<=0)
         {  FREE(nc->pattern);
            nc->pattern=NULL;
         }
      }
      Savenocookieprefs(&prefs.network,FALSE,NULL);
      Savenocookieprefs(&prefs.network,TRUE,NULL);
   }
   ReleaseSemaphore(&prefssema);
}

void Jsetupprefs(struct Jcontext *jc,struct Jobject *jnav)
{  struct Jvar *jv;
   ObtainSemaphore(&prefssema);
   if(!jmimetypes)
   {  jmimetypes=Newjobject(jc);
      jplugins=Newjobject(jc);
      Jmimetypes(jc);
      if(jmimetypes)
      {  if(jv=Jproperty(jc,jnav,"mimeTypes"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(jc,jv,jmimetypes);
         }
      }
      if(jplugins)
      {  if(jv=Jproperty(jc,jnav,"plugins"))
         {  Setjproperty(jv,JPROPHOOK_READONLY,NULL);
            Jasgobject(jc,jv,jplugins);
         }
      }
   }
   ReleaseSemaphore(&prefssema);
}

/*-----------------------------------------------------------------------*/

struct Namebreak
{  UBYTE buffer[128];
   UBYTE *list;
   UBYTE *current;
};


/* Copy next name to buffer. Return FALSE when done */
static BOOL Nextname(struct Namebreak *nb)
{  UBYTE *p,*q;
   if(!nb->current || !*nb->current) return FALSE;
   p=nb->current;
   while(isspace(*p)) p++;
   if(!*p) return FALSE;
   q=p;
   while(*q && *q!=',' && *q!=';') q++;
   nb->current=*q?(q+1):q;
   if(q-p>127) q=p+127;
   strncpy(nb->buffer,p,q-p);
   q=nb->buffer+(q-p)-1;
   while(q>=nb->buffer && isspace(*q)) q--;
   q[1]='\0';
   return TRUE;
}

/* Check if a font name is a generic font family */
static BOOL Isgenericfamily(UBYTE *name)
{  if(STRIEQUAL(name,"serif")) return TRUE;
   if(STRIEQUAL(name,"sans-serif")) return TRUE;
   if(STRIEQUAL(name,"monospace")) return TRUE;
   if(STRIEQUAL(name,"cursive")) return TRUE;
   if(STRIEQUAL(name,"fantasy")) return TRUE;
   return FALSE;
}

/* Try to open a font directly by name from the system.
 * Returns the opened font, or NULL if not found.
 * The actual font name used (with .font extension) is stored in usedname buffer.
 * Uses OpenDiskFont() from diskfont.library to open fonts from disk.
 */
static struct TextFont *Tryopenfontdirect(UBYTE *name,short fontsize,UBYTE *usedname,long usednamelen)
{  struct TextAttr ta={0};
   struct TextFont *font;
   UBYTE *p;
   /* Build font name with .font extension if not present */
   strncpy(usedname,name,usednamelen-1);
   usedname[usednamelen-1]='\0';
   p=usedname;
   while(*p) p++;
   if(p-usedname<usednamelen-5)
   {  if(p-usedname<5 || STRIEQUAL(p-5,".font"))
      {  /* Already has .font extension or too short */
      }
      else
      {  strcpy(p,".font");
      }
   }
   ta.ta_Name=usedname;
   ta.ta_YSize=fontsize;
   font=OpenDiskFont(&ta);
   return font;
}

struct Fontprefs *Matchfontprefs(struct Prefs *pr,UBYTE *face,short size,BOOL fixed)
{  struct Fontalias *fa;
   struct Namebreak nbf,nba;
   struct TextFont *directfont;
   UBYTE *genericfamily;
   UBYTE actualfontname[256];
   short fontsize;
   /* Get the actual font size from preferences for this HTML font size index (0-6).
    * This ensures consistent point size usage throughout the font selection process.
    * The size parameter corresponds to HTML font sizes 1-7 (mapped to indices 0-6).
    */
   fontsize=pr->browser.font[fixed?1:0][size].fontsize;
   /* First pass: try to open fonts directly by name from the system.
    * Use the default preference font size for consistency with the size index.
    */
   nbf.list=nbf.current=face;
   while(Nextname(&nbf))
   {  /* Skip generic families in direct opening pass */
      if(Isgenericfamily(nbf.buffer)) continue;
      /* Try to open the font directly using the consistent font size */
      directfont=Tryopenfontdirect(nbf.buffer,fontsize,actualfontname,sizeof(actualfontname));
      if(directfont)
      {  /* Check if font is appropriate (not proportional for fixed fonts) */
         if(!(fixed && (directfont->tf_Flags&FPF_PROPORTIONAL)))
         {  /* Font opened successfully and is appropriate */
            /* Add to alias list dynamically so it can be reused */
            fa=Addfontalias(&pr->browser.aliaslist,nbf.buffer);
            if(fa)
            {  if(!fa->fp[size].font)
               {  fa->fp[size].fontname=Dupstr(actualfontname,-1);
                  fa->fp[size].fontsize=fontsize;
                  fa->fp[size].font=directfont;
               }
               else
               {  /* Already in alias list, close the duplicate */
                  CloseFont(directfont);
               }
               return &fa->fp[size];
            }
            else
            {  /* Could not add to alias list, close font and continue */
               CloseFont(directfont);
            }
         }
         else
         {  /* Font is proportional but we need fixed-width */
            CloseFont(directfont);
         }
      }
   }
   /* Second pass: check alias list */
   nbf.list=nbf.current=face;
   while(Nextname(&nbf))
   {  for(fa=pr->browser.aliaslist.first;fa->next;fa=fa->next)
      {  nba.list=nba.current=fa->alias;
         while(Nextname(&nba))
         {  if(STRIEQUAL(nbf.buffer,nba.buffer))
            {  if(!fa->fp[size].font)
               {  Opennewfont(&fa->fp[size]);
               }
               if(fa->fp[size].font
               && !(fixed && (fa->fp[size].font->tf_Flags&FPF_PROPORTIONAL)))
               {  return &fa->fp[size];
               }
            }
         }
      }
   }
   /* Third pass: check for generic font families and map to defaults */
   genericfamily=NULL;
   nbf.list=nbf.current=face;
   while(Nextname(&nbf))
   {  if(Isgenericfamily(nbf.buffer))
      {  genericfamily=nbf.buffer;
         break;
      }
   }
   if(genericfamily)
   {  /* Map generic families to appropriate default fonts */
      if(STRIEQUAL(genericfamily,"serif"))
      {  /* Use default serif font (normal, not fixed) */
         return &pr->browser.font[0][size];
      }
      else if(STRIEQUAL(genericfamily,"sans-serif"))
      {  /* For sans-serif, try to find a sans-serif alias (e.g., "Arial,Helvetica,sans-serif") */
         /* Check alias list for sans-serif fonts */
         for(fa=pr->browser.aliaslist.first;fa->next;fa=fa->next)
         {  nba.list=nba.current=fa->alias;
            while(Nextname(&nba))
            {  if(STRIEQUAL(nba.buffer,"sans-serif") || STRIEQUAL(nba.buffer,"Arial") || STRIEQUAL(nba.buffer,"Helvetica"))
               {  if(!fa->fp[size].font)
                  {  Opennewfont(&fa->fp[size]);
                  }
                  if(fa->fp[size].font
                  && !(fixed && (fa->fp[size].font->tf_Flags&FPF_PROPORTIONAL)))
                  {  return &fa->fp[size];
                  }
               }
            }
         }
         /* If no alias found, try to open CGTriumvirate.font directly (default sans-serif font) */
         directfont=Tryopenfontdirect((UBYTE *)"CGTriumvirate",fontsize,actualfontname,sizeof(actualfontname));
         if(directfont)
         {  if(!(fixed && (directfont->tf_Flags&FPF_PROPORTIONAL)))
            {  fa=Addfontalias(&pr->browser.aliaslist,(UBYTE *)"CGTriumvirate");
               if(fa)
               {  if(!fa->fp[size].font)
                  {  fa->fp[size].fontname=Dupstr(actualfontname,-1);
                     fa->fp[size].fontsize=fontsize;
                     fa->fp[size].font=directfont;
                  }
                  else
                  {  CloseFont(directfont);
                  }
                  return &fa->fp[size];
               }
               else
               {  CloseFont(directfont);
               }
            }
            else
            {  CloseFont(directfont);
            }
         }
         /* Final fallback: use default font (may be serif, but better than nothing) */
         return &pr->browser.font[0][size];
      }
      else if(STRIEQUAL(genericfamily,"monospace"))
      {  /* Use default monospace font (fixed) */
         return &pr->browser.font[1][size];
      }
      else if(STRIEQUAL(genericfamily,"cursive"))
      {  /* Use default serif font for cursive */
         return &pr->browser.font[0][size];
      }
      else if(STRIEQUAL(genericfamily,"fantasy"))
      {  /* Use default sans-serif font for fantasy */
         return &pr->browser.font[0][size];
      }

   }
   /* Final fallback: use default preference font for type */
   return &pr->browser.font[fixed?1:0][size];
}

struct Fontprefs *Matchfont(UBYTE *face,short size,BOOL fixed)
{
   return Matchfontprefs(&prefs,face,size,fixed);
}

/* TT_OpenFont family names are concrete typeface names; literals like "serif" never match and
 * the next fallback in the list (e.g. DejaVu Sans from AWEB_UTF8_FONTFACE) wins. Reuse
 * Matchfontprefs() so generics resolve identically to the diskfont stack. */
void Expandgenericsinfontface(UBYTE *face,struct TextFont *font,UBYTE *out,ULONG outlen)
{
   struct Namebreak nbf;
   UBYTE *op;
   UBYTE *oend;
   UBYTE *p;
   BOOL first;
   BOOL fixedwidth;
   BOOL stripdotfont;
   short sizeidx;
   short i;
   struct Fontprefs *fp;
   struct Prefs *pr;
   ULONG bl;

   if(!out || outlen<4UL)
   {
      return;
   }
   out[0]='\0';
   if(!face || !face[0] || !font)
   {
      return;
   }
   pr=&prefs;
   fixedwidth=TRUE;
   if(font->tf_Flags & FPF_PROPORTIONAL)
   {
      fixedwidth=FALSE;
   }
   sizeidx=-1;
   for(i=0;i<NRFONTS;i++)
   {
      if(pr->browser.font[fixedwidth?1:0][i].fontsize==(short)font->tf_YSize)
      {
         sizeidx=i;
         break;
      }
   }
   if(sizeidx<0)
   {
      for(i=0;i<NRFONTS;i++)
      {
         if(pr->browser.font[fixedwidth?0:1][i].fontsize==(short)font->tf_YSize)
         {
            sizeidx=i;
            fixedwidth=fixedwidth?FALSE:TRUE;
            break;
         }
      }
   }
   if(sizeidx<0)
   {
      sizeidx=3;
      if(sizeidx>=NRFONTS)
      {
         sizeidx=NRFONTS-1;
      }
   }
   op=out;
   oend=out+outlen-1UL;
   first=TRUE;
   nbf.list=face;
   nbf.current=face;
   while(Nextname(&nbf))
   {
      stripdotfont=FALSE;
      if(!first)
      {
         if((ULONG)(oend-op)<3UL)
         {
            break;
         }
         *op++=',';
         *op++=' ';
         *op='\0';
      }
      first=FALSE;
      if(Isgenericfamily(nbf.buffer))
      {
         fp=Matchfontprefs(pr,nbf.buffer,sizeidx,fixedwidth);
         if(fp && fp->fontname && fp->fontname[0])
         {
            p=fp->fontname;
            stripdotfont=TRUE;
         }
         else
         {
            p=nbf.buffer;
         }
      }
      else
      {
         p=nbf.buffer;
      }
      bl=(ULONG)strlen((char *)p);
      if(stripdotfont && bl>5UL && STRIEQUAL(p+bl-5,".font"))
      {
         bl-=5UL;
      }
      if((ULONG)(oend-op)<bl)
      {
         bl=(ULONG)(oend-op);
      }
      if(bl==0UL)
      {
         break;
      }
      strncpy((char *)op,(char *)p,bl);
      op+=bl;
      *op='\0';
   }
}