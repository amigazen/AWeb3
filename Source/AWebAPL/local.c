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

/* local.c aweb localfile */

#include "aweb.h"
#include "fetch.h"
#include "fetchdriver.h"
#include "tcperr.h"
#include "window.h"
#include "asyncio.h"
#include "task.h"
#include "form.h"
#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>

#include <string.h>
#include <stdio.h>

#ifdef DEVELOPER
extern long localblocksize;
#endif

/* Maximum directory entries to display */
#define MAX_DIR_ENTRIES 256
/* GetFileIcon: max .info URL length (fits url_copy[] and icon_buf+200..511 build zone). */
#define GETFILEICON_URLLIMIT 255

/*-----------------------------------------------------------------------*/

/* Copy string, but escape HTML characters */
static long Htmlmove(UBYTE *to,UBYTE *from,long len)
{  long n=0;
   for(;len;from++,len--)
   {  switch(*from)
      {  case '<':
            strcpy(to,"&lt;");
            to+=4;
            n+=4;
            break;
         case '>':
            strcpy(to,"&gt;");
            to+=4;
            n+=4;
            break;
         case '&':
            strcpy(to,"&amp;");
            to+=5;
            n+=5;
            break;
         case '"':
            strcpy(to,"&quot;");
            to+=6;
            n+=6;
            break;
         default:
            *to++=*from;
            n++;
      }
   }
   return n;
}

/* Check if a file exists in a directory */
static BOOL FileExistsInDir(UBYTE *dirname,UBYTE *filename)
{  BPTR lock;
   BPTR file_lock;
   BOOL exists;
   UBYTE fullpath[512];
   
   if(!dirname || dirname[0]=='\0')
   {  /* No directory - just check filename directly */
      file_lock=Lock(filename,SHARED_LOCK);
      exists=(file_lock!=0);
      if(file_lock) UnLock(file_lock);
      return exists;
   }
   
   lock=Lock(dirname,SHARED_LOCK);
   if(!lock) return FALSE;
   
   strcpy(fullpath,dirname);
   if(fullpath[strlen(fullpath)-1]!='/' && fullpath[strlen(fullpath)-1]!=':')
   {  strcat(fullpath,"/");
   }
   strcat(fullpath,filename);
   
   file_lock=Lock(fullpath,SHARED_LOCK);
   exists=(file_lock!=0);
   if(file_lock) UnLock(file_lock);
   UnLock(lock);
   
   return exists;
}

/* Get icon HTML for file based on extension or type */
/* Returns pointer to static buffer containing HTML (img tag or entity reference) */
static UBYTE *GetFileIcon(UBYTE *filename,LONG entrytype,UBYTE *url_base,UBYTE *dirname,BOOL is_volume)
{  static UBYTE icon_buf[512];
   UBYTE *ext;
   LONG len;
   UBYTE *info_url;
   LONG info_url_len;
   UBYTE *p;
   LONG pos;
   UBYTE info_filename[256];
   BOOL info_exists;
   
   /* Build .info file path - special case for volumes */
   if(is_volume)
   {  /* For volumes: "VolumeName:Disk.info" */
      sprintf(info_filename,"%s:Disk.info",filename);
      /* Check if .info file exists - volumes are root level */
      info_exists=FileExistsInDir("",info_filename);
   }
   else
   {  /* For regular files: "filename.info" */
      sprintf(info_filename,"%s.info",filename);
      /* Check if .info file exists in the directory */
      info_exists=FileExistsInDir(dirname,info_filename);
   }
   
   if(info_exists)
   {  /* Build .info file URL */
      info_url_len=strlen(url_base)+strlen(filename)*3+20; /* *3 for URL encoding, +20 for ".info" and img tag */
      if(is_volume) info_url_len+=10; /* Extra space for ":Disk.info" */
      /* Must fit icon_buf+200 tail and url_copy[]; long file:/// paths used to pass the old sizeof check and overflow. */
      if(info_url_len>0 && info_url_len<GETFILEICON_URLLIMIT)
      {  info_url=icon_buf+200; /* Use later part of buffer for URL construction */
         /* Clear the URL buffer area first to remove any leftover data */
         memset(info_url,0,256);
         strcpy(info_url,url_base);
         pos=strlen(info_url);
         p=info_url+pos;
         
         /* Append filename with HTML entity escaping */
         len=Htmlmove(p,filename,strlen(filename));
         p[len]='\0';
         pos+=len;
         
         /* Append ".info" or ":Disk.info" */
         if(is_volume)
         {  strcat(info_url,":Disk.info");
         }
         else
         {  strcat(info_url,".info");
         }
         
         /* Copy the URL string before clearing icon_buf (since info_url points into icon_buf) */
         {  UBYTE url_copy[GETFILEICON_URLLIMIT+1];
            strcpy(url_copy,info_url);
            /* Clear icon_buf before building img tag to remove any leftover data */
            memset(icon_buf,0,sizeof(icon_buf));
            sprintf(icon_buf,"<IMG SRC=\"%s\" WIDTH=\"32\" HEIGHT=\"32\" ALT=\"\">",url_copy);
         }
         return icon_buf;
      }
   }
   
   /* Fallback to entity references if buffer too small */
   /* Directory (positive values indicate directory in AmigaOS) */
   if(entrytype>0)
   {  return "&folder;";
   }
   
   /* Find file extension */
   len=strlen(filename);
   ext=filename+len;
   while(ext>filename && *ext!='.') ext--;
   if(*ext=='.') ext++;
   else ext=filename+len; /* No extension */
   
   /* Check extension (case insensitive) */
   if(STRNIEQUAL(ext,"html",4) || STRNIEQUAL(ext,"htm",3))
   {  return "&html;";
   }
   else if(STRNIEQUAL(ext,"txt",3) || STRNIEQUAL(ext,"text",4))
   {  return "&text.document;";
   }
   else if(STRNIEQUAL(ext,"gif",3) || STRNIEQUAL(ext,"jpg",3) || 
            STRNIEQUAL(ext,"jpeg",4) || STRNIEQUAL(ext,"png",3) ||
            STRNIEQUAL(ext,"iff",3) || STRNIEQUAL(ext,"ilbm",4))
   {  return "&image;";
   }
   else if(STRNIEQUAL(ext,"zip",3) || STRNIEQUAL(ext,"lha",3) ||
            STRNIEQUAL(ext,"lzx",3) || STRNIEQUAL(ext,"gz",2))
   {  return "&archive;";
   }
   else if(STRNIEQUAL(ext,"mod",3) || STRNIEQUAL(ext,"wav",3) ||
            STRNIEQUAL(ext,"mp3",3) || STRNIEQUAL(ext,"aiff",4))
   {  return "&audio;";
   }
   else if(STRNIEQUAL(ext,"exe",3) || STRNIEQUAL(ext,"bin",3))
   {  return "&binary.document;";
   }
   
   /* Default to binary document */
   return "&binary.document;";
}

/* Format file size for display using locale */
static void FormatSize(UBYTE *buf,ULONG size)
{  if(size<1024)
   {  Lprintf(buf,"%ld",size);
   }
   else if(size<1048576)
   {  Lprintf(buf,"%ldK",size/1024);
   }
   else
   {  Lprintf(buf,"%ldM",size/1048576);
   }
}

/* Convert disk type to filesystem type string */
static void GetDiskTypeString(UBYTE *buf,LONG disk_type)
{  /* Disk type constants from dos/dos.h */
   /* All DOS-based filesystems start with 'DOS' (0x444F53) and have a subtype byte */
   if(disk_type==0x444F5300L) /* ID_DOS_DISK - 'DOS\0' */
   {  strcpy(buf,"OFS");
   }
   else if(disk_type==0x444F5301L) /* ID_FFS_DISK - 'DOS\1' */
   {  strcpy(buf,"FFS");
   }
   else if(disk_type==0x444F5302L) /* ID_INTER_DOS_DISK - 'DOS\2' */
   {  strcpy(buf,"OFS/I");
   }
   else if(disk_type==0x444F5303L) /* ID_INTER_FFS_DISK - 'DOS\3' */
   {  strcpy(buf,"FFS/I");
   }
   else if(disk_type==0x444F5304L) /* ID_FASTDIR_DOS_DISK - 'DOS\4' */
   {  strcpy(buf,"OFS/DC");
   }
   else if(disk_type==0x444F5305L) /* ID_FASTDIR_FFS_DISK - 'DOS\5' */
   {  strcpy(buf,"FFS/DC");
   }
   else if(disk_type==0x444F5306L) /* ID_LONG_DOS_DISK - 'DOS\6' */
   {  strcpy(buf,"OFS/LN");
   }
   else if(disk_type==0x444F5307L) /* ID_LONG_FFS_DISK - 'DOS\7' */
   {  strcpy(buf,"FFS/LN");
   }
   else if(disk_type==0x444F5308L) /* ID_COMPLONG_FFS_DISK - 'DOS\8' */
   {  strcpy(buf,"FFS/CLN");
   }
   else if(disk_type==0x4E444F53L) /* ID_NOT_REALLY_DOS - 'NDOS' */
   {  strcpy(buf,"Not recognized");
   }
   else if(disk_type==0x4B49434BL) /* ID_KICKSTART_DISK - 'KICK' */
   {  strcpy(buf,"Kickstart");
   }
   else if(disk_type==0x4D534400L) /* ID_MSDOS_DISK - 'MSD\0' */
   {  strcpy(buf,"FAT");
   }
   else if(disk_type==0x42414400L) /* ID_UNREADABLE_DISK - 'BAD\0' */
   {  strcpy(buf,"Unreadable");
   }
   else if(disk_type==-1) /* ID_NO_DISK_PRESENT */
   {  strcpy(buf,"No Disk");
   }
   else
   {  /* Unknown type - show as hex */
      sprintf(buf,"0x%08lX",disk_type);
   }
}

/* Format date for display from ExAll date fields using locale */
static void FormatFileDate(UBYTE *buf,ULONG days,ULONG mins,ULONG ticks)
{  struct DateStamp ds;
   ds.ds_Days=days;
   ds.ds_Minute=mins;
   ds.ds_Tick=ticks;
   Lprintdate(buf,"%d-%b-%Y %H:%M",&ds);
}

/* Generate directory listing HTML */
static void GenerateDirListing(struct Fetchdriver *fd,UBYTE *dirname,long lock)
{  struct ExAllData *exall_data;
   struct ExAllControl *eac;
   struct ExAllData *entry;
   UBYTE *html_buf;
   UBYTE *url_base;
   UBYTE size_buf[32];
   UBYTE date_buf[64];
   UBYTE *icon;
   ULONG buffer_size;
   LONG html_len;
   LONG url_base_len;
   BOOL is_dir;
   BOOL more;
   UBYTE header_done;
   LONG row_num;
   LONG html_buf_size;
   LONG row_start_pos;
   
   /* Allocate ExAllControl */
   eac=AllocDosObjectTags(DOS_EXALLCONTROL,TAG_END);
   if(!eac) return;
   
   /* Initialize ExAllControl */
   eac->eac_Entries=0;
   eac->eac_LastKey=0;
   eac->eac_MatchString=NULL;
   eac->eac_MatchFunc=NULL;
   
   /* Allocate buffer for ExAll data (enough for 256 entries) */
   buffer_size=MAX_DIR_ENTRIES*sizeof(struct ExAllData)+4096;
   exall_data=AllocMem(buffer_size,MEMF_CLEAR|MEMF_PUBLIC);
   if(!exall_data)
   {  FreeDosObject(DOS_EXALLCONTROL,eac);
      return;
   }
   
   /* Build base URL for links */
   /* Need: "file:///" (7 chars) + dirname + "/" (if needed) + null terminator */
   if(dirname && dirname[0]!='\0')
   {  url_base_len=strlen(dirname);
      if(url_base_len>0 && dirname[url_base_len-1]!='/' && dirname[url_base_len-1]!=':')
      {  url_base_len++; /* Need space for trailing / */
      }
      url_base_len+=8+1; /* "file:///" (7) + null terminator (1) + safety margin (1) */
   }
   else
   {  /* Root case - just "file:///" */
      url_base_len=8+1; /* "file:///" (7) + null terminator (1) */
   }
   url_base=AllocMem(url_base_len,MEMF_PUBLIC);
   if(!url_base)
   {  FreeMem(exall_data,buffer_size);
      FreeDosObject(DOS_EXALLCONTROL,eac);
      return;
   }
   strcpy(url_base,"file:///");
   if(dirname && dirname[0]!='\0')
   {  strcat(url_base,dirname);
      if(dirname[strlen(dirname)-1]!='/' && dirname[strlen(dirname)-1]!=':')
      {  strcat(url_base,"/");
      }
   }
   
   /* Allocate HTML buffer for incremental output - need enough for header + rows */
   html_buf_size=4096;
   html_buf=AllocMem(html_buf_size,MEMF_PUBLIC);
   if(!html_buf)
   {  FreeMem(url_base,url_base_len);
      FreeMem(exall_data,buffer_size);
      FreeDosObject(DOS_EXALLCONTROL,eac);
      return;
   }
   
   header_done=FALSE;
   row_num=0;
   
   /* Process directory using ExAll - may require multiple calls */
   do
   {  more=ExAll(lock,exall_data,buffer_size,ED_DATE,eac);
      
      if(!more && IoErr()!=ERROR_NO_MORE_ENTRIES)
      {  /* Error occurred */
         break;
      }
      
      if(eac->eac_Entries==0)
      {  continue; /* No entries in this batch */
      }
      
      /* Send HTML header on first batch */
      if(!header_done)
      {  LONG max_len;
         max_len=html_buf_size-1; /* Leave room for null terminator */
         html_len=sprintf(html_buf,
            "<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n"
            "<SCRIPT LANGUAGE=\"JavaScript1.1\">\n"
            "var sortCol = 0;\n"
            "var sortDir = 1;\n"
            "function getText(cell) {\n"
            "  var text = '';\n"
            "  for (var i = 0; i < cell.childNodes.length; i++) {\n"
            "    if (cell.childNodes[i].nodeType == 3) {\n"
            "      text += cell.childNodes[i].nodeValue;\n"
            "    } else if (cell.childNodes[i].nodeType == 1) {\n"
            "      text += getText(cell.childNodes[i]);\n"
            "    }\n"
            "  }\n"
            "  return text;\n"
            "}\n"
            "function sortTable(col) {\n"
            "  var table = document.filetable;\n"
            "  if (!table) return;\n"
            "  var tbody = table.tBodies[0];\n"
            "  var rows = tbody.rows;\n"
            "  var arr = [];\n"
            "  for (var i = 0; i < rows.length; i++) {\n"
            "    arr[i] = rows[i];\n"
            "  }\n"
            "  if (sortCol == col) sortDir = -sortDir;\n"
            "  else sortDir = 1;\n"
            "  sortCol = col;\n"
            "  for (var i = 0; i < arr.length - 1; i++) {\n"
            "    for (var j = i + 1; j < arr.length; j++) {\n"
            "      var aVal = getText(arr[i].cells[col]);\n"
            "      var bVal = getText(arr[j].cells[col]);\n"
            "      var swap = false;\n"
            "      if (col == 1 && aVal != '-' && bVal != '-') {\n"
            "        var aNum = parseInt(aVal) || 0;\n"
            "        var bNum = parseInt(bVal) || 0;\n"
            "        swap = (aNum - bNum) * sortDir > 0;\n"
            "      } else {\n"
            "        swap = (aVal < bVal ? -1 : aVal > bVal ? 1 : 0) * sortDir > 0;\n"
            "      }\n"
            "      if (swap) {\n"
            "        var tmp = arr[i];\n"
            "        arr[i] = arr[j];\n"
            "        arr[j] = tmp;\n"
            "      }\n"
            "    }\n"
            "  }\n"
            "  for (var i = 0; i < arr.length; i++) {\n"
            "    tbody.appendChild(arr[i]);\n"
            "  }\n"
            "}\n"
            "</SCRIPT>\n"
            "</HEAD>\n"
            "<BODY BGCOLOR=\"#AAAAAA\" TEXT=\"#000000\" LINK=\"#000000\" VLINK=\"#000000\" ALINK=\"#0000FF\" MARGINWIDTH=\"0\" MARGINHEIGHT=\"0\" TOPMARGIN=\"0\" LEFTMARGIN=\"0\">\n",dirname);
         if(html_len>=max_len) html_len=max_len-1;
         html_buf[html_len]='\0';
         
         
         if(html_len<max_len-100)
         {  LONG len;
            len=sprintf(html_buf+html_len,
               "<TABLE WIDTH=\"100%%\" BORDER=\"0\" CELLPADDING=\"2\" CELLSPACING=\"0\" NAME=\"filetable\" ID=\"filetable\">\n"
               "<THEAD>\n"
               "<TR>\n"
               "<TH WIDTH=\"60%%\"><A HREF=\"javascript:sortTable(0)\">Name</A></TH>\n"
               "<TH WIDTH=\"15%%\"><A HREF=\"javascript:sortTable(1)\">Size</A></TH>\n"
               "<TH WIDTH=\"25%%\"><A HREF=\"javascript:sortTable(2)\">Date</A></TH>\n"
               "</TR>\n"
               "</THEAD>\n"
               "<TBODY>\n");
            if(len>0 && html_len+len<max_len) html_len+=len;
         }
         
         /* Send header. Prepend AOURL_Reload like network fetches do via Srcupdatefetch:
          * docsource clears its buffer and notifies AODOC_Reload so Reloaddocument runs
          * before this HTML is appended. Without it, navigating from another page (e.g.
          * about:home with PRE open) can leave stale parse flags or an inconsistent body
          * so the listing title updates but the generated table never lays out. */
         Updatetaskattrs(
            AOURL_Reload,TRUE,
            AOURL_Contenttype,"text/html",
            AOURL_Data,html_buf,
            AOURL_Datalength,html_len,
            TAG_END);
         
         header_done=TRUE;
         html_len=0; /* Reset for row building */
      }
      
      /* Process each entry in this batch */
      entry=exall_data;
      while(entry)
      {  /* Skip . and .. entries */
         if(entry->ed_Name && STRNIEQUAL(entry->ed_Name,".",1) && 
            (entry->ed_Name[1]=='\0' || 
             (entry->ed_Name[1]=='.' && entry->ed_Name[2]=='\0')))
         {  entry=entry->ed_Next;
            continue;
         }
         
         if(!entry->ed_Name)
         {  entry=entry->ed_Next;
            continue;
         }
         
         /* Skip .info files - they are icon files, not regular files to list */
         {  LONG name_len;
            name_len=strlen(entry->ed_Name);
            if(name_len>=5 && STRNIEQUAL(entry->ed_Name+name_len-5,".info",5))
            {  entry=entry->ed_Next;
               continue;
            }
         }
         
         /* In AmigaOS, positive ed_Type indicates directory, negative indicates file */
         is_dir=(entry->ed_Type>0);
         icon=GetFileIcon(entry->ed_Name,entry->ed_Type,url_base,dirname,FALSE);
         
         /* Build HTML for this entry - alternating row colors */
         /* Start building row at position 0 in buffer */
         row_start_pos=0;
         {  LONG max_remaining;
            LONG len;
            UBYTE *new_buf;
            ULONG new_size;
            LONG estimated_row_size;
            
            /* Estimate row size: URL base + filename (possibly escaped) + fixed HTML */
            estimated_row_size=strlen(url_base)+strlen(entry->ed_Name)*2+200;
            
            /* Expand buffer if needed */
            max_remaining=html_buf_size-1;
            if(max_remaining<estimated_row_size)
            {  new_size=html_buf_size;
               while(new_size-1<estimated_row_size)
               {  new_size*=2;
                  if(new_size>65536) break; /* Safety limit */
               }
               new_buf=AllocMem(new_size,MEMF_PUBLIC);
               if(new_buf)
               {  /* html_len should be 0 after header, but copy just in case */
                  if(html_buf && html_len>0)
                  {  memcpy(new_buf,html_buf,html_len);
                  }
                  if(html_buf) FreeMem(html_buf,html_buf_size);
                  html_buf=new_buf;
                  html_buf_size=new_size;
                  max_remaining=html_buf_size-1;
               }
               else
               {  break; /* Can't expand, skip this entry */
               }
            }
            
            if(max_remaining<200) break; /* Not enough space */
            
            len=sprintf(html_buf+row_start_pos,"<TR BGCOLOR=\"%s\">\n<TD>%s <A HREF=\"%s",
               (row_num%2==0)?"#CCCCCC":"#DDDDDD",icon,url_base);
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            len=Htmlmove(html_buf+row_start_pos,entry->ed_Name,strlen(entry->ed_Name));
            if(len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            if(is_dir)
            {  if(max_remaining<10) break;
               len=sprintf(html_buf+row_start_pos,"/");
               if(len>0 && len<max_remaining) { row_start_pos+=len; max_remaining-=len; }
            }
            
            if(max_remaining<10) break;
            len=sprintf(html_buf+row_start_pos,"\">");
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            len=Htmlmove(html_buf+row_start_pos,entry->ed_Name,strlen(entry->ed_Name));
            if(len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            if(is_dir)
            {  if(max_remaining<10) break;
               len=sprintf(html_buf+row_start_pos,"/");
               if(len>0 && len<max_remaining) { row_start_pos+=len; max_remaining-=len; }
            }
            
            if(max_remaining<20) break;
            len=sprintf(html_buf+row_start_pos,"</A></TD>\n<TD>");
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            /* Size */
            if(is_dir)
            {  strcpy(size_buf,"-");
            }
            else
            {  FormatSize(size_buf,entry->ed_Size);
            }
            if(max_remaining<50) break;
            len=sprintf(html_buf+row_start_pos,"%s</TD>\n<TD>",size_buf);
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            /* Date */
            FormatFileDate(date_buf,entry->ed_Days,entry->ed_Mins,entry->ed_Ticks);
            if(max_remaining<100) break;
            len=sprintf(html_buf+row_start_pos,"%s</TD>\n</TR>\n",date_buf);
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            
            /* Emit only this row */
            Updatetaskattrs(
               AOURL_Data,html_buf,
               AOURL_Datalength,row_start_pos,
               TAG_END);
         }
         
         row_num++;
         
         /* Move to next entry */
         entry=entry->ed_Next;
      }
      
   } while(more);
   
   /* Send closing tags only if header was sent */
   if(header_done)
   {  LONG len;
      len=sprintf(html_buf,"</TBODY>\n</TABLE>\n</BODY>\n</HTML>\n");
      if(len>0 && len<html_buf_size)
      {  Updatetaskattrs(
            AOURL_Data,html_buf,
            AOURL_Datalength,len,
            TAG_END);
      }
   }

   /* Cleanup - free in reverse order of allocation */
   if(html_buf) FreeMem(html_buf,html_buf_size);
   if(url_base) FreeMem(url_base,url_base_len);
   if(exall_data) FreeMem(exall_data,buffer_size);
   if(eac) FreeDosObject(DOS_EXALLCONTROL,eac);
}

/*-----------------------------------------------------------------------*/

/* Generate volume listing HTML using LockDosList */
static void GenerateVolumeListing(struct Fetchdriver *fd)
{  struct DosList *dl;
   struct DosList *vol_entry;
   UBYTE *html_buf;
   UBYTE *url_base;
   UBYTE size_buf[32];
   UBYTE type_buf[32];
   UBYTE *icon;
   UBYTE *vol_name;
   LONG html_buf_size;
   LONG html_len;
   LONG row_start_pos;
   LONG row_num;
   UBYTE header_done;
   
   /* Lock DOS list for volumes */
   dl=LockDosList(LDF_VOLUMES|LDF_READ);
   if(!dl)
   {  return; /* Can't lock DOS list */
   }
   
   /* Allocate HTML buffer */
   html_buf_size=4096;
   html_buf=AllocMem(html_buf_size,MEMF_PUBLIC);
   if(!html_buf)
   {
      UnLockDosList(LDF_VOLUMES|LDF_READ);
      return;
   }
   
   /* Build base URL for links */
   url_base=AllocMem(9,MEMF_PUBLIC); /* "file:///" + null */
   if(!url_base)
   {
      FreeMem(html_buf,html_buf_size);
      UnLockDosList(LDF_VOLUMES|LDF_READ);
      return;
   }
   strcpy(url_base,"file:///");
   
   header_done=FALSE;
   row_num=0;
   html_len=0; /* Initialize html_len */
   
   /* Send HTML header first */
   {  LONG max_len;
      max_len=html_buf_size-1;
      html_len=sprintf(html_buf,
         "<HTML>\n<HEAD>\n<TITLE>Volumes</TITLE>\n"
         "<SCRIPT LANGUAGE=\"JavaScript1.1\">\n"
         "var sortCol = 0;\n"
         "var sortDir = 1;\n"
         "function getText(cell) {\n"
         "  var text = '';\n"
         "  for (var i = 0; i < cell.childNodes.length; i++) {\n"
         "    if (cell.childNodes[i].nodeType == 3) {\n"
         "      text += cell.childNodes[i].nodeValue;\n"
         "    } else if (cell.childNodes[i].nodeType == 1) {\n"
         "      text += getText(cell.childNodes[i]);\n"
         "    }\n"
         "  }\n"
         "  return text;\n"
         "}\n"
         "function sortTable(col) {\n"
         "  var table = document.filetable;\n"
         "  if (!table) return;\n"
         "  var tbody = table.tBodies[0];\n"
         "  var rows = tbody.rows;\n"
         "  var arr = [];\n"
         "  for (var i = 0; i < rows.length; i++) {\n"
         "    arr[i] = rows[i];\n"
         "  }\n"
         "  if (sortCol == col) sortDir = -sortDir;\n"
         "  else sortDir = 1;\n"
         "  sortCol = col;\n"
         "  for (var i = 0; i < arr.length - 1; i++) {\n"
         "    for (var j = i + 1; j < arr.length; j++) {\n"
         "      var aVal = getText(arr[i].cells[col]);\n"
         "      var bVal = getText(arr[j].cells[col]);\n"
         "      var swap = false;\n"
         "      if (col == 1 && aVal != '-' && bVal != '-') {\n"
         "        var aNum = parseInt(aVal) || 0;\n"
         "        var bNum = parseInt(bVal) || 0;\n"
         "        swap = (aNum - bNum) * sortDir > 0;\n"
         "      } else {\n"
         "        swap = (aVal < bVal ? -1 : aVal > bVal ? 1 : 0) * sortDir > 0;\n"
         "      }\n"
         "      if (swap) {\n"
         "        var tmp = arr[i];\n"
         "        arr[i] = arr[j];\n"
         "        arr[j] = tmp;\n"
         "      }\n"
         "    }\n"
         "  }\n"
         "  for (var i = 0; i < arr.length; i++) {\n"
         "    tbody.appendChild(arr[i]);\n"
         "  }\n"
         "}\n"
         "</SCRIPT>\n"
         "</HEAD>\n"
         "<BODY BGCOLOR=\"#AAAAAA\" TEXT=\"#000000\" LINK=\"#000000\" VLINK=\"#000000\" ALINK=\"#0000FF\" MARGINWIDTH=\"0\" MARGINHEIGHT=\"0\" TOPMARGIN=\"0\" LEFTMARGIN=\"0\">\n");
      if(html_len>=max_len) html_len=max_len-1;
      html_buf[html_len]='\0';
      
      if(html_len<max_len-100)
      {  LONG len;
         len=sprintf(html_buf+html_len,
            "<TABLE WIDTH=\"100%%\" BORDER=\"0\" CELLPADDING=\"2\" CELLSPACING=\"0\" NAME=\"filetable\" ID=\"filetable\">\n"
            "<THEAD>\n"
            "<TR>\n"
            "<TH WIDTH=\"50%%\"><A HREF=\"javascript:sortTable(0)\">Name</A></TH>\n"
            "<TH WIDTH=\"25%%\"><A HREF=\"javascript:sortTable(1)\">Size</A></TH>\n"
            "<TH WIDTH=\"25%%\"><A HREF=\"javascript:sortTable(2)\">Type</A></TH>\n"
            "</TR>\n"
            "</THEAD>\n"
            "<TBODY>\n");
         if(len>0 && html_len+len<max_len) html_len+=len;
      }
      
      /* Send header. Prepend AOURL_Reload like network fetches do via Srcupdatefetch:
       * docsource clears its buffer and notifies AODOC_Reload so Reloaddocument runs
       * before this HTML is appended. Without it, navigating from another page (e.g.
       * about:home with PRE open) can leave stale parse flags or an inconsistent body
       * so the listing title updates but the generated table never lays out. */
      Updatetaskattrs(
         AOURL_Reload,TRUE,
         AOURL_Contenttype,"text/html",
         AOURL_Data,html_buf,
         AOURL_Datalength,html_len,
         TAG_END);
      
      header_done=TRUE;
      html_len=0; /* Reset for row building */
   }
   
   /* Iterate through volumes - add safety limit to prevent infinite loop */
   {  LONG max_volumes;
      LONG volume_count;
      struct DosList *prev_entry;
      max_volumes=256;
      volume_count=0;
      prev_entry=dl; /* Start with the lock result */
      while(volume_count<max_volumes)
      {  vol_entry=NextDosEntry(prev_entry,LDF_VOLUMES);
         if(!vol_entry) break; /* No more entries */
         
         /* Safety check: if we get the same entry twice, break to prevent infinite loop */
         if(vol_entry==prev_entry && volume_count>0) break;
         prev_entry=vol_entry;
         volume_count++;
         
         /* Only process actual volumes - LDF_VOLUMES should only return volumes */
         /* But double-check the type to be safe */
         /* DLT_VOLUME is 2, DLT_DEVICE is 1, DLT_DIRECTORY is 3 */
         if(vol_entry->dol_Type!=2) continue; /* 2 = DLT_VOLUME */
         
         /* Show all volumes, even if not currently mounted (dol_Task may be NULL for ejected volumes) */
         /* We'll show them anyway so user can see what volumes exist */
         
         /* Get volume name - BCPL string */
         vol_name=(UBYTE *)BADDR(vol_entry->dol_Name);
         if(!vol_name) continue;
         
         /* Skip if name is empty */
         if(vol_name[0]==0) continue;
         
         /* BCPL string: first byte is length, then the string */
         {  UBYTE name_len;
            UBYTE vol_name_buf[32];
            LONG i;
            name_len=vol_name[0];
            if(name_len==0 || name_len>30) continue;
            /* Clear buffer first to remove any leftover data from previous iteration */
            memset(vol_name_buf,0,sizeof(vol_name_buf));
            for(i=0;i<name_len;i++)
            {  vol_name_buf[i]=vol_name[i+1];
            }
            vol_name_buf[name_len]='\0';
            /* Copy to a persistent location - vol_name is used after this block */
            {  static UBYTE persistent_vol_name[32];
               /* Clear persistent buffer first to remove any leftover data */
               memset(persistent_vol_name,0,sizeof(persistent_vol_name));
               strcpy(persistent_vol_name,vol_name_buf);
               vol_name=persistent_vol_name;
            }
         }
         
         /* Build HTML for this volume - try to use Disk.info icon */
         icon=GetFileIcon(vol_name,1,url_base,"",TRUE); /* entrytype=1 for directory, is_volume=TRUE, no dirname for volumes */
         row_start_pos=0;
         {  LONG max_remaining;
            LONG len;
            UBYTE *new_buf;
            ULONG new_size;
            LONG estimated_row_size;
            
            estimated_row_size=strlen(url_base)+strlen(vol_name)*2+200;
            max_remaining=html_buf_size-1;
            if(max_remaining<estimated_row_size)
            {  new_size=html_buf_size;
               while(new_size-1<estimated_row_size)
               {  new_size*=2;
                  if(new_size>65536) break;
               }
               new_buf=AllocMem(new_size,MEMF_PUBLIC);
               if(new_buf)
               {  if(html_buf && html_len>0)
                  {  memcpy(new_buf,html_buf,html_len);
                  }
                  if(html_buf) FreeMem(html_buf,html_buf_size);
                  html_buf=new_buf;
                  html_buf_size=new_size;
                  max_remaining=html_buf_size-1;
               }
               else
               {  break;
               }
            }
            
            if(max_remaining<200) break;
            
            len=sprintf(html_buf+row_start_pos,"<TR BGCOLOR=\"%s\">\n<TD>%s <A HREF=\"%s",
               (row_num%2==0)?"#CCCCCC":"#DDDDDD",icon,url_base);
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            len=Htmlmove(html_buf+row_start_pos,vol_name,strlen(vol_name));
            if(len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            if(max_remaining<10) break;
            len=sprintf(html_buf+row_start_pos,":\">");
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            len=Htmlmove(html_buf+row_start_pos,vol_name,strlen(vol_name));
            if(len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            if(max_remaining<10) break;
            len=sprintf(html_buf+row_start_pos,":</A></TD>\n<TD>");
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            /* Get volume size and filesystem type using Info() */
            {  BPTR vol_lock;
               UBYTE vol_path[64];
               __aligned struct InfoData id;
               ULONG total_size;
               
               /* Build volume path: "VolumeName:" */
               sprintf(vol_path,"%s:",vol_name);
               vol_lock=Lock(vol_path,SHARED_LOCK);
               if(vol_lock)
               {  memset(&id,0,sizeof(id));
                  if(Info(vol_lock,&id))
                  {  /* Calculate total size in bytes */
                     if(id.id_NumBlocks>0 && id.id_BytesPerBlock>0)
                     {  total_size=(ULONG)id.id_NumBlocks*(ULONG)id.id_BytesPerBlock;
                        FormatSize(size_buf,total_size);
                     }
                     else
                     {  strcpy(size_buf,"-");
                     }
                     
                     /* Get filesystem type string */
                     GetDiskTypeString(type_buf,id.id_DiskType);
                  }
                  else
                  {  /* Info() failed - show "-" for both */
                     strcpy(size_buf,"-");
                     strcpy(type_buf,"-");
                  }
                  UnLock(vol_lock);
               }
               else
               {  /* Lock failed - show "-" for both */
                  strcpy(size_buf,"-");
                  strcpy(type_buf,"-");
               }
            }
            
            if(max_remaining<50) break;
            len=sprintf(html_buf+row_start_pos,"%s</TD>\n<TD>",size_buf);
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            max_remaining=html_buf_size-row_start_pos-1;
            
            if(max_remaining<50) break;
            len=sprintf(html_buf+row_start_pos,"%s</TD>\n</TR>\n",type_buf);
            if(len<=0 || len>=max_remaining) break;
            row_start_pos+=len;
            
            /* Emit only this row */
            Updatetaskattrs(
               AOURL_Data,html_buf,
               AOURL_Datalength,row_start_pos,
               TAG_END);
            
            row_num++;
         }
      }
   }
   
   /* Send closing tags */
   if(header_done)
   {  LONG len;
      len=sprintf(html_buf,"</TBODY>\n</TABLE>\n</BODY>\n</HTML>\n");
      if(len>0 && len<html_buf_size)
      {  Updatetaskattrs(
            AOURL_Data,html_buf,
            AOURL_Datalength,len,
            TAG_END);
      }
   }

   /* Cleanup */
   if(html_buf) FreeMem(html_buf,html_buf_size);
   if(url_base) FreeMem(url_base,9);
   UnLockDosList(LDF_VOLUMES|LDF_READ);
}

/*-----------------------------------------------------------------------*/

void Localfiletask(struct Fetchdriver *fd)
{  long lock;
   void *fh;
   struct FileInfoBlock *fib;
   UBYTE *name,*buf=NULL,*p;
   UBYTE c;
   BOOL skipvalid=FALSE;
   BOOL is_root;
   long actual;
   ULONG date=0;
   if(fd->postmsg)
   {  Write(Output(),"\nPOST:\n",7);
      Write(Output(),fd->postmsg,strlen(fd->postmsg));
      Write(Output(),"\n",1);
   }
   if(fd->multipart)
   {  struct Multipartpart *mpp;
      Write(Output(),"\nPOST multipart/form-data:\n",27);
      for(mpp=fd->multipart->parts.first;mpp->next;mpp=mpp->next)
      {  if(mpp->lock)
         {  long fh;
            long r;
            if(fh=OpenFromLock(mpp->lock))
            {  mpp->lock=NULL;
               while(r=Read(fh,fd->block,fd->blocksize))
               {  Write(Output(),fd->block,r);
               }
               Close(fh);
            }
         }
         else
         {  Write(Output(),fd->multipart->buf.buffer+mpp->start,mpp->length);
         }
      }
   }
   name=fd->name;
   /* Check if this is root first - before trying index file */
   is_root=(name[0]=='\0' || 
            (name[0]=='/' && name[1]=='\0') ||
            (name[0]==':' && name[1]=='\0'));
   if(is_root)
   {  /* Root - generate volume listing using LockDosList */
      GenerateVolumeListing(fd);
      if(buf) FREE(buf);
      Updatetaskattrs(AOTSK_Async,TRUE,
         AOURL_Eof,TRUE,
         AOURL_Terminate,TRUE,
         TAG_END);
      return;
   }
   c=fd->name[strlen(fd->name)-1];
   if(c=='/' || c==':')
   {  /* Try index file first */
      ObtainSemaphore(&prefssema);
      if(buf=ALLOCTYPE(UBYTE,strlen(fd->name)+strlen(prefs.localindex)+2,MEMF_PUBLIC))
      {  strcpy(buf,fd->name);
         strcat(buf,prefs.localindex);
         name=buf;
      }
      ReleaseSemaphore(&prefssema);
   }
   lock=Lock(name,SHARED_LOCK);
   /* Replace MS-DOS \ by / */
   if(!lock && strchr(name,'\\'))
   {  if(buf || (buf=Dupstr(name,-1)))
      {  name=buf;
         while(p=strchr(name,'\\')) *p='/';
         lock=Lock(name,SHARED_LOCK);
      }
   }
   /* If index file not found and original path was a directory, try directory listing */
   if(!lock && (c=='/' || c==':'))
   {  UBYTE *orig_name;
      UBYTE *dir_buf;
      long dir_lock;
      /* Try the original directory path */
      orig_name=fd->name;
      dir_buf=NULL;
      dir_lock=Lock(orig_name,SHARED_LOCK);
      if(!dir_lock && strchr(orig_name,'\\'))
      {  if(dir_buf=Dupstr(orig_name,-1))
         {  UBYTE *dir_p;
            orig_name=dir_buf;
            while(dir_p=strchr(orig_name,'\\')) *dir_p='/';
            dir_lock=Lock(orig_name,SHARED_LOCK);
         }
      }
      if(dir_lock)
      {  if(fib=AllocDosObjectTags(DOS_FIB,TAG_END))
         {  if(Examine(dir_lock,fib))
            {  /* Check if it's a directory */
               if(fib->fib_DirEntryType>0)
               {  /* Directory - generate listing */
                  FreeDosObject(DOS_FIB,fib);
                  GenerateDirListing(fd,orig_name,dir_lock);
                  UnLock(dir_lock);
                  if(dir_buf) FREE(dir_buf);
                  if(buf) FREE(buf);
                  Updatetaskattrs(AOTSK_Async,TRUE,
                     AOURL_Eof,TRUE,
                     AOURL_Terminate,TRUE,
                     TAG_END);
                  return;
               }
            }
            FreeDosObject(DOS_FIB,fib);
         }
         UnLock(dir_lock);
      }
      if(dir_buf) FREE(dir_buf);
   }
   if(lock)
   {  if(fib=AllocDosObjectTags(DOS_FIB,TAG_END))
      {  if(Examine(lock,fib))
         {  /* Check if it's a directory (fib_DirEntryType > 0) */
            if(fib->fib_DirEntryType>0)
            {  /* Directory - generate listing */
               FreeDosObject(DOS_FIB,fib);
               GenerateDirListing(fd,name,lock);
               UnLock(lock);
               if(buf) FREE(buf);
               Updatetaskattrs(AOTSK_Async,TRUE,
                  AOURL_Eof,TRUE,
                  AOURL_Terminate,TRUE,
                  TAG_END);
               return;
            }
            else if(fib->fib_DirEntryType<0)
            {  /* Regular file */
               Updatetaskattrs(AOURL_Contentlength,fib->fib_Size,TAG_END);
            }
            date=fib->fib_Date.ds_Days*86400 +
                 fib->fib_Date.ds_Minute*60 +
                 fib->fib_Date.ds_Tick/TICKS_PER_SECOND;
            if(fd->validate && date<=fd->validate) skipvalid=TRUE;
         }
         FreeDosObject(DOS_FIB,fib);
      }
      if(!date) date=Today();
      if(skipvalid)
      {  if(!(fd->flags&FDVF_CACHERELOAD))
         {  Updatetaskattrs(AOURL_Notmodified,TRUE,TAG_END);
         }
      }
      else
      {  if(!(fd->flags&FDVF_CACHERELOAD))
         {  Updatetaskattrs(AOURL_Lastmodified,date,TAG_END);
         }
         if(fh=OpenAsync(name,MODE_READ,INPUTBLOCKSIZE))
         {  for(;;)
            {  
#ifdef DEVELOPER
               actual=ReadAsync(fh,fd->block,MIN(localblocksize,fd->blocksize));
#else
               actual=ReadAsync(fh,fd->block,fd->blocksize);
#endif
               if(Checktaskbreak()) break;
               Updatetaskattrs(
                  AOURL_Data,fd->block,
                  AOURL_Datalength,actual,
                  TAG_END);
               if(!actual) break;
            }
            CloseAsync(fh);
         }
         else
         {  Tcperror(fd,TCPERR_NOFILE,name);
         }
      }
      UnLock(lock);
   }
   else
   {  Tcperror(fd,TCPERR_NOFILE,name);
   }
   if(buf) FREE(buf);
   Updatetaskattrs(AOTSK_Async,TRUE,
      AOURL_Eof,TRUE,
      AOURL_Terminate,TRUE,
      TAG_END);
}

/*-----------------------------------------------------------------------*/
