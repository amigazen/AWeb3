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

/* xaweb.c - aweb x-aweb stuff */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include "aweb.h"
#include "fetchdriver.h"
#include "tcperr.h"
#include "file.h"
#include "task.h"

#define AMHOTLISTRXNAME "env:mosaic/hotlist.html"
#define AMHOTLIST20NAME "env:mosaic/.mosaic-hotlist-default"

/*-----------------------------------------------------------------------*/

static void Copyfile(struct Fetchdriver *fd,UBYTE *name)
{  long fh,actual;
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      TAG_END);
   if(fh=Open(name,MODE_OLDFILE))
   {  for(;;)
      {  actual=Read(fh,fd->block,INPUTBLOCKSIZE);
         if(actual<=0) break;
         if(Checktaskbreak()) break;
         Updatetaskattrs(
            AOURL_Data,fd->block,
            AOURL_Datalength,actual,
            TAG_END);
      }
      Close(fh);
   }
   else Tcperror(fd,TCPERR_NOFILE,name);
}

#ifndef DEMOVERSION
static void Hiehotlist(struct Fetchdriver *fd,UBYTE *name,UBYTE *hlname,BOOL amosaic)
{  long fh,actual;
   UBYTE *buf=ALLOCTYPE(UBYTE,FILEBLOCKSIZE,0);
   UBYTE *p;
   BOOL hr=FALSE;
   if(buf)
   {  if(fh=Open(name,MODE_OLDFILE))
      {  Updatetaskattrs(
            AOURL_Contenttype,"text/html",
            TAG_END);
         strcpy(fd->block,"<html><title>");
         strcat(fd->block,hlname);
         strcat(fd->block," ");
         strcat(fd->block,AWEBSTR(MSG_AWEB_HOTLISTTITLE));
         strcat(fd->block,"</title><h1>");
         strcat(fd->block,hlname);
         strcat(fd->block," ");
         strcat(fd->block,AWEBSTR(MSG_AWEB_HOTLISTTITLE));
         strcat(fd->block,"</h1><ul>");
         FGets(fh,buf,FILEBLOCKSIZE);
         if(amosaic) FGets(fh,buf,FILEBLOCKSIZE);
         for(;;)
         {  if(!FGets(fh,buf,FILEBLOCKSIZE)) break;
            if(STRNIEQUAL(buf,"@GROUP ",7))
            {  strcat(fd->block,"<hr><li>");
               strcat(fd->block,buf+7);
               strcat(fd->block,"<ul>");
               hr=FALSE;
            }
            else if(STRNIEQUAL(buf,"@GROUP_HIDE ",12))
            {  strcat(fd->block,"<hr><li>");
               strcat(fd->block,buf+12);
               strcat(fd->block,"<ul>");
               hr=FALSE;
            }
            else if(STRNIEQUAL(buf,"@ENDGROUP",9))
            {  strcat(fd->block,"</ul>");
               hr=TRUE;
            }
            else if(buf[0]!='@')
            {  if((p=strchr(buf,' ')) || (p=strchr(buf,'\n'))) *p='\0';
               if(hr)
               {  strcat(fd->block,"<hr>");
                  hr=FALSE;
               }
               strcat(fd->block,"<li><a href=\"");
               strcat(fd->block,buf);
               strcat(fd->block,"\">");
               if(!FGets(fh,buf,FILEBLOCKSIZE)) break;
               strcat(fd->block,buf);
               strcat(fd->block,"</a>\n");
            }
            actual=strlen(fd->block);
            if(actual>INPUTBLOCKSIZE-1000)
            {  Updatetaskattrs(
                  AOURL_Data,fd->block,
                  AOURL_Datalength,actual,
                  TAG_END);
               *fd->block='\0';
            }
         }
         strcat(fd->block,"</ul></body></html>");
         actual=strlen(fd->block);
         Updatetaskattrs(
            AOURL_Data,fd->block,
            AOURL_Datalength,actual,
            TAG_END);
         Close(fh);
      }
      else Tcperror(fd,TCPERR_NOFILE,name);
      FREE(buf);
   }
}

static UBYTE Hextodec(UBYTE c)
{  UBYTE ch=0;
   if(c>='0' && c<='9') ch=c-'0';
   else
   {  c=toupper(c);
      if(c>='A' && c<='F') ch=c+10-'A';
   }
   return ch;
}

static BOOL Cmdwarnok(UBYTE *cmd,BOOL arexx)
{  UBYTE *msg=AWEBSTR(arexx?MSG_CMDWARN_AREXX:MSG_CMDWARN_SHELL);
   UBYTE *buf;
   long l=strlen(msg)+strlen(cmd)+8;
   BOOL result=FALSE;
   if(buf=ALLOCTYPE(UBYTE,l,0))
   {  sprintf(buf,msg,cmd);
      result=Syncrequest(AWEBSTR(MSG_CMDWARN_TITLE),buf,AWEBSTR(MSG_CMDWARN_BUTTONS),0);
      FREE(buf);
   }
   return result;
}

static void Docommand(struct Fetchdriver *fd,UBYTE *cmd,BOOL arexx)
{  BOOL commands;
   ObtainSemaphore(&prefssema);
   commands=prefs.program.commands || (fd->flags&FDVF_COMMANDS);
   ReleaseSemaphore(&prefssema);
//if(!fd->referer) fd->referer="file://";
   if(fd->referer
   && (STRNIEQUAL(fd->referer,"file://",7) || STRNIEQUAL(fd->referer,"x-aweb:",7))
   && (commands || Cmdwarnok(cmd,arexx)))
   {  struct Buffer buf={0};
      UBYTE *p,*q,*end=cmd+strlen(cmd);
      BOOL value=FALSE;
      UBYTE ch;
      if(q=strchr(cmd,'?'))
      {  Addtobuffer(&buf,cmd,q-cmd);
         Addtobuffer(&buf," ",1);
         p=q+1;
         while(p<end)
         {  if(value)
            {  if(*p=='%')
               {  if(p<end-2)
                  {  ch=(Hextodec(p[1])<<4)+Hextodec(p[2]);
                     if(ch=='"') Addtobuffer(&buf,"*\"",2);
                     else if(ch=='\n') Addtobuffer(&buf,"*N",2);
                     else if(ch==0x1b) Addtobuffer(&buf,"*E",2);
                     else if(ch=='$') Addtobuffer(&buf,"*$",2);
                     else if(ch=='*') Addtobuffer(&buf,"**",2);
                     else if(ch!='\r') Addtobuffer(&buf,&ch,1);
                  }
                  p+=2;
               }
               else if(*p=='+') Addtobuffer(&buf," ",1);
               else if(*p=='*') Addtobuffer(&buf,"**",2);
               else if(*p=='&')
               {  Addtobuffer(&buf,"\" ",2);
                  value=FALSE;
               }
               else Addtobuffer(&buf,p,1);
            }
            else
            {  if(*p==',') Addtobuffer(&buf," ",1);
               else Addtobuffer(&buf,p,1);
               if(*p=='=')
               {  value=TRUE;
                  Addtobuffer(&buf,"\"",1);
               }
            }
            p++;
         }
         if(value) Addtobuffer(&buf,"\"",1);
      }
      else Addtobuffer(&buf,cmd,strlen(cmd));
      if(fd->postmsg)
      {  void *file;
         UBYTE *name;
         if(file=Anewobject(AOTP_FILE,
            AOFIL_Delete,FALSE,
            TAG_END))
         {  Asetattrs(file,
               AOFIL_Data,fd->postmsg,
               AOFIL_Datalength,strlen(fd->postmsg),
               TAG_END);
            Asetattrs(file,AOFIL_Eof,TRUE,TAG_END);
            name=(UBYTE *)Agetattr(file,AOFIL_Name);
            Addtobuffer(&buf," ",1);
            Addtobuffer(&buf,name,strlen(name));
            Adisposeobject(file);
         }
      }
      Addtobuffer(&buf,"",1);
      if(arexx)
      {  Updatetaskattrs(
            AOURL_Arexxcommand,buf.buffer,
            TAG_END);
      }
      else
      {  UBYTE *p;
         if(buf.buffer[0]=='"' && (p=strchr(buf.buffer+1,'"')))
         {  p++;           /* skip quote */
            if(*p)
            {  *p='\0';    /* terminate command */
               p++;        /* arguments */
            }
         }
         else if(p=strchr(buf.buffer+1,' '))
         {  *p='\0';       /* terminate command */
            p++;           /* arguments */
         }
         else p="";
         Spawn(FALSE,buf.buffer,p,"");
      }
      Freebuffer(&buf);
   }
}
#endif /* !DEMOVERSION */

static void Awebhotlist(struct Fetchdriver *fd)
{  void *tf=Anewobject(AOTP_FILE,TAG_END);
   if(tf)
   {  Buildhtmlhotlist(tf);
      Asetattrs(tf,AOFIL_Eof,TRUE,TAG_END);
      Copyfile(fd,(UBYTE *)Agetattr(tf,AOFIL_Name));
      Adisposeobject(tf);
   }
}

void Xawebtask(struct Fetchdriver *fd)
{  BOOL error=FALSE,eof=TRUE;
   if(STRNEQUAL(fd->name,"//",2)) fd->name+=2;
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      TAG_END);
   if(STRIEQUAL(fd->name,"hotlist"))
      Awebhotlist(fd);
#ifndef DEMOVERSION
   else if(STRIEQUAL(fd->name,"amhotlist.rexx"))
      Copyfile(fd,AMHOTLISTRXNAME);
   else if(STRIEQUAL(fd->name,"amhotlist.20"))
      Hiehotlist(fd,AMHOTLIST20NAME,"AMosaic",TRUE);
   else if(STRNIEQUAL(fd->name,"ibhotlist/",10))
      Hiehotlist(fd,fd->name+10,AWEBSTR(MSG_AWEB_OTHER),FALSE);
   else if(STRNIEQUAL(fd->name,"command/",8))
   {  Docommand(fd,fd->name+8,FALSE);
      eof=FALSE;
   }
   else if(STRNIEQUAL(fd->name,"rexx/",5))
   {  Docommand(fd,fd->name+5,TRUE);
      eof=FALSE;
   }
#endif
   else Tcperror(fd,TCPERR_XAWEB,fd->name);
   if(Checktaskbreak()) error=TRUE;
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Error,error,
      AOURL_Eof,eof,
      AOURL_Terminate,TRUE,
      TAG_END);
}

