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

/* object.h - AWeb re-usable objects */

#ifndef AWEB_OBJECT_H
#define AWEB_OBJECT_H

#ifndef NOPROTOTYPES
/*---- General interface ----*/

extern ULONG Amethod(struct Aobject *ao,ULONG method,...);
extern ULONG AmethodA(struct Aobject *ao,struct Amessage *amsg);

extern ULONG Amethodas(short objtype,struct Aobject *ao,ULONG method,...);
extern ULONG AmethodasA(short objtype,struct Aobject *ao,struct Amessage *amsg);

/*---- Method shortcuts ----*/

extern void *Anewobject(short type,...);
extern void Adisposeobject(struct Aobject *ao);
extern ULONG Asetattrs(struct Aobject *ao,...);
extern ULONG Agetattrs(struct Aobject *ao,...);
extern ULONG Agetattr(struct Aobject *ao,ULONG attrid);
extern ULONG Aupdateattrs(struct Aobject *ao,struct TagItem *maplist,...);
extern ULONG Arender(struct Aobject *ao,struct Coords *coo,
   long minx,long miny,long maxx,long maxy,ULONG flags,struct Buffer *text);
extern ULONG Ahittest(struct Aobject *ao,struct Coords *coo,
   long xco,long yco,USHORT flags,void *oldobject,struct Amhresult *amhr);
extern ULONG Agoactive(struct Aobject *ao,struct IntuiMessage *imsg,USHORT flags);
extern ULONG Ahandleinput(struct Aobject *ao,struct IntuiMessage *imsg,
   USHORT flags,struct Amiresult *amir);
extern ULONG Agoinactive(struct Aobject *ao);
extern ULONG Ameasure(struct Aobject *ao,long width,long height,long addwidth,
   USHORT flags,struct Buffer *text,struct Ammresult *ammr);
extern ULONG Alayout(struct Aobject *ao,long width,long height,
   USHORT flags,struct Buffer *text,long startx,struct Amlresult *amlr);
extern ULONG Aalign(struct Aobject *ao,long dx,long y,long baseline,long height);
extern ULONG Asrcupdate(struct Aobject *ao,struct Aobject *fetch,struct TagItem *tags);
extern ULONG Aaddchild(struct Aobject *ao,struct Aobject *child,ULONG relation);
extern ULONG Aremchild(struct Aobject *ao,struct Aobject *child,ULONG relation);
extern ULONG Anotify(struct Aobject *ao,struct Amessage *nmsg);
extern ULONG Amove(struct Aobject *ao,long dx,long dy);
extern ULONG Adragtest(struct Aobject *ao,struct Coords *coo,
   long xco,long yco,struct Amdresult *amdr);
extern ULONG Adragrender(struct Aobject *ao,struct Coords *coo,
   void *startobject,ULONG startobjpos,void *endobject,ULONG endobjpos,ULONG state);
extern ULONG Adragcopy(struct Aobject *ao,void *startobject,ULONG startobjpos,
   void *endobject,ULONG endobjpos,ULONG state,struct Buffer *clip);
extern ULONG Ajsetup(struct Aobject *ao,struct Jcontext *jc,
   struct Jobject *parent,struct Jobject *parentframe);
extern ULONG Ajonmouse(struct Aobject *ao,USHORT event);

/*---- Support functions and macros ----*/

extern long Gettagsdata(struct TagItem *tags,...);
   /* Call with taglist { tag, storage pointer }. Distributes tag values
    * found in the list. Returns nonzero if values assigned. */

extern void *Allocobject(short type,short size,struct Amset *ams);
   /* Allocates object of this size and type, obeying AOBJ_Pool in (ams). */

extern ULONG Asrcupdatetags(struct Aobject *ao,struct Aobject *fetch,...);
   /* Build AOM_SRCUPDATE taglist on the stack */

extern ULONG Anotifyset(struct Aobject *ao,...);
   /* Builds an AOM_SET message and does AOM_NOTIFY */

#endif /* !NOPROTOTYPES */

#define PUTATTR(t,v) *((ULONG *)(t)->ti_Data)=(ULONG)(v)

/*---- Root object ----*/

struct Aobject
{  void *next,*prev;             /* A MinNode */
   short objecttype;             /* Object type, see below */
};


/*---- Object types ----*/

#define AOTP_OBJECT        0x00  /* Root class */

/* application */
#define AOTP_APPLICATION   0x01  /* Program context */

/* cache & input */
#define AOTP_URL           0x02  /* An object address */
#define AOTP_CACHE         0x03  /* Cached object */
#define AOTP_FETCH         0x04  /* Retrieving object */

/* Objects */
#define AOTP_SOURCE        0x05  /* Object source interpreter */
#define AOTP_COPY          0x06  /* Object copy renderer */

/* Subtask */
#define AOTP_TASK          0x07  /* Subtask */

#ifdef AWEB4
/* Disk-cache idle flush controller (timer target); not a document object. */
#define AOTP_CACHECTL      0x08
#endif

/* General */
#define AOTP_SCROLLER      0x10  /* Scroller */
#define AOTP_FILE          0x11  /* Temporary or cache file */
#define AOTP_FILEREQ       0x12  /* Asynchroneous file requester */
#define AOTP_EDITOR        0x13  /* Synchroneous spawn of external editor */
#define AOTP_TIMER         0x14  /* Timer */

/* Display */
#define AOTP_WINDOW        0x20  /* Inutition window */
#define AOTP_WINHIS        0x21  /* Window display history */
#define AOTP_WHISWINDOW    0x22  /* Window history window */
#define AOTP_NETSTATWIN    0x23  /* Network status window */
#define AOTP_CABROWSE      0x24  /* Cache browser window */
#define AOTP_HOTLIST       0x25  /* Hotlist window */
#define AOTP_POPUP         0x26  /* Popup window */
#define AOTP_SEARCH        0x27  /* Search requester */
#define AOTP_PRINTWINDOW   0x28  /* Off-screen print window */
#define AOTP_PRINT         0x29  /* Print process */
#define AOTP_INFO          0x2a  /* Information window */
#define AOTP_AUTHEDIT      0x2b  /* Authorization edit window */
#define AOTP_SAVEIFF       0x2c  /* Save as IFF process */

/* Document object */
#define AOTP_DOCSOURCE     0x30  /* Document source */
#define AOTP_DOCUMENT      0x31  /* Document copy */
#define AOTP_FRAMESET      0x32  /* Document frame set */
#define AOTP_BODY          0x33  /* Document or table cell body */
#define AOTP_LINK          0x34  /* Hyperlink anchor */
#define AOTP_MAP           0x35  /* Client side image map */
#define AOTP_FORM          0x36  /* Document form */
#define AOTP_AREA          0x37  /* Client side image map area link */
#define AOTP_ELEMENT       0x38  /* HTML element */

/* Document element subclasses */
#define AOTP_BREAK         0x40  /* Line break element */
#define AOTP_TEXT          0x41  /* Text element */
#define AOTP_RULER         0x42  /* Ruler element */
#define AOTP_BULLET        0x43  /* List bullet element */
#define AOTP_TABLE         0x44  /* Table element */
#define AOTP_NAME          0x45  /* Fragment name element */
#define AOTP_FRAME         0x46  /* Frame element */
#define AOTP_FIELD         0x47  /* Form field element */
#define AOTP_INPUT         0x48  /* Text input form field element */
#define AOTP_CHECKBOX      0x49  /* Checkbox form field element */
#define AOTP_RADIO         0x4a  /* Radio button form field element */
#define AOTP_SELECT        0x4b  /* Selector form field element */
#define AOTP_TEXTAREA      0x4c  /* Textarea form field element */
#define AOTP_BUTTON        0x4d  /* Button form field element */
#define AOTP_HIDDEN        0x4e  /* Hidden form field element */
#define AOTP_FILEFIELD     0x4f  /* File upload form field element */

/* Image object */
#define AOTP_IMGSOURCE     0x60  /* Image source */
#define AOTP_IMGCOPY       0x61  /* Image copy */

/* Other objects */
#define AOTP_EXTPROG       0x62  /* Process by external program source */
#define AOTP_SAVEAS        0x63  /* Save to local disk source */

/* Sound object */
#define AOTP_SOUNDSOURCE   0x64  /* Sound source */
#define AOTP_SOUNDCOPY     0x65  /* Sound copy */

/* Document external source fragment (script, style) */
#define AOTP_DOCEXT        0x66  /* Document extension */

/* Driver interfaces */
#define AOTP_SOURCEDRIVER  0x71  /* General source driver */
#define AOTP_COPYDRIVER    0x72  /* General copy driver */

/* Plugins */
#define AOTP_PLUGIN        0x81  /* First plugin object type */


/*---- General object tags ----*/

#define AOBJ_DUMMYTAG(type) (TAG_USER+0x23EB0000+((type)<<8))

#define AOBJ_Dummy         AOBJ_DUMMYTAG(0)

#define AOBJ_Left          (AOBJ_Dummy+1)
#define AOBJ_Top           (AOBJ_Dummy+2)
#define AOBJ_Width         (AOBJ_Dummy+3)
#define AOBJ_Height        (AOBJ_Dummy+4)
   /* (long) coordinates relative to AOBJ_Frame */

#define AOBJ_Cframe        (AOBJ_Dummy+5)
   /* (void *) FRAME or BODY object, allows object to retrieve Coords info */

#define AOBJ_Window        (AOBJ_Dummy+6)
   /* (void *) The window in which the object is displayed. Up to
    * subclasses to implement */

#define AOBJ_Pool          (AOBJ_Dummy+7)
   /* (void *) memory pool to use for allocating */

#define AOBJ_Target        (AOBJ_Dummy+8)
   /* (struct Aobject *) Target object to send AOM_UPDATE messages to */

#define AOBJ_Map           (AOBJ_Dummy+9)
   /* (struct TagItem *) Tag map list to map attributes in AOM_UPDATE messages */

#define AOBJ_Layoutparent  (AOBJ_Dummy+10)
   /* (void *) Parent object responsible for layout. Set its AOBJ_Changedchild
    * attribute if the child's dimensions have changed. */

#define AOBJ_Changedchild  (AOBJ_Dummy+11)
   /* (void *) Child object that has changed dimensions. A new layout will
    * be performed. */

#define AOBJ_Winhis        (AOBJ_Dummy+12)
   /* (void *) Winhis object specifying the urls to load in a frame and
    * its subframes. */

#define AOBJ_Popupinfo     (AOBJ_Dummy+13)
   /* (ULONG *) GET a static zero terminated array of popup IDs. */

#define AOBJ_Popupselect   (AOBJ_Dummy+14)
   /* (ULONG) SET, user has selected this popup ID. */

#define AOBJ_Application   (AOBJ_Dummy+15)
   /* (void *) Objects attached to the APPLICATION object can get this
    * set to NULL if the application goes away. */

#define AOBJ_Nobackground  (AOBJ_Dummy+16)
   /* (BOOL) Don't use any background or text colours */

#define AOBJ_Bgchanged     (AOBJ_Dummy+17)
   /* (BOOL) Background has changed */

#define AOBJ_Frame         (AOBJ_Dummy+18)
   /* (void *) FRAME object */

#define AOBJ_Clipdrag      (AOBJ_Dummy+19)
   /* (BOOL *) Object supports clipboard dragging */

#define AOBJ_Jobject       (AOBJ_Dummy+20)   /* GET */
   /* (struct Jobject *) JS object connected to this object */

#define AOBJ_Queueid       (AOBJ_Dummy+21)   /* SET */
   /* (ULONG) General ID sent with notify-when-back-in-main queue messages */

#define AOBJ_Secure        (AOBJ_Dummy+22)   /* GET */
   /* (BOOL) If the document was retrieved via a secure connection */

#define AOBJ_Jscancel      (AOBJ_Dummy+23)   /* GET,SET */
   /* (BOOL) SET to cancel all JS timeouts, GET while JS is running to see if cancelled. */

#define AOBJ_Pointertype   (AOBJ_Dummy+24)
   /* (USHORT) Type of pointer, see below.
    * Used to return on AOM_HITTEST, SET in WINDOW object,
    * or get pointer from Apppointer() */

#define AOBJ_Isframeset    (AOBJ_Dummy+25)   /* GET */
   /* (BOOL) If the document is a frameset document */

#define AOBJ_Jsowner       (AOBJ_Dummy+26)   /* NEW,SET */
   /* (struct Aobject *) Owner object to send AOM_UPDATE when a JS event handler
    * should be run. */

#define AOBJ_Jsfocus       (AOBJ_Dummy+27)   /* SET,UPDATE */
#define AOBJ_Jsblur        (AOBJ_Dummy+28)   /* SET,UPDATE */
#define AOBJ_Jschange      (AOBJ_Dummy+29)   /* SET,UPDATE */
   /* (struct Jcontext *) When AOM_SET with Jcontext, execute the method in
    * this context.
    * When AOM_UPDATE with NULL Jcontext, run the event handler. */

#define AOBJ_Queuedata     (AOBJ_Dummy+30)   /* SET */
   /* (ULONG) General userdata sent with AOBJ_Queueid */

#define AOBJ_Statustext    (AOBJ_Dummy+31)   /* GET */
   /* (UBYTE *) Status text to be displayed. Only supported by objects that may return
    * AMHR_STATUS on an AOM_HITTEST message. */

#define AOBJ_Changedbgimage (AOBJ_Dummy+32) /* SET */
   /* (struct Bgimage *) Background image has changed, notify users to re-render */

#define AOBJ_     (AOBJ_Dummy+)
#define AOBJ_     (AOBJ_Dummy+)

/*---- Relationships ----*/

#define AOREL_DUMMYTAG(type)  ((type)<<16)

/*---- Methods ----*/

#define AOM_DUMMYTAG(type) ((type)<<16)

#define AOM_DUMMY          AOM_DUMMYTAG(0)

#define AOM_NEW            (AOM_DUMMY+1)
   /* Creates object, returns pointer to object */

#define AOM_SET            (AOM_DUMMY+2)
   /* Sets one or more attributes. Returns nonzero if it needs rendering */

#define AOM_GET            (AOM_DUMMY+3)
   /* Get one or more attributes */

#define AOM_DISPOSE        (AOM_DUMMY+4)
   /* Disposes the object */

#define AOM_RENDER         (AOM_DUMMY+5)
   /* Renders the object. */

#define AOM_HITTEST        (AOM_DUMMY+6)
   /* Check if coordinates fall within object. Return any URL referenced */

#define AOM_GOACTIVE       (AOM_DUMMY+7)
   /* Attempt to become the input focus. Returns AMR_ACTIVE, AMR_CHANGED or AMR_NOREUSE */

#define AOM_HANDLEINPUT    (AOM_DUMMY+8)
   /* Handle an input message. Returns AMR_ACTIVE, AMR_REUSE or AMR_NOREUSE */

#define AOM_GOINACTIVE     (AOM_DUMMY+9)
   /* De-activate */

#define AOM_UPDATE         (AOM_DUMMY+10)
   /* Re-render the active part of the object */

#define AOM_MEASURE        (AOM_DUMMY+11)
   /* Measure this object */

#define AOM_LAYOUT         (AOM_DUMMY+12)
   /* Layout this object */

#define AOM_ALIGN          (AOM_DUMMY+13)
   /* Align this object to the baseline */

#define AOM_SRCUPDATE      (AOM_DUMMY+14)
   /* An update of the source data or headers */

#define AOM_ADDCHILD       (AOM_DUMMY+15)
   /* Add a child object */

#define AOM_REMCHILD       (AOM_DUMMY+16)
   /* Remove a child object */

#define AOM_NOTIFY         (AOM_DUMMY+17)
   /* Process and pass to children */

#define AOM_MOVE           (AOM_DUMMY+18)
   /* Move object around */

#define AOM_SEARCHPOS      (AOM_DUMMY+19)
   /* Get the search start position */

#define AOM_SEARCHSET      (AOM_DUMMY+20)
   /* Scroll to found position */

#define AOM_GETREXX        (AOM_DUMMY+21)
   /* Get ARexx info */

#define AOM_DRAGTEST       (AOM_DUMMY+22)
   /* Find object for these drag coordinates */

#define AOM_DRAGRENDER     (AOM_DUMMY+23)
   /* Render selection between these two drag points */

#define AOM_DRAGCOPY       (AOM_DUMMY+24)
   /* Copy text between drag points */

#define AOM_JSETUP         (AOM_DUMMY+25)
   /* Setup JS environment */

#define AOM_JONMOUSE       (AOM_DUMMY+26)
   /* JS onMouseOver or onMouseOut event */

#define AOM_INSTALL        (AOM_DUMMY+98)
   /* Installs the dispatcher. */

#define AOM_DEINSTALL      (AOM_DUMMY+99)
   /* De-install the dispatcher. No object will be supplied. */


/*---- Activation codes ----*/
/* Returned for AOM_GOACTIVE and AOM_HANDLEINPUT */

#define AMR_ACTIVE         1
   /* Object wants to become or remain active. */

#define AMR_REUSE          2
   /* Object wants to become inactive, and event should be re-used */

#define AMR_NOREUSE        3
   /* Object doesn't want to become or remain active, and event should be thrown away */

#define AMR_CHANGED        4
   /* Object has accepted the activation message but wants to be deactivated immediately */

#define AMR_NOCARE         5
   /* Object doesn't care about this event, keep active or inactivate as per default */

#define AMR_DEFER          6
   /* Object wants to remain active, and wants to be called again after all input
    * messages have been read. */


/*---- Pointer types ----*/

#define APTR_DEFAULT       0
#define APTR_RESIZEHOR     1  /* Resize frame in horizontal direction (vertical edge) */
#define APTR_RESIZEVERT    2  /* Resize frame in vertical direction (horizontal edge) */
#define APTR_HAND          3  /* Pointing hand shape */
#define APTR_TEXT          4  /* Text input/selection pointer */
#define APTR_NOTALLOWED    5  /* Operation not allowed (disabled element) */
#define APTR_CONTEXTMENU   6  /* Context menu available */


/*---- Messages ----*/

struct Amessage
{  ULONG method;
};


/* AOM_NEW, AOM_SET, AOM_GET */
struct Amset
{  struct Amessage amsg;
   struct TagItem *tags;   /* Attributes to set or get */
};


/* AOM_MEASURE */
/* Object should determine its width and height.
 * When computing minimum width, pure text should add (addwidth) to the minimum
 * width of the first word of text. Pure text should return the width of the
 * last word (not ending in space) in (ammr->addwidth). */
struct Ammeasure
{  struct Amessage amsg;
   long width,height;      /* Target width and height */
   long addwidth;          /* Width to add to your first word. */
   USHORT flags;
   struct Buffer *text;    /* Text buffer to use for text buffer offsets */
   struct Ammresult *ammr;
};

#define AMMF_CHANGED    0x0001   /* Measure only new stuff since last measure */
#define AMMF_NOWRAP     0x0002   /* Don't wrap lines */
#define AMMF_MINHEIGHT  0x0004   /* Return minimum height too */

struct Ammresult
{  long width;             /* Nominal (maximum) width of object */
   long minwidth;          /* Minimum needed for this object */
   long minheight;         /* Minimum height needed, only if AMMF_MINHEIGHT */
   long addwidth;          /* Width to add to the first word of consecutive text */
   BOOL newline;           /* If next element is on a new line */
};

/* AOM_LAYOUT */
/* Object should layout itself.
 * Container objects like document or body layout their children.
 * Element objects should layout themselves in the x direction within the given
 * width and height, starting at startx (startx and width relative to same
 * starting point).
 * You should set endx at your end or break position in any case.
 * If you fit, return AMLR_OK.
 * If you don't fit and a break can appear before you, return AMLR_NOFIT.
 * If you don't fit and break can appear within you, layout the section that
 * fits and return AMLR_MORE. You will be called again with AMLF_MORE set.
 * If you don't fit and there is no known break position (e.g. in text, that
 * can be adjacent to previous text), return AMLR_BREAK. The parent will call
 * your predecessors with the AMLF_BREAK flag set. If this flag is set, you
 * may return:
 *  AMLR_OK if there can be a break after you,
 *  AMLR_NOFIT tf there can be a break before you,
 *  AMLR_MORE if there can be a break within you,
 *  AMLR_BREAK if there is no known break position.
 *
 * If the parent can't find a break position then it will call the nonfitting
 * element with the AMLF_FORCE flag set. Place yourself at startx regardless of
 * fitting. If you can break yourself you may return AMLR_MORE, but not AMLR_NOFIT
 * or AMLR_BREAK. If you are forced, and there is no break position after you,
 * return AMLR_FBREAK. Forcing will continue until a break is found.
 *
 * If called with AMLF_RETRY set, discard all sections that are layed out but
 * not yet aligned. Bullets are considered to be aligned when called with
 * AMLF_RETRY set.
 * If AMLF_FITHEIGHT is set, do a normal layout but stop if your height has
 * become greater than the target height. 
 *
 * If the AMLF_INTABLE flag is set, the object resides in a table cell.
 * In that case it should not attempt to fit itself to a height given as a
 * percentage.
 */
struct Amlayout
{  struct Amessage amsg;
   long width,height;      /* Target width and height to layout within */
   USHORT flags;
   struct Buffer *text;    /* Text buffer to use for text buffer offsets */
   long startx;            /* Start on this position within width */            
   struct Amlresult *amlr;
};

#define AMLF_MORE       0x0001   /* Layout remainder of object */
#define AMLF_RETRY      0x0002   /* Layout only non-aligned parts of the object */
#define AMLF_FORCE      0x0004   /* Layout even if it doesn't fit */
#define AMLF_BREAK      0x0008   /* Asking for break position */
#define AMLF_CHANGED    0x0010   /* Layout only if changed */
#define AMLF_NOWRAP     0x0020   /* Don't wrap lines */
#define AMLF_FITHEIGHT  0x0040   /* Fit in height or stop */
#define AMLF_FIRST      0x0080   /* First object in this line */
#define AMLF_INTABLE    0x0100   /* Object resides within table */

struct Amlresult
{  short result;
   long endx;              /* Resulting x position after this object or after break */
   long above;             /* Maximum height above the baseline */
   long below;             /* Maximum height below the baseline */
   long toph;              /* Maximum height for top-aligned objects */
   long bottomh;           /* Maximum height for absbottom-aligned objects */
};

/* result values: */
#define AMLR_OK         0x0001   /* Object fits */
#define AMLR_NOFIT      0x0000   /* Object doesnt fit, wants (wanted) */
#define AMLR_MORE       0x0003   /* Did partial, wants to be layed out again with AMLF_MORE
                                  * on a new line */
#define AMLR_BREAK      0x0004   /* Doesn't fit, and no known break position */
#define AMLR_FBREAK     0x0005   /* Forced to fit, but no break position after */
#define AMLR_NEWLINE    0x0009   /* Object fits, but terminates this line */
#define AMLR_FLOATING   0x0010   /* Object wants to float left or right */
#define AMLR_NLCLRLEFT  0x0029   /* Object fits, start new line clear to left margin */
#define AMLR_NLCLRRIGHT 0x0049   /* Object fits, start new line clear to right margin */
#define AMLR_NLCLRALL   0x0069   /* Object fits, start new line clear to both margins */
#define AMLR_FHAGAIN    0x0080   /* Object didn't fit after FITHEIGHT request, but
                                  * must be layed out again. */

/* result value flags: */
#define AMLRF_OK        0x0001
#define AMLRF_MORE      0x0002
#define AMLRF_BREAK     0x0004
#define AMLRF_NEWLINE   0x0008
#define AMLRF_FLOATING  0x0010
#define AMLRF_CLRLEFT   0x0020
#define AMLRF_CLRRIGHT  0x0040


/* AOM_ALIGN */
/* After having gathered the width and vertical alignment parameters, the parent
 * sends AOM_ALIGN's to its children. They should correct their x position (to
 * allow centering and right-alignment) and compute their y position. */
struct Amalign
{  struct Amessage amsg;
   long dx;                /* Add this to your x-position. */
   long y;                 /* Top y position. */
   long baseline;          /* Baseline relative to top y position */
   long height;            /* Total height of line */
};


/* AOM_RENDER */
/* The object should render itself.
 * The Coords structure, if present, contains offsets (dx,dy) to apply to the
 * object relative coordinates, in order to get RastPort coordinates.
 * If no Coords is present in the message, the object should obtain one
 * itself by calling Framecoords().
 * If the AMRF_UPDATESELECTED or AMRF_UPDATENORMAL flags are set, the object
 * need not rerender itself completely, only change its appearance into the
 * required state. */
struct Amrender
{  struct Amessage amsg;
   struct Coords *coords;  /* Rastport, coordinate offsets to use. If present,
                            * rastport is already clipped. If not present, then
                            * object should call Framecoords() and do clipping
                            * itself */
   struct Arect rect;      /* Frame relative coordinates of portion to render. */
   USHORT flags;
   struct Buffer *text;    /* Text buffer to use for text buffer offsets */
};

#define AMRF_CLEAR            0x0001   /* Clear background before rendering */
#define AMRF_DISPTITLE        0x0002   /* Display title in window */
#define AMRF_UPDATESELECTED   0x0004   /* Update yourself to selected state */
#define AMRF_UPDATENORMAL     0x0008   /* Update yourself to normal state */
#define AMRF_CHANGED          0x0010   /* Render only new stuff since last layout */
#define AMRF_CLEARHL          0x0020   /* Clear highlight portion */
#define AMRF_CLEARBG          0x0040   /* Clear only if a different background applies */

#define AMRMAX          0x3fffffff     /* Use as reasonable maximum render limit */


/* AOM_HITTEST */
/* When the mouse pointer falls within the aox/aoy/aow/aoh box, objects are
 * sent this message. The object should check if coordinates (after proper offset)
 * fall within its activatable limits. To offset, subtract coords->dx,dy from
 * xco,yco and compare with your internal aox/aoy/aow/aoh relative coordinates.
 * If that is the case:
 * If (oldobject) is zero or not equal to you, or you want to display a new
 * statustext, you should return AMHR_NEWHIT.
 * If (oldobject) is equal to you, and you are still hit by these coordinates,
 * and there is no change in the statustext, return AMHR_OLDHIT.
 * In both cases, fill in (amhr->object).
 * If there is a new statustext to display, create a dynamic string and store
 * it in (amlr->text). In case of an old hit with unchanged statustext, leave
 * this field NULL. The previous string will be used.
 * The same holds true for the tooltip text.
 *
 * If you *only* want to display a status text but don't want to process further
 * input, return AMHR_STATUS. You will then be queried for the status text
 * (AOBJ_Statustext).
 *
 * If you would support a popup menu at the current position if the AMHF_POPUP
 * flag would have been set, then OR your return code with the AMHR_POPUP flag.
 *
 * If you support scrolling, and no child object has set the focus field already
 * you mey set it to yourself. On the next mouse click the keyboard scroll focus
 * will be set to you. */
struct Amhittest
{  struct Amessage amsg;
   struct Coords *coords;  /* Coordinate offsets */
   long xco,yco;           /* Rastport coordinates to check. Offset these with
                            * the (coords) values */
   USHORT flags;           /* Special flags, see below */
   struct Aobject *oldobject;
                           /* Previously hit object, checked again after mouse move. */
   struct Amhresult *amhr;
};

/* hittest flags */
#define AMHF_DOWNLOAD   0x0001   /* Download qualifier is active */
#define AMHF_POPUP      0x0002   /* Popup menu qualifier is active */
#define AMHF_DEFER      0x0004   /* Deferred action for AOM_HANDLEINPUT */
#define AMHF_POPUPREL   0x0008   /* Popup qualified mouse button is released */

struct Amhresult
{  void *object;           /* The object hit, should be ready to receive AOM_GOACTIVE. */
   UBYTE *text;            /* Status gadget text. Dynamic string. */
   void *focus;            /* The object that wants keyboard scroll focus. */
   void *jonmouse;         /* The new object that wants AOM_JONMOUSE */
   USHORT ptrtype;         /* Pointer type to set while this object is hit */
   UBYTE *tooltip;         /* Tooltip text. Dynamic string. */
};

/* hittest return values: */
#define AMHR_NOHIT      0x0000
#define AMHR_NEWHIT     0x0001   /* Coordinates fall within new object */
#define AMHR_OLDHIT     0x0002   /* Coordinates still fall within old object */
#define AMHR_STATUS     0x0003   /* No object hit but query me for status text */
#define AMHR_POPUP      0x0100   /* Supports popup menu */

#define AMHR_RESULTMASK 0x00ff

/* AOM_GOACTIVE */
/* The user clicked within the activatable limits of this object. An AOM_HITTEST
 * was done immediately before this message to ensure this.
 * If the (imsg) field is not a mouse click, this object could have been activated
 * by another object. In that case no AOM_HITTEST was done before.
 * The object may set the resurn value to one of three:
 * AMR_ACTIVE: if the object becomes active and wants to receive AOM_HANDLEINPUT
 *    messages.
 * AMR_CHANGED: if the object accepts activation but wants to be inactivated
 *    immediately. An AOM_GOINACTIVE message will be sent to you.
 * AMR_NOREUSE: if the object doesn't want to become active. */
struct Amgoactive
{  struct Amessage amsg;
   struct IntuiMessage *imsg;
                           /* The IntuiMessage that caused activation. The
                            * MouseX/Y coordinates in this message are RastPort
                            * coordinates and have to be corrected with Coords
                            * offsets (you have to request a Coords yourself). */
   USHORT flags;           /* AMHF_DOWNLOAD or AMHF_POPUP qualifier */
};


/* AOM_HANDLEINPUT */
/* If you returned AMR_ACTIVE on a previous AOM_GOACTIVE, you will be sent these
 * messages for every IntuiMessage that AWeb receives. Note that the MouseX and Y
 * coordinates in the IntuiMessage are RastPort coordinates. Before using them
 * you should correct them by subtracting the dx and dy values from a new Coords.
 * Note: it is NOT allowed to cache a Coords structure between AOM_HANDLEINPUT
 * processings. The object may have been moved around.
 * After processing the message, you may return one of three:
 * AMR_ACTIVE: if you want to stay active.
 * AMR_DEFER: if you want to stay active but want to be called again after all
 *    queued IntuiMessages have been read.
 * AMR_REUSE: if you want to be inactivated, and the IntuiMessage should be used
 *    for further processing.
 * AMR_NOREUSE: if you want to be inactivated and the IntuiMessage should be
 *    thrown away.
 * Additionally you may return AMR_NOCARE if you don't know what to do with this
 * event. You will be kept active, or deactivated, as per default.
 * If you want to be inactivated you will be sent an AOM_GOINACTIVE message.
 * If the statustext changes as a result of this IntuiMessage (other than clearing
 * it because of inactivation), you can supply a new dynamic string in
 * (amir->text). If the statustext doesn't change, leave this field NULL.
 * If the input event would result in activation of another object, ask your
 * parent what object that would be. Return a pointer in (amir->newobject) and
 * set return value AMR_NOREUSE.
 *
 * If you return AMR_DEFER, you will be send an AOM_HANDLEINPUT message with
 * no (imsg), the AMHF_DEFER flag set and no (amir) after all IntuiMessages have
 * been processed. You may want to use this to integrate a stream of events (like
 * MOUSEMOVE events) and process the integrated result only once. If you have
 * returned AMHF_DEFER before when you return AMR_REUSE or AMR_NOREUSE, you
 * will be sent a AMHF_DEFER type AOM_HANDLEINPUT before the AOM_GOINACTIVE.
 *
 * If the popup menu mouse button was released, the AMHF_POPUPREL flag is set.
 * This allows the catch of all releases, depending on the activation setting.
 *
 * Note that the DOWNLOAD or POPUP qualifier may have changed when receiving an
 * IDCMP_RAWKEY class Intuimessage. */
struct Aminput
{  struct Amessage amsg;
   struct IntuiMessage *imsg;
                           /* The IntuiMessage to be handled. */
   USHORT flags;           /* AMHF_DOWNLOAD or AMHF_POPUP qualifier */
   struct Amiresult *amir;
};

struct Amiresult
{  UBYTE *text;            /* New status gadget text. Dynamic string. */
   struct Aobject *newobject;
                           /* If not NULL, this object wants to become the active one. */
};


/* AOM_GOINACTIVE */
/* After the object requested to be inactivated this message will be sent.
 * Also this message could be sent if AWeb forces the object to become
 * inactive, for example if a new document is loaded in the window. */
/* The AOM_GOINACTIVE method uses a standard Amessage structure. */


/* AOM_UPDATE */
/* Set objects as a result of an update of other objects attribute change. */
/* The AOM_UPDATE method uses an Amset structure */


/* AOM_SRCUPDATE */
/* Something was input for the addressed object. Tags are URL's scrupdate
 * tags.
 * The message contains the address of the FETCH object to address for interrupt etc. */
struct Amsrcupdate
{  struct Amessage amsg;
   struct Aobject *fetch;  /* The FETCH object reading this */
   struct TagItem *tags;   /* See url.h */
};


/* AOM_ADDCHILD, AOM_REMCHILD */
/* Object should add this child to its list of children, or remove it.
 * The relation attribute can be used to implement different parent child
 * relations. */
struct Amadd
{  struct Amessage amsg;
   struct Aobject *child;  /* child object to add or remove */
   ULONG relation;         /* parent child relation */
};


/* AOM_NOTIFY */
/* Object should process the message if it understands it, and then forward
 * the NOTIFY message to all its children (unless the message processing
 * forbids this). */
struct Amnotify
{  struct Amessage amsg;
   struct Amessage *nmsg;  /* The message to process and forward */
};


/* AOM_MOVE */
/* Object should change its aox and aoy coordinates, and pass the message
 * to its children */
struct Ammove
{  struct Amessage amsg;
   long dx,dy;             /* Add these to your aox,aoy. */
};


/* AOM_SEARCHPOS */
/* Frame object should fill in the current top position. Copydriver should set
 * a Buffer pointer and the buffer position corresponding to this top
 * position, and the first valid buffer position. Return value during the
 * actual search is zero for not yet found, nonzero for found.
 * If AMSF_CURRENTPOS is not set, the current top position is not needed. Just
 * set the buffer and startpos. */
struct Amsearch
{  struct Amessage amsg;
   USHORT flags;
   long left,top;          /* Text display position */
   struct Buffer *text;    /* Text buffer to search in */
   long pos;               /* Buffer position */
   long length;            /* Length of highlight */
   long startpos;          /* First valid buffer position */
};

#define AMSF_CURRENTPOS    0x0001   /* Find the current position, otherwise only startpos */
#define AMSF_HIGHLIGHT     0x0002   /* Highlight section */
#define AMSF_UNHIGHLIGHT   0x0004   /* Remove highlight */


/* AOM_SEARCHSET */
/* Copydriver should find the scroll position corresponding to this
 * buffer position. Return value during this search is zero for not yet
 * found, nonzero for found. If AMSF_HIGHLIGHT is set, the section of
 * the text should be highlighted.
 * Frame object should then scroll to this position.
 * If AMSF_UNHIGHLIGHT is set, the hightlight should be removed, and
 * the frame should not be scrolled. */
/* The AOM_SEARCHSET method uses an Amsearch structure. */


/* AOM_GETREXX */
/* Will be sent in a notify message.
 * Copydriver should obtain the information, and set the next stem
 * variable(s). Then pass to childs when appropriate. Frames should
 * forward this message only if the (frame) is NULL, or equal to
 * the frame itself.
 * (index) is the last index number used for the stem, and must be
 * incremented before each Setstemvar(). Since index numbers start
 * at 1, it is also the number of variables set so far. */
struct Amgetrexx
{  struct Amessage amsg;
   struct Aobject *frame;  /* Target frame */
   long info;              /* What to get, see below */
   struct Arexxcmd *ac;    /* Arexx context */
   UBYTE *stem;            /* Stem base name */
   long index;             /* Last used stem index */
};

#define AMGRI_FRAMES       1  /* Frame names */
#define AMGRI_LINKS        2  /* Hyperlinks */
#define AMGRI_IMAGES       3  /* Image urls */
#define AMGRI_INFO         4  /* HTTP headers, META, LINK */
#define AMGRI_ALLFRAMES    5  /* Frame names, recursive */
#define AMGRI_NAMES        6  /* Fragment names */


/* AOM_DRAGTEST */
/* When the user starts dragging the mouse, the first draggable object is
 * determined. This is the drag start point. After each mouse move, the
 * new drag end point is searched.
 * Only clipboard selectable (text) objects must respond to this message.
 * The first object is wanted that either:
 * - Contains the X and Y coordinates
 * - Contains the Y coordinate and has left edge to the right of X coordinate
 * - Has top edge below the Y coordinate.
 * Elements will receive this message only if these conditions are met.
 *
 * Object must return itself, an optional ULONG to indicate some internal
 * position (will be passed with AOM_DRAGRENDER and AOM_DRAGCOPY), and return
 * AMDR_HIT.
 *
 * If cursor is not on draggable section but scan must not continue, return
 * AMDR_STOP.
 */
struct Amdragtest
{  struct Amessage amsg;
   struct Coords *coords;  /* Coordinate offsets */
   long xco,yco;           /* Rastport coordinates to check. Offset these with
                            * the (coords) values */
   struct Amdresult *amdr;
};

struct Amdresult
{  void *object;           /* The object hit */
   ULONG objpos;           /* Position within the object */
};

/* Drag return values */
#define AMDR_NOHIT      0x0000
#define AMDR_HIT        0x0001
#define AMDR_STOP       0x0002


/* AOM_DRAGRENDER */
/* After drag start and end points are established, this message highlights
 * the block.
 * Actions for the start object, depending on the current state:
 *  AMDS_BEFORE:  highlight from the startobjpos position, unhighlight
 *                before. Set state to AMDS_RENDER.
 *  AMDS_REVERSE: highlight upto the startobjpos position. Unhighlight
 *                after. Set state to AMDS_AFTER.
 *
 * Actions for the end object, depending on the current state:
 *  AMDS_BEFORE:  highlight from the endobjpos position, unhighlight
 *                before. Set state to AMDS_REVERSE.
 *  AMDS_RENDER:  highlight upto the endobjpos position. Unhighlight
 *                after. Set state to AMDS_AFTER.
 *
 * Actions for other objects, depending on the current state:
 *  AMDS_BEFORE:  unhighlight.
 *  AMDS_RENDER, AMDS_REVERSE: highlight.
 *  AMDS_AFTER:   unhighlight.
 *
 * All objects that receive (or set) AMDS_AFTER and were not highlighted
 * before should set the state to AMDS_DONE.
 *
 * Note that the start and end objects may be the same.
 * If both are NULL, the first object that was highlighted should change
 * the state from AMDS_BEFORE to AMDS_AFTER.
 */
struct Amdragrender
{  struct Amessage amsg;
   struct Coords *coords;  /* Rastport, coordinate offsets */
   void *startobject;      /* Drag start object */
   ULONG startobjpos;      /* Position within the object */
   void *endobject;        /* Drag end object */
   ULONG endobjpos;        /* Position within the object */
   USHORT state;           /* Processing state */
};

#define AMDS_BEFORE     0  /* Before first object */
#define AMDS_RENDER     1  /* Between start and end object */
#define AMDS_REVERSE    2  /* Between end and start object */
#define AMDS_AFTER      3  /* After last object, but still updating */
#define AMDS_DONE       4  /* Nothing left to update */


/* AOM_DRAGCOPY */
/* Selected text must be copied to the clip buffer.
 * Actions are like AOM_DRAGRENDER, but instead of highlighting the
 * text should be added to the buffer.
 */
struct Amdragcopy
{  struct Amessage amsg;
   void *startobject;      /* Drag start object */
   ULONG startobjpos;      /* Position within the object */
   void *endobject;        /* Drag end object */
   ULONG endobjpos;        /* Position within the object */
   USHORT state;           /* Processing state */
   struct Buffer *clip;    /* Buffer to add text to */
};


/* AOM_JSETUP */
/* Set up a JS environment.
 * Frame should create (jc) is it doesn't exist yet, create JS object
 * for itself and pass the message to its children.
 */
struct Amjsetup
{  struct Amessage amsg;
   struct Jcontext *jc;    /* JS context */
   struct Jobject *parent; /* JS object of parent */
   struct Jobject *parentframe;  /* JS object of parent frame */
};


/* AOM_JONMOUSE */
/* Sent to object after AOM_HITTEST to trigger the JS onMouseOver
 * or onMouseOut events.
 */
struct Amjonmouse
{  struct Amessage amsg;
   USHORT event;           /* See below */
};

#define AMJE_ONMOUSEOVER   1  /* onMouseOver event */
#define AMJE_ONMOUSEOUT    2  /* onMouseOut event */


/* AOM_INSTALL */
/* OO system should install this dispatcher for this object type.
 * If object type is zero on input, assign a new ID.
 * Returns the object type, or zero in case of error. */
struct Aminstall
{  struct Amessage amsg;
   long objecttype;
   long (*dispatcher)(void *,struct Amessage *);
};


#endif