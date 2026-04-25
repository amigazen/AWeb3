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

/* file.c - AWeb temporary file object */

#include "aweb.h"
#include "file.h"
#include <dos/stdio.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/icon.h>

/*------------------------------------------------------------------------*/

struct File
{  struct Aobject object;
   long fh;                      /* File handle or NULL if closed. */
   USHORT icontype;
   UBYTE *name;
   UBYTE *comment;
   USHORT flags;
   long length;
};

#define FILF_DELETE        0x0001   /* Delete file when disposed. */
#define FILF_NEW           0x0002   /* No file created yet. */
#define FILF_PIPE          0x0004   /* Create a PIPE file. */
#define FILF_ERROR         0x0008   /* Writing to file has errored. */
#define FILF_COMMON        0x0010   /* Attached to commonfiles list */

static struct SignalSemaphore filesema;
static long tempnum=0;

static LIST(File) commonfiles;

/*------------------------------------------------------------------------*/

static UBYTE *Makefilename(UBYTE *ext,BOOL pipe)
{  UBYTE *path,*name,buf[16];
   long len,num;
   ObtainSemaphore(&filesema);
   if(pipe) path="PIPE:";
   else path=prefs.temppath;
   len=strlen(path)+16;
   if(ext && *ext) len+=strlen(ext)+1;
   if(name=ALLOCTYPE(UBYTE,len,MEMF_PUBLIC))
   {  num=++tempnum;
      sprintf(buf,"AWTF%05d",num);
      strcpy(name,path);
      AddPart(name,buf,len);
      if(ext && *ext)
      {  strcat(name,".");
         strcat(name,ext);
      }
   }
   ReleaseSemaphore(&filesema);
   return name;
}

static void Saveiconfile(struct File *fil)
{  
#ifndef DEMOVERSION
   struct DiskObject *dob;
   if(prefs.saveicons && fil->icontype)
   {  /* Use GetDiskObjectNew() which queries DefIcons if running,
        * providing file-type-specific default icons */
      if(dob=GetDiskObjectNew(fil->name))
      {  if(fil->icontype==FILEICON_TEXT)
         {  dob->do_DefaultTool=programname;
         }
         PutDiskObject(fil->name,dob);
         FreeDiskObject(dob);
      }
   }
#endif
}

static void Deletefile(struct File *fil)
{  DeleteFile(fil->name);
   if(fil->icontype) DeleteDiskObject(fil->name);
}

/* Copy data from other file */
static void Copyfile(struct File *fil,UBYTE *name)
{  UBYTE *buffer=ALLOCTYPE(UBYTE,FILEBLOCKSIZE,MEMF_PUBLIC);
   long fh,read;
   if(buffer && name && (fh=Open(name,MODE_OLDFILE)))
   {  do
      {  read=Read(fh,buffer,FILEBLOCKSIZE);
         FWrite(fil->fh,buffer,1,read);
         fil->length+=read;
      } while(read==FILEBLOCKSIZE);
      Close(fh);
   }
   if(buffer) FREE(buffer);
}

/* Get the file size. */
static long Filesize(struct File *fil)
{  long size=0;
   long lock;
   struct FileInfoBlock *fib;
   if(fil->fh)
   {  size=fil->length;
   }
   else
   {  if(lock=Lock(fil->name,SHARED_LOCK))
      {  if(fib=AllocDosObjectTags(DOS_FIB,TAG_END))
         {  if(Examine(lock,fib))
            {  size=fib->fib_Size;
            }
            FreeDosObject(DOS_FIB,fib);
         }
         UnLock(lock);
      }
   }
   return size;
}

/* Get the date stamp */
static ULONG Datestamp(struct File *fil)
{  ULONG stamp=0;
   long lock;
   struct FileInfoBlock *fib;
   if(lock=Lock(fil->name,SHARED_LOCK))
   {  if(fib=AllocDosObjectTags(DOS_FIB,TAG_END))
      {  if(Examine(lock,fib))
         {  stamp=fib->fib_Date.ds_Days*86400 +
                  fib->fib_Date.ds_Minute*60 +
                  fib->fib_Date.ds_Tick/TICKS_PER_SECOND;
         }
         FreeDosObject(DOS_FIB,fib);
      }
      UnLock(lock);
   }
   return stamp;
}

/*------------------------------------------------------------------------*/

static long Setfile(struct File *fil,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   UBYTE *data=NULL,*ext=NULL,*copyfile=NULL;
   long length=0,clen,wlen;
   BOOL eof=FALSE,append=FALSE,common=FALSE;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFIL_Name:
            if(!fil->name) fil->name=Dupstr((UBYTE *)tag->ti_Data,-1);
            break;
         case AOFIL_Extension:
            ext=(UBYTE *)tag->ti_Data;
            break;
         case AOFIL_Data:
            data=(UBYTE *)tag->ti_Data;
            break;
         case AOFIL_Datalength:
            length=tag->ti_Data;
            if(data && length && fil->fh)
            {  wlen=FWrite(fil->fh,data,1,length);
               fil->length+=wlen;
               if(wlen!=length)
               {  fil->flags|=FILF_ERROR;
               }
            }
            data=NULL;
            break;
         case AOFIL_Eof:
            eof=TRUE;
            break;
         case AOFIL_Delete:
            if(tag->ti_Data) fil->flags|=FILF_DELETE;
            else fil->flags&=~FILF_DELETE;
            break;
         case AOFIL_Icontype:
            fil->icontype=tag->ti_Data;
            break;
         case AOFIL_Comment:
            if(fil->comment) FREE(fil->comment);
            clen=strlen((UBYTE *)tag->ti_Data);
            fil->comment=Dupstr((UBYTE *)tag->ti_Data,MIN(79,clen));
            break;
         case AOFIL_Append:
            append=BOOLVAL(tag->ti_Data);
            break;
         case AOFIL_Copyfile:
            copyfile=(UBYTE *)tag->ti_Data;
            if(copyfile && fil->fh)
            {  Copyfile(fil,copyfile);
            }
            break;
         case AOFIL_Stringdata:
            data=(UBYTE *)tag->ti_Data;
            if(data) length=strlen(data);
            if(data && length && fil->fh)
            {  FWrite(fil->fh,data,1,length);
               fil->length+=length;
            }
            data=NULL;
            break;
         case AOFIL_Pipe:
            if(tag->ti_Data)
            {  fil->flags&=~FILF_DELETE;
               fil->flags|=FILF_PIPE;
            }
            else
            {  fil->flags&=~FILF_PIPE;
            }
            break;
         case AOFIL_Commonfile:
            common=BOOLVAL(tag->ti_Data);
            break;
      }
   }
   if(fil->flags&FILF_NEW)
   {  if(!fil->name)
      {  fil->name=Makefilename(ext,BOOLVAL(fil->flags&FILF_PIPE));
         append=FALSE;
      }
      if(append && (fil->fh=Open(fil->name,MODE_OLDFILE)))
      {  Seek(fil->fh,0,OFFSET_END);
      }
      else if(fil->flags&FILF_PIPE)
      {  fil->fh=Open(fil->name,MODE_NEWFILE);
      }
      else
      {  fil->fh=Open(fil->name,MODE_NEWFILE);
         if(fil->fh) Close(fil->fh);
         fil->fh=Open(fil->name,MODE_OLDFILE);  // Open in shared mode
      }
      SetVBuf(fil->fh,NULL,BUF_FULL,2048);
      fil->flags&=~FILF_NEW;
   }
   if(eof && fil->fh)
   {  Close(fil->fh);
      fil->fh=NULL;
      if(fil->icontype && !(fil->flags&FILF_PIPE)) Saveiconfile(fil);
      if(fil->comment)
      {  SetComment(fil->name,fil->comment);
         FREE(fil->comment);
         fil->comment=NULL;
      }
   }
   if(common && !(fil->flags&FILF_COMMON))
   {  ObtainSemaphore(&filesema);
      ADDTAIL(&commonfiles,fil);
      ReleaseSemaphore(&filesema);
      fil->flags|=FILF_COMMON;
   }
   return 0;
}

static void Disposefile(struct File *fil)
{  if(fil->fh) Close(fil->fh);
   if(fil->flags&FILF_DELETE) Deletefile(fil);
   if(fil->name) FREE(fil->name);
   if(fil->comment) FREE(fil->comment);
   if(fil->flags&FILF_COMMON)
   {  ObtainSemaphore(&filesema);
      REMOVE(fil);
      ReleaseSemaphore(&filesema);
   }
   Amethodas(AOTP_OBJECT,fil,AOM_DISPOSE);
}

static struct File *Newfile(struct Amset *ams)
{  struct File *fil;
   if(fil=Allocobject(AOTP_FILE,sizeof(struct File),ams))
   {  fil->flags|=FILF_DELETE|FILF_NEW;
      Setfile(fil,ams);
      if(!fil->fh)
      {  Disposefile(fil);
         fil=NULL;
      }
   }
   return fil;
}

static long Getfile(struct File *fil,struct Amset *ams)
{  struct TagItem *tag,*tstate=ams->tags;
   while(tag=NextTagItem(&tstate))
   {  switch(tag->ti_Tag)
      {  case AOFIL_Name:
            PUTATTR(tag,fil->name);
            break;
         case AOFIL_Eof:
            PUTATTR(tag,!fil->fh && !(fil->flags&FILF_NEW));
            break;
         case AOFIL_Filesize:
            PUTATTR(tag,Filesize(fil));
            break;
         case AOFIL_Datestamp:
            PUTATTR(tag,Datestamp(fil));
            break;
         case AOFIL_Error:
            PUTATTR(tag,BOOLVAL(fil->flags&FILF_ERROR));
            break;
      }
   }
   return 0;
}

static void Deinstallfile(void)
{  struct File *fil;
   while(fil=REMHEAD(&commonfiles)) Adisposeobject(fil);
}

static long Dispatch(struct File *fil,struct Amessage *amsg)
{  long result=0;
   switch(amsg->method)
   {  case AOM_NEW:
         result=(long)Newfile((struct Amset *)amsg);
         break;
      case AOM_SET:
         result=Setfile(fil,(struct Amset *)amsg);
         break;
      case AOM_GET:
         result=Getfile(fil,(struct Amset *)amsg);
         break;
      case AOM_DISPOSE:
         Disposefile(fil);
         break;
      case AOM_DEINSTALL:
         Deinstallfile();
         break;
   }
   return result;
}

/*------------------------------------------------------------------------*/

BOOL Installfile(void)
{  InitSemaphore(&filesema);
   NEWLIST(&commonfiles);
   if(!Amethod(NULL,AOM_INSTALL,AOTP_FILE,Dispatch)) return FALSE;
   return TRUE;
}

void *Findcommonfile(ULONG id)
{  struct File *fil;
   ObtainSemaphore(&filesema);
   for(fil=commonfiles.first;fil->next;fil=fil->next)
   {  if((ULONG)fil==id) break;
   }
   ReleaseSemaphore(&filesema);
   if(!fil->next) fil=NULL;
   return fil;
}
