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

/* jdebug.c - AWeb JS runtime debugger */

#include "awebjs.h"
#include "jprotos.h"
#include "keyfile.h"
#include <stdarg.h>

/* Missing Reaction defines - not available in proto headers */
#ifndef REACTION_Underscore
#define REACTION_Underscore 0x80000000
#endif
#include <intuition/intuition.h>
#include <dos/dos.h>
#include <libraries/asl.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <classes/window.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/label.h>
#include <proto/string.h>

#ifndef DEMOVERSION

static struct Screen *screen;
static void *winobj;
static struct Window *window;
static struct Gadget *linenrgad,*linegad,*exprgad,*resultgad;
static ULONG sigmask;

struct ClassLibrary *WindowBase,*LayoutBase,*ButtonBase,*LabelBase,*StringBase;

enum GADGET_IDS
{  GID_OVER=1,GID_INTO,GID_TEST,GID_RUN,GID_STOP,GID_EXPR,GID_DUMP,
};

static short left=0,top=0,width=0;

static void Debugdump(struct Jcontext *jc);
static UWORD dumpnr;

/*-----------------------------------------------------------------------*/

static void Setgadgetattrs(struct Gadget *gad,struct Window *win,struct Requester *req,...)
{  struct TagItem *tags=(struct TagItem *)((ULONG *)&req+1);
   if(SetGadgetAttrsA(gad,win,req,tags)) RefreshGList(gad,win,req,1);
}

static void Opendebug(struct Jcontext *jc)
{  if(!(screen=LockPubScreen(jc->screenname)))
   {  screen=LockPubScreen(NULL);
   }
   if(screen
   && (WindowBase=(struct ClassLibrary *)OpenLibrary("window.class",OSNEED(0,44)))
   && (LayoutBase=(struct ClassLibrary *)OpenLibrary("gadgets/layout.gadget",OSNEED(0,44)))
   && (ButtonBase=(struct ClassLibrary *)OpenLibrary("gadgets/button.gadget",OSNEED(0,44)))
   && (StringBase=(struct ClassLibrary *)OpenLibrary("gadgets/string.gadget",OSNEED(0,44)))
   && (LabelBase=(struct ClassLibrary *)OpenLibrary("images/label.image",OSNEED(0,44))))
   {  if(!width)
      {  width=screen->Width/2;
      }
      winobj=WindowObject,
         WA_Title,"AWeb JavaScript debugger",
         WA_DepthGadget,TRUE,
         WA_DragBar,TRUE,
         WA_SizeGadget,TRUE,
         WA_CloseGadget,TRUE,
         WA_Activate,Calleractive(),
         WA_AutoAdjust,TRUE,
         WA_SimpleRefresh,TRUE,
         WA_PubScreen,screen,
         WA_Left,left,
         WA_Top,top,
         WA_InnerWidth,width,
         WA_IDCMP,IDCMP_RAWKEY,
         WINDOW_LockHeight,TRUE,
         WINDOW_IDCMPHook,&idcmphook,
         WINDOW_IDCMPHookBits,IDCMP_CHANGEWINDOW,
         WINDOW_Layout,VLayoutObject,
            LAYOUT_SpaceOuter,TRUE,
            StartMember,HLayoutObject,
               LAYOUT_SpaceInner,FALSE,
               StartMember,linenrgad=ButtonObject,
                  GA_ReadOnly,TRUE,
                  GA_Text," ",
                  BUTTON_Justification,BCJ_RIGHT,
               EndMember,
               CHILD_WeightedWidth,10,
               StartMember,linegad=ButtonObject,
                  GA_ReadOnly,TRUE,
                  GA_Text," ",
                  BUTTON_Justification,BCJ_LEFT,
                  REACTION_Underscore,0,
               EndMember,
               CHILD_WeightedWidth,90,
            EndMember,
            MemberLabel("Line"),
            StartMember,HLayoutObject,
               StartMember,ButtonObject,
                  GA_ID,GID_OVER,
                  GA_RelVerify,TRUE,
                  GA_Text,"_Over",
               EndMember,
               StartMember,ButtonObject,
                  GA_ID,GID_INTO,
                  GA_RelVerify,TRUE,
                  GA_Text,"_Into",
               EndMember,
               StartMember,ButtonObject,
                  GA_ID,GID_TEST,
                  GA_RelVerify,TRUE,
                  GA_Text,"_Test",
               EndMember,
               StartMember,ButtonObject,
                  GA_ID,GID_DUMP,
                  GA_RelVerify,TRUE,
                  GA_Text,"_Dump",
               EndMember,
               StartMember,ButtonObject,
                  GA_ID,GID_RUN,
                  GA_RelVerify,TRUE,
                  GA_Text,"_Run",
               EndMember,
               StartMember,ButtonObject,
                  GA_ID,GID_STOP,
                  GA_RelVerify,TRUE,
                  GA_Text,"_Stop",
               EndMember,
            EndMember,
            StartMember,exprgad=StringObject,
               GA_ID,GID_EXPR,
               GA_RelVerify,TRUE,
               STRINGA_TextVal,"",
               STRINGA_MaxChars,127,
            EndMember,
            MemberLabel("_Expression"),
            StartMember,resultgad=ButtonObject,
               GA_ReadOnly,TRUE,
               GA_Text," ",
               BUTTON_Justification,BCJ_LEFT,
               REACTION_Underscore,0,
            EndMember,
            MemberLabel("Result"),
         End,
      EndWindow;
      if(winobj)
      {  if(window=RA_OpenWindow(winobj))
         {  GetAttr(WINDOW_SigMask,winobj,&sigmask);
         }
         else
         {  DisposeObject(winobj);
            winobj=NULL;
         }
      }
   }
}

static void Closedebug(void)
{  if(window)
   {  left=window->LeftEdge;
      top=window->TopEdge;
      width=window->Width-window->BorderLeft-window->BorderRight;
   }
   window=NULL;
   if(winobj) DisposeObject(winobj);winobj=NULL;
   if(LabelBase) CloseLibrary((struct Library *)LabelBase);LabelBase=NULL;
   if(StringBase) CloseLibrary((struct Library *)StringBase);StringBase=NULL;
   if(ButtonBase) CloseLibrary((struct Library *)ButtonBase);ButtonBase=NULL;
   if(LayoutBase) CloseLibrary((struct Library *)LayoutBase);LayoutBase=NULL;
   if(WindowBase) CloseLibrary((struct Library *)WindowBase);WindowBase=NULL;
   if(screen) UnlockPubScreen(NULL,screen);screen=NULL;
}

/* Execute expr and show result */
static void Testexpression(struct Jcontext *jc,UBYTE *expr)
{  struct Value val,eval;
   struct Variable *varref;
   UWORD flags,dflags;
   val.type=0;          /* Save current value */
   Asgvalue(&val,jc->val);
   varref=jc->varref;
   flags=jc->flags;
   dflags=jc->dflags;   /* Prevent debugging expr */
   jc->dflags&=~(DEBF_DEBUG|DEBF_DBREAK);
   Jeval(jc,expr);
   eval.type=0;
   Asgvalue(&eval,jc->val);
   Tostring(&eval,jc);
   Setgadgetattrs(resultgad,window,NULL,GA_Text,eval.value.svalue,TAG_END);
   Clearvalue(&eval);
   Asgvalue(jc->val,&val);
   jc->varref=varref;
   jc->flags=flags;
   jc->dflags=dflags;
   Clearvalue(&val);
}

#endif /* !DEMOVERSION */

/*-----------------------------------------------------------------------*/

void Startdebugger(struct Jcontext *jc)
{  
#ifndef DEMOVERSION
   if(!winobj)
   {  Opendebug(jc);
      if(winobj) jc->dflags|=DEBF_DOPEN;
   }
   jc->dflags|=DEBF_DEBUG|DEBF_DBREAK;
#endif
}

void Stopdebugger(struct Jcontext *jc)
{
#ifndef DEMOVERSION
   if(winobj && (jc->dflags&DEBF_DOPEN))
   {  Closedebug();
      jc->dflags&=~(DEBF_DEBUG|DEBF_DBREAK|DEBF_DOPEN);
   }
#endif
}

void Setdebugger(struct Jcontext *jc,struct Element *elt)
{
#ifndef DEMOVERSION
   struct Jbuffer *jb=NULL;
   ULONG result;
   UBYTE *expr,*p;
   UBYTE buf[16];
   BOOL go=FALSE;
   if(window && elt)
   {  sprintf(buf,"%d",elt->linenr);
      Setgadgetattrs(linenrgad,window,NULL,GA_Text,buf,TAG_END);
      if(jb=Jdecompile(jc,elt))
      {  for(p=jb->buffer;*p;p++)
         {  if(*p=='\n') *p=' ';
         }
         Setgadgetattrs(linegad,window,NULL,GA_Text,jb->buffer,TAG_END);
      }
      /* First clear event queue, then wait for new events. */
      while((result=RA_HandleInput(winobj,NULL))!=WMHI_LASTMSG);
      ActivateWindow(window);
      while(!go)
      {  Wait(sigmask);
         while((result=RA_HandleInput(winobj,NULL))!=WMHI_LASTMSG)
         {  switch(result&WMHI_CLASSMASK)
            {  case WMHI_CLOSEWINDOW:
                  jc->dflags&=~(DEBF_DEBUG|DEBF_DBREAK);
                  jc->flags|=EXF_STOP;
                  go=TRUE;
                  break;
               case WMHI_GADGETUP:
                  switch(result&WMHI_GADGETMASK)
                  {  case GID_OVER:
                        jc->dflags&=~DEBF_DBREAK;
                        go=TRUE;
                        break;
                     case GID_INTO:
                        go=TRUE;
                        break;
                     case GID_TEST:
                        if(jb)
                        {  expr=jb->buffer;
                           Testexpression(jc,expr);
                        }
                        break;
                     case GID_RUN:
                        jc->dflags&=~(DEBF_DEBUG|DEBF_DBREAK);
                        go=TRUE;
                        break;
                     case GID_STOP:
                        jc->dflags&=~(DEBF_DEBUG|DEBF_DBREAK);
                        jc->flags|=EXF_STOP;
                        go=TRUE;
                        break;
                     case GID_EXPR:
                        expr=NULL;
                        GetAttr(STRINGA_TextVal,exprgad,(ULONG *)&expr);
                        if(expr)
                        {  Testexpression(jc,expr);
                        }
                        break;
                     case GID_DUMP:
                        Debugdump(jc);
                        break;
                  }
                  break;
               case WMHI_RAWKEY:
                  switch(result&WMHI_GADGETMASK)
                  {  case 0x43:  /* num enter */
                     case 0x44:  /* enter */
                        jc->dflags&=~DEBF_DBREAK;
                        go=TRUE;
                        break;
                     case 0x45:  /* esc */
                        jc->dflags&=~(DEBF_DEBUG|DEBF_DBREAK);
                        jc->flags|=EXF_STOP;
                        go=TRUE;
                        break;
                  }
                  break;
            }
         }
      }
      if(!(jc->dflags&DEBF_DEBUG))
      {  Closedebug();
      }
   }
   if(jb) Freejbuffer(jb);
#endif
}

/*-----------------------------------------------------------------------*/

#ifndef DEMOVERSION

static void Debugdumpnr(struct Jobject *jo)
{  struct Variable *v;
   if(!jo->dumpnr)
   {  for(v=jo->properties.first;v->next;v=v->next)
      {  if(!(v->flags&VARF_HIDDEN))
         {  jo->dumpnr=++dumpnr;
            break;
         }
      }
   }
}

static void Debugdumpvar(long fh,struct Jcontext *jc,struct Variable *v)
{  struct Value val={0};
   struct Jobject *ref;
   UBYTE buf[16];
   UBYTE *value,*valindent;
   UBYTE vtype;
   if(v->val.type==VTP_OBJECT)
   {  ref=v->val.value.obj.ovalue;
      if(ref)
      {  Debugdumpnr(ref);
         if(ref->dumpnr)
         {  sprintf(buf,"(see #%d) ",ref->dumpnr);
         }
         else
         {  *buf='\0';
         }
         jc->flags&=~EXF_STOP;
         if(Callproperty(jc,ref,"toString") && jc->val->type==VTP_STRING)
         {  value=jc->val->value.svalue;
            valindent="\n                    ";
         }
         else
         {  value="";
            valindent="";
         }
         if(ref->constructor && ref->constructor->function
         && ref->constructor->function->name)
         {  FPrintf(fh,"    %-12s[O]=%sobject '%s'%s%s\n",
               v->name,buf,ref->constructor->function->name,valindent,value);
         }
         else if(ref->function && ref->function->name)
         {  if(ref->dumpnr)
            {  FPrintf(fh,"    %-12s[O]=%sfunction '%s'%s%s\n",
                  v->name,buf,ref->function->name,valindent,value);
            }
         }
         else
         {  FPrintf(fh,"    %-12s[O]=%sobject%s%s\n",v->name,buf,valindent,value);
         }
      }
      else
      {  FPrintf(fh,"    %-12s[O]=null\n",v->name);
      }
   }
   else
   {  if(!Callvhook(v,jc,VHC_GET,&val))
      {  Asgvalue(&val,&v->val);
      }
      vtype="?NBSO"[val.type];
      Tostring(&val,jc);
      FPrintf(fh,"    %-12s[%lc]=%s\n",v->name,vtype,val.value.svalue);
   }
   Clearvalue(&val);
}

/* Variable name sort compare. Numerics are sorted in numeric order, alphanumeric
 * case-insensitive after numeric. */
static int Sortvarcmp(struct Variable **v1,struct Variable **v2)
{  UBYTE *name1=(*v1)->name;
   UBYTE *name2=(*v2)->name;
   UBYTE *p;
   BOOL num=TRUE;
   for(p=name1;num && *p;p++)
   {  num=isdigit(*p);
   }
   for(p=name2;num && *p;p++)
   {  num=isdigit(*p);
   }
   if(num)
   {  return atoi(name1)-atoi(name2);
   }
   else
   {  return stricmp(name1,name2);
   }
}

static void Debugdumpvarlist(long fh,struct Jcontext *jc,LIST(Variable) *list)
{  struct Variable *v;
   struct Variable **vars;
   long nvars,i;
   nvars=0;
   for(v=list->first;v->next;v=v->next)
   {  if(v->name && !(v->flags&VARF_HIDDEN))
      {  nvars++;
      }
   }
   if(nvars)
   {  if(vars=ALLOCTYPE(struct Variable *,nvars,0,jc->pool))
      {  i=0;
         for(v=list->first;v->next;v=v->next)
         {  if(v->name && !(v->flags&VARF_HIDDEN))
            {  vars[i++]=v;
            }
         }
         qsort(vars,nvars,sizeof(struct Variable *),Sortvarcmp);
         for(i=0;i<nvars;i++)
         {  Debugdumpvar(fh,jc,vars[i]);
         }
         FREE(vars);
      }
   }
}

static void Debugdumpobject(long fh,struct Jcontext *jc,struct Jobject *jo)
{  FPrintf(fh,"\n%3ld -- ",(long)jo->dumpnr);
   if(jo->constructor && jo->constructor->function && jo->constructor->function->name)
   {  FPrintf(fh,"Object '%s'\n",jo->constructor->function->name);
   }
   else if(jo->function && jo->function->name)
   {  FPrintf(fh,"Function '%s'\n",jo->function->name);
   }
   else
   {  FPrintf(fh,"Object\n");
   }
   Debugdumpvarlist(fh,jc,&jo->properties);
}

static void Debugdump(struct Jcontext *jc)
{  long fh;
   struct Value val;
   struct Variable *varref;
   UWORD flags,dflags;
   struct Jobject *jo;
   struct Function *f;
   UWORD d;
   UBYTE *sep="---------------------------------------------------";
   struct FileRequester *fr;
   static UBYTE filename[256]="T:JSDump";
   BOOL ok=FALSE;
   if(fr=AllocAslRequestTags(ASL_FileRequest,
      ASLFR_Screen,screen,
      ASLFR_TitleText,"Select dump filename",
      ASLFR_InitialFile,filename,
      ASLFR_PositiveText,"Dump",
      ASLFR_RejectIcons,TRUE,
      ASLFR_DoSaveMode,TRUE,
      TAG_END))
   {  if(AslRequest(fr,NULL))
      {  strncpy(filename,fr->fr_Drawer,255);
         filename[255]='\0';
         if(AddPart(filename,fr->fr_File,255))
         {  ok=TRUE;
         }
      }
      FreeAslRequest(fr);
   }
   if(!ok) return;
    
   val.type=0;          /* Save current value */
   Asgvalue(&val,jc->val);
   varref=jc->varref;
   flags=jc->flags;
   dflags=jc->dflags;   /* Prevent debugging expr */
   jc->dflags&=~(DEBF_DEBUG|DEBF_DBREAK);
   jc->flags&=~EXF_ERRORS;
   if(fh=Open(filename,MODE_NEWFILE))
   {  for(jo=jc->objects.first;jo->next;jo=jo->next)
      {  jo->dumpnr=0;
      }
      dumpnr=0;
      FPrintf(fh,"%s\nFunction call stack\n%s\n",sep,sep);
      for(f=jc->functions.first;f->next;f=f->next)
      {  if(f->next->next)
         {  FPrintf(fh,"\nIn function");
            if(f->def && f->def->function && f->def->function->name)
            {  FPrintf(fh," '%s'\n",f->def->function->name);
            }
            else
            {  FPrintf(fh,"\n");
            }
            Debugdumpvarlist(fh,jc,&f->local);
         }
         else
         {  FPrintf(fh,"\nIn program\n");
         }
         jo=f->fscope;
         if(jo)
         {  if(!jo->dumpnr) jo->dumpnr=++dumpnr;
            FPrintf(fh,"    + global variables (see #%ld)\n",(long)jo->dumpnr);
         }
      }
      FPrintf(fh,"\n%s\nReferenced objects\n%s\n",sep,sep);
      for(d=1;d<=dumpnr;d++)
      {  for(jo=jc->objects.first;jo->next;jo=jo->next)
         {  if(jo->dumpnr==d)
            {  Debugdumpobject(fh,jc,jo);
            }
         }
      }
      Close(fh);
   }
   else
   {  UBYTE *fn=filename;
      va_list args;
      va_start(args,fn);
      Errorrequester(jc,-1,NULL,0,"Can't open file %s",args);
      va_end(args);
   }
   Asgvalue(jc->val,&val);
   jc->varref=varref;
   jc->flags=flags;
   jc->dflags=dflags;
   Clearvalue(&val);
}

#endif
