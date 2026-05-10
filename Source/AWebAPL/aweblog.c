/**********************************************************************
 *
 * aweblog.c — single global debug log path for AWeb (guarded by httpdebug).
 *
 * Line format: {Mmm dd hh:mm:ss} aweb[0x{task}] {facility}: {message}
 * (No syslog PRI prefix — facility + module naming carry the intent.)
 *
 * Timestamps: DOS DateStamp() → seconds since 1-Jan-1978 → utility Amiga2Date().
 * Do not pass struct DateStamp to Date2Amiga() — that API expects ClockData *.
 *
 **********************************************************************/

#include "aweblog.h"

#include <dos/dos.h>
#include <proto/dos.h>
/* intuition.library exposes CurrentTime(sec,usec); DOS DateStamp() fills DateStamp. */
#include <proto/exec.h>
#include <proto/utility.h>
#include <utility/date.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern struct Library *UtilityBase;
extern BOOL httpdebug;

struct SignalSemaphore debug_log_sema;
BOOL debug_log_sema_initialized = FALSE;

static const char mon_ab[12][4] =
{
   "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

/* Convert DOS DateStamp to seconds since 01-Jan-1978 00:00:00 (Amiga epoch). */
static ULONG DateStampToAmigaSeconds(struct DateStamp *ds)
{
   ULONG days;
   ULONG mins;
   ULONG sec;

   days = (ULONG)ds->ds_Days;
   mins = (ULONG)ds->ds_Minute;
   sec = (ULONG)ds->ds_Tick / (ULONG)TICKS_PER_SECOND;
   return days * (24UL * 60UL * 60UL) + mins * 60UL + sec;
}

static void FormatTimestamp(char *buf, long buflen)
{
   struct DateStamp ds;
   ULONG amigaSecs;
   struct ClockData cd;

   DateStamp(&ds);
   amigaSecs = DateStampToAmigaSeconds(&ds);

   if(UtilityBase && buflen >= 24)
   {
      Amiga2Date(amigaSecs, &cd);
      if(cd.month >= 1 && cd.month <= 12 && cd.mday >= 1 && cd.mday <= 31)
      {
         sprintf(buf, "%s %2lu %02lu:%02lu:%02lu",
            mon_ab[cd.month - 1],
            (unsigned long)cd.mday,
            (unsigned long)cd.hour,
            (unsigned long)cd.min,
            (unsigned long)cd.sec);
      }
      else
      {
         sprintf(buf, "ds=%lu", (unsigned long)ds.ds_Days);
      }
   }
   else
   {
      sprintf(buf, "ds=%lu", (unsigned long)ds.ds_Days);
   }
}

void InitAwebLog(void)
{
   if(debug_log_sema_initialized)
      return;
   InitSemaphore(&debug_log_sema);
   debug_log_sema_initialized = TRUE;
}

void AwebLogV(const char *facility, const char *fmt, va_list ap)
{
   char ts[48];
   char msg[2048];
   char head[160];
   ULONG tid;
   struct Task *task;
   BPTR out;
   int tries;
   long hl;
   long ml;

   if(!httpdebug)
      return;
   if(!debug_log_sema_initialized)
      return;
   if(!fmt)
      return;

   tries = 0;
   while(!AttemptSemaphore(&debug_log_sema))
   {
      tries++;
      if(tries >= 200)
         return;
      Delay(1);
   }

   FormatTimestamp(ts, (long)sizeof(ts));
   task = FindTask(NULL);
   tid = (ULONG)task;

   vsprintf(msg, fmt, ap);
   ml = (long)strlen(msg);
   if(ml >= (long)sizeof(msg))
      msg[sizeof(msg) - 1] = '\0';

   /* vfprintf-style strings often end with \n; we emit one newline ourselves. */
   while(ml > 0 && (msg[ml - 1] == '\n' || msg[ml - 1] == '\r'))
   {
      ml--;
      msg[ml] = '\0';
   }

   sprintf(head, "%s aweb[0x%08lX] %s: ",
      ts, tid, facility ? facility : "aweb");

   out = Output();
   hl = (long)strlen(head);
   Write(out, head, hl);
   Write(out, msg, (LONG)strlen(msg));
   Write(out, "\n", 1);

   ReleaseSemaphore(&debug_log_sema);
}

void AwebLog(const char *facility, const char *fmt, ...)
{
   va_list ap;

   va_start(ap, fmt);
   AwebLogV(facility, fmt, ap);
   va_end(ap);
}

void AwebLogRaw(const void *buf, long len)
{
   BPTR out;
   int tries;

   if(!httpdebug)
      return;
   if(!debug_log_sema_initialized)
      return;
   if(!buf || len <= 0)
      return;

   tries = 0;
   while(!AttemptSemaphore(&debug_log_sema))
   {
      tries++;
      if(tries >= 200)
         return;
      Delay(1);
   }

   out = Output();
   Write(out, buf, len);
   ReleaseSemaphore(&debug_log_sema);
}
