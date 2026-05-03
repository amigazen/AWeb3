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

/* editor.c - AWeb synchroneous external editor object */

#include "aweb.h"
#include "editor.h"
#include "application.h"
#include "file.h"
#include "task.h"
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/intuition.h>
#include <proto/utility.h>

/*------------------------------------------------------------------------*/

#define MAXSTRLEN 255

struct Editor
{  struct Aobject object;
   void *task;
   void *target;
   struct TagItem *maplist;
   void *file;
   UBYTE *filename;                 /* Same pointer as file name from editor->file */
   UBYTE *screenname;
   ULONG stamp;
   USHORT flags;
   struct NotifyRequest nfreq;
};

#define EDTF_DISPOSED         0x0001   /* Object is disposed */
#define EDTF_LIVEFILE         0x0002   /* Editing live file */
#define EDTF_DYNAMICNAME      0x0004   /* Filename is dynamic */

#define AOEDT_Close     (AOEDT_Dummy+101)
   /* (BOOL) Editor closed */

static LIST(Editor) editors;

static struct MsgPort *notifyport;

/*------------------------------------------------------------------------*/

static void Editortask(struct Editor *ed)
{  UBYTE *params[2];  /* filename, screenname */
   UBYTE *cmd;
   params[0]=Fullname(ed->filename);
   params[1]=ed->screenname;
   ObtainSemaphore(&prefssema);
   if(cmd=ALLOCTYPE(UBYTE,
      strlen(prefs.program.editcmd)+Pformatlength(prefs.program.editargs,"fn",params)+32,0))
   {  sprintf(cmd,"\"%s\" ",prefs.program.editcmd);
      Pformat(cmd+strlen(cmd),prefs.program.editargs,"fn",params,TRUE);
      /* Can't add a delete because file will be deleted before DOS notification
       * is processed. */
   }
   ReleaseSemaphore(&prefssema);
   if(cmd)
   {  SystemTags(cmd,TAG_END);
      FREE(cmd);
   }
   if(params[0]) FREE(params[0]);
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOEDT_Close,TRUE,
      TAG_END);
}

/*------------------------------------------------------------------------*/

/* Process file notifications. */
static void Processnotify(void)
{  struct NotifyMessage *msg;
   struct Editor *ed;
   long length,fh;
   UBYTE *data;
   long lock;
   struct FileInfoBlock *fib;
   ULONG date=0;
   while(msg=(struct NotifyMessage *)GetMsg(notifyport))
   {  ed=(struct Editor *)msg->nm_NReq->nr_UserData;
      if(ed->target)
      {  length=0;
         if(lock=Lock(ed->filename,SHARED_LOCK))
         {  if(fib=AllocDosObjectTags(DOS_FIB,TAG_END))
            {  if(Examine(lock,fib))
               {  length=fib->fib_Size;
                  date=fib->fib_Date.ds_Days*86400 +
                       fib->fib_Date.ds_Minute*60 +
                       fib->fib_Date.ds_Tick/TICKS_PER_SECOND;
               }
               FreeDosObject(DOS_FIB,fib);
            }
            UnLock(lock);
         }
         if(data=ALLOCTYPE(UBYTE,length+1,0))
         {  if(fh=Open(ed->filename,MODE_OLDFILE))
            {  Read(fh,data,length);
               Close(fh);
            }
            Aupdateattrs(ed->target,ed->maplist,
               AOBJ_Target,ed,
               AOEDT_Data,data,
               AOEDT_Datalength,length,
               (ed->flags&EDTF_LIVEFILE)?AOEDT_Filedate:TAG_IGNORE,date,
               TAG_END);
            FREE(data);
         }
      }
      ReplyMsg(msg);
   }
}

static void Disposeeditorreal(struct Editor *ed)
{  ed->flags|=EDTF_DISPOSED;
   ed->target=NULL;
   if(ed->nfreq.nr_Name) EndNotify(&ed->nfreq);
   Processnotify();
   if(ed->task)
   {  Adisposeobject(ed->task);
   }
   if(ed->file) Adisposeobject(ed->file);
   if(ed->filename && (ed->flags&EDTF_DYNAMICNAME)) FREE(ed->filename);
   REMOVE(ed);
   Amethodas(AOTP_OBJECT,ed,AOM_DISPOSE);
}

/*------------------------------------------------------------------------*/

static long Updateeditor(struct Editor *ed,struct Amset *ams)
{  BOOL closed=FALSE;
   struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOEDT_Close:
            if(tag->ti_Data) closed=TRUE;
            break;
      }
   }
   if(closed)
   {  Adisposeobject(ed->task);
      ed->task=NULL;
      if(ed->flags&EDTF_DISPOSED) Disposeeditorreal(ed);
   }
   return 0;
}

static long Seteditor(struct Editor *ed,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *data=NULL;
   long length=0;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOEDT_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOEDT_Datalength:
            length=tag->ti_Data;
            if(!ed->file)
            {  ed->file=Anewobject(AOTP_FILE,TAG_END);
            }
            if(data && ed->file)
            {  Asetattrs(ed->file,
                  AOFIL_Data,data,
                  AOFIL_Datalength,length,
                  TAG_END);
            }
            data=NULL;
            break;
         case AOEDT_Filename:
            if(ed->filename && (ed->flags&EDTF_DYNAMICNAME)) FREE(ed->filename);
            ed->filename=Dupstr((UBYTE *)tag->ti_Data,-1);
            if(ed->filename) ed->flags|=EDTF_LIVEFILE|EDTF_DYNAMICNAME;
            break;
         case AOBJ_Target:
            ed->target=(void *)tag->ti_Data;
            break;
         case AOBJ_Map:
            ed->maplist=(struct TagItem *)tag->ti_Data;
            break;
      }
   }
   return 0;
}

static struct Editor *Neweditor(struct Amset *ams)
{  struct Editor *ed;
   if(ed=Allocobject(AOTP_EDITOR,sizeof(struct Editor),ams))
   {  ADDTAIL(&editors,ed);
      Seteditor(ed,ams);
      if(ed->file)
      {  Asetattrs(ed->file,AOFIL_Eof,TRUE,TAG_END);
      }
      if(!ed->filename)
      {  ed->filename=(UBYTE *)Agetattr(ed->file,AOFIL_Name);
      }
      ed->nfreq.nr_Name=ed->filename;
      ed->nfreq.nr_Port=notifyport;
      ed->nfreq.nr_Flags=NRF_SEND_MESSAGE;
      ed->nfreq.nr_UserData=(ULONG)ed;
      StartNotify(&ed->nfreq);
      ed->screenname=(UBYTE *)Agetattr(Aweb(),AOAPP_Screenname);
      if(ed->filename
      && (ed->task=Anewobject(AOTP_TASK,
         AOTSK_Entry,Editortask,
         AOTSK_Name,"AWeb editor",
         AOTSK_Userdata,ed,
         AOBJ_Target,ed,
         TAG_END)))
      {  Asetattrs(ed->task,AOTSK_Start,TRUE,TAG_END);
      }
      else
      {  Disposeeditorreal(ed);
         ed=NULL;
      }
   }
   return ed;
}

static void Disposeeditor(struct Editor *ed)
{  if(ed->task)
   {  ed->flags|=EDTF_DISPOSED;
      ed->target=NULL;
      Asetattrs(ed->task,
         AOTSK_Stop,TRUE,
         AOTSK_Async,TRUE,
         TAG_END);
   }
   else
   {  Disposeeditorreal(ed);
   }
}

static void Deinstalleditor(void)
{  struct Editor *ed;
   while(ed=REMHEAD(&editors)) Disposeeditorreal(ed);
   if(notifyport)
   {  Setprocessfun(notifyport->mp_SigBit,NULL);
      DeleteMsgPort(notifyport);
   }
}

static long Dispatch(struct Editor *ed,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Neweditor((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Seteditor(ed,(struct Amset *)amsg);
         break;
      case AOM_UPDATE:
         result=Updateeditor(ed,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposeeditor(ed);
         break;
      case AOM_DEINSTALL:
         Deinstalleditor();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installeditor(void)
{  NEWLIST(&editors);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_EDITOR,Dispatch)) return FALSE;
   if(!(notifyport=CreateMsgPort())) return FALSE;
   Setprocessfun(notifyport->mp_SigBit,Processnotify);
   return TRUE;
}
