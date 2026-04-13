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

/* arexxjs.c - AWeb js arexx support */
/* experimental support for sending commands to arexx ports */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/locale.h>
#include <proto/rexxsyslib.h>
#include <proto/utility.h>
#include <rexx/errors.h>

#include "jslib.h"
#include "keyfile.h"
#include "rexxmsgext.h"

/* AWebJSBase is needed for jslib.h pragma libcall directives */
/* It's defined in awebjs.c, but pragma libcall expects struct Library * */
extern struct Library *AWebJSBase;

/* Forward declarations */
BOOL initarexx(struct Jcontext *jc);
void freearexx(struct Jcontext *jc);
/* SendCommand is defined later in this file, no forward declaration needed */

struct RexxMsgExt _rmex;
struct RexxMsgExt *rmex = &_rmex;
BOOL arexxisgood = FALSE;

struct MsgPort *replyport = NULL;

static WORD SendARexxMsgHost(STRPTR RString, STRPTR Host, STRPTR Extension, BOOL results);

/* RexxSysBase is declared in proto/rexxsyslib.h as struct RxsLib * */

/* SetJSVarCallBack - Hook function to set JavaScript variables from ARexx */
static __saveds ULONG SetJSVarCallBack(struct Hook *hook, APTR unused, struct RexxMsg *rm)
{
    struct Jcontext *jc;
    STRPTR varname;
    STRPTR value;
    ULONG length;
    STRPTR nextdot;
    STRPTR p;
    STRPTR varnamecopy;
    struct Jobject *jo;
    struct Jobject *jthis;
    struct Jvar *jv;
    
    jc = ((struct RexxMsgExt *)(APTR)rm->rm_avail)->rme_UserData;
    varname = ((struct SetVarHookData *)hook->h_Data)->name;
    value = ((struct SetVarHookData *)hook->h_Data)->value;
    
    /* Need to check for dots and split the variable by them then create a object 'tree' from it */
    /* If the last character is a dot we give up straight away. */
    
    length = strlen(varname);
    jthis = Jthis(jc);
    
    if(varname[length - 1] == '.')
    {
        return ERR10_018; /* Invalid argument to function */
    }
    if((jv = Jproperty(jc, jthis, "varname")))
    {
        Jasgstring(jc, jv, varname);
    }
    /* Make a copy of the varname string so we can safely modify it as we parse it */
    if((varnamecopy = AllocVec(length + 1, MEMF_PUBLIC | MEMF_CLEAR)))
    {
        strcpy(varnamecopy, varname);
        p = varnamecopy;
        
        while((nextdot = strchr(p, '.')))
        {
            nextdot[0] = '\0';
            if((jv = Jproperty(jc, jthis, p)))
            {
                if(!(jo = Jtoobject(jc, jv)))
                {
                    if((jo = Newjobject(jc)))
                    {
                        Jasgobject(jc, jv, jo);
                    }
                }
                if(jo)
                {
                    jthis = jo;
                }
            }
            p = nextdot + 1;
        }
        if((jv = Jproperty(jc, jthis, p)))
        {
            Jasgstring(jc, jv, value);
        }
        FreeVec(varnamecopy);
    }
    return 0;
}

/* GetJSVarCallBack - Hook function to get JavaScript variables for ARexx */
static __saveds ULONG GetJSVarCallBack(struct Hook *hook, APTR unused, struct RexxMsg *rm)
{
    struct Jcontext *jc;
    STRPTR varname;
    STRPTR buffer;
    ULONG length;
    STRPTR nextdot;
    STRPTR p;
    STRPTR varnamecopy;
    struct Jobject *jthis;
    struct Jvar *jv;
    STRPTR value;
    
    jc = ((struct RexxMsgExt *)(APTR)rm->rm_avail)->rme_UserData;
    varname = ((struct GetVarHookData *)hook->h_Data)->name;
    buffer = ((struct GetVarHookData *)hook->h_Data)->buffer;  /* sizelimit 255 bytes! */
    
    /* Need to check for dots and split the variable by them then walk the object 'tree' from it */
    /* If the last character is a dot we give up straight away. */
    
    length = strlen(varname);
    jthis = Jthis(jc);
    
    if(varname[length - 1] == '.')
    {
        return ERR10_018; /* Invalid argument to function */
    }
    
    /* make a copy of the varname string so we can safely modify it as we parse it */
    if((varnamecopy = AllocVec(length + 1, MEMF_PUBLIC | MEMF_CLEAR)))
    {
        strcpy(varnamecopy, varname);
        p = varnamecopy;
    
        while((nextdot = strchr(p, '.')))
        {
            nextdot[0] = '\0';
            if((jv = Jproperty(jc, jthis, p)))
            {
                if(!(jthis = Jtoobject(jc, jv)))
                {
                    FreeVec(varnamecopy);
                    return ERR10_003; /* No memory available. */
                }
            }
            else
            { 
                FreeVec(varnamecopy);
                return ERR10_040; /* Invalid variable name; */
            }
            p = nextdot + 1;
        }
        if((jv = Jproperty(jc, jthis, p)))
        {
            value = Jtostring(jc, jv);
            if(value)
            {
                strncpy(buffer, value, 255);
                buffer[255] = '\0';
            }
        }
        else
        {
            FreeVec(varnamecopy);
            return ERR10_040; /* Invalid variable name; */
        }
        FreeVec(varnamecopy);
    }
    return 0;
}

/* initarexx - Initialize ARexx support */
BOOL initarexx(struct Jcontext *jc)
{
    /* Open RexxSysLib */
    /* Note: Cast is safe as struct RxsLib extends struct Library */
    if((RexxSysBase = (struct RxsLib *)OpenLibrary("rexxsyslib.library", 0)))
    {
        /* Initialize rmex structure */
        rmex->rme_SetVarHook = NULL;
        rmex->rme_GetVarHook = NULL;
        rmex->rme_UserData = NULL;
        
        /* Allocate and initialize the hooks */
        if((rmex->rme_SetVarHook = AllocVec(sizeof(struct Hook), MEMF_PUBLIC | MEMF_CLEAR)))
        {
            rmex->rme_SetVarHook->h_Entry = (HOOKFUNC)SetJSVarCallBack;
            rmex->rme_SetVarHook->h_SubEntry = (HOOKFUNC)SetJSVarCallBack;
            
            if((rmex->rme_GetVarHook = AllocVec(sizeof(struct Hook), MEMF_PUBLIC | MEMF_CLEAR)))
            {
                rmex->rme_GetVarHook->h_Entry = (HOOKFUNC)GetJSVarCallBack;
                rmex->rme_GetVarHook->h_SubEntry = (HOOKFUNC)GetJSVarCallBack;
                
                /* Create a msg port for replies */
                /* Needn't be public */
                if((replyport = CreateMsgPort()))
                {
                    rmex->rme_UserData = jc;  /* in this command line version of javascript there can only be one context. */
                    arexxisgood = TRUE;
                    return TRUE;
                }
                FreeVec(rmex->rme_GetVarHook);
                rmex->rme_GetVarHook = NULL;
            }
            FreeVec(rmex->rme_SetVarHook);
            rmex->rme_SetVarHook = NULL;
        }
        CloseLibrary((struct Library *)RexxSysBase);
        RexxSysBase = NULL;
    }
    arexxisgood = FALSE;
    return FALSE;
}

/* freearexx - Clean up ARexx support */
void freearexx(struct Jcontext *jc)
{
    arexxisgood = FALSE;
    
    /* Free the hooks */
    if(rmex->rme_SetVarHook)
    {
        FreeVec(rmex->rme_SetVarHook);
        rmex->rme_SetVarHook = NULL;
    }
    if(rmex->rme_GetVarHook)
    {
        FreeVec(rmex->rme_GetVarHook);
        rmex->rme_GetVarHook = NULL;
    }
    
    /* Delete The Port */
    if(replyport)
    {
        DeleteMsgPort(replyport);
        replyport = NULL;
    }
    if(RexxSysBase)
    {
        CloseLibrary(RexxSysBase);
        RexxSysBase = NULL;
    }
}

/* SendCommand - JavaScript function to send ARexx commands */
/* Note: __saveds only (no __asm) since it's used as a function pointer */
__saveds void SendCommand(struct Jcontext *jc)
{
    struct Jvar *jhost;
    struct Jvar *jcommand;
    struct Jvar *jresults;
    STRPTR host;
    STRPTR command;
    BOOL results;
    struct RexxMsg *rm;
    ULONG RC;
    ULONG RC2;
    STRPTR Result;
    struct Jobject *jthis;
    struct Jvar *jv;
    
    if(!arexxisgood)
    {
        printf("WARNING: ARexx not correctly initialised\n");
        Jasgboolean(jc, NULL, FALSE);
        return;
    }
    
    /* To start simple fail if insufficient args (really should throw an exception!) */
    if(!(jhost = Jfargument(jc, 0)))
    {
        Jasgboolean(jc, NULL, FALSE);
        return;
    }
    if(!(jcommand = Jfargument(jc, 1)))
    {
        Jasgboolean(jc, NULL, FALSE);
        return;
    }
    
    /* this one optional defaults to true */
    jresults = Jfargument(jc, 2);
    if(jresults)
    {
        results = Jtoboolean(jc, jresults);
    }
    else
    {
        results = TRUE;
    }
    
    host = Jtostring(jc, jhost);
    command = Jtostring(jc, jcommand);
    if(SendARexxMsgHost(command, host, "", results))
    {
        /* Now wait for the result ! */
        Wait(1 << replyport->mp_SigBit);
        
        rm = (struct RexxMsg *)GetMsg(replyport);
        if(rm && rm->rm_Node.mn_Node.ln_Type == NT_REPLYMSG)
        {
            /* Extract the result */
            /* Need to set result rc and rc2 in our parent object */
            RC = 0;
            RC2 = 0;
            Result = NULL;
            
            if((RC = rm->rm_Result1))
            {
                RC2 = rm->rm_Result2;
            }
            else
            {
                Result = (STRPTR)rm->rm_Result2;
            }
            
            jthis = Jthis(jc);
            
            if((jv = Jproperty(jc, jthis, "RC")))
            {
                Jasgnumber(jc, jv, RC);
            }
            if((jv = Jproperty(jc, jthis, "RESULT")))
            {
                if(Result)
                {
                    Jasgstring(jc, jv, Result);
                }
                else
                {
                    Jasgstring(jc, jv, ""); /* would prefer to set this to undefined but not sure if I can with modifying the javascript lib. */
                }
            }
            
            if(Result)
            {
                /* I think this my responsibility to 'free' guess things will go bang if not! */
                DeleteArgstring((STRPTR)rm->rm_Result2);
            }
            /*
             * Free the arguments and the message...
             */
            DeleteArgstring(rm->rm_Args[0]);
            DeleteRexxMsg(rm);
            Jasgboolean(jc, NULL, TRUE);
            return;
        }
        else
        {  /* GetMsg() removed a packet from our port; must not drop it */
            if(rm)
            {  DeleteRexxMsg(rm);
            }
            printf("WARNING! Unexpected packet! SendCommand %ld\n", __LINE__);
        }
    }
    Jasgboolean(jc, NULL, FALSE);
}

/* SendARexxMsgHost - Internal function to send ARexx message to a host */
static WORD SendARexxMsgHost(STRPTR RString, STRPTR Host, STRPTR Extension, BOOL results)
{
    struct MsgPort *RexxPort;
    struct RexxMsg *rmsg;
    BOOL flag;
    BOOL found;
    
    flag = FALSE;
    found = FALSE;
    if(replyport) 
    {
        if(RString)
        {
            if((rmsg = CreateRexxMsg(replyport, Extension, Host)))
            {
                rmsg->rm_Action = RXCOMM | (results ? RXFF_RESULT : 0);
                rmsg->rm_avail = (LONG)rmex;
                if((rmsg->rm_Args[0] = CreateArgstring(RString, (LONG)strlen(RString))))
                {
                    /*
                     * We need to find the RexxPort and this needs
                     * to be done in a Forbid()
                     */
                    Forbid();
                    if((RexxPort = FindPort(Host)))
                    {
                        /*
                         * We found the port, so put the
                         * message to it...
                         */
                        found = TRUE;
                        PutMsg(RexxPort, (struct Message *)rmsg);
                        flag = TRUE;
                    }
                    else
                    {
                        /*
                         * No port, so clean up...
                         */
                        DeleteArgstring(rmsg->rm_Args[0]);
                        DeleteRexxMsg(rmsg);
                    }
                    Permit();
                }
                else
                {
                    DeleteRexxMsg(rmsg);
                }
            }
        }
    }
    return(flag);
}

