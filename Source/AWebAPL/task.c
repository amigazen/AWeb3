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

/* task.c - AWeb subtask object */

#include "aweb.h"
#include "task.h"
#include "window.h"
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

/*------------------------------------------------------------------------*/

struct Atask
{  struct Aobject object;
   Subtaskfunction *entry;
   void *userdata;
   long stacksize;
   UBYTE *name;
   USHORT state;
   USHORT flags;
   void *target;
   struct TagItem *maplist;
   struct Process *proc;
   struct MsgPort *subport;
   struct MsgPort *updport;   /* Replyport for update messages */
   struct Ataskmsg *startup;  /* Startup msg, replied just before (entry) is called. */
   struct SignalSemaphore sema;
   struct SignalSemaphore runsema;  /* Occupied while subtask is running */
   void *windowptr;
   void *taskuserdata;
};

#define TSKS_NEW        1     /* New task */
#define TSKS_RUN        2     /* Task is running */
#define TSKS_SUSPEND    3     /* Task is suspended */
#define TSKS_DEAD       4     /* Task is dead. */

#define TSKF_DISPOSING     0x0001   /* Already disposing */
#define TSKF_PROCESSING    0x0002   /* Processing message, can't dispose */
#define TSKF_MUSTDISPOSE   0x0004   /* Must dispose when processing returns */
#define TSKF_STARTED       0x0008   /* Task did start */

/* Private extension of the Taskmsg structure */
struct Ataskmsg
{  struct Message execmsg;
   struct Amessage *amsg;     /* If TSMF_UPDATEATTRS is set, it's a taglist, not a Amessage */
   long result;
   struct Atask *task;           
   Repliedfunction *replied;
   USHORT flags;
};

#define TSMF_ASYNC         0x0001   /* Async message */
#define TSMF_VANILLASET    0x0002   /* A vanilla Asetattrsasync() message. Clean up after reply */
#define TSMF_STOPMSG       0x0004   /* Subtask created this AOTSK_Stop,TRUE message */
#define TSMF_UPDATE        0x0008   /* Update message from subtask to main. */
#define TSMF_UPDATEATTRS   0x0010   /* Message is ready to use. */
#define TSMF_SUSPEND       0x0020   /* Suspend subtask */
#define TSMF_RELEASE       0x0040   /* Release subtask */

static LIST(Atask) tasks;
static struct Process *mainproc;    /* AWebs main process */

/* startsema is used to protect the subtask from inspecting its tc_UserData
 * field before the main task has set it up.
 * portsema is used to protect the main task reply port because the main
 * task could do some manual traversing and removing. */
static struct SignalSemaphore startsema,portsema;

static struct MsgPort *taskport; /* Message reply port */
static struct MsgPort *semaport; /* Procure() port for subtask's runsema */

static void Dodisposetask(struct Atask *task);

/*------------------------------------------------------------------------*/

/* The subtask wrapper */
static void __saveds Subtask(void)
{  struct Process *process;
   struct Atask *task;
   struct MsgPort *port;
   struct Ataskmsg *msg;
   ObtainSemaphore(&startsema);
   process=(struct Process *)FindTask(NULL);
   task=process->tc_UserData;
   ObtainSemaphore(&task->runsema);
   ReleaseSemaphore(&startsema);
   /* Ensure a newly spawned subtask does not inherit a stale Ctrl-C break bit.
    * Stopping old fetches uses SIGBREAKF_CTRL_C to interrupt blocking I/O; if that
    * bit leaks into new subtasks, new user-initiated loads can be aborted too. */
   SetSignal(0, SIGBREAKF_CTRL_C);
   process->pr_WindowPtr=task->windowptr;
   if(task->subport=CreateMsgPort())
   {  task->updport=CreateMsgPort();
      task->state=TSKS_RUN;
      ReplyMsg(task->startup);
      task->entry(task->userdata);
      ObtainSemaphore(&task->sema);
      while(msg=(struct Ataskmsg *)GetMsg(task->subport))
      {  ObtainSemaphore(&portsema);
         ReplyMsg(msg);
         ReleaseSemaphore(&portsema);
      }
      port=task->subport;
      task->subport=NULL;
      task->state=TSKS_DEAD;
      ReleaseSemaphore(&task->sema);
      DeleteMsgPort(port);
//KPrintF("%08lx - Subtask ends\n",task);
   }
   else
   {  task->state=TSKS_DEAD;
      ReplyMsg(task->startup);
   }
   ReleaseSemaphore(&task->runsema);
}

ULONG Waittask(ULONG signals)
{  ULONG got=0;
   struct Atask *task=FindTask(NULL)->tc_UserData;
#ifdef DEVELOPER
   struct Task *thistask;
#endif
   if(!task->subport->lh_Head->ln_Succ)
   {  
//KPrintF("%08lx - Wait for %08lx\n",task,signals);
      got=Wait(signals|(1<<task->subport->mp_SigBit)|SIGBREAKF_CTRL_C);
//KPrintF("%08lx - Wait for %08lx got %08lx\n",task,signals,got);
      if(got&SIGBREAKF_CTRL_C)
      {  /* Reset Ctrl-C flag after Wait() has cleared it... */
#ifdef DEVELOPER
         thistask = FindTask(NULL);
         Printf("DEBUG: Waittask: SIGBREAKF_CTRL_C observed, reasserting (task=%08lx got=%08lx)\n",
                (ULONG)thistask, got);
#endif
         SetSignal(SIGBREAKF_CTRL_C,SIGBREAKF_CTRL_C);
      }
      got&=signals;
   }
   return got;
}

static struct Ataskmsg *Stopmsg(struct Atask *task)
{  struct Ataskmsg *atm;
   struct Amset *ams;
   if(atm=ALLOCSTRUCT(Ataskmsg,1,MEMF_CLEAR))
   {  if(ams=ALLOCSTRUCT(Amset,1,MEMF_CLEAR))
      {  if(ams->tags=AllocateTagItems(2))
         {  ams->method=AOM_SET;
            ams->tags[0].ti_Tag=AOTSK_Stop;
            ams->tags[0].ti_Data=TRUE;
            ams->tags[1].ti_Tag=TAG_END;
            atm->task=task;
            atm->amsg=ams;
            atm->flags=TSMF_STOPMSG;
            return atm;
         }
         FREE(ams);
      }
      FREE(atm);
   }
   return NULL;
}

struct Taskmsg *Gettaskmsg(void)
{  struct Ataskmsg *atm;
   struct Atask *task=FindTask(NULL)->tc_UserData;
   BOOL suspend=FALSE;
   ULONG mask=SetSignal(0,0);
#ifdef DEVELOPER
   struct Task *thistask;
#endif
   for(;;)
   {  if(mask&SIGBREAKF_CTRL_C)
      {  SetSignal(SIGBREAKF_CTRL_C,SIGBREAKF_CTRL_C);
#ifdef DEVELOPER
         thistask = FindTask(NULL);
         Printf("DEBUG: Gettaskmsg: SIGBREAKF_CTRL_C set, returning stopmsg (task=%08lx mask=%08lx)\n",
                (ULONG)thistask, mask);
#endif
         return Stopmsg(task);
      }
      ObtainSemaphore(&task->sema);
      for(atm=(struct Ataskmsg *)task->subport->lh_Head;atm->ln_Succ;
         atm=(struct Ataskmsg *)atm->ln_Succ)
      {  if(atm->flags&(TSMF_SUSPEND|TSMF_RELEASE))
         {  REMOVE(atm);
            if(atm->flags&TSMF_SUSPEND)
            {  suspend=TRUE;
               task->state=TSKS_SUSPEND;
            }
            else
            {  suspend=FALSE;
               task->state=TSKS_RUN;
            }
            break;
         }
         if(suspend && !(atm->flags&TSMF_ASYNC))
         {  REMOVE(atm);
            break;
         }
      }
      ReleaseSemaphore(&task->sema);
      if(atm->ln_Succ)
      {  ObtainSemaphore(&portsema);
         ReplyMsg(atm);
         ReleaseSemaphore(&portsema);
      }
      else if(suspend)
      {  mask=Wait((1<<task->subport->mp_SigBit)|SIGBREAKF_CTRL_C);
      }
      else
      {  break;
      }
   }
   return (struct Ataskmsg *)GetMsg(task->subport);
}

BOOL Obtaintasksemaphore(struct SignalSemaphore *sema)
{  struct SemaphoreMessage smsg={0},*msg;
   ULONG mask=SetSignal(0,0);
   struct MsgPort *port=CreateMsgPort();
   if(port)
   {  smsg.mn_ReplyPort=port;
//KPrintF("%08lx - Obtaintasksemaphore %08lx\n",FindTask(NULL)->tc_UserData,sema);
      Procure(sema,&smsg);
      for(;;)
      {  /* If break signal received, cancel the semaphore request. The message
          * will be replied with a NULL ssm_Semaphore field.
          * Reset the break signal if it was cleared by Wait(). */
         if(mask&SIGBREAKF_CTRL_C)
         {  Vacate(sema,&smsg);
//KPrintF("%08lx - Obtaintasksemaphore %08lx got Ctrl-C\n",FindTask(NULL)->tc_UserData,sema);
            SetSignal(SIGBREAKF_CTRL_C,SIGBREAKF_CTRL_C);
         }
         /* See if our message has replied yet. If so, break the wait loop. */
         while((msg=(struct SemaphoreMessage *)GetMsg(port)) && msg!=&smsg);
         if(msg) break;
         mask=Wait((1<<port->mp_SigBit)|SIGBREAKF_CTRL_C);
      }
   }
   if(port) DeleteMsgPort(port);
//KPrintF("%08lx - Obtaintasksemaphore %08lx continue\n",FindTask(NULL)->tc_UserData,sema);
   return BOOLVAL(smsg.ssm_Semaphore);
}

void Replytaskmsg(struct Taskmsg *tsm)
{  struct Ataskmsg *atm=(struct Ataskmsg *)tsm;
   if(atm->flags&TSMF_STOPMSG)
   {  FreeTagItems(((struct Amset *)atm->amsg)->tags);
      FREE(atm->amsg);
      FREE(atm);
   }
   else
   {  ObtainSemaphore(&portsema);
      ReplyMsg(atm);
      ReleaseSemaphore(&portsema);
   }
}

BOOL Checktaskbreak(void)
{  ULONG mask;
#ifdef DEVELOPER
   struct Task *thistask;
#endif
   mask = SetSignal(0,0);
   if(mask & SIGBREAKF_CTRL_C)
   {
#ifdef DEVELOPER
      thistask = FindTask(NULL);
      Printf("DEBUG: Checktaskbreak: SIGBREAKF_CTRL_C set (task=%08lx mask=%08lx)\n",
             (ULONG)thistask, mask);
#endif
      return TRUE;
   }
   return FALSE;
}

long Updatetask(struct Amessage *amsg)
{  struct Ataskmsg *atm;
   struct Atask *task=FindTask(NULL)->tc_UserData;
   long result=0;
   if(task->updport)
   {  if(atm=ALLOCSTRUCT(Ataskmsg,1,MEMF_PUBLIC|MEMF_CLEAR))
      {  atm->mn_ReplyPort=task->updport;
         atm->amsg=amsg;
         atm->task=task;
         atm->flags=TSMF_UPDATE;
         ObtainSemaphore(&portsema);
//KPrintF("%08lx - Updatetask msg=%08lx\n",task,atm);
         PutMsg(taskport,atm);
         ReleaseSemaphore(&portsema);
         WaitPort(task->updport);
//KPrintF("%08lx - Continues  msg=%08lx\n",task,atm);
         atm=(struct Ataskmsg *)GetMsg(task->updport);
         result=atm->result;
         FREE(atm);
      }
   }
   return result;
}

long UpdatetaskattrsA(struct TagItem *tags)
{  struct Ataskmsg *atm;
   struct Atask *task=FindTask(NULL)->tc_UserData;
   long result=0;
   BOOL async;
   if(task->updport)
   {  if(atm=ALLOCSTRUCT(Ataskmsg,1,MEMF_PUBLIC|MEMF_CLEAR))
      {  async=GetTagData(AOTSK_Async,FALSE,tags);
         atm->mn_ReplyPort=task->updport;
         if(async)
         {  atm->amsg=(struct Amessage *)CloneTagItems(tags);
         }
         else
         {  atm->amsg=(struct Amessage *)tags;
         }
         atm->task=task;
         atm->flags=TSMF_UPDATE|TSMF_UPDATEATTRS;
         if(async) atm->flags|=TSMF_ASYNC;
//KPrintF("%08lx - Updatetaskattrs msg=%08lx async=%d\n",task,atm,async);
         ObtainSemaphore(&portsema);
         PutMsg(taskport,atm);
         ReleaseSemaphore(&portsema);
         if(!async)
         {  WaitPort(task->updport);
//KPrintF("%08lx - Continues  msg=%08lx\n",task,atm);
            atm=(struct Ataskmsg *)GetMsg(task->updport);
            result=atm->result;
            FREE(atm);
         }
      }
   }
   return result;
}

long Updatetaskattrs(ULONG tag,...)
{  return UpdatetaskattrsA((struct TagItem *)&tag);
}

void Settaskuserdata(void *data)
{  struct Atask *task=FindTask(NULL)->tc_UserData;
   if(task) task->taskuserdata=data;
}

void *Gettaskuserdata(void)
{  struct Atask *task=FindTask(NULL)->tc_UserData;
   return task?task->taskuserdata:NULL;
}

/*------------------------------------------------------------------------*/

/* Process a replied or update message */
static void Processreply(struct Ataskmsg *atm)
{  struct Atask *task;
   if(atm)
   {  task=atm->task;
//KPrintF("%08lx * Processreply msg=%08lx\n",task,atm);
      if(atm->flags&TSMF_UPDATE)
      {  if(task->target)
         {  task->flags|=TSKF_PROCESSING;
            if(atm->flags&TSMF_UPDATEATTRS)
            {  atm->result=Aupdateattrs(task->target,task->maplist,
                  AOBJ_Target,task,
                  TAG_MORE,atm->amsg);
            }
            else
            {  atm->result=Aupdateattrs(task->target,task->maplist,
                  AOBJ_Target,task,
                  AOTSK_Message,atm->amsg,
                  TAG_END);
            }
            task->flags&=~TSKF_PROCESSING;
         }
         if(atm->flags&TSMF_ASYNC)
         {  FreeTagItems((struct TagItem *)atm->amsg);
            FREE(atm);
         }
         else
         {  
//KPrintF("%08lx * Replied      msg=%08lx\n",task,atm);
            ReplyMsg(atm);
         }
      }
      else
      {  if(atm->replied)
         {  atm->replied(task,atm);
         }
         if(atm->flags&TSMF_VANILLASET)
         {  FreeTagItems(((struct Amset *)atm->amsg)->tags);
            FREE(atm->amsg);
         }
         FREE(atm);
      }
      if(task->flags&TSKF_MUSTDISPOSE)
      {  Dodisposetask(task);
      }
   }
}

/* Process all queued messages for this task */
static void Processtaskreply(struct Atask *task)
{  struct Ataskmsg *msg;
   BOOL process;
   do
   {  ObtainSemaphore(&portsema);
      process=FALSE;
      for(msg=(struct Ataskmsg *)taskport->lh_Head;msg->ln_Succ;
         msg=(struct Ataskmsg *)msg->ln_Succ)
      {  if(msg->task==task)
         {  REMOVE(msg);
            process=TRUE;
            break;
         }
      }
      ReleaseSemaphore(&portsema);
      if(process) Processreply(msg);
   } while(process);
}

/* Set our port's signal, after having Wait()'ed for it but not processing
 * all messages from the port. */
static void Setportsignal(void)
{  ULONG signal=1<<taskport->mp_SigBit;
   SetSignal(signal,signal);
}

/* Wait until this message has been replied. Process other messages from
 * the same task. */
static struct Ataskmsg *Waitformsg(struct Ataskmsg *atm)
{  struct Ataskmsg *msg;
   BOOL process;
   for(;;)
   {  process=FALSE;
      ObtainSemaphore(&portsema);
      for(msg=(struct Ataskmsg *)taskport->lh_Head;msg->ln_Succ;
         msg=(struct Ataskmsg *)msg->ln_Succ)
      {  if(msg==atm)
         {  REMOVE(msg);
            ReleaseSemaphore(&portsema);
            Setportsignal();
            return atm;
         }
         else if(msg->task==atm->task)
         {  REMOVE(msg);
            process=TRUE;
            /* Need to release the semaphore before processing the reply */
            break;
         }
      }
      ReleaseSemaphore(&portsema);
      if(process) Processreply(msg);
      else Wait(1<<taskport->mp_SigBit);
   }
}

/* Start the subtask */
static void Starttask(struct Atask *task)
{  if(task->entry)
   {  if(task->startup=ALLOCSTRUCT(Ataskmsg,1,MEMF_CLEAR|MEMF_PUBLIC))
      {  task->startup->mn_ReplyPort=taskport;
         task->windowptr=(void *)Agetattr(Firstwindow(),AOWIN_Window);
         ObtainSemaphore(&startsema);
         task->proc=CreateNewProcTags(NP_Entry,Subtask,
            NP_Name,task->name,
            NP_Output,Output(),
            NP_CloseOutput,FALSE,
            NP_StackSize,task->stacksize,
            TAG_END);
         if(task->proc)
         {  task->proc->tc_UserData=task;
            ReleaseSemaphore(&startsema);
            Waitformsg(task->startup);
            task->flags|=TSKF_STARTED;
         }
         else
         {  ReleaseSemaphore(&startsema);
            task->state=TSKS_DEAD;
         }
         FREE(task->startup);
         task->startup=NULL;
      }
      else task->state=TSKS_DEAD;
   }
   else task->state=TSKS_DEAD;
}

/* Stop the subtask */
static void Stoptask(struct Atask *task,BOOL async)
{  struct SemaphoreMessage smsg={0},*msg;
   ULONG mask;
//KPrintF("%08lx * Stoptask\n",task);
   ObtainSemaphore(&task->sema);
   if(task->state!=TSKS_DEAD && task->state!=TSKS_NEW)
   {  
//KPrintF("%08lx * Stoptask Signal Ctrl-C\n",task);
      Signal(task->proc,SIGBREAKF_CTRL_C);
   }
   ReleaseSemaphore(&task->sema);
   if(!async)
   {  /* First process any messages put to our port before main() has got (and unset)
       * the signals, or else Wait() will deadlock. */
      Processtaskreply(task);
      /* Wait until subtask has terminated but process other messages from this task */
      smsg.mn_ReplyPort=semaport;
      Procure(&task->runsema,&smsg);
      for(;;)
      {  mask=Wait((1<<taskport->mp_SigBit)|(1<<semaport->mp_SigBit));
//KPrintF("%08lx * Stoptask mask=%08lx (taskport=%08lx semaport=%08lx)\n",task,mask,
//1<<taskport->mp_SigBit,1<<semaport->mp_SigBit);
         if(mask&(1<<semaport->mp_SigBit))
         {  if((msg=(struct SemaphoreMessage *)GetMsg(semaport))
            && msg->ssm_Semaphore==&task->runsema)
            {  break;
            }
         }
         if(mask&(1<<taskport->mp_SigBit))
         {  Processtaskreply(task);
         }
      }
      ReleaseSemaphore(&task->runsema);
      Setportsignal();
   }
}

/* Suspend the subtask */
static void Suspendtask(struct Atask *task,BOOL async)
{  struct Ataskmsg *atm;
   BOOL put=FALSE;
   if(atm=ALLOCSTRUCT(Ataskmsg,1,MEMF_PUBLIC|MEMF_CLEAR))
   {  atm->mn_ReplyPort=taskport;
      atm->task=task;
      atm->flags=TSMF_SUSPEND;
      ObtainSemaphore(&task->sema);
      if(task->state==TSKS_RUN && task->subport)
      {  PutMsg(task->subport,atm);
         put=TRUE;
      }
      ReleaseSemaphore(&task->sema);
      if(put && !async)
      {  Waitformsg(atm);
         put=FALSE;
      }
      if(!put)
      {  Processreply(atm);
      }
   }
}

/* Release the subtask */
static void Releasetask(struct Atask *task)
{  struct Ataskmsg *atm;
   if(atm=ALLOCSTRUCT(Ataskmsg,1,MEMF_PUBLIC|MEMF_CLEAR))
   {  atm->mn_ReplyPort=taskport;
      atm->task=task;
      atm->flags=TSMF_RELEASE;
      ObtainSemaphore(&task->sema);
      if(task->state==TSKS_SUSPEND && task->subport)
      {  PutMsg(task->subport,atm);
      }
      else
      {  FREE(atm);
      }
      ReleaseSemaphore(&task->sema);
   }
}

/* Send this Amessage */
static void Messagetask(struct Atask *task,struct Amessage *amsg,USHORT flags,
   Repliedfunction *rpf)
{  struct Ataskmsg *atm;
   BOOL put=FALSE;
   if(atm=ALLOCSTRUCT(Ataskmsg,1,MEMF_PUBLIC|MEMF_CLEAR))
   {  atm->mn_ReplyPort=taskport;
      atm->amsg=amsg;
      atm->task=task;
      atm->replied=rpf;
      atm->flags=flags;
      ObtainSemaphore(&task->sema);
      if(task->state!=TSKS_DEAD && task->subport)
      {  PutMsg(task->subport,atm);
         put=TRUE;
      }
      ReleaseSemaphore(&task->sema);
      if(put && !(flags&TSMF_ASYNC))
      {  Waitformsg(atm);
         put=FALSE;
      }
      if(!put)
      {  Processreply(atm);
      }
   }
}

/* Process replied message queue */
static void Processtask(void)
{  struct Ataskmsg *atm;
/*
   struct List list;
   NewList(&list);
   while(atm=(struct Ataskmsg *)GetMsg(taskport))
   {  AddTail(&list,atm);
   }
   while(atm=(struct Ataskmsg *)RemHead(&list))
   {  Processreply(atm);
   }
*/

   /* Process one message; then if another one is waiting set our signal again */
   if(atm=(struct Ataskmsg *)GetMsg(taskport))
   {  Processreply(atm);
   }
   ObtainSemaphore(&portsema);
   if(taskport->mp_MsgList.lh_Head->ln_Succ)
   {  Setportsignal();
   }
   ReleaseSemaphore(&portsema);

}

/* Do dispose this task. */
static void Dodisposetask(struct Atask *task)
{  task->flags&=~TSKF_MUSTDISPOSE;
   Stoptask(task,FALSE);
   Processtaskreply(task);
   if(task->name) FREE(task->name);
   REMOVE(task);
   Amethodas(AOTP_OBJECT,task,AOM_DISPOSE);
}

/*------------------------------------------------------------------------*/

static long Settask(struct Atask *task,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   struct Amessage *amsg=NULL;
   Repliedfunction *rpf=NULL;
   BOOL start=FALSE,stop=FALSE,suspend=FALSE,release=FALSE,async=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOTSK_Entry:
            task->entry=(Subtaskfunction *)tag->ti_Data;
            break;
         case AOTSK_Userdata:
            task->userdata=(void *)tag->ti_Data;
            break;
         case AOTSK_Stacksize:
            task->stacksize=tag->ti_Data;
            break;
         case AOTSK_Name:
            if(task->name) FREE(task->name);
            task->name=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOTSK_Start:
            if(task->state==TSKS_NEW) start=BOOLVAL(tag->ti_Data);
            break;
         case AOTSK_Stop:
            stop=BOOLVAL(tag->ti_Data);
            break;
         case AOTSK_Suspend:
            if(tag->ti_Data) suspend=TRUE;
            else if(tag->ti_Data) release=TRUE;
            break;
         case AOTSK_Async:
            async=BOOLVAL(tag->ti_Data);
            break;
         case AOTSK_Message:
            amsg=(struct Amessage *)tag->ti_Data;
            break;
         case AOTSK_Replied:
            rpf=(Repliedfunction *)tag->ti_Data;
            break;
         case AOBJ_Target:
            task->target=(void *)tag->ti_Data;
            break;
         case AOBJ_Map:
            task->maplist=(struct TagItem *)tag->ti_Data;
            break;
      }
   }
   if(start)
   {  Starttask(task);
   }
   if(stop)
   {  Stoptask(task,async);
   }
   if(suspend)
   {  Suspendtask(task,async);
   }
   if(release)
   {  Releasetask(task);
   }
   if(amsg)
   {  Messagetask(task,amsg,async?TSMF_ASYNC:0,rpf);
   }
   return 0;
}

static struct Atask *Newtask(struct Amset *ams)
{  struct Atask *task;
   if(task=Allocobject(AOTP_TASK,sizeof(struct Atask),ams))
   {  ADDTAIL(&tasks,task);
      InitSemaphore(&task->sema);
      InitSemaphore(&task->runsema);
      task->stacksize=20000;
      task->state=TSKS_NEW;
      Settask(task,ams);
   }
   return task;
}

static long Gettask(struct Atask *task,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOTSK_Userdata:
            PUTATTR(tag,task->userdata);
            break;
         case AOTSK_Start:
            PUTATTR(tag,task->state==TSKS_RUN);
            break;
         case AOTSK_Stop:
            PUTATTR(tag,task->state==TSKS_DEAD);
            break;
         case AOTSK_Suspend:
            PUTATTR(tag,task->state==TSKS_SUSPEND);
            break;
         case AOTSK_Started:
            PUTATTR(tag,BOOLVAL(task->flags&TSKF_STARTED));
            break;
      }
   }
   return 0;
}

static void Disposetask(struct Atask *task)
{  if(!(task->flags&TSKF_DISPOSING))
   {  task->flags|=TSKF_DISPOSING;
      if(task->flags&TSKF_PROCESSING)
      {  task->flags|=TSKF_MUSTDISPOSE;
         task->target=NULL;   /* Prevent future msgs to be sent to disposed object */
      }
      else
      {  Dodisposetask(task);
      }
   }
}

static void Deinstalltask(void)
{  while(tasks.first->next)
   {  Adisposeobject(tasks.first);
   }
   if(taskport)
   {  Setprocessfun(taskport->mp_SigBit,NULL);
      DeleteMsgPort(taskport);
   }
   if(semaport) DeleteMsgPort(semaport);
}

static long Dispatch(struct Atask *task,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newtask((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Settask(task,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Gettask(task,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposetask(task);
         break;
      case AOM_DEINSTALL:
         Deinstalltask();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installtask(void)
{  NEWLIST(&tasks);
   InitSemaphore(&startsema);
   InitSemaphore(&portsema);
   if(!(taskport=CreateMsgPort())) return FALSE;
   if(!(semaport=CreateMsgPort())) return FALSE;
   Setprocessfun(taskport->mp_SigBit,Processtask);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_TASK,Dispatch)) return FALSE;
   mainproc=(struct Process *)FindTask(NULL);
   return TRUE;
}

void AsetattrsasyncA(struct Aobject *ao,struct TagItem *tags)
{  struct Ataskmsg *atm;
   struct Amset *ams;
   struct Atask *task=(struct Atask *)ao;
   if(ao)
   {  if(atm=ALLOCSTRUCT(Ataskmsg,1,MEMF_CLEAR))
      {  if(ams=ALLOCSTRUCT(Amset,1,MEMF_CLEAR))
         {  if(ams->tags=CloneTagItems(tags))
            {  ams->method=AOM_SET;
               atm->mn_ReplyPort=taskport;
               atm->task=task;
               atm->amsg=ams;
               atm->flags=TSMF_VANILLASET;
               atm->replied=(Repliedfunction *)GetTagData(AOTSK_Replied,NULL,ams->tags);
               ObtainSemaphore(&task->sema);
               if(task->state!=TSKS_DEAD && task->subport)
               {  PutMsg(task->subport,atm);
                  ReleaseSemaphore(&task->sema);
                  return;
               }
               ReleaseSemaphore(&task->sema);
               FreeTagItems(ams->tags);
            }
            FREE(ams);
         }
         FREE(atm);
      }
   }
}

void Asetattrsasync(struct Aobject *ao,...)
{  AsetattrsasyncA(ao,VARARG(ao));
}

void AsetattrssyncA(struct Aobject *ao,struct TagItem *tags)
{  struct Amset ams;
   ams.method=AOM_SET;
   ams.tags=tags;
   Asetattrs(ao,
      AOTSK_Message,&ams,
      TAG_END);
}

void Asetattrssync(struct Aobject *ao,...)
{  AsetattrssyncA(ao,VARARG(ao));
}

BOOL Isawebtask(struct Task *t)
{  struct Atask *task;
   BOOL result=FALSE;
   if(t==mainproc) return TRUE;
   Forbid();
   for(task=tasks.first;task->next;task=task->next)
   {  if(t==task->proc)
      {  result=TRUE;
         break;
      }
   }
   Permit();
   return result;
}