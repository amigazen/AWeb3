/* jconfig.sas --- jconfig.h for Amiga systems using SAS C 6.0 and up. */
/* see jconfig.doc for explanations */

#define HAVE_PROTOTYPES
#define HAVE_UNSIGNED_CHAR
#define HAVE_UNSIGNED_SHORT
/* #define void char */
/* #define const */
#undef CHAR_IS_UNSIGNED
#define HAVE_STDDEF_H
#define HAVE_STDLIB_H
#undef NEED_BSD_STRINGS
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS
#undef NEED_SHORT_EXTERNAL_NAMES
/* Define this if you get warnings about undefined structures. */
#undef INCOMPLETE_TYPES_BROKEN

#ifdef JPEG_INTERNALS

#undef RIGHT_SHIFT_IS_UNSIGNED
/* SAS/C 6.x rejects "__inline__" before LOCAL()/static on a separate
 * line (jchuff.c emit_bits, jcphuff.c).  Leave INLINE empty per IJG
 * makefile.sas / jconfig.sas guidance. */
#define INLINE
/* These are for configuring the JPEG memory manager. */
#undef DEFAULT_MAX_MEM

#define TEMP_DIRECTORY "T:"	/* recommended setting for Amiga */

#define NO_MKTEMP		/* SAS C doesn't have mktemp() */

#define SHORTxSHORT_32		/* produces better DCT code with SAS C */

#endif /* JPEG_INTERNALS */

#ifdef JPEG_CJPEG_DJPEG

#define BMP_SUPPORTED		/* BMP image file format */
#define GIF_SUPPORTED		/* GIF image file format */
#define PPM_SUPPORTED		/* PBMPLUS PPM/PGM image file format */
#undef RLE_SUPPORTED		/* Utah RLE image file format */
#define TARGA_SUPPORTED		/* Targa image file format */

#undef TWO_FILE_COMMANDLINE
#undef NEED_SIGNAL_CATCHER
#undef DONT_USE_B_MODE

/* Define this if you want percent-done progress reports from cjpeg/djpeg. */
#undef PROGRESS_REPORT

#endif /* JPEG_CJPEG_DJPEG */
