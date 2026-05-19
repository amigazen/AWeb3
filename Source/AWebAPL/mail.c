/**********************************************************************
 * 
 * This file is part of the AWeb APL distribution
 *
 * Copyright (C) 2002 Yvon Rozijn
 * Changes Copyright (C) 2025-2026 amigazen project
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

/* mail.c - AWeb mailer */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "aweblib.h"
#include "tcperr.h"
#include "fetchdriver.h"
#include "task.h"
#include "application.h"
#include "awebtcp.h"
#include "awebssl.h"
#include "mail.h"
#include <stdarg.h>
#include <exec/resident.h>
#include <libraries/asl.h>
#include <graphics/displayinfo.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <proto/exec.h>
#include <proto/asl.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/socket.h>

#ifndef LOCALONLY

struct Library *AwebTcpBase;
struct Library *AwebSslBase;
void *AwebPluginBase;

/*-----------------------------------------------------------------------*/
/* AWebLib module startup */

__asm __saveds struct Library *Initlib(
   register __a6 struct ExecBase *sysbase,
   register __a0 struct SegList *seglist,
   register __d0 struct Library *libbase);

__asm __saveds struct Library *Openlib(
   register __a6 struct Library *libbase);

__asm __saveds struct SegList *Closelib(
   register __a6 struct Library *libbase);

__asm __saveds struct SegList *Expungelib(
   register __a6 struct Library *libbase);

__asm __saveds ULONG Extfunclib(void);

__asm __saveds void Fetchdrivertask(
   register __a0 struct Fetchdriver *fd);

/* Function declarations for project dependent hook functions */
static ULONG Initaweblib(struct Library *libbase);
static void Expungeaweblib(struct Library *libbase);

struct Library *MailBase;

static APTR libseglist;

LONG __saveds __asm Libstart(void)
{  return -1;
}

static APTR functable[]=
{  Openlib,
   Closelib,
   Expungelib,
   Extfunclib,
   Fetchdrivertask,
   (APTR)-1
};

/* Init table used in library initialization. */
static ULONG inittab[]=
{  sizeof(struct Library),
   (ULONG) functable,
   0,
   (ULONG) Initlib
};

static char __aligned libname[]="mail.aweblib";
static char __aligned libid[]="mail.aweblib " AWEBLIBVSTRING " " __AMIGADATE__;

/* The ROM tag */
struct Resident __aligned romtag=
{  RTC_MATCHWORD,
   &romtag,
   &romtag+1,
   RTF_AUTOINIT,
   AWEBLIBVERSION,
   NT_LIBRARY,
   0,
   libname,
   libid,
   inittab
};

__asm __saveds struct Library *Initlib(
   register __a6 struct ExecBase *sysbase,
   register __a0 struct SegList *seglist,
   register __d0 struct Library *libbase)
{  SysBase=sysbase;
   MailBase=libbase;
   libbase->lib_Revision=AWEBLIBREVISION;
   libseglist=seglist;
   if(!Initaweblib(libbase))
   {  Expungeaweblib(libbase);
      libbase=NULL;
   }
   return libbase;
}

__asm __saveds struct Library *Openlib(
   register __a6 struct Library *libbase)
{  libbase->lib_OpenCnt++;
   libbase->lib_Flags&=~LIBF_DELEXP;
   if(libbase->lib_OpenCnt==1)
   {  AwebPluginBase=OpenLibrary("awebplugin.library",0);
   }
#ifndef DEMOVERSION
   if(!Fullversion())
   {  Closelib(libbase);
      return NULL;
   }
#endif
   return libbase;
}

__asm __saveds struct SegList *Closelib(
   register __a6 struct Library *libbase)
{  libbase->lib_OpenCnt--;
   if(libbase->lib_OpenCnt==0)
   {  Mail_imap_release_stale();
      if(AwebPluginBase)
      {  CloseLibrary(AwebPluginBase);
         AwebPluginBase=NULL;
      }
      if(libbase->lib_Flags&LIBF_DELEXP)
      {  return Expungelib(libbase);
      }
   }
   return NULL;
}

__asm __saveds struct SegList *Expungelib(
   register __a6 struct Library *libbase)
{  if(libbase->lib_OpenCnt==0)
   {  ULONG size=libbase->lib_NegSize+libbase->lib_PosSize;
      UBYTE *ptr=(UBYTE *)libbase-libbase->lib_NegSize;
      Remove((struct Node *)libbase);
      Expungeaweblib(libbase);
      FreeMem(ptr,size);
      return libseglist;
   }
   libbase->lib_Flags|=LIBF_DELEXP;
   return NULL;
}

__asm __saveds ULONG Extfunclib(void)
{  return 0;
}

/*-----------------------------------------------------------------------*/

void Mail_debug(UBYTE *fmt,...)
{
#if MAIL_DEBUG
   UBYTE buf[512];
   long len;
   va_list args;
   if(!AwebPluginBase || !fmt) return;
   va_start(args,fmt);
   vsprintf((char *)buf,(char *)fmt,args);
   va_end(args);
   len=strlen((char *)buf);
   if(len>500) len=500;
   buf[len]='\n';
   buf[len+1]='\0';
   Aprintf((char *)buf);
#else
   (void)fmt;
#endif
}

/*-----------------------------------------------------------------------*/

/* Convert hexadecimal */
static UBYTE Hextodec(UBYTE c)
{  UBYTE ch=0;
   if(c>='0' && c<='9') ch=c-'0';
   else
   {  c=toupper(c);
      if(c>='A' && c<='F') ch=c+10-'A';
   }
   return ch;
}

/* Remove URL escape from block. Return new length (always <= old length) */
static long Unescape(UBYTE *block,long len)
{  UBYTE *start,*p,*end;
   long newlen=len;
   start=block;
   end=block+len;
   while(start<end)
   {  for(p=start;p<end && *p!='%';p++)
      {  if(*p=='+') *p=' ';
      }
      if(p<end)
      {  /* Now *p=='%' */
         if(p<end-2)
         {  *p=(Hextodec(p[1])<<4)+Hextodec(p[2]);
            memmove(p+1,p+3,end-p-3);
            newlen-=2;
         }
      }
      start=p+1;
   }
   return newlen;
}

/* Append this block with dangerous characters HTML escaped. */
static void Addblock(struct Buffer *buf,UBYTE *block,long len)
{  UBYTE *start,*p,*end,*esc;
   start=block;
   end=block+len;
   while(start<end)
   {  for(p=start;p<end && *p!='<' && *p!='&' && *p!='\'';p++);
      Addtobuffer(buf,start,p-start);
      if(p<end)
      {  switch(*p)
         {  case '<':esc="&lt;";break;
            case '&':esc="&amp;";break;
            case '\'':esc="&#39;";break;
            default: esc="";break;
         }
         Addtobuffer(buf,esc,strlen(esc));
         p++;  /* Skip over dangerous character */
      }
      start=p;
   }
}

/* Build mail form in HTML format */
static void Buildmailform(struct Fetchdriver *fd)
{  UBYTE *to,*p,*q,*subject=NULL;
   UBYTE *params[2];
   ULONG file;
   struct Buffer buf={0};
   long len,subjlen;
   short i;
   BOOL first,formmail;
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      TAG_END);
   to=fd->name+7; /* skip mailto: */
   if(p=strchr(to,'?'))
   {  *p++=0;
      params[0]=p;
   }
   else params[0]=NULL;
   params[1]=fd->postmsg;
   for(i=0;i<2;i++)
   {  if(params[i])
      {  p=params[i];
         for(;;)
         {  if(STRNIEQUAL(p,"SUBJECT=",8))
            {  subject=p+8;
               if(q=strchr(subject,'&')) subjlen=q-subject;
               else
               {  subjlen=strlen(subject);
                  if(p==params[i]) params[i]=NULL; /* subject is only form parameter */
               }
               break;
            }
            if(q=strchr(p,'&')) p=q+1;
            else break;
         }
      }
   }
   formmail=(params[0] || params[1]);
   p=fd->block;
   p+=sprintf(p,"<HTML>\n<HEAD>\n<TITLE>");
   p+=sprintf(p,AWEBSTR(MSG_MAIL_MAILTO_TITLE),to);
   p+=sprintf(p,"</TITLE>\n</HEAD>\n<BODY>\n<H1>");
   p+=sprintf(p,AWEBSTR(MSG_MAIL_MAILTO_HEADER),to);
   p+=sprintf(p,"</H1>\n<FORM ACTION='x-aweb:mail/1/mail' METHOD=POST>\n");
   p+=sprintf(p,"<TABLE>\n<TR>\n<TD ALIGN='right'>%s\n"
      "<TD><INPUT NAME='to' VALUE='%s' SIZE=60>\n",AWEBSTR(MSG_MAIL_TO),to);
   p+=sprintf(p,"<TR>\n<TD ALIGN='right'>%s\n"
      "<TD><INPUT NAME='subject' VALUE='",AWEBSTR(MSG_MAIL_SUBJECT));
   if(subject)
   {  Addtobuffer(&buf,fd->block,p-fd->block);
      subjlen=Unescape(subject,subjlen);
      Addblock(&buf,subject,subjlen);
      p=fd->block;
   }
   else
   {  p+=sprintf(p,"mailto:%s",to);
   }
   p+=sprintf(p,"' SIZE=60>\n");
   p+=sprintf(p,"</TABLE>\n%s"
      " (<INPUT TYPE=CHECKBOX NAME='extra' VALUE='Y'%s> %s)"
      "<BR>\n<TEXTAREA NAME='data' COLS=70 ROWS=20>\n",
      AWEBSTR(MSG_MAIL_MESSAGE_BODY),
      formmail?" CHECKED":"",
      AWEBSTR(MSG_MAIL_EXTRA_HEADERS));
   if(formmail)
   {  p+=sprintf(p,"Content-type: application/x-www-form-urlencoded\n\n");
      Addtobuffer(&buf,fd->block,p-fd->block);
      first=TRUE;
      for(i=0;i<2;i++)
      {  while(params[i] && *params[i])
         {  if(!(q=strchr(params[i],'&'))) q=params[i]+strlen(params[i]);
            if(!STRNIEQUAL(params[i],"SUBJECT=",8))
            {  if(!first) Addblock(&buf,"&",1);
               first=FALSE;
               Addblock(&buf,params[i],q-params[i]);
            }
            params[i]=q;
            if(*params[i]) params[i]++;
         }
         p=fd->block;
      }
   }
   else
   {  ObtainSemaphore(fd->prefssema);
      if(fd->prefs->sigfile)
      {  p+=sprintf(p,"&#10;\n--\n");
         Addtobuffer(&buf,fd->block,p-fd->block);
         if(file=Open(fd->prefs->sigfile,MODE_OLDFILE))
         {  first=TRUE;
            while(len=Read(file,fd->block,fd->blocksize))
            {  if(first && len>3 && STRNEQUAL(fd->block,"--\n",3))
               {  Addblock(&buf,fd->block+3,len-3);
               }
               else
               {  Addblock(&buf,fd->block,len);
               }
               first=FALSE;
            }
            Close(file);
         }
         p=fd->block;
      }
      ReleaseSemaphore(fd->prefssema);
   }
   p+=sprintf(p,"</TEXTAREA>\n<BR><BR>\n<INPUT TYPE=SUBMIT VALUE='%s'> "
      "<INPUT TYPE=RESET VALUE='%s'> "
      "<INPUT TYPE=SUBMIT NAME='Return' VALUE='%s'>\n",
      AWEBSTR(MSG_MAIL_SEND),AWEBSTR(MSG_MAIL_RESET),AWEBSTR(MSG_MAIL_RETURN));
   p+=sprintf(p,"</FORM>\n</BODY>\n</HTML>\n");
   Addtobuffer(&buf,fd->block,p-fd->block);
   Updatetaskattrs(
      AOURL_Data,buf.buffer,
      AOURL_Datalength,buf.length,
      TAG_END);
   Freebuffer(&buf);
}

/*-----------------------------------------------------------------------*/

struct Mailinfo
{  struct Buffer buf;
   UBYTE *to;
};

/* See if mail form was submitted from RETURN button */
static BOOL Findreturn(struct Fetchdriver *fd)
{  UBYTE *p;
   p=fd->postmsg;
   while(p=strchr(p,'&'))
   {  if(STRNIEQUAL(p,"&RETURN=",8)) return TRUE;
      p++;
   }
   return FALSE;
}

/* Build the mail message. Expects form fields in order:
 * To, Subject, [Inreplyto], [extra=y], Data. */
static void Buildmessage(struct Fetchdriver *fd,struct Mailinfo *mi)
{  UBYTE *p,*q,*start;
   long len;
   p=fd->postmsg;
   if(!STRNIEQUAL(p,"TO=",3)) return;
   p+=3;
   if(!(q=strchr(p,'&'))) return;
   *q++='\0';
   len=Unescape(p,q-p);
   mi->to=Dupstr(p,len);
   ObtainSemaphore(fd->prefssema);
   len=sprintf(fd->block,"From: %s (%s)\r\n",fd->prefs->emailaddr,fd->prefs->fullname);
   Addtobuffer(&mi->buf,fd->block,len);
   len=sprintf(fd->block,"To: %s\r\n",mi->to);
   Addtobuffer(&mi->buf,fd->block,len);
   p=q;
   if(!STRNIEQUAL(p,"SUBJECT=",8))
   {  ReleaseSemaphore(fd->prefssema);
      return;
   }
   p+=8;
   if(!(q=strchr(p,'&')))
   {  ReleaseSemaphore(fd->prefssema);
      return;
   }
   *q++='\0';
   Unescape(p,q-p);
   len=sprintf(fd->block,"Subject: %s\r\n",p);
   Addtobuffer(&mi->buf,fd->block,len);
   if(*fd->prefs->replyaddr)
   {  len=sprintf(fd->block,"Reply-to: %s\r\n",fd->prefs->replyaddr);
      Addtobuffer(&mi->buf,fd->block,len);
   }
   ReleaseSemaphore(fd->prefssema);
   p=q;
   if(STRNIEQUAL(p,"INREPLYTO=",10))
   {  p+=10;
      if(!(q=strchr(p,'&'))) return;
      *q++='\0';
      Unescape(p,q-p);
      len=sprintf(fd->block,"In-Reply-To: %s\r\n",p);
      Addtobuffer(&mi->buf,fd->block,len);
      p=q;
   }
   len=sprintf(fd->block,"X-Mailer: Amiga-AWeb/%s\r\n",Awebversion());
   Addtobuffer(&mi->buf,fd->block,len);
   if(STRNIEQUAL(p,"EXTRA=Y&",8))
   {  p+=8;
   }
   else
   {  Addtobuffer(&mi->buf,"\r\n",2);
   }
   if(!STRNIEQUAL(p,"DATA=",5)) return;
   p+=5;
   len=Unescape(p,strlen(p));
   p[len]='\0';
   start=p;
   for(;;)
   {  if(*p=='.')
      {  Addtobuffer(&mi->buf,start,p-start);
         Addtobuffer(&mi->buf,".",1);
         start=p;
      }
      if(!(q=strchr(p,'\r'))) break;
      q+=2; /* Skip over \r\n */
      p=q;
   }
   Addtobuffer(&mi->buf,start,strlen(start));
   p=mi->buf.buffer+mi->buf.length-2;
   if(!(p[0]=='\r' && p[1]=='\n'))
   {  Addtobuffer(&mi->buf,"\r\n",2);
   }
}

/* Show mail error requester. Return TRUE if retry requested. */
static BOOL Mailretry(struct Fetchdriver *fd,struct Mailinfo *mi,short err)
{  struct FileRequester *afr;
   struct Screen *scr;
   long result,file,height;
   struct DimensionInfo dim={0};
   if(err==SMR_NOCONNECT)
   {  ObtainSemaphore(fd->prefssema);
      if(*fd->prefs->smtphost)
      {  sprintf(fd->block,AWEBSTR(MSG_MAIL_NOCONNECT),fd->prefs->smtphost);
      }
      else
      {  strcpy(fd->block,AWEBSTR(MSG_MAIL_NOSMTPHOST));
      }
      ReleaseSemaphore(fd->prefssema);
   }
   else
   {  strcpy(fd->block,AWEBSTR(MSG_MAIL_MAILFAILED));
   }
   result=Syncrequest(AWEBSTR(MSG_MAIL_TITLE),fd->block,AWEBSTR(MSG_MAIL_BUTTONS));
   if(result==2) /* Save */
   {  if(scr=(struct Screen *)Agetattr(Aweb(),AOAPP_Screen))
      {  if(GetDisplayInfoData(NULL,(UBYTE *)&dim,sizeof(dim),DTAG_DIMS,
            GetVPModeID(&scr->ViewPort)))
         {  height=(dim.Nominal.MaxY-dim.Nominal.MinY+1)*4/5;
         }
         else
         {  height=scr->Height*4/5;
         }
         if(afr=AllocAslRequestTags(ASL_FileRequest,
            ASLFR_Screen,scr,
            ASLFR_TitleText,AWEBSTR(MSG_MAIL_SAVETITLE),
            ASLFR_InitialHeight,height,
            ASLFR_InitialDrawer,"RAM:",
            ASLFR_InitialFile,"MailMessage",
            ASLFR_RejectIcons,TRUE,
            ASLFR_DoSaveMode,TRUE,
            TAG_END))
         {  if(AslRequest(afr,NULL))
            {  strcpy(fd->block,afr->fr_Drawer);
               AddPart(fd->block,afr->fr_File,fd->blocksize);
               if(file=Open(fd->block,MODE_OLDFILE))
               {  Seek(file,0,OFFSET_END);
               }
               else
               {  file=Open(fd->block,MODE_NEWFILE);
               }
               if(file)
               {  Write(file,mi->buf.buffer,mi->buf.length);
                  Close(file);
               }
            }
            FreeAslRequest(afr);
         }
      }
      result=0;
   }
   return (BOOL)(result==1);
}

/* Connect and transfer the message (SMTP with AUTH/TLS via mailimap.c) */
static short Sendmessage(struct Fetchdriver *fd,struct Mailinfo *mi)
{  return Mailsmtp_send(fd,mi->to,mi->buf.buffer,mi->buf.length);
}

/* Build result page */
static void Buildresult(struct Fetchdriver *fd,struct Mailinfo *mi)
{  long len;
   Freebuffer(&mi->buf);
   len=sprintf(fd->block,"<HTML>\n<HEAD>\n<TITLE>");
   Addtobuffer(&mi->buf,fd->block,len);
   len=sprintf(fd->block,AWEBSTR(MSG_MAIL_MAIL_SENT_TITLE),mi->to);
   Addtobuffer(&mi->buf,fd->block,len);
   len=sprintf(fd->block,"</TITLE>\n</HEAD>\n<BODY>\n");
   Addtobuffer(&mi->buf,fd->block,len);
   len=sprintf(fd->block,AWEBSTR(MSG_MAIL_MAIL_SENT),mi->to);
   Addtobuffer(&mi->buf,fd->block,len);
   len=sprintf(fd->block,"<p>\n<FORM ACTION='x-aweb:mail/2'>\n<INPUT TYPE=SUBMIT VALUE='%s'>\n</FORM>\n",
      AWEBSTR(MSG_MAIL_RETURN));
   Addtobuffer(&mi->buf,fd->block,len);
   len=sprintf(fd->block,"</BODY>\n</HTML>\n");
   Addtobuffer(&mi->buf,fd->block,len);
   Updatetaskattrs(
      AOURL_Contenttype,"text/html",
      AOURL_Data,mi->buf.buffer,
      AOURL_Datalength,mi->buf.length,
      TAG_END);
}

/* Send mail */
static void Sendmail(struct Fetchdriver *fd)
{  struct Mailinfo mi={0};
   short r;
   if(Findreturn(fd))
   {  Updatetaskattrs(
         AOURL_Gohistory,-1,
         TAG_END);
   }
   else
   {  Buildmessage(fd,&mi);
      for(;;)
      {  r=Sendmessage(fd,&mi);
         if(r==SMR_OK) break;
         if(!Mailretry(fd,&mi,r)) break;
      }
      if(r==SMR_OK)
      {  Buildresult(fd,&mi);
      }
      Freebuffer(&mi.buf);
      if(mi.to) FREE(mi.to);
   }
}

/*-----------------------------------------------------------------------*/
/* Embedded mail client UI (IMAP folders / index / message) */

static struct Mailfolder Mail_folder_cache[MAIL_MAX_FOLDERS];
static long Mail_folder_cache_count;
static BOOL Mail_folder_cache_valid;

static void Mail_bprintf(struct Buffer *buf,UBYTE *scratch,UBYTE *fmt,...)
{  va_list args;
   long len;
   va_start(args,fmt);
   vsprintf(scratch,fmt,args);
   va_end(args);
   len=strlen(scratch);
   Addtobuffer(buf,scratch,len);
}

static void Mail_addblock(struct Buffer *buf,UBYTE *block,long len)
{  UBYTE *start;
   UBYTE *p;
   UBYTE *end;
   UBYTE *esc;
   if(len<0) len=strlen(block);
   start=block;
   end=block+len;
   while(start<end)
   {  for(p=start;p<end && *p!='<' && *p!='&' && *p!='\'';p++);
      Addtobuffer(buf,start,p-start);
      if(p<end)
      {  switch(*p)
         {  case '<':esc="&lt;";break;
            case '&':esc="&amp;";break;
            case '\'':esc="&#39;";break;
            default:esc="";break;
         }
         Addtobuffer(buf,esc,strlen(esc));
         p++;
      }
      start=p;
   }
}

static void Mail_cell_truncate(UBYTE *dst,long dstmax,UBYTE *src,long maxshow)
{  long n;
   if(!dst || dstmax<=0) return;
   dst[0]='\0';
   if(!src || !*src) return;
   n=strlen(src);
   if(n>maxshow) n=maxshow;
   if(n>=dstmax) n=dstmax-1;
   memcpy(dst,src,n);
   dst[n]='\0';
   if((long)strlen(src)>maxshow && dstmax>4)
   {  if(n>dstmax-4) n=dstmax-4;
      dst[n]='\0';
      strcat(dst,"...");
   }
}

static void Mail_short_date(UBYTE *dst,long dstmax,UBYTE *date)
{  UBYTE *p;
   UBYTE *q;
   UBYTE *r;
   if(!dst || dstmax<=0) return;
   dst[0]='\0';
   if(!date || !*date) return;
   p=strchr(date,',');
   if(p)
   {  p++;
      while(*p==' ') p++;
      strncpy(dst,p,dstmax-1);
   }
   else
   {  strncpy(dst,date,dstmax-1);
   }
   dst[dstmax-1]='\0';
   /* Drop :SS seconds if present (e.g. "05 Mar 2025 12:34:56 +0000"). */
   r=dst+strlen(dst);
   while(r>dst && (*r==' ' || *r=='+' || *r=='-' || (*r>='0' && *r<='9'))) r--;
   if(r>dst && *r>='0' && *r<='9')
   {  q=r;
      while(q>dst && *q>='0' && *q<='9') q--;
      if(q>dst && *q==':')
      {  *q='\0';
         while(q>dst && *(q-1)==' ') q--;
      }
   }
}

/* Message index: full-width table rows (subject / from / date). */
static void Mail_index_table_start(struct Buffer *buf,UBYTE *scratch)
{  Mail_bprintf(buf,scratch,
      "<HTML><HEAD><TITLE>Mail</TITLE></HEAD>\n"
      "<BODY TOPMARGIN=0 LEFTMARGIN=0 MARGINWIDTH=0 MARGINHEIGHT=0>\n"
      "<TABLE BORDER=1 WIDTH=\"100%%\" CELLPADDING=2 CELLSPACING=0>\n");
}

static void Mail_index_append_rows(struct Buffer *buf,UBYTE *scratch,
   UBYTE *enc,struct Mailuid *items,long nitems,long start,long count,
   unsigned long *uids)
{  long i;
   long k;
   UBYTE subj[72];
   UBYTE from[48];
   UBYTE sdate[24];
   (void)uids;
   (void)start;
   (void)count;
   for(i=count-1;i>=start;i--)
   {  k=i-start;
      if(k>=nitems) continue;
      Mail_cell_truncate(subj,sizeof(subj),items[k].subject,56);
      Mail_cell_truncate(from,sizeof(from),items[k].from,36);
      Mail_short_date(sdate,sizeof(sdate),items[k].date);
      Mail_bprintf(buf,scratch,"<TR VALIGN=TOP><TD>");
      if(items[k].unseen) Mail_bprintf(buf,scratch,"* ");
      Mail_bprintf(buf,scratch,"<A HREF='mail:%s/uid/%lu' TARGET='MailMessage'>",enc,
         items[k].uid);
      Mail_addblock(buf,subj,-1);
      Mail_bprintf(buf,scratch,"</A><BR>");
      Mail_addblock(buf,from,-1);
      if(sdate[0])
      {  Mail_bprintf(buf,scratch,"<BR>");
         Mail_addblock(buf,sdate,-1);
      }
      Mail_bprintf(buf,scratch,"</TD></TR>\n");
   }
}

static void Mail_index_table_end(struct Buffer *buf,UBYTE *scratch)
{  Mail_bprintf(buf,scratch,"</TABLE></BODY></HTML>\n");
}

static void Mail_sort_folders(struct Mailfolder *folders,long count)
{  struct Mailfolder tmp;
   long i;
   long j;
   for(i=0;i<count-1;i++)
   {  for(j=i+1;j<count;j++)
      {  BOOL swap;
         swap=FALSE;
         if(STRIEQUAL(folders[i].name,"INBOX")) continue;
         if(STRIEQUAL(folders[j].name,"INBOX"))
         {  swap=TRUE;
         }
         else if(stricmp(folders[i].name,folders[j].name)>0)
         {  swap=TRUE;
         }
         if(swap)
         {  tmp=folders[i];
            folders[i]=folders[j];
            folders[j]=tmp;
         }
      }
   }
}

static void Mail_refresh_folder_cache(struct Fetchdriver *fd)
{  struct Mailconn mc={0};
   long i;
   mc.fd=fd;
   Mail_folder_cache_valid=FALSE;
   Mail_folder_cache_count=0;
   if(Mailimap_open(&mc,FALSE))
   {  Mail_folder_cache_count=Mailimap_list(&mc,Mail_folder_cache,MAIL_MAX_FOLDERS);
      Mailimap_close(&mc);
      Mail_debug("Mail_refresh_folder_cache: LIST count=%ld",Mail_folder_cache_count);
   }
   if(Mail_folder_cache_count<=0)
   {  strcpy(Mail_folder_cache[0].name,"INBOX");
      Mail_folder_cache_count=1;
   }
   else
   {  long n;
      n=0;
      for(i=0;i<Mail_folder_cache_count;i++)
      {  if(Mail_folder_cache[i].name[0]=='/'
            && Mail_folder_cache[i].name[1]=='\0') continue;
         if(Mail_folder_cache[i].name[0]=='.'
            && Mail_folder_cache[i].name[1]=='\0') continue;
         if(n!=i) Mail_folder_cache[n]=Mail_folder_cache[i];
         n++;
      }
      Mail_folder_cache_count=n;
      if(Mail_folder_cache_count<=0)
      {  strcpy(Mail_folder_cache[0].name,"INBOX");
         Mail_folder_cache_count=1;
      }
      Mail_sort_folders(Mail_folder_cache,Mail_folder_cache_count);
   }
   Mail_folder_cache_valid=TRUE;
   for(i=0;i<Mail_folder_cache_count;i++)
   {  Mail_debug("Mail folder cache[%ld]=%s",i,Mail_folder_cache[i].name);
   }
}

static void Mail_showfolders(struct Fetchdriver *fd)
{  struct Buffer buf={0};
   long nfolders;
   long i;
   UBYTE enc[128];
   UBYTE label[96];
   (void)fd;
   if(!Mail_folder_cache_valid)
   {  strcpy(Mail_folder_cache[0].name,"INBOX");
      Mail_folder_cache_count=1;
      Mail_folder_cache_valid=TRUE;
   }
   nfolders=Mail_folder_cache_count;
   Updatetaskattrs(AOURL_Contenttype,"text/html",TAG_END);
   Mail_bprintf(&buf,fd->block,
      "<HTML><HEAD><TITLE>Mail</TITLE></HEAD>\n"
      "<BODY TOPMARGIN=0 LEFTMARGIN=0 MARGINWIDTH=0 MARGINHEIGHT=0>\n"
      "<TABLE BORDER=1 WIDTH=\"100%%\" CELLPADDING=3 CELLSPACING=0>\n");
   for(i=0;i<nfolders;i++)
   {  Mail_encode_folder(Mail_folder_cache[i].name,enc,sizeof(enc));
      Mail_folder_label(Mail_folder_cache[i].name,label,sizeof(label));
      Mail_bprintf(&buf,fd->block,"<TR><TD WIDTH=\"100%%\">");
      Mail_bprintf(&buf,fd->block,
         "<A HREF='x-aweb:mail/6/%s' TARGET='MailIndex'>",enc);
      Mail_addblock(&buf,label,-1);
      Mail_bprintf(&buf,fd->block,"</A></TD></TR>\n");
   }
   Mail_bprintf(&buf,fd->block,"</TABLE>\n");
   Mail_bprintf(&buf,fd->block,
      "<P><A HREF='x-aweb:mail/compose' TARGET='MailCompose'>Compose new message</A></P>\n");
   Mail_bprintf(&buf,fd->block,"</BODY></HTML>\n");
   Updatetaskattrs(AOURL_Data,buf.buffer,AOURL_Datalength,buf.length,TAG_END);
   Freebuffer(&buf);
}

static void Mail_showindex(struct Fetchdriver *fd,UBYTE *folder)
{  struct Mailconn mc={0};
   struct Buffer buf={0};
   unsigned long uids[MAIL_MAX_UIDS];
   struct Mailuid *items;
   long count;
   long i;
   long maxfetch;
   long start;
   long nfetch;
   long k;
   UBYTE enc[128];
   BOOL opened;
   BOOL selected;
   BOOL list_built;
   BOOL oom;
   BOOL cancelled;
   mc.fd=fd;
   if(!folder || !*folder) folder=(UBYTE *)"INBOX";
   ObtainSemaphore(fd->prefssema);
   maxfetch=fd->prefs->maxartnews;
   if(maxfetch<=0) maxfetch=100;
   ReleaseSemaphore(fd->prefssema);
   Mail_debug("Mail_showindex folder=%s url=%s",folder,fd->name?fd->name:(UBYTE *)"");
   Updatetaskattrs(AOURL_Contenttype,"text/html",TAG_END);
   Mail_encode_folder(folder,enc,sizeof(enc));
   count=0;
   opened=FALSE;
   selected=FALSE;
   list_built=FALSE;
   oom=FALSE;
   cancelled=FALSE;
   /* Wait for any prior IMAP fetch to finish (do not steal the session lock). */
   if(Mailimap_open(&mc,FALSE))
   {  opened=TRUE;
      if(Mailimap_select(&mc,folder))
      {  selected=TRUE;
         count=Mailimap_search_all(&mc,uids,MAIL_MAX_UIDS);
         Mail_debug("Mail_showindex UID SEARCH count=%ld",count);
         start=count-maxfetch;
         if(start<0) start=0;
         nfetch=count-start;
         if(nfetch>0)
         {  items=ALLOCTYPE(struct Mailuid,nfetch,MEMF_CLEAR);
            if(items)
            {  for(i=count-1;i>=start;i--)
               {  k=i-start;
                  if(Checktaskbreak())
                  {  cancelled=TRUE;
                     break;
                  }
                  items[k].uid=uids[i];
                  Mailimap_fetch_envelope(&mc,items[k].uid,
                     items[k].subject,sizeof(items[k].subject),
                     items[k].from,sizeof(items[k].from),
                     items[k].date,sizeof(items[k].date),
                     &items[k].unseen);
               }
               if(!cancelled)
               {  Freebuffer(&buf);
                  Mail_index_table_start(&buf,fd->block);
                  Mail_index_append_rows(&buf,fd->block,enc,items,nfetch,start,count,uids);
                  Mail_index_table_end(&buf,fd->block);
                  list_built=TRUE;
                  Mail_debug("Mail_showindex: list built (%ld lines)",nfetch);
               }
               else
               {  Mail_debug("Mail_showindex: cancelled during fetch");
               }
               FREE(items);
            }
            else
            {  oom=TRUE;
            }
         }
         else
         {  Mail_debug("Mail_showindex: folder empty");
         }
      }
      else
      {  Mail_debug("Mail_showindex: SELECT failed for %s",folder);
      }
   }
   else
   {  Mail_debug("Mail_showindex: IMAP open failed");
   }
   if(!list_built)
   {  Freebuffer(&buf);
      Mail_index_table_start(&buf,fd->block);
      Mail_bprintf(&buf,fd->block,"<TR><TD>");
      if(cancelled)
      {  Mail_bprintf(&buf,fd->block,"Message list load cancelled.");
      }
      else if(oom)
      {  Mail_bprintf(&buf,fd->block,"Out of memory loading message list.");
      }
      else if(!opened)
      {  Mail_bprintf(&buf,fd->block,
            "IMAP connect/login failed (see AWeb log if MAIL_DEBUG).");
      }
      else if(!selected)
      {  UBYTE flabel[96];
         Mail_folder_label(folder,flabel,sizeof(flabel));
         Mail_bprintf(&buf,fd->block,"IMAP SELECT failed for folder \"%s\".",flabel);
      }
      else if(opened && selected && count==0)
      {  Mail_bprintf(&buf,fd->block,"(No messages in this folder.)");
      }
      Mail_bprintf(&buf,fd->block,"</TD></TR>\n");
      Mail_index_table_end(&buf,fd->block);
   }
   Updatetaskattrs(AOURL_Data,buf.buffer,AOURL_Datalength,buf.length,TAG_END);
   Mail_debug("Mail_showindex: document sent (%ld bytes)",buf.length);
   if(opened)
   {  Mailimap_close(&mc);
   }
   Freebuffer(&buf);
}

static void Mail_showfolder(struct Fetchdriver *fd,UBYTE *folder)
{  BOOL framed;
   struct Buffer buf={0};
   UBYTE enc[128];
   UBYTE label[96];
   ObtainSemaphore(fd->prefssema);
   framed=fd->prefs->framednews && fd->prefs->doframes;
   ReleaseSemaphore(fd->prefssema);
   if(!folder || !*folder) folder=(UBYTE *)"INBOX";
   Mail_encode_folder(folder,enc,sizeof(enc));
   Mail_folder_label(folder,label,sizeof(label));
   if(framed)
   {  Mail_refresh_folder_cache(fd);
      Mail_bprintf(&buf,fd->block,
         "<HTML><HEAD><TITLE>Mail - %s</TITLE></HEAD>\n"
         "<FRAMESET COLS='18%%,32%%,50%%'>\n"
         " <FRAME SRC='x-aweb:mail/folders' NAME='MailFolders'>\n"
         " <FRAME SRC='x-aweb:mail/6/%s' NAME='MailIndex'>\n"
         " <FRAME NAME='MailMessage'>\n"
         "</FRAMESET><NOFRAMES><BODY>%s</BODY></NOFRAMES></HTML>\n",
         label,enc,"This page requires frames.");
      Updatetaskattrs(AOURL_Contenttype,"text/html",
         AOURL_Data,buf.buffer,AOURL_Datalength,buf.length,TAG_END);
      Freebuffer(&buf);
   }
   else
   {  Mail_showindex(fd,folder);
   }
}

static void Mail_showmessage(struct Fetchdriver *fd,UBYTE *folder,unsigned long uid)
{  struct Mailconn mc={0};
   struct Buffer err={0};
   UBYTE *body;
   long bodylen;
   BOOL ok;
   mc.fd=fd;
   if(!folder || !*folder) folder=(UBYTE *)"INBOX";
   body=NULL;
   bodylen=0;
   ok=FALSE;
   if(Mail_cache_read(fd,folder,uid,&body,&bodylen))
   {  ok=TRUE;
   }
   else
   {  /* FALSE = wait for index/other IMAP use to finish (do not fail as "busy"). */
      if(Mailimap_open(&mc,FALSE))
      {  if(Mailimap_select(&mc,folder))
         {  if(Mailimap_fetch_body(&mc,uid,&body,&bodylen))
            {  Mail_cache_write(fd,folder,uid,body,bodylen);
               Mailimap_store_seen(&mc,uid);
               ok=TRUE;
            }
            else
            {  Mail_debug("Mail_showmessage: BODY fetch failed uid=%lu",uid);
            }
         }
         else
         {  Mail_debug("Mail_showmessage: SELECT failed folder=%s",folder);
         }
         Mailimap_close(&mc);
      }
      else
      {  Mail_debug("Mail_showmessage: IMAP open failed uid=%lu",uid);
         /* Recover if a prior fetch left the global IMAP lock held. */
         Mail_imap_release_stale();
      }
   }
   /* Deliver raw RFC822; source.c runs Filterplugin() via message/rfc822 MIME.
    * Copy into a delivery buffer: Updatetaskattrs is synchronous but the filter
    * may retain pointers only until it copies, and we must not FREE the IMAP
    * buffer until the source task has finished reading this chunk. */
   if(ok && body)
   {  UBYTE *deliver;
      deliver=ALLOCTYPE(UBYTE,bodylen,MEMF_PUBLIC);
      if(deliver)
      {  memcpy(deliver,body,bodylen);
         FREE(body);
         body=NULL;
         Updatetaskattrs(AOURL_Contenttype,"message/rfc822",
            AOURL_Data,deliver,
            AOURL_Datalength,bodylen,
            TAG_END);
         FREE(deliver);
      }
      else
      {  Updatetaskattrs(AOURL_Contenttype,"message/rfc822",
            AOURL_Data,body,
            AOURL_Datalength,bodylen,
            TAG_END);
         FREE(body);
      }
   }
   else
   {  Updatetaskattrs(AOURL_Contenttype,"text/html",TAG_END);
      Mail_bprintf(&err,fd->block,"<HTML><BODY><H1>Could not load message</H1></BODY></HTML>\n");
      Updatetaskattrs(AOURL_Data,err.buffer,AOURL_Datalength,err.length,TAG_END);
      Freebuffer(&err);
   }
}

static void Mail_mainpage(struct Fetchdriver *fd)
{  struct Buffer buf={0};
   UBYTE enc[128];
   Mail_encode_folder((UBYTE *)"INBOX",enc,sizeof(enc));
   Updatetaskattrs(AOURL_Contenttype,"text/html",TAG_END);
   Mail_bprintf(&buf,fd->block,
      "<HTML><HEAD><TITLE>AWeb Mail</TITLE></HEAD><BODY>\n<H1>AWeb Mail</H1>\n");
   Mail_bprintf(&buf,fd->block,"<P><A HREF='mail:%s'>Open Inbox</A></P>\n",enc);
   Mail_bprintf(&buf,fd->block,
      "<P><A HREF='x-aweb:mail/compose' TARGET='MailCompose'>Compose new message</A></P>\n");
   Mail_bprintf(&buf,fd->block,"</BODY></HTML>\n");
   Updatetaskattrs(AOURL_Data,buf.buffer,AOURL_Datalength,buf.length,TAG_END);
   Freebuffer(&buf);
}

static UBYTE mail_compose_url[280];

static void Mail_compose(struct Fetchdriver *fd,UBYTE *to,UBYTE *subject)
{  strcpy(mail_compose_url,"mailto:");
   if(to && *to)
   {  strncat(mail_compose_url,to,sizeof(mail_compose_url)-8);
      if(subject && *subject)
      {  strcat(mail_compose_url,"?subject=");
         strncat(mail_compose_url,subject,sizeof(mail_compose_url)-strlen(mail_compose_url)-1);
      }
   }
   fd->name=mail_compose_url;
   Buildmailform(fd);
}

/*-----------------------------------------------------------------------*/

__saveds __asm void Fetchdrivertask(register __a0 struct Fetchdriver *fd)
{  UBYTE folder[128];
   unsigned long uid;
   BOOL has_uid;
   Mail_debug("Mail fetch: %s",fd->name?fd->name:(UBYTE *)"");
   if(STRNIEQUAL(fd->name,"mailto:",7))
   {  Buildmailform(fd);
   }
   else if(STRNIEQUAL(fd->name,"mail:",5))
   {  if(Mail_parse_url(fd->name,folder,sizeof(folder),&uid,&has_uid))
      {  Mail_debug("Mail_parse folder=%s uid=%lu has_uid=%d",
            folder,uid,(int)has_uid);
         if(!fd->name[5])
         {  Mail_showfolder(fd,(UBYTE *)"INBOX");
         }
         else if(has_uid)
         {  Mail_showmessage(fd,folder,uid);
         }
         else
         {  Mail_showfolder(fd,folder);
         }
      }
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:mail/folders",19))
   {  Mail_showfolders(fd);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:mail/6/",14))
   {  Mail_decode_folder(fd->name+14,folder,sizeof(folder));
      Mail_showindex(fd,folder);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:mail/compose",19))
   {  Mail_compose(fd,NULL,NULL);
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:mail/1/mail",18))
   {  if(fd->referer
      && (STRNIEQUAL(fd->referer,"mailto:",7)
         || STRNIEQUAL(fd->referer,"x-aweb:news/reply?",18)
         || STRNIEQUAL(fd->referer,"x-aweb:mail/compose",19)))
      {  Sendmail(fd);
      }
   }
   else if(STRNIEQUAL(fd->name,"x-aweb:mail/2",13))
   {  Updatetaskattrs(
         AOURL_Gohistory,-2,
         TAG_END);
   }
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

/*-----------------------------------------------------------------------*/

static ULONG Initaweblib(struct Library *libbase)
{  if(!(DOSBase=(struct DosLibrary *)OpenLibrary("dos.library",39))) return FALSE;
   if(!(AslBase=OpenLibrary("asl.library",OSNEED(39,44)))) return FALSE;
   if(!(GfxBase=(struct GfxBase *)OpenLibrary("graphics.library",39))) return FALSE;
   if(!(UtilityBase=OpenLibrary("utility.library",39))) return FALSE;
   return TRUE;
}

static void Expungeaweblib(struct Library *libbase)
{  if(UtilityBase) CloseLibrary(UtilityBase);
   if(GfxBase) CloseLibrary(GfxBase);
   if(AslBase) CloseLibrary(AslBase);
   if(DOSBase) CloseLibrary(DOSBase);
}

#endif /* !LOCALONLY */

