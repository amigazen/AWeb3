/**********************************************************************
 *
 * Unified debug logging (HTTPDEBUG / httpdebug).
 * Line format: {Mmm dd hh:mm:ss} aweb[0x{task}] {facility}: {message}
 * Serialized with debug_log_sema so concurrent tasks do not interleave lines.
 *
 **********************************************************************/

#ifndef AWEBLOG_H
#define AWEBLOG_H

#include <exec/types.h>
#include <exec/semaphores.h>
#include <stdarg.h>

extern struct SignalSemaphore debug_log_sema;
extern BOOL debug_log_sema_initialized;

void InitAwebLog(void);
void AwebLog(const char *facility, const char *fmt, ...);
void AwebLogV(const char *facility, const char *fmt, va_list ap);
void AwebLogRaw(const void *buf, long len);

#endif
