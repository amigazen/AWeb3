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

/* task.h - AWeb task object */

#ifndef AWEB_TASK_H
#define AWEB_TASK_H

#include "object.h"
#include <exec/ports.h>

/* Main process creates this object. When AOTSK_Start is set to TRUE,
 * the subtask is started and its AOTSK_Entry function is called.
 * This function must call the Gettaskmsg() function regularly to check
 * for incoming messages. If AOTSK_Stop is TRUE in such a message, the
 * subtask function must stop its processing and return. Incoming
 * messages (including the AOTSK_Stop) must be replied using Replytaskmsg().
 * If the subtask expects no messages at all, it can use Checktaskbreak()
 * to see if it was stopped.
 *
 * The main process can send AWeb OO messages to the subtask, by setting
 * the AOTSK_Message attribute. The main task waits until the subtask has
 * replied the message, unless AOTSK_Async was set to TRUE.
 * While waiting, other messages from the same task are processed to avoid
 * deadlocks.
 * For Async messages, make sure you don't pass any data allocated on the stack!
 *
 * Regardless of AOTSK_Async, the AOTSK_Replied function (if any) will be
 * called when the message has been replied.
 *
 * The subtask can send the main task AWeb OO messages by the Updatetask()
 * function. If there is an AOBJ_Target set, the message is forwarded to
 * the target as AOM_UPDATE with AOTSK_Message set to the message.
 * If the subtasks uses Updatetaskattrs(), the original taglist is used
 * directly in the AOM_UPDATE message to the target (after any required
 * mapping).
 * In both cases, AOBJ_Target in the AOM_UPDATE message is set to the
 * task object.
 *
 * The main task can suspend the subtask by setting AOTSK_Suspend to TRUE.
 * Setting it to FALSE restarts the subtask. The subtask is only suspended
 * when it calls Gettaskmsg(). If AOTSK_Async is not set together with
 * AOTSK_Suspend, the main task waits until the subtask is actually suspended.
 * While the subtask is suspended, synchroneous messages are replied
 * immediately to prevent deadlocks. Asynchroneous messages are queued
 * and passed to the subtask when it awakes.
 */


/*--- task hook function prototypes ---*/

typedef void Subtaskfunction(void *userdata);
   /* The main function of the subtask. */

typedef void Repliedfunction(void *task,struct Taskmsg *msg);
   /* Process hook after message has been replied. */


/*--- task tags ---*/

#define AOTSK_Dummy        AOBJ_DUMMYTAG(AOTP_TASK)

#define AOTSK_Entry        (AOTSK_Dummy+1)   /* NEW */
   /* (Subtaskfunction *) Entry point of the subtask. */

#define AOTSK_Userdata     (AOTSK_Dummy+2)   /* NEW,GET */
   /* (void *) User data passed to the subtask. */

#define AOTSK_Stacksize    (AOTSK_Dummy+3)   /* NEW */
   /* (long) Stack size for the subtask. Default 20000. */

#define AOTSK_Name         (AOTSK_Dummy+4)   /* NEW */
   /* (UBYTE *) Process name for the subtask */

#define AOTSK_Start        (AOTSK_Dummy+5)   /* NEW,SET,GET */
   /* (BOOL) Starts the subtask. */

#define AOTSK_Stop         (AOTSK_Dummy+6)   /* SET,GET */
   /* (BOOL) Terminates the subtask. */

#define AOTSK_Suspend      (AOTSK_Dummy+7)   /* SET,GET */
   /* (BOOL) Temporarily suspends the subtask, or let it go again. */

#define AOTSK_Async        (AOTSK_Dummy+8)   /* SET */
   /* (BOOL) If FALSE, the main task will wait until the message
    * has been replied. If TRUE, the main task will continue; pass a
    * Repliedfunction to process any result.
    * Default FALSE. */

#define AOTSK_Replied      (AOTSK_Dummy+9)   /* SET */
   /* (Repliedfunction *) This function is called when the message
    * has been replied. */

#define AOTSK_Message      (AOTSK_Dummy+10)  /* SET,UPDATE */
   /* (struct Amessage *) Aweb OO-message to send to the subtask, 
    * or the message send by the subtask to main. */

#define AOTSK_Started      (AOTSK_Dummy+11)  /* GET */
   /* (BOOL) If the task was started. Use this instead of AOTSK_Start
    * after setting AOTSK_Start because of race conditions.
    * (Subtask may have been finished before the main task had
    * a chance to GET this attribute) */

#define AOTSK_    (AOTSK_Dummy+)
#define AOTSK_    (AOTSK_Dummy+)

#ifndef NOPROTOTYPES

/*--- task functions ---*/

/* Called by main task: */

extern void AsetattrsasyncA(struct Aobject *ao,struct TagItem *tags);
extern void Asetattrsasync(struct Aobject *ao,...);
   /* Creates an AOM_SET message with a copy of the tags passed and
    * sends it asynchroneously to the subtask. Upon reply the message
    * and the copy taglist will be freed automatically, after the
    * Repliedfunction (if any) has been called. */

extern void AsetattrssyncA(struct Aobject *ao,struct TagItem *tags);
extern void Asetattrssync(struct Aobject *ao,...);
   /* Creates an AOM_SET message and sends it synchroneously to the
    * subtask. */

/* Called by subtask: */

extern ULONG Waittask(ULONG signals);
   /* Waits for one of these signals, or an incoming Taskmsg.
    * Returns the signals received or zero if there was only an incoming
    * Taskmsg. */

extern struct Taskmsg *Gettaskmsg(void);
   /* Checks for any incoming messages for the subtask.
    * Returns a Taskmsg or NULL if no messages were available. */

extern void Replytaskmsg(struct Taskmsg *msg);
   /* Replies the message. */

extern BOOL Checktaskbreak(void);
   /* Checks to see if this task was stopped. */

extern long Updatetask(struct Amessage *amsg);
   /* Sends this message to the main task. */

extern long UpdatetaskattrsA(struct TagItem *tags);
extern long Updatetaskattrs(ULONG tag,...);
   /* Builds an AOM_UPDATE message and sends it to the main task. 
    * If the taglist contains AOTSK_Async,TRUE the subtask will not wait
    * until the message is replied. */

extern BOOL Obtaintasksemaphore(struct SignalSemaphore *sema);
   /* Obtains this semaphore, but listens also for AOTSK_Stop.
    * Returns TRUE if you have obtained the semaphore, or FALSE
    * if you should stop.
    * Currently this function does not listen to AOTSK_Suspend. */

extern void Settaskuserdata(void *data);
extern void *Gettaskuserdata(void);
   /* Set or get task-specific userdata. This is NOT the same as
    * AOTSK_Userdata. Use this to pass global data to hook functions
    * without userdata argument. */

/* Support, could be called by main or subtask */

extern BOOL Isawebtask(struct Task *t);
   /* Returns TRUE if this Task is AWebs main task or one of AWebs subtasks */

/* Number of subtasks abandoned after a sync stop timeout (exit robustness). */
extern long Taskabandonedcount(void);

#endif /* !NOPROTOTYPES */

/*--- task structures ---*/

/* Taskmsg is used to pass messages to the subtask. */
struct Taskmsg
{  struct Message execmsg;       /* The EXEC message header */
   struct Amessage *amsg;        /* The AWeb OO-message */
   long result;                  /* Return value of the method */
   /* Data beyond this point is private */
};

#endif
