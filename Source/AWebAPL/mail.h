/**********************************************************************
 *
 * mail.h - AWeb embedded mail client (internal)
 *
 * Copyright (C) 2026 amigazen project
 *
 **********************************************************************/

#ifndef MAIL_H
#define MAIL_H

#ifndef LOCALONLY

#define MAIL_CACHE_SUBDIR "Mail"
#define MAIL_TAG_PREFIX   "A"
#define MAIL_MAX_FOLDERS  64
#define MAIL_MAX_UIDS     500
#define MAIL_IMAP_PORT    993
#define MAIL_SMTP_PORT    587
#define MAIL_SMTP_SSLPORT 465
/* Largest single message body we will read back from the on-disk cache.
 * Anything bigger is treated as suspect (collision with a non-mail file). */
#define MAIL_MAX_CACHED_BODY (16L*1024L*1024L)

/* Set to 0 to disable; logs via Aprintf when awebplugin.library is open */
#ifndef MAIL_DEBUG
#define MAIL_DEBUG 1
#endif

enum SENDMESSAGE_RETURNS
{  SMR_FAIL,SMR_NOCONNECT,SMR_OK,
};

struct Mailconn
{  struct Fetchdriver *fd;
   struct Library *socketbase;
   struct hostent *hent;
   long sock;
   UBYTE *buffer;
   long length;
   long linelen;
   long bufsize;
   long tag;
   BOOL use_ssl;
   BOOL ssl_active;
   BOOL imap_lock_held;
   UBYTE hostname[128];
};

struct Mailfolder
{  UBYTE name[96];
};

struct Mailuid
{  unsigned long uid;
   UBYTE subject[128];
   UBYTE from[96];
   UBYTE date[48];
   BOOL unseen;
};

/* IMAP session */
BOOL Mailimap_open(struct Mailconn *mc,BOOL optional);
void Mailimap_close(struct Mailconn *mc);
void Mail_imap_release_stale(void);
long Mailimap_list(struct Mailconn *mc,struct Mailfolder *folders,long maxfolders);
BOOL Mailimap_select(struct Mailconn *mc,UBYTE *mailbox);
long Mailimap_search_all(struct Mailconn *mc,unsigned long *uids,long maxuids);
long Mailimap_fetch_envelope(struct Mailconn *mc,unsigned long uid,
   UBYTE *subject,long subjmax,UBYTE *from,long frommax,
   UBYTE *date,long datemax,BOOL *unseen);
/* Progress callback invoked between batch chunks (NULL allowed). cancel hook
 * may set *cancel to TRUE to abort cleanly without dropping the IMAP socket. */
typedef void (*MailEnvelopeProgress)(void *udata,long done,long total,
   BOOL *cancel);
long Mailimap_fetch_envelopes(struct Mailconn *mc,unsigned long *uids,long nuids,
   struct Mailuid *items,long itemmax,
   MailEnvelopeProgress progress,void *udata);
BOOL Mailimap_fetch_body(struct Mailconn *mc,unsigned long uid,
   UBYTE **body,long *bodylen);
BOOL Mailimap_store_seen(struct Mailconn *mc,unsigned long uid);

/* SMTP (enhanced) */
short Mailsmtp_send(struct Fetchdriver *fd,UBYTE *to,UBYTE *message,long msglen);

/* Debug trace (AWeb shell / log when MAIL_DEBUG and awebplugin.library) */
void Mail_debug(UBYTE *fmt,...);

/* Derive IMAP hostname from network SMTP host pref (same Mail/News page) */
void Mail_imap_host(struct Fetchdriver *fd,UBYTE *buf,long buflen);

/* URL helpers: mail:folder/uid or mail:folder */
BOOL Mail_parse_url(UBYTE *url,UBYTE *folder,long foldermax,
   unsigned long *uid,BOOL *has_uid);

void Mail_encode_folder(UBYTE *folder,UBYTE *out,long outmax);
BOOL Mail_decode_folder(UBYTE *encoded,UBYTE *out,long outmax);
void Mail_folder_label(UBYTE *imapname,UBYTE *label,long labelmax);

/* Message body cache under prefs cache path */
BOOL Mail_cache_read(struct Fetchdriver *fd,UBYTE *folder,unsigned long uid,
   UBYTE **data,long *datalen);
BOOL Mail_cache_write(struct Fetchdriver *fd,UBYTE *folder,unsigned long uid,
   UBYTE *data,long datalen);

#endif /* !LOCALONLY */

#endif /* MAIL_H */
