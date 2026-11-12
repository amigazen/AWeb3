#ifndef LOCALE_H
#define LOCALE_H


/****************************************************************************/


/* This file was created automatically by CatComp.
 * Do NOT edit by hand!
 */


#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifdef CATCOMP_ARRAY
#undef CATCOMP_NUMBERS
#undef CATCOMP_STRINGS
#define CATCOMP_NUMBERS
#define CATCOMP_STRINGS
#endif

#ifdef CATCOMP_BLOCK
#undef CATCOMP_STRINGS
#define CATCOMP_STRINGS
#endif


/****************************************************************************/


#ifdef CATCOMP_NUMBERS

#define MSG_REQUEST_TITLE 1
#define MSG_ABOUT_TITLE 100
#define MSG_ABOUT_REGNR 101
#define MSG_ABOUT_REGTO 102
#define MSG_ABOUT_SCREENNAME 103
#define MSG_ABOUT_PORTNAME 104
#define MSG_ABOUT_EMAIL 105
#define MSG_ABOUT_WWW 106
#define MSG_ABOUT_OK 107
#define MSG_ABOUT_UNREGISTERED 108
#define MSG_ABOUT_DEMO 109
#define MSG_ABOUT_DISTRIBUTED 110
#define MSG_ABOUT_TRANSLATOR 111
#define MSG_ABOUT_SPECIAL 112
#define MSG_QUIT_TEXT 200
#define MSG_QUIT_OK 201
#define MSG_QUIT_CANCEL 202
#define MSG_QUIT_WARNING 203
#define MSG_DEMO_TEXT 300
#define MSG_DEMO_OK 301
#define MSG_SCRCLOSE_TEXT 400
#define MSG_SCRCLOSE_OK 401
#define MSG_NWS_TITLE 500
#define MSG_NWS_QUEUED 501
#define MSG_NWS_STARTED 502
#define MSG_NWS_LOOKUP 503
#define MSG_NWS_CONNECT 504
#define MSG_NWS_WAIT 505
#define MSG_NWS_PROCESS 506
#define MSG_NWS_LOGIN 507
#define MSG_NWS_CPS 508
#define MSG_NWS_NEWSGROUP 509
#define MSG_NWS_UPLOAD 510
#define MSG_AUTH_TITLE 600
#define MSG_AUTH_PROMPT 601
#define MSG_AUTH_USERID 602
#define MSG_AUTH_PASSWORD 603
#define MSG_AUTH_OK 604
#define MSG_AUTH_CANCEL 605
#define MSG_AUTH_PROMPT_FTP 606
#define MSG_AUTH_USERID_FTP 607
#define MSG_AUTH_PROMPT_PROXY 608
#define MSG_FILE_SAVETITLE 700
#define MSG_FILE_SAVE 701
#define MSG_FILE_INCTEXT 702
#define MSG_DUMMY_1 703
#define MSG_FILE_EXISTTEXT 704
#define MSG_FILE_EXISTBUTTONS 705
#define MSG_FILE_AREXXTITLE 706
#define MSG_FILE_HOTLISTTITLE 707
#define MSG_FILE_LOCALTITLE 708
#define MSG_FILE_OPEN 709
#define MSG_FILE_TEMPTITLE 710
#define MSG_FILE_TEMPBUTTONS 711
#define MSG_FILE_INCBUTTONS2 712
#define MSG_FILE_SETTINGSTITLE 713
#define MSG_FILE_UPLOADTITLE 714
#define MSG_TCP_ENDTCP 800
#define MSG_TCP_BUTTONS 801
#define MSG_STARTUP_FONTS 900
#define MSG_STARTUP_IMAGES 901
#define MSG_STARTUP_CACHE 902
#define MSG_HOTL_TITLE 1000
#define MSG_HOTL_ADDLINK 1001
#define MSG_HOTL_ADDGROUP 1002
#define MSG_HOTL_REMOVE 1003
#define MSG_HOTL_FOLLOW 1004
#define MSG_HOTL_NAME 1005
#define MSG_HOTL_URL 1006
#define MSG_HOTL_NEWGROUP 1007
#define MSG_HOTL_NEWLINK 1008
#define MSG_HOTL_TITLE_VIEWER 1009
#define MSG_HOTL_VURLS 1010
#define MSG_HOTL_MANAGER 1011
#define MSG_HOTL_TITLE_MANAGER 1012
#define MSG_HOTL_LTYPE 1013
#define MSG_HOTL_LTYPE_REST 1014
#define MSG_HOTL_LTYPE_ALL 1015
#define MSG_HOTL_LTYPE_GROUPS 1016
#define MSG_HOTL_LSORT 1017
#define MSG_HOTL_LSORT_TITLE 1018
#define MSG_HOTL_LSORT_URL 1019
#define MSG_HOTL_MOVEIN 1020
#define MSG_HOTL_MOVEOUT 1021
#define MSG_HOTL_MOVE 1022
#define MSG_HOTL_DEL 1023
#define MSG_HOTL_WHERE 1024
#define MSG_HOTL_SHOW 1025
#define MSG_HOTL_SORT 1026
#define MSG_HOTL_PARENT 1027
#define MSG_HOTL_ALL 1028
#define MSG_HOTL_ADDENTRY 1029
#define MSG_HOTL_ADDGROUP2 1030
#define MSG_HOTL_URLS 1031
#define MSG_HOTL_REVERT 1032
#define MSG_HOTL_SAVE 1033
#define MSG_HOTL_LADD 1034
#define MSG_HOTL_FIND 1035
#define MSG_HOTL_PATTERN 1036
#define MSG_HOTL_NEXT 1037
#define MSG_HOTL_DEFAULT_TITLE 1038
#define MSG_HOTL_DEFAULT_GROUP 1039
#define MSG_HOTL_TITLE_ALLGROUPS 1040
#define MSG_HOTL_TITLE_WHERE 1041
#define MSG_HOTL_TITLE_REST 1042
#define MSG_HOTL_TITLE_ALL 1043
#define MSG_HOTL_TITLE_ROOTGROUPS 1044
#define MSG_HOTL_LSORT_DATE 1045
#define MSG_WHIS_TITLE 1100
#define MSG_WHIS_FILTER 1101
#define MSG_WHIS_WINDOW 1102
#define MSG_WHIS_ORDER 1103
#define MSG_WHIS_DISPLAY 1104
#define MSG_WHIS_ORDER_NATURAL 1105
#define MSG_WHIS_ORDER_MAINLINE 1106
#define MSG_WHIS_ORDER_RETRIEVED 1107
#define MSG_WHIS_ORDER_TITLE 1108
#define MSG_WHIS_ORDER_URL 1109
#define MSG_CABR_TITLE 1200
#define MSG_CABR_URL 1201
#define MSG_CABR_DATE 1202
#define MSG_CABR_SIZE 1203
#define MSG_CABR_TYPE 1204
#define MSG_CABR_FILE 1205
#define MSG_CABR_STATUS 1206
#define MSG_CABR_SORTBY 1207
#define MSG_CABR_OPEN 1208
#define MSG_CABR_SAVE 1209
#define MSG_CABR_DELETE 1210
#define MSG_CABR_FIND 1211
#define MSG_CABR_PATTERN 1212
#define MSG_CABR_NEXT 1213
#define MSG_PROGRESS_TITLE 1300
#define MSG_PROGRESS_CANCEL 1301
#define MSG_FIXCACHE_WARNING 1400
#define MSG_FIXCACHE_BUTTONS 1401
#define MSG_FIXCACHE_PROGRESS 1402
#define MSG_FIXCACHE_DELETEIMAGES 1403
#define MSG_FIXCACHE_DELETEDOCS 1404
#define MSG_FIXCACHE_ERASE 1405
#define MSG_FIXCACHE_CORRUPT 1406
#define MSG_SEARCH_STRING 1500
#define MSG_SEARCH_IGNORECASE 1501
#define MSG_SEARCH_SEARCH 1502
#define MSG_SEARCH_FROMTOP 1503
#define MSG_SEARCH_BACKWARDS 1504
#define MSG_SEARCH_CANCEL 1505
#define MSG_SEARCH_NOTFOUND 1506
#define MSG_SEARCH_OK 1507
#define MSG_POPUP_LOADIMAGE 1600
#define MSG_POPUP_RELOADIMAGE 1601
#define MSG_POPUP_SAVEIMAGE 1602
#define MSG_POPUP_DOWNLOADIMAGE 1603
#define MSG_POPUP_FLUSHIMAGE 1604
#define MSG_POPUP_SHOWIMAGE 1605
#define MSG_POPUP_OPENLINK 1606
#define MSG_POPUP_OPENLINKNW 1607
#define MSG_POPUP_LOADLINK 1608
#define MSG_POPUP_SAVELINK 1609
#define MSG_POPUP_DOWNLOADLINK 1610
#define MSG_POPUP_ADDLINK 1611
#define MSG_POPUP_RELOADFRAME 1612
#define MSG_POPUP_SAVEFRAME 1613
#define MSG_POPUP_VIEWFRAME 1614
#define MSG_POPUP_SHOWFRAME 1615
#define MSG_POPUP_SEARCHFRAME 1616
#define MSG_POPUP_INFOFRAME 1617
#define MSG_PRINTP_TITLE 1700
#define MSG_PRINTP_SCALE 1701
#define MSG_PRINTP_PRINTBG 1702
#define MSG_PRINTP_FORMFEED 1703
#define MSG_PRINTP_CENTER 1704
#define MSG_PRINTP_PRINT 1705
#define MSG_PRINTP_CANCEL 1706
#define MSG_PRINT_NOPRINTER 1707
#define MSG_PRINT_OK 1708
#define MSG_PRINT_PROGRESS 1709
#define MSG_PRINTP_LAYOUTWIDTH 1710
#define MSG_PRINTP_LW_WINDOW 1711
#define MSG_PRINTP_LW_DOCUMENT 1712
#define MSG_PRINTP_LW_SCREEN 1713
#define MSG_COOKIE_TITLE 1800
#define MSG_COOKIE_WARNING 1801
#define MSG_COOKIE_NAME 1802
#define MSG_COOKIE_VALUE 1803
#define MSG_COOKIE_DOMAIN 1804
#define MSG_COOKIE_PATH 1805
#define MSG_COOKIE_COMMENT 1806
#define MSG_COOKIE_MAXAGE 1807
#define MSG_COOKIE_EXPIRES 1808
#define MSG_COOKIE_ONCE 1809
#define MSG_COOKIE_ACCEPT 1810
#define MSG_COOKIE_NEVER 1811
#define MSG_COOKIE_CANCEL 1812
#define MSG_COOKIE_SECURE 1813
#define MSG_COOKIE_UNSECURE 1814
#define MSG_INFO_TITLE 1900
#define MSG_INFO_FROMCACHE 1901
#define MSG_INFO_XFER 1902
#define MSG_INFO_META 1903
#define MSG_INFO_LINK 1904
#define MSG_INFO_CIPHER 1905
#define MSG_INFO_SSLLIBRARY 1906
#define MSG_USERBUTTON_CACHE 2000
#define MSG_USERBUTTON_CLOCK 2001
#define MSG_AUTHEDIT_TITLE 2100
#define MSG_AUTHEDIT_TITLE_SERVER 2101
#define MSG_AUTHEDIT_TITLE_USERID 2102
#define MSG_AUTHEDIT_DEL 2103
#define MSG_AUTHEDIT_SERVER 2104
#define MSG_AUTHEDIT_USERID 2105
#define MSG_AUTHEDIT_PASSWORD 2106
#define MSG_JSPROMPT_OK 2200
#define MSG_JSPROMPT_CANCEL 2201
#define MSG_FORMWARN_TITLE 2300
#define MSG_FORMWARN_WARNING 2301
#define MSG_FORMWARN_BUTTONS 2302
#define MSG_MAIL_TITLE 2400
#define MSG_MAIL_NOSMTPHOST 2401
#define MSG_MAIL_NOCONNECT 2402
#define MSG_MAIL_MAILFAILED 2403
#define MSG_MAIL_BUTTONS 2404
#define MSG_MAIL_SAVETITLE 2405
#define MSG_MAIL_MAILTO_TITLE 2406
#define MSG_MAIL_MAILTO_HEADER 2407
#define MSG_MAIL_TO 2408
#define MSG_MAIL_SUBJECT 2409
#define MSG_MAIL_MESSAGE_BODY 2410
#define MSG_MAIL_EXTRA_HEADERS 2411
#define MSG_MAIL_SEND 2412
#define MSG_MAIL_RESET 2413
#define MSG_MAIL_RETURN 2414
#define MSG_MAIL_MAIL_SENT_TITLE 2415
#define MSG_MAIL_MAIL_SENT 2416
#define MSG_NEWS_TITLE 2500
#define MSG_NEWS_NONNTPHOST 2501
#define MSG_NEWS_NOCONNECT 2502
#define MSG_NEWS_POSTFAILED 2503
#define MSG_NEWS_BUTTONS 2504
#define MSG_NEWS_SAVETITLE 2505
#define MSG_NEWS_NEWS_HEADER 2506
#define MSG_NEWS_SUBSCRIBED 2507
#define MSG_NEWS_NEW_ARTICLES 2508
#define MSG_NEWS_CATCH_UP 2509
#define MSG_NEWS_UNSUBSCRIBE 2510
#define MSG_NEWS_OTHER_GROUP 2511
#define MSG_NEWS_SUBSCRIBE 2512
#define MSG_NEWS_READ 2513
#define MSG_NEWS_SEARCH_FOR_GROUP 2514
#define MSG_NEWS_SEARCH 2515
#define MSG_NEWS_LONG_DOWNLOAD 2516
#define MSG_NEWS_NO_SUBJECT 2517
#define MSG_NEWS_LINES 2518
#define MSG_NEWS_POST_NEW_ARTICLE 2519
#define MSG_NEWS_GROUP_LIST 2520
#define MSG_NEWS_ERROR_NO_GROUP 2521
#define MSG_NEWS_USE_FRAMES 2522
#define MSG_NEWS_ERROR_NO_ARTICLE 2523
#define MSG_NEWS_FOLLOW_UP 2524
#define MSG_NEWS_REPLY 2525
#define MSG_NEWS_SAVE 2526
#define MSG_NEWS_SUBJECT 2527
#define MSG_NEWS_NEWSGROUPS 2528
#define MSG_NEWS_ARTICLE_BODY 2529
#define MSG_NEWS_EXTRA_HEADERS 2530
#define MSG_NEWS_POST_ARTICLE 2531
#define MSG_NEWS_RESET 2532
#define MSG_NEWS_RETURN 2533
#define MSG_NEWS_FOLLOWUP_TITLE 2534
#define MSG_NEWS_FOLLOWUP_TO_GROUPS 2535
#define MSG_NEWS_PLEASE_DELETE 2536
#define MSG_NEWS_REPLY_TITLE 2537
#define MSG_NEWS_REPLY_TO_ADDRESS 2538
#define MSG_NEWS_ARTICLE_POSTED_TITLE 2539
#define MSG_NEWS_ARTICLE_POSTED 2540
#define MSG_NEWS_DOWNLOAD_TITLE 2541
#define MSG_NEWS_DOWNLOAD_BODY 2542
#define MSG_NEWS_SEARCH_RESULT 2543
#define MSG_NEWS_FIRST_MATCHING 2544
#define MSG_NEWS_FOUND_MATCHING 2545
#define MSG_CMDWARN_TITLE 2600
#define MSG_CMDWARN_SHELL 2601
#define MSG_CMDWARN_AREXX 2602
#define MSG_CMDWARN_BUTTONS 2603
#define MSG_SSLWARN_SSL_TITLE 2700
#define MSG_SSLWARN_SSL_TEXT 2701
#define MSG_SSLWARN_SSL_BUTTONS 2702
#define MSG_SSLWARN_SSL_NO_SSL 2703
#define MSG_SSLWARN_CERT_TITLE 2704
#define MSG_SSLWARN_CERT_TEXT 2705
#define MSG_SSLWARN_CERT_BUTTONS 2706
#define MSG_SSLWARN_SSL_NO_SSL2 2707
#define MSG_UNKMIME_TITLE 2800
#define MSG_UNKMIME_TEXT 2801
#define MSG_UNKMIME_BUTTONS 2802
#define MSG_REQUEST_COPYURL 2803
#define MSG_OPENURL_TITLE 2900
#define MSG_OPENURL_TEXT 2901
#define MSG_OPENURL_BUTTONS 2902
#define MSG_SAVEIFF_PROGRESS 3000
#define MSG_PROJECT_MENU 10000
#define MSG_PROJECT_NEWWINDOW 10001
#define MSG_PROJECT_CLOSEWINDOW 10002
#define MSG_PROJECT_OPENURL 10003
#define MSG_PROJECT_OPENWWW 10004
#define MSG_PROJECT_OPENLOCAL 10005
#define MSG_PROJECT_OPENSEARCH 10006
#define MSG_PROJECT_SOURCE 10007
#define MSG_PROJECT_SAVEHTML 10008
#define MSG_PROJECT_ABOUT 10009
#define MSG_PROJECT_QUIT 10010
#define MSG_PROJECT_SEARCH 10011
#define MSG_PROJECT_PRINT 10012
#define MSG_PROJECT_EDIT 10013
#define MSG_PROJECT_INFO 10014
#define MSG_PROJECT_OPENNEWS 10015
#define MSG_PROJECT_ICONIFY 10016
#define MSG_PROJECT_SAVEIFF 10017
#define MSG_CONTROL_MENU 10100
#define MSG_CONTROL_LOADNOW 10101
#define MSG_CONTROL_LOADNOWALL 10102
#define MSG_CONTROL_LOADNOWMAPS 10103
#define MSG_CONTROL_NETWORKSTATUS 10104
#define MSG_DUMMY_2 10105
#define MSG_CONTROL_CANCEL 10106
#define MSG_CONTROL_NEXTWINDOW 10107
#define MSG_CONTROL_PREVWINDOW 10108
#define MSG_CONTROL_NOPROXY 10109
#define MSG_CONTROL_RLOAD 10110
#define MSG_CONTROL_RLOADDOC 10111
#define MSG_CONTROL_RLOADIMGS 10112
#define MSG_CONTROL_PLAYBGSOUND 10113
#define MSG_CONTROL_COPYURL 10114
#define MSG_CONTROL_PASTEURL 10115
#define MSG_CONTROL_COPYBLOCK 10116
#define MSG_CONTROL_DRAGGING 10117
#define MSG_CONTROL_BREAKJS 10118
#define MSG_CONTROL_DEBUGJS 10119
#define MSG_CONTROL_RESET 10120
#define MSG_CACHE_MENU 10200
#define MSG_DUMMY_3 10201
#define MSG_DUMMY_4 10202
#define MSG_DUMMY_5 10203
#define MSG_DUMMY_6 10204
#define MSG_CACHE_SAVEAUTH 10205
#define MSG_CACHE_FLUSHAUTH 10206
#define MSG_CACHE_BROWSER 10207
#define MSG_CACHE_FLUSH 10208
#define MSG_CACHE_FLSHIMGSND 10209
#define MSG_CACHE_FLSHIMGS 10210
#define MSG_CACHE_FLSHDOCSND 10211
#define MSG_CACHE_DELETE 10212
#define MSG_CACHE_DELIMGS 10213
#define MSG_CACHE_DELDOCS 10214
#define MSG_CACHE_CLEAR 10215
#define MSG_CACHE_FIX 10216
#define MSG_CACHE_EDITAUTH 10217
#define MSG_NAVIGATE_MENU 10300
#define MSG_NAVIGATE_BACK 10301
#define MSG_NAVIGATE_FWD 10302
#define MSG_NAVIGATE_HOME 10303
#define MSG_NAVIGATE_HISTORY 10304
#define MSG_HOTLIST_MENU 10400
#define MSG_HOTLIST_ADDHOT 10401
#define MSG_HOTLIST_SHOWHOT 10402
#define MSG_HOTLIST_MAINT 10403
#define MSG_HOTLIST_SAVE 10404
#define MSG_HOTLIST_RESTORE 10405
#define MSG_HOTLIST_AMHOTRX 10406
#define MSG_HOTLIST_AMHOT20 10407
#define MSG_HOTLIST_IBHOT 10408
#define MSG_HOTLIST_VIEW 10409
#define MSG_HOTLIST_MGR 10410
#define MSG_SETTINGS_MENU 10500
#define MSG_SETTINGS_LOADIMG 10501
#define MSG_SETTINGS_LOADIMGALL 10502
#define MSG_SETTINGS_LOADIMGMAPS 10503
#define MSG_SETTINGS_LOADIMGOFF 10504
#define MSG_DUMMY_7 10505
#define MSG_DUMMY_8 10506
#define MSG_SETTINGS_BROWSER 10507
#define MSG_SETTINGS_PROGRAM 10508
#define MSG_SETTINGS_NETWORK 10509
#define MSG_SETTINGS_CLASSACT 10510
#define MSG_SETTINGS_SAVEALL 10511
#define MSG_SETTINGS_SNAPSHOT 10512
#define MSG_SETTINGS_SNAPSHOTALT 10513
#define MSG_SETTINGS_BGIMAGES 10514
#define MSG_SETTINGS_BGSOUND 10515
#define MSG_SETTINGS_SAVEAS 10516
#define MSG_SETTINGS_LOAD 10517
#define MSG_SETTINGS_GUI 10518
#define MSG_HELP_MENU 10600
#define MSG_HELP_HELP 10601
#define MSG_HELP_AWEBHOME 10602
#define MSG_HELP_AWEBFAQ 10603
#define MSG_HELP_REGISTER 10604
#define MSG_AREXX_MENU 10700
#define MSG_AREXX_AREXX 10701
#define MSG_ERROR_CANTOPEN 20000
#define MSG_ERROR_CANTOPENV 20001
#define MSG_ERROR_NEEDOS30 20002
#define MSG_ERROR_CANTQUIT 20003
#define MSG_EPART_ERROR 21000
#define MSG_EPART_RETURL 21001
#define MSG_EPART_ADDRSCHEME 21002
#define MSG_EPART_NOLIB 21003
#define MSG_EPART_NOHOST 21004
#define MSG_EPART_NOCONNECT 21005
#define MSG_EPART_NOCONNECT_TIMEOUT 21006
#define MSG_EPART_NOCONNECT_REFUSED 21007
#define MSG_EPART_NOCONNECT_RESET 21008
#define MSG_EPART_NOCONNECT_UNREACH 21009
#define MSG_EPART_NOCONNECT_HOSTUNREACH 21010
#define MSG_EPART_NOFILE 21011
#define MSG_EPART_XAWEB 21012
#define MSG_EPART_FLUSHED_HEAD 22000
#define MSG_EPART_FLUSHED_MSG 22001
#define MSG_EPART_FLUSHED_NORELOAD 22002
#define MSG_EPART_NOLOGIN 22003
#define MSG_EPART_NOAWEBLIB 22004
#define MSG_EPART_NOPROGRAM 22005
#define MSG_AWEB_EXTWINTITLE 30000
#define MSG_AWEB_BYTESREAD 30001
#define MSG_AWEB_FORMSUBMIT 30002
#define MSG_AWEB_FORMRESET 30003
#define MSG_AWEB_INDEXPROMPT 30004
#define MSG_AWEB_NODOCTITLE 30005
#define MSG_AWEB_SCREENTITLE 30006
#define MSG_AWEB_HOTLISTTITLE 30007
#define MSG_AWEB_GOPHERINDEX 30008
#define MSG_AWEB_GOPHERMENU 30009
#define MSG_AWEB_WINDOWHIS 30010
#define MSG_AWEB_OTHER 30011
#define MSG_AWEB_LOOKUP 30012
#define MSG_AWEB_CONNECT 30013
#define MSG_AWEB_WAITING 30014
#define MSG_AWEB_TCPSTART 30015
#define MSG_AWEB_FORMLOCATION 30016
#define MSG_AWEB_FORMBUTTON 30017
#define MSG_AWEB_FRAME_RESIZE 30018
#define MSG_AWEB_LOGIN 30019
#define MSG_AWEB_NEWSGROUP 30020
#define MSG_AWEB_NEWSSCAN 30021
#define MSG_AWEB_NEWSSORT 30022
#define MSG_AWEB_NEWSPOST 30023
#define MSG_AWEB_MAILSEND 30024
#define MSG_AWEB_UPLOAD 30025

#endif /* CATCOMP_NUMBERS */


/****************************************************************************/


#ifdef CATCOMP_STRINGS

#define MSG_REQUEST_TITLE_STR "AWeb request"
#define MSG_ABOUT_TITLE_STR "About AWeb"
#define MSG_ABOUT_REGNR_STR "Registration #"
#define MSG_ABOUT_REGTO_STR "Registered to"
#define MSG_ABOUT_SCREENNAME_STR "Public screen name"
#define MSG_ABOUT_PORTNAME_STR "ARexx port name"
#define MSG_ABOUT_EMAIL_STR "E-mail"
#define MSG_ABOUT_WWW_STR "WWW"
#define MSG_ABOUT_OK_STR "_OK"
#define MSG_ABOUT_UNREGISTERED_STR "Unregistered version\n"
#define MSG_ABOUT_DEMO_STR "Unregistered demo version\n"
#define MSG_ABOUT_DISTRIBUTED_STR "Distributed by %s"
#define MSG_ABOUT_TRANSLATOR_STR ""
#define MSG_ABOUT_SPECIAL_STR "Special version for distribution with Amiga OS\n"
#define MSG_QUIT_TEXT_STR "Are you sure you want to quit AWeb?"
#define MSG_QUIT_OK_STR "_OK"
#define MSG_QUIT_CANCEL_STR "_Cancel"
#define MSG_QUIT_WARNING_STR "WARNING: Transfers will be aborted!\n\n"
#define MSG_DEMO_TEXT_STR "This is the demo version of AWeb\nmultiple document windows are not supported"
#define MSG_DEMO_OK_STR "_OK"
#define MSG_SCRCLOSE_TEXT_STR "AWeb can't close its screen.\nPlease close all windows."
#define MSG_SCRCLOSE_OK_STR "Ok"
#define MSG_NWS_TITLE_STR "Network status"
#define MSG_NWS_QUEUED_STR "Queued"
#define MSG_NWS_STARTED_STR "Started"
#define MSG_NWS_LOOKUP_STR "Looking up"
#define MSG_NWS_CONNECT_STR "Connecting"
#define MSG_NWS_WAIT_STR "Waiting"
#define MSG_NWS_PROCESS_STR "Processing"
#define MSG_NWS_LOGIN_STR "Logging in"
#define MSG_NWS_CPS_STR "%ld cps"
#define MSG_NWS_NEWSGROUP_STR "Scanning"
#define MSG_NWS_UPLOAD_STR "Uploading"
#define MSG_AUTH_TITLE_STR "Authorization"
#define MSG_AUTH_PROMPT_STR "Userid and password required for"
#define MSG_AUTH_USERID_STR "_Userid"
#define MSG_AUTH_PASSWORD_STR "_Password"
#define MSG_AUTH_OK_STR "_OK"
#define MSG_AUTH_CANCEL_STR "_Cancel"
#define MSG_AUTH_PROMPT_FTP_STR "Password required for"
#define MSG_AUTH_USERID_FTP_STR "Userid"
#define MSG_AUTH_PROMPT_PROXY_STR "Userid and password required for proxy"
#define MSG_FILE_SAVETITLE_STR "Select save filename"
#define MSG_FILE_SAVE_STR "Save"
#define MSG_FILE_INCTEXT_STR "Incomplete file!\n%s\nExpected: %ld bytes\nReceived: %ld bytes\n\nChoose OK to keep the partial data."
#define MSG_DUMMY_1_STR ""
#define MSG_FILE_EXISTTEXT_STR "File:\n%s\nalready exists!"
#define MSG_FILE_EXISTBUTTONS_STR "_Overwrite|_Append|_New name|_Cancel"
#define MSG_FILE_AREXXTITLE_STR "Select ARexx macro"
#define MSG_FILE_HOTLISTTITLE_STR "Select hierarchical hotlist"
#define MSG_FILE_LOCALTITLE_STR "Open local file"
#define MSG_FILE_OPEN_STR "Open"
#define MSG_FILE_TEMPTITLE_STR "Can't find temporary path:\n%s\nUse T: instead."
#define MSG_FILE_TEMPBUTTONS_STR "_OK"
#define MSG_FILE_INCBUTTONS2_STR "_OK|_Retry|_Cancel"
#define MSG_FILE_SETTINGSTITLE_STR "Select settings directory"
#define MSG_FILE_UPLOADTITLE_STR "Select file to upload"
#define MSG_TCP_ENDTCP_STR "AWeb is stopped.\nShould the TCP connection be terminated too?"
#define MSG_TCP_BUTTONS_STR "_Yes|_No"
#define MSG_STARTUP_FONTS_STR "Loading fonts"
#define MSG_STARTUP_IMAGES_STR "Loading GUI images"
#define MSG_STARTUP_CACHE_STR "Loading cache index"
#define MSG_HOTL_TITLE_STR "AWeb hotlist"
#define MSG_HOTL_ADDLINK_STR "_Add link"
#define MSG_HOTL_ADDGROUP_STR "Add _group"
#define MSG_HOTL_REMOVE_STR "Remove"
#define MSG_HOTL_FOLLOW_STR "_Follow"
#define MSG_HOTL_NAME_STR "_Name"
#define MSG_HOTL_URL_STR "_URL"
#define MSG_HOTL_NEWGROUP_STR "New group"
#define MSG_HOTL_NEWLINK_STR "New link"
#define MSG_HOTL_TITLE_VIEWER_STR "AWeb hotlist viewer"
#define MSG_HOTL_VURLS_STR "UR_Ls"
#define MSG_HOTL_MANAGER_STR "_Manager"
#define MSG_HOTL_TITLE_MANAGER_STR "AWeb hotlist manager"
#define MSG_HOTL_LTYPE_STR "L_ist"
#define MSG_HOTL_LTYPE_REST_STR "Rest"
#define MSG_HOTL_LTYPE_ALL_STR "All"
#define MSG_HOTL_LTYPE_GROUPS_STR "Groups"
#define MSG_HOTL_LSORT_STR "S_ort"
#define MSG_HOTL_LSORT_TITLE_STR "Title"
#define MSG_HOTL_LSORT_URL_STR "URL"
#define MSG_HOTL_MOVEIN_STR "_< In"
#define MSG_HOTL_MOVEOUT_STR "Out _>"
#define MSG_HOTL_MOVE_STR "< _Move"
#define MSG_HOTL_DEL_STR "_Delete"
#define MSG_HOTL_WHERE_STR "_Where"
#define MSG_HOTL_SHOW_STR "S_how"
#define MSG_HOTL_SORT_STR "Sor_t"
#define MSG_HOTL_PARENT_STR "_Parent"
#define MSG_HOTL_ALL_STR "G_roups"
#define MSG_HOTL_ADDENTRY_STR "+ _Entry"
#define MSG_HOTL_ADDGROUP2_STR "+ _Group"
#define MSG_HOTL_URLS_STR "UR_Ls"
#define MSG_HOTL_REVERT_STR "Re_vert"
#define MSG_HOTL_SAVE_STR "_Save"
#define MSG_HOTL_LADD_STR "_Add"
#define MSG_HOTL_FIND_STR "_Find"
#define MSG_HOTL_PATTERN_STR "Mat_ch"
#define MSG_HOTL_NEXT_STR "Ne_xt"
#define MSG_HOTL_DEFAULT_TITLE_STR "** New link **"
#define MSG_HOTL_DEFAULT_GROUP_STR "** New group **"
#define MSG_HOTL_TITLE_ALLGROUPS_STR "All groups"
#define MSG_HOTL_TITLE_WHERE_STR "Groups containing this entry"
#define MSG_HOTL_TITLE_REST_STR "Unassigned entries"
#define MSG_HOTL_TITLE_ALL_STR "All entries"
#define MSG_HOTL_TITLE_ROOTGROUPS_STR "Root-level groups"
#define MSG_HOTL_LSORT_DATE_STR "Date"
#define MSG_WHIS_TITLE_STR "Window history"
#define MSG_WHIS_FILTER_STR "_Filter"
#define MSG_WHIS_WINDOW_STR "_Window"
#define MSG_WHIS_ORDER_STR "_Order"
#define MSG_WHIS_DISPLAY_STR "_Display"
#define MSG_WHIS_ORDER_NATURAL_STR "Natural"
#define MSG_WHIS_ORDER_MAINLINE_STR "Mainline"
#define MSG_WHIS_ORDER_RETRIEVED_STR "Retrieved"
#define MSG_WHIS_ORDER_TITLE_STR "Title"
#define MSG_WHIS_ORDER_URL_STR "URL"
#define MSG_CABR_TITLE_STR "Cache browser"
#define MSG_CABR_URL_STR "URL"
#define MSG_CABR_DATE_STR "Date"
#define MSG_CABR_SIZE_STR "Size"
#define MSG_CABR_TYPE_STR "Type"
#define MSG_CABR_FILE_STR "File"
#define MSG_CABR_STATUS_STR "%ld files, %ld kB"
#define MSG_CABR_SORTBY_STR "_Sort by"
#define MSG_CABR_OPEN_STR "_Open"
#define MSG_CABR_SAVE_STR "Sa_ve"
#define MSG_CABR_DELETE_STR "_Delete"
#define MSG_CABR_FIND_STR "_Find"
#define MSG_CABR_PATTERN_STR "_Match"
#define MSG_CABR_NEXT_STR "_Next"
#define MSG_PROGRESS_TITLE_STR "AWeb progress"
#define MSG_PROGRESS_CANCEL_STR "Cancel"
#define MSG_FIXCACHE_WARNING_STR "WARNING!\nThis function will delete all foreign files from the\n%s drawer"
#define MSG_FIXCACHE_BUTTONS_STR "_OK|_Cancel"
#define MSG_FIXCACHE_PROGRESS_STR "Fixing the disk cache..."
#define MSG_FIXCACHE_DELETEIMAGES_STR "Are you sure you want to delete\nall images from the cache?"
#define MSG_FIXCACHE_DELETEDOCS_STR "Are you sure you want to delete\nall documents from the cache?"
#define MSG_FIXCACHE_ERASE_STR "Are you sure you want to delete\nall documents and images from the cache?"
#define MSG_FIXCACHE_CORRUPT_STR "The cache registration is unreadable\nor from an older version of AWeb.\nCached files cannot be used any more.\nDo you want to clean up now?"
#define MSG_SEARCH_STRING_STR "Search _for"
#define MSG_SEARCH_IGNORECASE_STR "_Ignore case"
#define MSG_SEARCH_SEARCH_STR "_Search"
#define MSG_SEARCH_FROMTOP_STR "From _top"
#define MSG_SEARCH_BACKWARDS_STR "_Backwards"
#define MSG_SEARCH_CANCEL_STR "_Cancel"
#define MSG_SEARCH_NOTFOUND_STR "String not found"
#define MSG_SEARCH_OK_STR "_OK"
#define MSG_POPUP_LOADIMAGE_STR "Load image"
#define MSG_POPUP_RELOADIMAGE_STR "Reload image"
#define MSG_POPUP_SAVEIMAGE_STR "Save image"
#define MSG_POPUP_DOWNLOADIMAGE_STR "Download image"
#define MSG_POPUP_FLUSHIMAGE_STR "Flush image"
#define MSG_POPUP_SHOWIMAGE_STR "Show image"
#define MSG_POPUP_OPENLINK_STR "Open link"
#define MSG_POPUP_OPENLINKNW_STR "Open link in new window"
#define MSG_POPUP_LOADLINK_STR "Load link in background"
#define MSG_POPUP_SAVELINK_STR "Save link"
#define MSG_POPUP_DOWNLOADLINK_STR "Download link"
#define MSG_POPUP_ADDLINK_STR "Add link to hotlist"
#define MSG_POPUP_RELOADFRAME_STR "Reload contents"
#define MSG_POPUP_SAVEFRAME_STR "Save source"
#define MSG_POPUP_VIEWFRAME_STR "View source"
#define MSG_POPUP_SHOWFRAME_STR "Show separately"
#define MSG_POPUP_SEARCHFRAME_STR "Search in contents"
#define MSG_POPUP_INFOFRAME_STR "Info"
#define MSG_PRINTP_TITLE_STR "Print parameters"
#define MSG_PRINTP_SCALE_STR "_Scale percentage"
#define MSG_PRINTP_PRINTBG_STR "Print _backgrounds"
#define MSG_PRINTP_FORMFEED_STR "_Formfeed"
#define MSG_PRINTP_CENTER_STR "Ce_nter"
#define MSG_PRINTP_PRINT_STR "_Print"
#define MSG_PRINTP_CANCEL_STR "_Cancel"
#define MSG_PRINT_NOPRINTER_STR "Unable to access the printer"
#define MSG_PRINT_OK_STR "_OK"
#define MSG_PRINT_PROGRESS_STR "Printing in progress..."
#define MSG_PRINTP_LAYOUTWIDTH_STR "_Layout Width"
#define MSG_PRINTP_LW_WINDOW_STR "Window"
#define MSG_PRINTP_LW_DOCUMENT_STR "Document"
#define MSG_PRINTP_LW_SCREEN_STR "Screen"
#define MSG_COOKIE_TITLE_STR "Cookie alert"
#define MSG_COOKIE_WARNING_STR "About to set a Cookie!"
#define MSG_COOKIE_NAME_STR "Cookie name:"
#define MSG_COOKIE_VALUE_STR "Cookie value:"
#define MSG_COOKIE_DOMAIN_STR "For domain:"
#define MSG_COOKIE_PATH_STR "Path:"
#define MSG_COOKIE_COMMENT_STR "Comment:"
#define MSG_COOKIE_MAXAGE_STR "Max-age:"
#define MSG_COOKIE_EXPIRES_STR "Expires:"
#define MSG_COOKIE_ONCE_STR "_Once"
#define MSG_COOKIE_ACCEPT_STR "_Accept"
#define MSG_COOKIE_NEVER_STR "_Never"
#define MSG_COOKIE_CANCEL_STR "_Cancel"
#define MSG_COOKIE_SECURE_STR "AWeb will send this cookie back\nover secure connections only."
#define MSG_COOKIE_UNSECURE_STR "AWeb will send this cookie back\nover UNSECURE connections too."
#define MSG_INFO_TITLE_STR "Information"
#define MSG_INFO_FROMCACHE_STR "Read from AWeb cache"
#define MSG_INFO_XFER_STR "Transfer headers:"
#define MSG_INFO_META_STR "Meta information:"
#define MSG_INFO_LINK_STR "Clickable links:"
#define MSG_INFO_CIPHER_STR "HTTPS cipher method: %s"
#define MSG_INFO_SSLLIBRARY_STR "SSL library used: %s"
#define MSG_USERBUTTON_CACHE_STR "Cache"
#define MSG_USERBUTTON_CLOCK_STR "Clock"
#define MSG_AUTHEDIT_TITLE_STR "Edit authorizations"
#define MSG_AUTHEDIT_TITLE_SERVER_STR "Server"
#define MSG_AUTHEDIT_TITLE_USERID_STR "Userid"
#define MSG_AUTHEDIT_DEL_STR "_Del"
#define MSG_AUTHEDIT_SERVER_STR "Server"
#define MSG_AUTHEDIT_USERID_STR "_Userid"
#define MSG_AUTHEDIT_PASSWORD_STR "_Password"
#define MSG_JSPROMPT_OK_STR "_OK"
#define MSG_JSPROMPT_CANCEL_STR "_Cancel"
#define MSG_FORMWARN_TITLE_STR "AWeb warning"
#define MSG_FORMWARN_WARNING_STR "Data from this form will be sent\nover an UNSECURE connection."
#define MSG_FORMWARN_BUTTONS_STR "_OK|_Cancel"
#define MSG_MAIL_TITLE_STR "AWeb mailer error"
#define MSG_MAIL_NOSMTPHOST_STR "You have to configure your SMTP host first.\nSee Network/Mail settings."
#define MSG_MAIL_NOCONNECT_STR "Can't connect to %s."
#define MSG_MAIL_MAILFAILED_STR "Sending mail message failed."
#define MSG_MAIL_BUTTONS_STR "_Retry|_Save|_Cancel"
#define MSG_MAIL_SAVETITLE_STR "Save failed message"
#define MSG_MAIL_MAILTO_TITLE_STR "Mail to %s"
#define MSG_MAIL_MAILTO_HEADER_STR "Mail to: %s"
#define MSG_MAIL_TO_STR "To:"
#define MSG_MAIL_SUBJECT_STR "Subject:"
#define MSG_MAIL_MESSAGE_BODY_STR "Message body:"
#define MSG_MAIL_EXTRA_HEADERS_STR "Extra mail headers included"
#define MSG_MAIL_SEND_STR "Send mail"
#define MSG_MAIL_RESET_STR "Reset"
#define MSG_MAIL_RETURN_STR "Return"
#define MSG_MAIL_MAIL_SENT_TITLE_STR "Mail sent to: %s"
#define MSG_MAIL_MAIL_SENT_STR "<H1>Mail sent</H1>\nYour mail message to <B>%s</B> has been sent succesfully."
#define MSG_NEWS_TITLE_STR "AWeb newsreader error"
#define MSG_NEWS_NONNTPHOST_STR "You have to configure your NNTP host first.\nSee Network/News settings."
#define MSG_NEWS_NOCONNECT_STR "Can't connect to %s."
#define MSG_NEWS_POSTFAILED_STR "Posting article failed."
#define MSG_NEWS_BUTTONS_STR "_Retry|_Save|_Cancel"
#define MSG_NEWS_SAVETITLE_STR "Save failed article"
#define MSG_NEWS_NEWS_HEADER_STR "News"
#define MSG_NEWS_SUBSCRIBED_STR "Subscribed newsgroups"
#define MSG_NEWS_NEW_ARTICLES_STR "%8ld new articles"
#define MSG_NEWS_CATCH_UP_STR "Catch up"
#define MSG_NEWS_UNSUBSCRIBE_STR "Unsubscribe"
#define MSG_NEWS_OTHER_GROUP_STR "Other group"
#define MSG_NEWS_SUBSCRIBE_STR "Subscribe"
#define MSG_NEWS_READ_STR "Read"
#define MSG_NEWS_SEARCH_FOR_GROUP_STR "Search for group"
#define MSG_NEWS_SEARCH_STR "Search"
#define MSG_NEWS_LONG_DOWNLOAD_STR "The first time you search for a group,\na list of all newsgroup names will be downloaded.\nThis may take several minutes to complete."
#define MSG_NEWS_NO_SUBJECT_STR "(no subject)"
#define MSG_NEWS_LINES_STR "(%s lines)"
#define MSG_NEWS_POST_NEW_ARTICLE_STR "Post new article"
#define MSG_NEWS_GROUP_LIST_STR "Group list"
#define MSG_NEWS_ERROR_NO_GROUP_STR "<h1>Error</h1>\nGroup %s does not exist."
#define MSG_NEWS_USE_FRAMES_STR "News is configured to use frames"
#define MSG_NEWS_ERROR_NO_ARTICLE_STR "Article %s does not exist<BR>"
#define MSG_NEWS_FOLLOW_UP_STR "Follow-up"
#define MSG_NEWS_REPLY_STR "Reply in e-mail"
#define MSG_NEWS_SAVE_STR "Save article"
#define MSG_NEWS_SUBJECT_STR "Subject:"
#define MSG_NEWS_NEWSGROUPS_STR "Newsgroups:"
#define MSG_NEWS_ARTICLE_BODY_STR "Article body:"
#define MSG_NEWS_EXTRA_HEADERS_STR "Extra news headers included"
#define MSG_NEWS_POST_ARTICLE_STR "Post article"
#define MSG_NEWS_RESET_STR "Reset"
#define MSG_NEWS_RETURN_STR "Return"
#define MSG_NEWS_FOLLOWUP_TITLE_STR "Follow-up to article"
#define MSG_NEWS_FOLLOWUP_TO_GROUPS_STR "Follow-up to:"
#define MSG_NEWS_PLEASE_DELETE_STR "Please delete unnecessary quoted lines"
#define MSG_NEWS_REPLY_TITLE_STR "Reply to article"
#define MSG_NEWS_REPLY_TO_ADDRESS_STR "Reply to:"
#define MSG_NEWS_ARTICLE_POSTED_TITLE_STR "Article posted"
#define MSG_NEWS_ARTICLE_POSTED_STR "<H1>Article posted</h1>\nYour article has been posted."
#define MSG_NEWS_DOWNLOAD_TITLE_STR "Download newsgroup list"
#define MSG_NEWS_DOWNLOAD_BODY_STR "<H1>Downloading...</H1>\nDownloading newsgroup list. This can take several minutes."
#define MSG_NEWS_SEARCH_RESULT_STR "Search result"
#define MSG_NEWS_FIRST_MATCHING_STR "First %ld of %ld newsgroup names matching <B>%s</B>"
#define MSG_NEWS_FOUND_MATCHING_STR "Found %ld newsgroup names matching <B>%s</B>"
#define MSG_CMDWARN_TITLE_STR "AWeb warning"
#define MSG_CMDWARN_SHELL_STR "About to execute shell command:\n%s"
#define MSG_CMDWARN_AREXX_STR "About to start ARexx program:\n%s"
#define MSG_CMDWARN_BUTTONS_STR "_OK|_Cancel"
#define MSG_SSLWARN_SSL_TITLE_STR "AWeb warning"
#define MSG_SSLWARN_SSL_TEXT_STR "Cannot make secure connection to %s:\n%s\n \nUse an unsecure connection instead?"
#define MSG_SSLWARN_SSL_BUTTONS_STR "_Unsecure|_Cancel"
#define MSG_SSLWARN_SSL_NO_SSL_STR "Secure connections are only supported with the Miami TCP program and AmiSSL."
#define MSG_SSLWARN_CERT_TITLE_STR "AWeb warning"
#define MSG_SSLWARN_CERT_TEXT_STR "Certificate for %s cannot be verified.\nDetails: %s\n \nIf you continue the data will be encrypted (protection against eavesdropping), but the identity of the server cannot be verified (-NO- protection against fraud).\n \nDo you want to continue?"
#define MSG_SSLWARN_CERT_BUTTONS_STR "_OK|_Cancel"
#define MSG_SSLWARN_SSL_NO_SSL2_STR "Secure connections are only supported when using AmiSSL."
#define MSG_UNKMIME_TITLE_STR "Unknown content type"
#define MSG_UNKMIME_TEXT_STR "Unrecognized file will be saved\n \nURL:   %s\nContent type:   %s\nExtension:   %s\n \nYou might want to cancel the save now, and reload this file after configuring a viewer or action for this MIME type in the Browser settings (Viewers)."
#define MSG_UNKMIME_BUTTONS_STR "_Save|_Cancel"
#define MSG_REQUEST_COPYURL_STR "Copy _URL to clipboard"
#define MSG_OPENURL_TITLE_STR "AWeb request"
#define MSG_OPENURL_TEXT_STR "Type in the URL to open:"
#define MSG_OPENURL_BUTTONS_STR "_OK|_Cancel"
#define MSG_SAVEIFF_PROGRESS_STR "Saving IFF file..."
#define MSG_PROJECT_MENU_STR "Project"
#define MSG_PROJECT_NEWWINDOW_STR "N/New Window"
#define MSG_PROJECT_CLOSEWINDOW_STR "K/Close Window"
#define MSG_PROJECT_OPENURL_STR "U/Open URL"
#define MSG_PROJECT_OPENWWW_STR "W/Open WWW"
#define MSG_PROJECT_OPENLOCAL_STR "L/Open Local..."
#define MSG_PROJECT_OPENSEARCH_STR "E/Search"
#define MSG_PROJECT_SOURCE_STR "View Source..."
#define MSG_PROJECT_SAVEHTML_STR "S/Save Source..."
#define MSG_PROJECT_ABOUT_STR "?/About..."
#define MSG_PROJECT_QUIT_STR "Q/Quit..."
#define MSG_PROJECT_SEARCH_STR "G/Search..."
#define MSG_PROJECT_PRINT_STR "P/Print..."
#define MSG_PROJECT_EDIT_STR "Edit Source..."
#define MSG_PROJECT_INFO_STR "I/Info..."
#define MSG_PROJECT_OPENNEWS_STR "Open News"
#define MSG_PROJECT_ICONIFY_STR "\\/Iconify"
#define MSG_PROJECT_SAVEIFF_STR "Save as IFF..."
#define MSG_CONTROL_MENU_STR "Control"
#define MSG_CONTROL_LOADNOW_STR "Load Images"
#define MSG_CONTROL_LOADNOWALL_STR "All Images"
#define MSG_CONTROL_LOADNOWMAPS_STR "M/Maps Only"
#define MSG_CONTROL_NETWORKSTATUS_STR "Network Status..."
#define MSG_DUMMY_2_STR ""
#define MSG_CONTROL_CANCEL_STR "Z/Cancel Load"
#define MSG_CONTROL_NEXTWINDOW_STR "./Next Window"
#define MSG_CONTROL_PREVWINDOW_STR ",/Previous Window"
#define MSG_CONTROL_NOPROXY_STR "Y/Disable Proxy"
#define MSG_CONTROL_RLOAD_STR "Reload"
#define MSG_CONTROL_RLOADDOC_STR "R/Current Document"
#define MSG_CONTROL_RLOADIMGS_STR "Current Images"
#define MSG_CONTROL_PLAYBGSOUND_STR "J/Play Background Sounds"
#define MSG_CONTROL_COPYURL_STR "Copy URL"
#define MSG_CONTROL_PASTEURL_STR "Paste URL"
#define MSG_CONTROL_COPYBLOCK_STR "C/Copy Block"
#define MSG_CONTROL_DRAGGING_STR "Dragging"
#define MSG_CONTROL_BREAKJS_STR "X/Break JavaScript"
#define MSG_CONTROL_DEBUGJS_STR "Debug JavaScript"
#define MSG_CONTROL_RESET_STR "Reset Frames"
#define MSG_CACHE_MENU_STR "Cache"
#define MSG_DUMMY_3_STR ""
#define MSG_DUMMY_4_STR ""
#define MSG_DUMMY_5_STR ""
#define MSG_DUMMY_6_STR ""
#define MSG_CACHE_SAVEAUTH_STR "Save Authorizations"
#define MSG_CACHE_FLUSHAUTH_STR "Flush Authorizations"
#define MSG_CACHE_BROWSER_STR "Cache Browser..."
#define MSG_CACHE_FLUSH_STR "Flush Memory Cache"
#define MSG_CACHE_FLSHIMGSND_STR "Nondisplayed Images"
#define MSG_CACHE_FLSHIMGS_STR "All Images"
#define MSG_CACHE_FLSHDOCSND_STR "Nondisplayed Documents"
#define MSG_CACHE_DELETE_STR "Delete Disk Cache"
#define MSG_CACHE_DELIMGS_STR "All Images"
#define MSG_CACHE_DELDOCS_STR "All Documents"
#define MSG_CACHE_CLEAR_STR "Clear Disk Cache"
#define MSG_CACHE_FIX_STR "Repair Disk Cache..."
#define MSG_CACHE_EDITAUTH_STR "Edit Authorizations..."
#define MSG_NAVIGATE_MENU_STR "Navigate"
#define MSG_NAVIGATE_BACK_STR "B/Back"
#define MSG_NAVIGATE_FWD_STR "F/Forward"
#define MSG_NAVIGATE_HOME_STR "D/Home"
#define MSG_NAVIGATE_HISTORY_STR "-/History..."
#define MSG_HOTLIST_MENU_STR "Hotlist"
#define MSG_HOTLIST_ADDHOT_STR "A/Add To Hotlist"
#define MSG_HOTLIST_SHOWHOT_STR "H/Show Hotlist"
#define MSG_HOTLIST_MAINT_STR "T/Maintenance..."
#define MSG_HOTLIST_SAVE_STR "Save Hotlist"
#define MSG_HOTLIST_RESTORE_STR "Restore Last Saved"
#define MSG_HOTLIST_AMHOTRX_STR "AMosaic (ARexx)"
#define MSG_HOTLIST_AMHOT20_STR "AMosaic (2.0)"
#define MSG_HOTLIST_IBHOT_STR "Other..."
#define MSG_HOTLIST_VIEW_STR "V/View Hotlist..."
#define MSG_HOTLIST_MGR_STR "T/Manage Hotlist..."
#define MSG_SETTINGS_MENU_STR "Settings"
#define MSG_SETTINGS_LOADIMG_STR "Image Loading"
#define MSG_SETTINGS_LOADIMGALL_STR "All Images"
#define MSG_SETTINGS_LOADIMGMAPS_STR "Maps Only"
#define MSG_SETTINGS_LOADIMGOFF_STR "Off"
#define MSG_DUMMY_7_STR ""
#define MSG_DUMMY_8_STR ""
#define MSG_SETTINGS_BROWSER_STR "Browser Settings..."
#define MSG_SETTINGS_PROGRAM_STR "Program Settings..."
#define MSG_SETTINGS_NETWORK_STR "Network Settings..."
#define MSG_SETTINGS_CLASSACT_STR "Reaction Settings..."
#define MSG_SETTINGS_SAVEALL_STR "Save Current Settings"
#define MSG_SETTINGS_SNAPSHOT_STR "Snapshot Windows"
#define MSG_SETTINGS_SNAPSHOTALT_STR "Snapshot Zoom"
#define MSG_SETTINGS_BGIMAGES_STR "Background Images"
#define MSG_SETTINGS_BGSOUND_STR "Background Sound"
#define MSG_SETTINGS_SAVEAS_STR "Save Settings As..."
#define MSG_SETTINGS_LOAD_STR "Load Settings..."
#define MSG_SETTINGS_GUI_STR "GUI Settings..."
#define MSG_HELP_MENU_STR "Help"
#define MSG_HELP_HELP_STR "Documentation"
#define MSG_HELP_AWEBHOME_STR "AWeb Home"
#define MSG_HELP_AWEBFAQ_STR "www.amigazen.com/aweb/index.html"
#define MSG_HELP_REGISTER_STR "Registration"
#define MSG_AREXX_MENU_STR "ARexx"
#define MSG_AREXX_AREXX_STR "!/Run ARexx Macro..."
#define MSG_ERROR_CANTOPEN_STR "Can't open %s"
#define MSG_ERROR_CANTOPENV_STR "Can't open %s version %ld"
#define MSG_ERROR_NEEDOS30_STR "AWeb needs OS 3.1 or better"
#define MSG_ERROR_CANTQUIT_STR "Cannot quit!\n%s\nis still in use"
#define MSG_EPART_ERROR_STR "Error"
#define MSG_EPART_RETURL_STR "While retrieving URL"
#define MSG_EPART_ADDRSCHEME_STR "The &quot;%s&quot; addressing scheme is not implemented in this version of AWeb"
#define MSG_EPART_NOLIB_STR "A TCP stack must be started first"
#define MSG_EPART_NOHOST_STR "Cannot resolve host name &quot;%s&quot;"
#define MSG_EPART_NOCONNECT_STR "Cannot connect to &quot;%s&quot;"
#define MSG_EPART_NOCONNECT_TIMEOUT_STR "Connection to &quot;%s&quot; timed out"
#define MSG_EPART_NOCONNECT_REFUSED_STR "Connection to &quot;%s&quot; was refused by the server"
#define MSG_EPART_NOCONNECT_RESET_STR "Connection to &quot;%s&quot; was reset by the server"
#define MSG_EPART_NOCONNECT_UNREACH_STR "Network unreachable - cannot connect to &quot;%s&quot;"
#define MSG_EPART_NOCONNECT_HOSTUNREACH_STR "Host unreachable - cannot connect to &quot;%s&quot;"
#define MSG_EPART_NOFILE_STR "Cannot open file &quot;%s&quot;"
#define MSG_EPART_XAWEB_STR "Invalid internal (x-aweb:) name &quot;%s&quot;"
#define MSG_EPART_FLUSHED_HEAD_STR "Flushed"
#define MSG_EPART_FLUSHED_MSG_STR "This document is no longer available in memory"
#define MSG_EPART_FLUSHED_NORELOAD_STR "This document cannot be retrieved again. You will have to submit the form again that produced this page. Beware of undesired side-effects if you submit that form twice, like a duplicate registration."
#define MSG_EPART_NOLOGIN_STR "Cannot login at &quot;%s&quot; as &quot;%s&quot;"
#define MSG_EPART_NOAWEBLIB_STR "Unable to load the AWebLib module for &quot;%s&quot;."
#define MSG_EPART_NOPROGRAM_STR "You haven't configured a program for the &quot;%s&quot; addressing scheme."
#define MSG_AWEB_EXTWINTITLE_STR "AWeb External Program"
#define MSG_AWEB_BYTESREAD_STR "Bytes read"
#define MSG_AWEB_FORMSUBMIT_STR "Submit"
#define MSG_AWEB_FORMRESET_STR "Reset"
#define MSG_AWEB_INDEXPROMPT_STR "This is a searchable index. Enter keywords: "
#define MSG_AWEB_NODOCTITLE_STR "(No document loaded)"
#define MSG_AWEB_SCREENTITLE_STR "AWeb public screen - Name AWeb"
#define MSG_AWEB_HOTLISTTITLE_STR "Hotlist"
#define MSG_AWEB_GOPHERINDEX_STR "Gopher index"
#define MSG_AWEB_GOPHERMENU_STR "Gopher menu"
#define MSG_AWEB_WINDOWHIS_STR "Window history"
#define MSG_AWEB_OTHER_STR "Other"
#define MSG_AWEB_LOOKUP_STR "Looking up %s"
#define MSG_AWEB_CONNECT_STR "Making %s connection to %s"
#define MSG_AWEB_WAITING_STR "%s request sent; waiting for response"
#define MSG_AWEB_TCPSTART_STR "Establishing TCP connection"
#define MSG_AWEB_FORMLOCATION_STR "Submit form at x=%ld, y=%ld"
#define MSG_AWEB_FORMBUTTON_STR "Start"
#define MSG_AWEB_FRAME_RESIZE_STR "Resize frame"
#define MSG_AWEB_LOGIN_STR "Logging in at %s"
#define MSG_AWEB_NEWSGROUP_STR "Scanning %s"
#define MSG_AWEB_NEWSSCAN_STR "Scanning %s (%ld articles)"
#define MSG_AWEB_NEWSSORT_STR "Sorting %s"
#define MSG_AWEB_NEWSPOST_STR "Posting article"
#define MSG_AWEB_MAILSEND_STR "Sending mail message"
#define MSG_AWEB_UPLOAD_STR "Uploading file"

#endif /* CATCOMP_STRINGS */


/****************************************************************************/


#ifdef CATCOMP_ARRAY

struct CatCompArrayType
{
    LONG   cca_ID;
    STRPTR cca_Str;
};

static const struct CatCompArrayType CatCompArray[] =
{
    {MSG_REQUEST_TITLE,(STRPTR)MSG_REQUEST_TITLE_STR},
    {MSG_ABOUT_TITLE,(STRPTR)MSG_ABOUT_TITLE_STR},
    {MSG_ABOUT_REGNR,(STRPTR)MSG_ABOUT_REGNR_STR},
    {MSG_ABOUT_REGTO,(STRPTR)MSG_ABOUT_REGTO_STR},
    {MSG_ABOUT_SCREENNAME,(STRPTR)MSG_ABOUT_SCREENNAME_STR},
    {MSG_ABOUT_PORTNAME,(STRPTR)MSG_ABOUT_PORTNAME_STR},
    {MSG_ABOUT_EMAIL,(STRPTR)MSG_ABOUT_EMAIL_STR},
    {MSG_ABOUT_WWW,(STRPTR)MSG_ABOUT_WWW_STR},
    {MSG_ABOUT_OK,(STRPTR)MSG_ABOUT_OK_STR},
    {MSG_ABOUT_UNREGISTERED,(STRPTR)MSG_ABOUT_UNREGISTERED_STR},
    {MSG_ABOUT_DEMO,(STRPTR)MSG_ABOUT_DEMO_STR},
    {MSG_ABOUT_DISTRIBUTED,(STRPTR)MSG_ABOUT_DISTRIBUTED_STR},
    {MSG_ABOUT_TRANSLATOR,(STRPTR)MSG_ABOUT_TRANSLATOR_STR},
    {MSG_ABOUT_SPECIAL,(STRPTR)MSG_ABOUT_SPECIAL_STR},
    {MSG_QUIT_TEXT,(STRPTR)MSG_QUIT_TEXT_STR},
    {MSG_QUIT_OK,(STRPTR)MSG_QUIT_OK_STR},
    {MSG_QUIT_CANCEL,(STRPTR)MSG_QUIT_CANCEL_STR},
    {MSG_QUIT_WARNING,(STRPTR)MSG_QUIT_WARNING_STR},
    {MSG_DEMO_TEXT,(STRPTR)MSG_DEMO_TEXT_STR},
    {MSG_DEMO_OK,(STRPTR)MSG_DEMO_OK_STR},
    {MSG_SCRCLOSE_TEXT,(STRPTR)MSG_SCRCLOSE_TEXT_STR},
    {MSG_SCRCLOSE_OK,(STRPTR)MSG_SCRCLOSE_OK_STR},
    {MSG_NWS_TITLE,(STRPTR)MSG_NWS_TITLE_STR},
    {MSG_NWS_QUEUED,(STRPTR)MSG_NWS_QUEUED_STR},
    {MSG_NWS_STARTED,(STRPTR)MSG_NWS_STARTED_STR},
    {MSG_NWS_LOOKUP,(STRPTR)MSG_NWS_LOOKUP_STR},
    {MSG_NWS_CONNECT,(STRPTR)MSG_NWS_CONNECT_STR},
    {MSG_NWS_WAIT,(STRPTR)MSG_NWS_WAIT_STR},
    {MSG_NWS_PROCESS,(STRPTR)MSG_NWS_PROCESS_STR},
    {MSG_NWS_LOGIN,(STRPTR)MSG_NWS_LOGIN_STR},
    {MSG_NWS_CPS,(STRPTR)MSG_NWS_CPS_STR},
    {MSG_NWS_NEWSGROUP,(STRPTR)MSG_NWS_NEWSGROUP_STR},
    {MSG_NWS_UPLOAD,(STRPTR)MSG_NWS_UPLOAD_STR},
    {MSG_AUTH_TITLE,(STRPTR)MSG_AUTH_TITLE_STR},
    {MSG_AUTH_PROMPT,(STRPTR)MSG_AUTH_PROMPT_STR},
    {MSG_AUTH_USERID,(STRPTR)MSG_AUTH_USERID_STR},
    {MSG_AUTH_PASSWORD,(STRPTR)MSG_AUTH_PASSWORD_STR},
    {MSG_AUTH_OK,(STRPTR)MSG_AUTH_OK_STR},
    {MSG_AUTH_CANCEL,(STRPTR)MSG_AUTH_CANCEL_STR},
    {MSG_AUTH_PROMPT_FTP,(STRPTR)MSG_AUTH_PROMPT_FTP_STR},
    {MSG_AUTH_USERID_FTP,(STRPTR)MSG_AUTH_USERID_FTP_STR},
    {MSG_AUTH_PROMPT_PROXY,(STRPTR)MSG_AUTH_PROMPT_PROXY_STR},
    {MSG_FILE_SAVETITLE,(STRPTR)MSG_FILE_SAVETITLE_STR},
    {MSG_FILE_SAVE,(STRPTR)MSG_FILE_SAVE_STR},
    {MSG_FILE_INCTEXT,(STRPTR)MSG_FILE_INCTEXT_STR},
    {MSG_DUMMY_1,(STRPTR)MSG_DUMMY_1_STR},
    {MSG_FILE_EXISTTEXT,(STRPTR)MSG_FILE_EXISTTEXT_STR},
    {MSG_FILE_EXISTBUTTONS,(STRPTR)MSG_FILE_EXISTBUTTONS_STR},
    {MSG_FILE_AREXXTITLE,(STRPTR)MSG_FILE_AREXXTITLE_STR},
    {MSG_FILE_HOTLISTTITLE,(STRPTR)MSG_FILE_HOTLISTTITLE_STR},
    {MSG_FILE_LOCALTITLE,(STRPTR)MSG_FILE_LOCALTITLE_STR},
    {MSG_FILE_OPEN,(STRPTR)MSG_FILE_OPEN_STR},
    {MSG_FILE_TEMPTITLE,(STRPTR)MSG_FILE_TEMPTITLE_STR},
    {MSG_FILE_TEMPBUTTONS,(STRPTR)MSG_FILE_TEMPBUTTONS_STR},
    {MSG_FILE_INCBUTTONS2,(STRPTR)MSG_FILE_INCBUTTONS2_STR},
    {MSG_FILE_SETTINGSTITLE,(STRPTR)MSG_FILE_SETTINGSTITLE_STR},
    {MSG_FILE_UPLOADTITLE,(STRPTR)MSG_FILE_UPLOADTITLE_STR},
    {MSG_TCP_ENDTCP,(STRPTR)MSG_TCP_ENDTCP_STR},
    {MSG_TCP_BUTTONS,(STRPTR)MSG_TCP_BUTTONS_STR},
    {MSG_STARTUP_FONTS,(STRPTR)MSG_STARTUP_FONTS_STR},
    {MSG_STARTUP_IMAGES,(STRPTR)MSG_STARTUP_IMAGES_STR},
    {MSG_STARTUP_CACHE,(STRPTR)MSG_STARTUP_CACHE_STR},
    {MSG_HOTL_TITLE,(STRPTR)MSG_HOTL_TITLE_STR},
    {MSG_HOTL_ADDLINK,(STRPTR)MSG_HOTL_ADDLINK_STR},
    {MSG_HOTL_ADDGROUP,(STRPTR)MSG_HOTL_ADDGROUP_STR},
    {MSG_HOTL_REMOVE,(STRPTR)MSG_HOTL_REMOVE_STR},
    {MSG_HOTL_FOLLOW,(STRPTR)MSG_HOTL_FOLLOW_STR},
    {MSG_HOTL_NAME,(STRPTR)MSG_HOTL_NAME_STR},
    {MSG_HOTL_URL,(STRPTR)MSG_HOTL_URL_STR},
    {MSG_HOTL_NEWGROUP,(STRPTR)MSG_HOTL_NEWGROUP_STR},
    {MSG_HOTL_NEWLINK,(STRPTR)MSG_HOTL_NEWLINK_STR},
    {MSG_HOTL_TITLE_VIEWER,(STRPTR)MSG_HOTL_TITLE_VIEWER_STR},
    {MSG_HOTL_VURLS,(STRPTR)MSG_HOTL_VURLS_STR},
    {MSG_HOTL_MANAGER,(STRPTR)MSG_HOTL_MANAGER_STR},
    {MSG_HOTL_TITLE_MANAGER,(STRPTR)MSG_HOTL_TITLE_MANAGER_STR},
    {MSG_HOTL_LTYPE,(STRPTR)MSG_HOTL_LTYPE_STR},
    {MSG_HOTL_LTYPE_REST,(STRPTR)MSG_HOTL_LTYPE_REST_STR},
    {MSG_HOTL_LTYPE_ALL,(STRPTR)MSG_HOTL_LTYPE_ALL_STR},
    {MSG_HOTL_LTYPE_GROUPS,(STRPTR)MSG_HOTL_LTYPE_GROUPS_STR},
    {MSG_HOTL_LSORT,(STRPTR)MSG_HOTL_LSORT_STR},
    {MSG_HOTL_LSORT_TITLE,(STRPTR)MSG_HOTL_LSORT_TITLE_STR},
    {MSG_HOTL_LSORT_URL,(STRPTR)MSG_HOTL_LSORT_URL_STR},
    {MSG_HOTL_MOVEIN,(STRPTR)MSG_HOTL_MOVEIN_STR},
    {MSG_HOTL_MOVEOUT,(STRPTR)MSG_HOTL_MOVEOUT_STR},
    {MSG_HOTL_MOVE,(STRPTR)MSG_HOTL_MOVE_STR},
    {MSG_HOTL_DEL,(STRPTR)MSG_HOTL_DEL_STR},
    {MSG_HOTL_WHERE,(STRPTR)MSG_HOTL_WHERE_STR},
    {MSG_HOTL_SHOW,(STRPTR)MSG_HOTL_SHOW_STR},
    {MSG_HOTL_SORT,(STRPTR)MSG_HOTL_SORT_STR},
    {MSG_HOTL_PARENT,(STRPTR)MSG_HOTL_PARENT_STR},
    {MSG_HOTL_ALL,(STRPTR)MSG_HOTL_ALL_STR},
    {MSG_HOTL_ADDENTRY,(STRPTR)MSG_HOTL_ADDENTRY_STR},
    {MSG_HOTL_ADDGROUP2,(STRPTR)MSG_HOTL_ADDGROUP2_STR},
    {MSG_HOTL_URLS,(STRPTR)MSG_HOTL_URLS_STR},
    {MSG_HOTL_REVERT,(STRPTR)MSG_HOTL_REVERT_STR},
    {MSG_HOTL_SAVE,(STRPTR)MSG_HOTL_SAVE_STR},
    {MSG_HOTL_LADD,(STRPTR)MSG_HOTL_LADD_STR},
    {MSG_HOTL_FIND,(STRPTR)MSG_HOTL_FIND_STR},
    {MSG_HOTL_PATTERN,(STRPTR)MSG_HOTL_PATTERN_STR},
    {MSG_HOTL_NEXT,(STRPTR)MSG_HOTL_NEXT_STR},
    {MSG_HOTL_DEFAULT_TITLE,(STRPTR)MSG_HOTL_DEFAULT_TITLE_STR},
    {MSG_HOTL_DEFAULT_GROUP,(STRPTR)MSG_HOTL_DEFAULT_GROUP_STR},
    {MSG_HOTL_TITLE_ALLGROUPS,(STRPTR)MSG_HOTL_TITLE_ALLGROUPS_STR},
    {MSG_HOTL_TITLE_WHERE,(STRPTR)MSG_HOTL_TITLE_WHERE_STR},
    {MSG_HOTL_TITLE_REST,(STRPTR)MSG_HOTL_TITLE_REST_STR},
    {MSG_HOTL_TITLE_ALL,(STRPTR)MSG_HOTL_TITLE_ALL_STR},
    {MSG_HOTL_TITLE_ROOTGROUPS,(STRPTR)MSG_HOTL_TITLE_ROOTGROUPS_STR},
    {MSG_HOTL_LSORT_DATE,(STRPTR)MSG_HOTL_LSORT_DATE_STR},
    {MSG_WHIS_TITLE,(STRPTR)MSG_WHIS_TITLE_STR},
    {MSG_WHIS_FILTER,(STRPTR)MSG_WHIS_FILTER_STR},
    {MSG_WHIS_WINDOW,(STRPTR)MSG_WHIS_WINDOW_STR},
    {MSG_WHIS_ORDER,(STRPTR)MSG_WHIS_ORDER_STR},
    {MSG_WHIS_DISPLAY,(STRPTR)MSG_WHIS_DISPLAY_STR},
    {MSG_WHIS_ORDER_NATURAL,(STRPTR)MSG_WHIS_ORDER_NATURAL_STR},
    {MSG_WHIS_ORDER_MAINLINE,(STRPTR)MSG_WHIS_ORDER_MAINLINE_STR},
    {MSG_WHIS_ORDER_RETRIEVED,(STRPTR)MSG_WHIS_ORDER_RETRIEVED_STR},
    {MSG_WHIS_ORDER_TITLE,(STRPTR)MSG_WHIS_ORDER_TITLE_STR},
    {MSG_WHIS_ORDER_URL,(STRPTR)MSG_WHIS_ORDER_URL_STR},
    {MSG_CABR_TITLE,(STRPTR)MSG_CABR_TITLE_STR},
    {MSG_CABR_URL,(STRPTR)MSG_CABR_URL_STR},
    {MSG_CABR_DATE,(STRPTR)MSG_CABR_DATE_STR},
    {MSG_CABR_SIZE,(STRPTR)MSG_CABR_SIZE_STR},
    {MSG_CABR_TYPE,(STRPTR)MSG_CABR_TYPE_STR},
    {MSG_CABR_FILE,(STRPTR)MSG_CABR_FILE_STR},
    {MSG_CABR_STATUS,(STRPTR)MSG_CABR_STATUS_STR},
    {MSG_CABR_SORTBY,(STRPTR)MSG_CABR_SORTBY_STR},
    {MSG_CABR_OPEN,(STRPTR)MSG_CABR_OPEN_STR},
    {MSG_CABR_SAVE,(STRPTR)MSG_CABR_SAVE_STR},
    {MSG_CABR_DELETE,(STRPTR)MSG_CABR_DELETE_STR},
    {MSG_CABR_FIND,(STRPTR)MSG_CABR_FIND_STR},
    {MSG_CABR_PATTERN,(STRPTR)MSG_CABR_PATTERN_STR},
    {MSG_CABR_NEXT,(STRPTR)MSG_CABR_NEXT_STR},
    {MSG_PROGRESS_TITLE,(STRPTR)MSG_PROGRESS_TITLE_STR},
    {MSG_PROGRESS_CANCEL,(STRPTR)MSG_PROGRESS_CANCEL_STR},
    {MSG_FIXCACHE_WARNING,(STRPTR)MSG_FIXCACHE_WARNING_STR},
    {MSG_FIXCACHE_BUTTONS,(STRPTR)MSG_FIXCACHE_BUTTONS_STR},
    {MSG_FIXCACHE_PROGRESS,(STRPTR)MSG_FIXCACHE_PROGRESS_STR},
    {MSG_FIXCACHE_DELETEIMAGES,(STRPTR)MSG_FIXCACHE_DELETEIMAGES_STR},
    {MSG_FIXCACHE_DELETEDOCS,(STRPTR)MSG_FIXCACHE_DELETEDOCS_STR},
    {MSG_FIXCACHE_ERASE,(STRPTR)MSG_FIXCACHE_ERASE_STR},
    {MSG_FIXCACHE_CORRUPT,(STRPTR)MSG_FIXCACHE_CORRUPT_STR},
    {MSG_SEARCH_STRING,(STRPTR)MSG_SEARCH_STRING_STR},
    {MSG_SEARCH_IGNORECASE,(STRPTR)MSG_SEARCH_IGNORECASE_STR},
    {MSG_SEARCH_SEARCH,(STRPTR)MSG_SEARCH_SEARCH_STR},
    {MSG_SEARCH_FROMTOP,(STRPTR)MSG_SEARCH_FROMTOP_STR},
    {MSG_SEARCH_BACKWARDS,(STRPTR)MSG_SEARCH_BACKWARDS_STR},
    {MSG_SEARCH_CANCEL,(STRPTR)MSG_SEARCH_CANCEL_STR},
    {MSG_SEARCH_NOTFOUND,(STRPTR)MSG_SEARCH_NOTFOUND_STR},
    {MSG_SEARCH_OK,(STRPTR)MSG_SEARCH_OK_STR},
    {MSG_POPUP_LOADIMAGE,(STRPTR)MSG_POPUP_LOADIMAGE_STR},
    {MSG_POPUP_RELOADIMAGE,(STRPTR)MSG_POPUP_RELOADIMAGE_STR},
    {MSG_POPUP_SAVEIMAGE,(STRPTR)MSG_POPUP_SAVEIMAGE_STR},
    {MSG_POPUP_DOWNLOADIMAGE,(STRPTR)MSG_POPUP_DOWNLOADIMAGE_STR},
    {MSG_POPUP_FLUSHIMAGE,(STRPTR)MSG_POPUP_FLUSHIMAGE_STR},
    {MSG_POPUP_SHOWIMAGE,(STRPTR)MSG_POPUP_SHOWIMAGE_STR},
    {MSG_POPUP_OPENLINK,(STRPTR)MSG_POPUP_OPENLINK_STR},
    {MSG_POPUP_OPENLINKNW,(STRPTR)MSG_POPUP_OPENLINKNW_STR},
    {MSG_POPUP_LOADLINK,(STRPTR)MSG_POPUP_LOADLINK_STR},
    {MSG_POPUP_SAVELINK,(STRPTR)MSG_POPUP_SAVELINK_STR},
    {MSG_POPUP_DOWNLOADLINK,(STRPTR)MSG_POPUP_DOWNLOADLINK_STR},
    {MSG_POPUP_ADDLINK,(STRPTR)MSG_POPUP_ADDLINK_STR},
    {MSG_POPUP_RELOADFRAME,(STRPTR)MSG_POPUP_RELOADFRAME_STR},
    {MSG_POPUP_SAVEFRAME,(STRPTR)MSG_POPUP_SAVEFRAME_STR},
    {MSG_POPUP_VIEWFRAME,(STRPTR)MSG_POPUP_VIEWFRAME_STR},
    {MSG_POPUP_SHOWFRAME,(STRPTR)MSG_POPUP_SHOWFRAME_STR},
    {MSG_POPUP_SEARCHFRAME,(STRPTR)MSG_POPUP_SEARCHFRAME_STR},
    {MSG_POPUP_INFOFRAME,(STRPTR)MSG_POPUP_INFOFRAME_STR},
    {MSG_PRINTP_TITLE,(STRPTR)MSG_PRINTP_TITLE_STR},
    {MSG_PRINTP_SCALE,(STRPTR)MSG_PRINTP_SCALE_STR},
    {MSG_PRINTP_PRINTBG,(STRPTR)MSG_PRINTP_PRINTBG_STR},
    {MSG_PRINTP_FORMFEED,(STRPTR)MSG_PRINTP_FORMFEED_STR},
    {MSG_PRINTP_CENTER,(STRPTR)MSG_PRINTP_CENTER_STR},
    {MSG_PRINTP_PRINT,(STRPTR)MSG_PRINTP_PRINT_STR},
    {MSG_PRINTP_CANCEL,(STRPTR)MSG_PRINTP_CANCEL_STR},
    {MSG_PRINT_NOPRINTER,(STRPTR)MSG_PRINT_NOPRINTER_STR},
    {MSG_PRINT_OK,(STRPTR)MSG_PRINT_OK_STR},
    {MSG_PRINT_PROGRESS,(STRPTR)MSG_PRINT_PROGRESS_STR},
    {MSG_PRINTP_LAYOUTWIDTH,(STRPTR)MSG_PRINTP_LAYOUTWIDTH_STR},
    {MSG_PRINTP_LW_WINDOW,(STRPTR)MSG_PRINTP_LW_WINDOW_STR},
    {MSG_PRINTP_LW_DOCUMENT,(STRPTR)MSG_PRINTP_LW_DOCUMENT_STR},
    {MSG_PRINTP_LW_SCREEN,(STRPTR)MSG_PRINTP_LW_SCREEN_STR},
    {MSG_COOKIE_TITLE,(STRPTR)MSG_COOKIE_TITLE_STR},
    {MSG_COOKIE_WARNING,(STRPTR)MSG_COOKIE_WARNING_STR},
    {MSG_COOKIE_NAME,(STRPTR)MSG_COOKIE_NAME_STR},
    {MSG_COOKIE_VALUE,(STRPTR)MSG_COOKIE_VALUE_STR},
    {MSG_COOKIE_DOMAIN,(STRPTR)MSG_COOKIE_DOMAIN_STR},
    {MSG_COOKIE_PATH,(STRPTR)MSG_COOKIE_PATH_STR},
    {MSG_COOKIE_COMMENT,(STRPTR)MSG_COOKIE_COMMENT_STR},
    {MSG_COOKIE_MAXAGE,(STRPTR)MSG_COOKIE_MAXAGE_STR},
    {MSG_COOKIE_EXPIRES,(STRPTR)MSG_COOKIE_EXPIRES_STR},
    {MSG_COOKIE_ONCE,(STRPTR)MSG_COOKIE_ONCE_STR},
    {MSG_COOKIE_ACCEPT,(STRPTR)MSG_COOKIE_ACCEPT_STR},
    {MSG_COOKIE_NEVER,(STRPTR)MSG_COOKIE_NEVER_STR},
    {MSG_COOKIE_CANCEL,(STRPTR)MSG_COOKIE_CANCEL_STR},
    {MSG_COOKIE_SECURE,(STRPTR)MSG_COOKIE_SECURE_STR},
    {MSG_COOKIE_UNSECURE,(STRPTR)MSG_COOKIE_UNSECURE_STR},
    {MSG_INFO_TITLE,(STRPTR)MSG_INFO_TITLE_STR},
    {MSG_INFO_FROMCACHE,(STRPTR)MSG_INFO_FROMCACHE_STR},
    {MSG_INFO_XFER,(STRPTR)MSG_INFO_XFER_STR},
    {MSG_INFO_META,(STRPTR)MSG_INFO_META_STR},
    {MSG_INFO_LINK,(STRPTR)MSG_INFO_LINK_STR},
    {MSG_INFO_CIPHER,(STRPTR)MSG_INFO_CIPHER_STR},
    {MSG_INFO_SSLLIBRARY,(STRPTR)MSG_INFO_SSLLIBRARY_STR},
    {MSG_USERBUTTON_CACHE,(STRPTR)MSG_USERBUTTON_CACHE_STR},
    {MSG_USERBUTTON_CLOCK,(STRPTR)MSG_USERBUTTON_CLOCK_STR},
    {MSG_AUTHEDIT_TITLE,(STRPTR)MSG_AUTHEDIT_TITLE_STR},
    {MSG_AUTHEDIT_TITLE_SERVER,(STRPTR)MSG_AUTHEDIT_TITLE_SERVER_STR},
    {MSG_AUTHEDIT_TITLE_USERID,(STRPTR)MSG_AUTHEDIT_TITLE_USERID_STR},
    {MSG_AUTHEDIT_DEL,(STRPTR)MSG_AUTHEDIT_DEL_STR},
    {MSG_AUTHEDIT_SERVER,(STRPTR)MSG_AUTHEDIT_SERVER_STR},
    {MSG_AUTHEDIT_USERID,(STRPTR)MSG_AUTHEDIT_USERID_STR},
    {MSG_AUTHEDIT_PASSWORD,(STRPTR)MSG_AUTHEDIT_PASSWORD_STR},
    {MSG_JSPROMPT_OK,(STRPTR)MSG_JSPROMPT_OK_STR},
    {MSG_JSPROMPT_CANCEL,(STRPTR)MSG_JSPROMPT_CANCEL_STR},
    {MSG_FORMWARN_TITLE,(STRPTR)MSG_FORMWARN_TITLE_STR},
    {MSG_FORMWARN_WARNING,(STRPTR)MSG_FORMWARN_WARNING_STR},
    {MSG_FORMWARN_BUTTONS,(STRPTR)MSG_FORMWARN_BUTTONS_STR},
    {MSG_MAIL_TITLE,(STRPTR)MSG_MAIL_TITLE_STR},
    {MSG_MAIL_NOSMTPHOST,(STRPTR)MSG_MAIL_NOSMTPHOST_STR},
    {MSG_MAIL_NOCONNECT,(STRPTR)MSG_MAIL_NOCONNECT_STR},
    {MSG_MAIL_MAILFAILED,(STRPTR)MSG_MAIL_MAILFAILED_STR},
    {MSG_MAIL_BUTTONS,(STRPTR)MSG_MAIL_BUTTONS_STR},
    {MSG_MAIL_SAVETITLE,(STRPTR)MSG_MAIL_SAVETITLE_STR},
    {MSG_MAIL_MAILTO_TITLE,(STRPTR)MSG_MAIL_MAILTO_TITLE_STR},
    {MSG_MAIL_MAILTO_HEADER,(STRPTR)MSG_MAIL_MAILTO_HEADER_STR},
    {MSG_MAIL_TO,(STRPTR)MSG_MAIL_TO_STR},
    {MSG_MAIL_SUBJECT,(STRPTR)MSG_MAIL_SUBJECT_STR},
    {MSG_MAIL_MESSAGE_BODY,(STRPTR)MSG_MAIL_MESSAGE_BODY_STR},
    {MSG_MAIL_EXTRA_HEADERS,(STRPTR)MSG_MAIL_EXTRA_HEADERS_STR},
    {MSG_MAIL_SEND,(STRPTR)MSG_MAIL_SEND_STR},
    {MSG_MAIL_RESET,(STRPTR)MSG_MAIL_RESET_STR},
    {MSG_MAIL_RETURN,(STRPTR)MSG_MAIL_RETURN_STR},
    {MSG_MAIL_MAIL_SENT_TITLE,(STRPTR)MSG_MAIL_MAIL_SENT_TITLE_STR},
    {MSG_MAIL_MAIL_SENT,(STRPTR)MSG_MAIL_MAIL_SENT_STR},
    {MSG_NEWS_TITLE,(STRPTR)MSG_NEWS_TITLE_STR},
    {MSG_NEWS_NONNTPHOST,(STRPTR)MSG_NEWS_NONNTPHOST_STR},
    {MSG_NEWS_NOCONNECT,(STRPTR)MSG_NEWS_NOCONNECT_STR},
    {MSG_NEWS_POSTFAILED,(STRPTR)MSG_NEWS_POSTFAILED_STR},
    {MSG_NEWS_BUTTONS,(STRPTR)MSG_NEWS_BUTTONS_STR},
    {MSG_NEWS_SAVETITLE,(STRPTR)MSG_NEWS_SAVETITLE_STR},
    {MSG_NEWS_NEWS_HEADER,(STRPTR)MSG_NEWS_NEWS_HEADER_STR},
    {MSG_NEWS_SUBSCRIBED,(STRPTR)MSG_NEWS_SUBSCRIBED_STR},
    {MSG_NEWS_NEW_ARTICLES,(STRPTR)MSG_NEWS_NEW_ARTICLES_STR},
    {MSG_NEWS_CATCH_UP,(STRPTR)MSG_NEWS_CATCH_UP_STR},
    {MSG_NEWS_UNSUBSCRIBE,(STRPTR)MSG_NEWS_UNSUBSCRIBE_STR},
    {MSG_NEWS_OTHER_GROUP,(STRPTR)MSG_NEWS_OTHER_GROUP_STR},
    {MSG_NEWS_SUBSCRIBE,(STRPTR)MSG_NEWS_SUBSCRIBE_STR},
    {MSG_NEWS_READ,(STRPTR)MSG_NEWS_READ_STR},
    {MSG_NEWS_SEARCH_FOR_GROUP,(STRPTR)MSG_NEWS_SEARCH_FOR_GROUP_STR},
    {MSG_NEWS_SEARCH,(STRPTR)MSG_NEWS_SEARCH_STR},
    {MSG_NEWS_LONG_DOWNLOAD,(STRPTR)MSG_NEWS_LONG_DOWNLOAD_STR},
    {MSG_NEWS_NO_SUBJECT,(STRPTR)MSG_NEWS_NO_SUBJECT_STR},
    {MSG_NEWS_LINES,(STRPTR)MSG_NEWS_LINES_STR},
    {MSG_NEWS_POST_NEW_ARTICLE,(STRPTR)MSG_NEWS_POST_NEW_ARTICLE_STR},
    {MSG_NEWS_GROUP_LIST,(STRPTR)MSG_NEWS_GROUP_LIST_STR},
    {MSG_NEWS_ERROR_NO_GROUP,(STRPTR)MSG_NEWS_ERROR_NO_GROUP_STR},
    {MSG_NEWS_USE_FRAMES,(STRPTR)MSG_NEWS_USE_FRAMES_STR},
    {MSG_NEWS_ERROR_NO_ARTICLE,(STRPTR)MSG_NEWS_ERROR_NO_ARTICLE_STR},
    {MSG_NEWS_FOLLOW_UP,(STRPTR)MSG_NEWS_FOLLOW_UP_STR},
    {MSG_NEWS_REPLY,(STRPTR)MSG_NEWS_REPLY_STR},
    {MSG_NEWS_SAVE,(STRPTR)MSG_NEWS_SAVE_STR},
    {MSG_NEWS_SUBJECT,(STRPTR)MSG_NEWS_SUBJECT_STR},
    {MSG_NEWS_NEWSGROUPS,(STRPTR)MSG_NEWS_NEWSGROUPS_STR},
    {MSG_NEWS_ARTICLE_BODY,(STRPTR)MSG_NEWS_ARTICLE_BODY_STR},
    {MSG_NEWS_EXTRA_HEADERS,(STRPTR)MSG_NEWS_EXTRA_HEADERS_STR},
    {MSG_NEWS_POST_ARTICLE,(STRPTR)MSG_NEWS_POST_ARTICLE_STR},
    {MSG_NEWS_RESET,(STRPTR)MSG_NEWS_RESET_STR},
    {MSG_NEWS_RETURN,(STRPTR)MSG_NEWS_RETURN_STR},
    {MSG_NEWS_FOLLOWUP_TITLE,(STRPTR)MSG_NEWS_FOLLOWUP_TITLE_STR},
    {MSG_NEWS_FOLLOWUP_TO_GROUPS,(STRPTR)MSG_NEWS_FOLLOWUP_TO_GROUPS_STR},
    {MSG_NEWS_PLEASE_DELETE,(STRPTR)MSG_NEWS_PLEASE_DELETE_STR},
    {MSG_NEWS_REPLY_TITLE,(STRPTR)MSG_NEWS_REPLY_TITLE_STR},
    {MSG_NEWS_REPLY_TO_ADDRESS,(STRPTR)MSG_NEWS_REPLY_TO_ADDRESS_STR},
    {MSG_NEWS_ARTICLE_POSTED_TITLE,(STRPTR)MSG_NEWS_ARTICLE_POSTED_TITLE_STR},
    {MSG_NEWS_ARTICLE_POSTED,(STRPTR)MSG_NEWS_ARTICLE_POSTED_STR},
    {MSG_NEWS_DOWNLOAD_TITLE,(STRPTR)MSG_NEWS_DOWNLOAD_TITLE_STR},
    {MSG_NEWS_DOWNLOAD_BODY,(STRPTR)MSG_NEWS_DOWNLOAD_BODY_STR},
    {MSG_NEWS_SEARCH_RESULT,(STRPTR)MSG_NEWS_SEARCH_RESULT_STR},
    {MSG_NEWS_FIRST_MATCHING,(STRPTR)MSG_NEWS_FIRST_MATCHING_STR},
    {MSG_NEWS_FOUND_MATCHING,(STRPTR)MSG_NEWS_FOUND_MATCHING_STR},
    {MSG_CMDWARN_TITLE,(STRPTR)MSG_CMDWARN_TITLE_STR},
    {MSG_CMDWARN_SHELL,(STRPTR)MSG_CMDWARN_SHELL_STR},
    {MSG_CMDWARN_AREXX,(STRPTR)MSG_CMDWARN_AREXX_STR},
    {MSG_CMDWARN_BUTTONS,(STRPTR)MSG_CMDWARN_BUTTONS_STR},
    {MSG_SSLWARN_SSL_TITLE,(STRPTR)MSG_SSLWARN_SSL_TITLE_STR},
    {MSG_SSLWARN_SSL_TEXT,(STRPTR)MSG_SSLWARN_SSL_TEXT_STR},
    {MSG_SSLWARN_SSL_BUTTONS,(STRPTR)MSG_SSLWARN_SSL_BUTTONS_STR},
    {MSG_SSLWARN_SSL_NO_SSL,(STRPTR)MSG_SSLWARN_SSL_NO_SSL_STR},
    {MSG_SSLWARN_CERT_TITLE,(STRPTR)MSG_SSLWARN_CERT_TITLE_STR},
    {MSG_SSLWARN_CERT_TEXT,(STRPTR)MSG_SSLWARN_CERT_TEXT_STR},
    {MSG_SSLWARN_CERT_BUTTONS,(STRPTR)MSG_SSLWARN_CERT_BUTTONS_STR},
    {MSG_SSLWARN_SSL_NO_SSL2,(STRPTR)MSG_SSLWARN_SSL_NO_SSL2_STR},
    {MSG_UNKMIME_TITLE,(STRPTR)MSG_UNKMIME_TITLE_STR},
    {MSG_UNKMIME_TEXT,(STRPTR)MSG_UNKMIME_TEXT_STR},
    {MSG_UNKMIME_BUTTONS,(STRPTR)MSG_UNKMIME_BUTTONS_STR},
    {MSG_REQUEST_COPYURL,(STRPTR)MSG_REQUEST_COPYURL_STR},
    {MSG_OPENURL_TITLE,(STRPTR)MSG_OPENURL_TITLE_STR},
    {MSG_OPENURL_TEXT,(STRPTR)MSG_OPENURL_TEXT_STR},
    {MSG_OPENURL_BUTTONS,(STRPTR)MSG_OPENURL_BUTTONS_STR},
    {MSG_SAVEIFF_PROGRESS,(STRPTR)MSG_SAVEIFF_PROGRESS_STR},
    {MSG_PROJECT_MENU,(STRPTR)MSG_PROJECT_MENU_STR},
    {MSG_PROJECT_NEWWINDOW,(STRPTR)MSG_PROJECT_NEWWINDOW_STR},
    {MSG_PROJECT_CLOSEWINDOW,(STRPTR)MSG_PROJECT_CLOSEWINDOW_STR},
    {MSG_PROJECT_OPENURL,(STRPTR)MSG_PROJECT_OPENURL_STR},
    {MSG_PROJECT_OPENWWW,(STRPTR)MSG_PROJECT_OPENWWW_STR},
    {MSG_PROJECT_OPENLOCAL,(STRPTR)MSG_PROJECT_OPENLOCAL_STR},
    {MSG_PROJECT_OPENSEARCH,(STRPTR)MSG_PROJECT_OPENSEARCH_STR},
    {MSG_PROJECT_SOURCE,(STRPTR)MSG_PROJECT_SOURCE_STR},
    {MSG_PROJECT_SAVEHTML,(STRPTR)MSG_PROJECT_SAVEHTML_STR},
    {MSG_PROJECT_ABOUT,(STRPTR)MSG_PROJECT_ABOUT_STR},
    {MSG_PROJECT_QUIT,(STRPTR)MSG_PROJECT_QUIT_STR},
    {MSG_PROJECT_SEARCH,(STRPTR)MSG_PROJECT_SEARCH_STR},
    {MSG_PROJECT_PRINT,(STRPTR)MSG_PROJECT_PRINT_STR},
    {MSG_PROJECT_EDIT,(STRPTR)MSG_PROJECT_EDIT_STR},
    {MSG_PROJECT_INFO,(STRPTR)MSG_PROJECT_INFO_STR},
    {MSG_PROJECT_OPENNEWS,(STRPTR)MSG_PROJECT_OPENNEWS_STR},
    {MSG_PROJECT_ICONIFY,(STRPTR)MSG_PROJECT_ICONIFY_STR},
    {MSG_PROJECT_SAVEIFF,(STRPTR)MSG_PROJECT_SAVEIFF_STR},
    {MSG_CONTROL_MENU,(STRPTR)MSG_CONTROL_MENU_STR},
    {MSG_CONTROL_LOADNOW,(STRPTR)MSG_CONTROL_LOADNOW_STR},
    {MSG_CONTROL_LOADNOWALL,(STRPTR)MSG_CONTROL_LOADNOWALL_STR},
    {MSG_CONTROL_LOADNOWMAPS,(STRPTR)MSG_CONTROL_LOADNOWMAPS_STR},
    {MSG_CONTROL_NETWORKSTATUS,(STRPTR)MSG_CONTROL_NETWORKSTATUS_STR},
    {MSG_DUMMY_2,(STRPTR)MSG_DUMMY_2_STR},
    {MSG_CONTROL_CANCEL,(STRPTR)MSG_CONTROL_CANCEL_STR},
    {MSG_CONTROL_NEXTWINDOW,(STRPTR)MSG_CONTROL_NEXTWINDOW_STR},
    {MSG_CONTROL_PREVWINDOW,(STRPTR)MSG_CONTROL_PREVWINDOW_STR},
    {MSG_CONTROL_NOPROXY,(STRPTR)MSG_CONTROL_NOPROXY_STR},
    {MSG_CONTROL_RLOAD,(STRPTR)MSG_CONTROL_RLOAD_STR},
    {MSG_CONTROL_RLOADDOC,(STRPTR)MSG_CONTROL_RLOADDOC_STR},
    {MSG_CONTROL_RLOADIMGS,(STRPTR)MSG_CONTROL_RLOADIMGS_STR},
    {MSG_CONTROL_PLAYBGSOUND,(STRPTR)MSG_CONTROL_PLAYBGSOUND_STR},
    {MSG_CONTROL_COPYURL,(STRPTR)MSG_CONTROL_COPYURL_STR},
    {MSG_CONTROL_PASTEURL,(STRPTR)MSG_CONTROL_PASTEURL_STR},
    {MSG_CONTROL_COPYBLOCK,(STRPTR)MSG_CONTROL_COPYBLOCK_STR},
    {MSG_CONTROL_DRAGGING,(STRPTR)MSG_CONTROL_DRAGGING_STR},
    {MSG_CONTROL_BREAKJS,(STRPTR)MSG_CONTROL_BREAKJS_STR},
    {MSG_CONTROL_DEBUGJS,(STRPTR)MSG_CONTROL_DEBUGJS_STR},
    {MSG_CONTROL_RESET,(STRPTR)MSG_CONTROL_RESET_STR},
    {MSG_CACHE_MENU,(STRPTR)MSG_CACHE_MENU_STR},
    {MSG_DUMMY_3,(STRPTR)MSG_DUMMY_3_STR},
    {MSG_DUMMY_4,(STRPTR)MSG_DUMMY_4_STR},
    {MSG_DUMMY_5,(STRPTR)MSG_DUMMY_5_STR},
    {MSG_DUMMY_6,(STRPTR)MSG_DUMMY_6_STR},
    {MSG_CACHE_SAVEAUTH,(STRPTR)MSG_CACHE_SAVEAUTH_STR},
    {MSG_CACHE_FLUSHAUTH,(STRPTR)MSG_CACHE_FLUSHAUTH_STR},
    {MSG_CACHE_BROWSER,(STRPTR)MSG_CACHE_BROWSER_STR},
    {MSG_CACHE_FLUSH,(STRPTR)MSG_CACHE_FLUSH_STR},
    {MSG_CACHE_FLSHIMGSND,(STRPTR)MSG_CACHE_FLSHIMGSND_STR},
    {MSG_CACHE_FLSHIMGS,(STRPTR)MSG_CACHE_FLSHIMGS_STR},
    {MSG_CACHE_FLSHDOCSND,(STRPTR)MSG_CACHE_FLSHDOCSND_STR},
    {MSG_CACHE_DELETE,(STRPTR)MSG_CACHE_DELETE_STR},
    {MSG_CACHE_DELIMGS,(STRPTR)MSG_CACHE_DELIMGS_STR},
    {MSG_CACHE_DELDOCS,(STRPTR)MSG_CACHE_DELDOCS_STR},
    {MSG_CACHE_CLEAR,(STRPTR)MSG_CACHE_CLEAR_STR},
    {MSG_CACHE_FIX,(STRPTR)MSG_CACHE_FIX_STR},
    {MSG_CACHE_EDITAUTH,(STRPTR)MSG_CACHE_EDITAUTH_STR},
    {MSG_NAVIGATE_MENU,(STRPTR)MSG_NAVIGATE_MENU_STR},
    {MSG_NAVIGATE_BACK,(STRPTR)MSG_NAVIGATE_BACK_STR},
    {MSG_NAVIGATE_FWD,(STRPTR)MSG_NAVIGATE_FWD_STR},
    {MSG_NAVIGATE_HOME,(STRPTR)MSG_NAVIGATE_HOME_STR},
    {MSG_NAVIGATE_HISTORY,(STRPTR)MSG_NAVIGATE_HISTORY_STR},
    {MSG_HOTLIST_MENU,(STRPTR)MSG_HOTLIST_MENU_STR},
    {MSG_HOTLIST_ADDHOT,(STRPTR)MSG_HOTLIST_ADDHOT_STR},
    {MSG_HOTLIST_SHOWHOT,(STRPTR)MSG_HOTLIST_SHOWHOT_STR},
    {MSG_HOTLIST_MAINT,(STRPTR)MSG_HOTLIST_MAINT_STR},
    {MSG_HOTLIST_SAVE,(STRPTR)MSG_HOTLIST_SAVE_STR},
    {MSG_HOTLIST_RESTORE,(STRPTR)MSG_HOTLIST_RESTORE_STR},
    {MSG_HOTLIST_AMHOTRX,(STRPTR)MSG_HOTLIST_AMHOTRX_STR},
    {MSG_HOTLIST_AMHOT20,(STRPTR)MSG_HOTLIST_AMHOT20_STR},
    {MSG_HOTLIST_IBHOT,(STRPTR)MSG_HOTLIST_IBHOT_STR},
    {MSG_HOTLIST_VIEW,(STRPTR)MSG_HOTLIST_VIEW_STR},
    {MSG_HOTLIST_MGR,(STRPTR)MSG_HOTLIST_MGR_STR},
    {MSG_SETTINGS_MENU,(STRPTR)MSG_SETTINGS_MENU_STR},
    {MSG_SETTINGS_LOADIMG,(STRPTR)MSG_SETTINGS_LOADIMG_STR},
    {MSG_SETTINGS_LOADIMGALL,(STRPTR)MSG_SETTINGS_LOADIMGALL_STR},
    {MSG_SETTINGS_LOADIMGMAPS,(STRPTR)MSG_SETTINGS_LOADIMGMAPS_STR},
    {MSG_SETTINGS_LOADIMGOFF,(STRPTR)MSG_SETTINGS_LOADIMGOFF_STR},
    {MSG_DUMMY_7,(STRPTR)MSG_DUMMY_7_STR},
    {MSG_DUMMY_8,(STRPTR)MSG_DUMMY_8_STR},
    {MSG_SETTINGS_BROWSER,(STRPTR)MSG_SETTINGS_BROWSER_STR},
    {MSG_SETTINGS_PROGRAM,(STRPTR)MSG_SETTINGS_PROGRAM_STR},
    {MSG_SETTINGS_NETWORK,(STRPTR)MSG_SETTINGS_NETWORK_STR},
    {MSG_SETTINGS_CLASSACT,(STRPTR)MSG_SETTINGS_CLASSACT_STR},
    {MSG_SETTINGS_SAVEALL,(STRPTR)MSG_SETTINGS_SAVEALL_STR},
    {MSG_SETTINGS_SNAPSHOT,(STRPTR)MSG_SETTINGS_SNAPSHOT_STR},
    {MSG_SETTINGS_SNAPSHOTALT,(STRPTR)MSG_SETTINGS_SNAPSHOTALT_STR},
    {MSG_SETTINGS_BGIMAGES,(STRPTR)MSG_SETTINGS_BGIMAGES_STR},
    {MSG_SETTINGS_BGSOUND,(STRPTR)MSG_SETTINGS_BGSOUND_STR},
    {MSG_SETTINGS_SAVEAS,(STRPTR)MSG_SETTINGS_SAVEAS_STR},
    {MSG_SETTINGS_LOAD,(STRPTR)MSG_SETTINGS_LOAD_STR},
    {MSG_SETTINGS_GUI,(STRPTR)MSG_SETTINGS_GUI_STR},
    {MSG_HELP_MENU,(STRPTR)MSG_HELP_MENU_STR},
    {MSG_HELP_HELP,(STRPTR)MSG_HELP_HELP_STR},
    {MSG_HELP_AWEBHOME,(STRPTR)MSG_HELP_AWEBHOME_STR},
    {MSG_HELP_AWEBFAQ,(STRPTR)MSG_HELP_AWEBFAQ_STR},
    {MSG_HELP_REGISTER,(STRPTR)MSG_HELP_REGISTER_STR},
    {MSG_AREXX_MENU,(STRPTR)MSG_AREXX_MENU_STR},
    {MSG_AREXX_AREXX,(STRPTR)MSG_AREXX_AREXX_STR},
    {MSG_ERROR_CANTOPEN,(STRPTR)MSG_ERROR_CANTOPEN_STR},
    {MSG_ERROR_CANTOPENV,(STRPTR)MSG_ERROR_CANTOPENV_STR},
    {MSG_ERROR_NEEDOS30,(STRPTR)MSG_ERROR_NEEDOS30_STR},
    {MSG_ERROR_CANTQUIT,(STRPTR)MSG_ERROR_CANTQUIT_STR},
    {MSG_EPART_ERROR,(STRPTR)MSG_EPART_ERROR_STR},
    {MSG_EPART_RETURL,(STRPTR)MSG_EPART_RETURL_STR},
    {MSG_EPART_ADDRSCHEME,(STRPTR)MSG_EPART_ADDRSCHEME_STR},
    {MSG_EPART_NOLIB,(STRPTR)MSG_EPART_NOLIB_STR},
    {MSG_EPART_NOHOST,(STRPTR)MSG_EPART_NOHOST_STR},
    {MSG_EPART_NOCONNECT,(STRPTR)MSG_EPART_NOCONNECT_STR},
    {MSG_EPART_NOCONNECT_TIMEOUT,(STRPTR)MSG_EPART_NOCONNECT_TIMEOUT_STR},
    {MSG_EPART_NOCONNECT_REFUSED,(STRPTR)MSG_EPART_NOCONNECT_REFUSED_STR},
    {MSG_EPART_NOCONNECT_RESET,(STRPTR)MSG_EPART_NOCONNECT_RESET_STR},
    {MSG_EPART_NOCONNECT_UNREACH,(STRPTR)MSG_EPART_NOCONNECT_UNREACH_STR},
    {MSG_EPART_NOCONNECT_HOSTUNREACH,(STRPTR)MSG_EPART_NOCONNECT_HOSTUNREACH_STR},
    {MSG_EPART_NOFILE,(STRPTR)MSG_EPART_NOFILE_STR},
    {MSG_EPART_XAWEB,(STRPTR)MSG_EPART_XAWEB_STR},
    {MSG_EPART_FLUSHED_HEAD,(STRPTR)MSG_EPART_FLUSHED_HEAD_STR},
    {MSG_EPART_FLUSHED_MSG,(STRPTR)MSG_EPART_FLUSHED_MSG_STR},
    {MSG_EPART_FLUSHED_NORELOAD,(STRPTR)MSG_EPART_FLUSHED_NORELOAD_STR},
    {MSG_EPART_NOLOGIN,(STRPTR)MSG_EPART_NOLOGIN_STR},
    {MSG_EPART_NOAWEBLIB,(STRPTR)MSG_EPART_NOAWEBLIB_STR},
    {MSG_EPART_NOPROGRAM,(STRPTR)MSG_EPART_NOPROGRAM_STR},
    {MSG_AWEB_EXTWINTITLE,(STRPTR)MSG_AWEB_EXTWINTITLE_STR},
    {MSG_AWEB_BYTESREAD,(STRPTR)MSG_AWEB_BYTESREAD_STR},
    {MSG_AWEB_FORMSUBMIT,(STRPTR)MSG_AWEB_FORMSUBMIT_STR},
    {MSG_AWEB_FORMRESET,(STRPTR)MSG_AWEB_FORMRESET_STR},
    {MSG_AWEB_INDEXPROMPT,(STRPTR)MSG_AWEB_INDEXPROMPT_STR},
    {MSG_AWEB_NODOCTITLE,(STRPTR)MSG_AWEB_NODOCTITLE_STR},
    {MSG_AWEB_SCREENTITLE,(STRPTR)MSG_AWEB_SCREENTITLE_STR},
    {MSG_AWEB_HOTLISTTITLE,(STRPTR)MSG_AWEB_HOTLISTTITLE_STR},
    {MSG_AWEB_GOPHERINDEX,(STRPTR)MSG_AWEB_GOPHERINDEX_STR},
    {MSG_AWEB_GOPHERMENU,(STRPTR)MSG_AWEB_GOPHERMENU_STR},
    {MSG_AWEB_WINDOWHIS,(STRPTR)MSG_AWEB_WINDOWHIS_STR},
    {MSG_AWEB_OTHER,(STRPTR)MSG_AWEB_OTHER_STR},
    {MSG_AWEB_LOOKUP,(STRPTR)MSG_AWEB_LOOKUP_STR},
    {MSG_AWEB_CONNECT,(STRPTR)MSG_AWEB_CONNECT_STR},
    {MSG_AWEB_WAITING,(STRPTR)MSG_AWEB_WAITING_STR},
    {MSG_AWEB_TCPSTART,(STRPTR)MSG_AWEB_TCPSTART_STR},
    {MSG_AWEB_FORMLOCATION,(STRPTR)MSG_AWEB_FORMLOCATION_STR},
    {MSG_AWEB_FORMBUTTON,(STRPTR)MSG_AWEB_FORMBUTTON_STR},
    {MSG_AWEB_FRAME_RESIZE,(STRPTR)MSG_AWEB_FRAME_RESIZE_STR},
    {MSG_AWEB_LOGIN,(STRPTR)MSG_AWEB_LOGIN_STR},
    {MSG_AWEB_NEWSGROUP,(STRPTR)MSG_AWEB_NEWSGROUP_STR},
    {MSG_AWEB_NEWSSCAN,(STRPTR)MSG_AWEB_NEWSSCAN_STR},
    {MSG_AWEB_NEWSSORT,(STRPTR)MSG_AWEB_NEWSSORT_STR},
    {MSG_AWEB_NEWSPOST,(STRPTR)MSG_AWEB_NEWSPOST_STR},
    {MSG_AWEB_MAILSEND,(STRPTR)MSG_AWEB_MAILSEND_STR},
    {MSG_AWEB_UPLOAD,(STRPTR)MSG_AWEB_UPLOAD_STR},
};

#endif /* CATCOMP_ARRAY */


/****************************************************************************/


#ifdef CATCOMP_BLOCK

static const char CatCompBlock[] =
{
    "\x00\x00\x00\x01\x00\x0E"
    MSG_REQUEST_TITLE_STR "\x00\x00"
    "\x00\x00\x00\x64\x00\x0C"
    MSG_ABOUT_TITLE_STR "\x00\x00"
    "\x00\x00\x00\x65\x00\x10"
    MSG_ABOUT_REGNR_STR "\x00\x00"
    "\x00\x00\x00\x66\x00\x0E"
    MSG_ABOUT_REGTO_STR "\x00"
    "\x00\x00\x00\x67\x00\x14"
    MSG_ABOUT_SCREENNAME_STR "\x00\x00"
    "\x00\x00\x00\x68\x00\x10"
    MSG_ABOUT_PORTNAME_STR "\x00"
    "\x00\x00\x00\x69\x00\x08"
    MSG_ABOUT_EMAIL_STR "\x00\x00"
    "\x00\x00\x00\x6A\x00\x04"
    MSG_ABOUT_WWW_STR "\x00"
    "\x00\x00\x00\x6B\x00\x04"
    MSG_ABOUT_OK_STR "\x00"
    "\x00\x00\x00\x6C\x00\x16"
    MSG_ABOUT_UNREGISTERED_STR "\x00"
    "\x00\x00\x00\x6D\x00\x1C"
    MSG_ABOUT_DEMO_STR "\x00\x00"
    "\x00\x00\x00\x6E\x00\x12"
    MSG_ABOUT_DISTRIBUTED_STR "\x00"
    "\x00\x00\x00\x6F\x00\x02"
    MSG_ABOUT_TRANSLATOR_STR "\x00\x00"
    "\x00\x00\x00\x70\x00\x30"
    MSG_ABOUT_SPECIAL_STR "\x00"
    "\x00\x00\x00\xC8\x00\x24"
    MSG_QUIT_TEXT_STR "\x00"
    "\x00\x00\x00\xC9\x00\x04"
    MSG_QUIT_OK_STR "\x00"
    "\x00\x00\x00\xCA\x00\x08"
    MSG_QUIT_CANCEL_STR "\x00"
    "\x00\x00\x00\xCB\x00\x26"
    MSG_QUIT_WARNING_STR "\x00"
    "\x00\x00\x01\x2C\x00\x4E"
    MSG_DEMO_TEXT_STR "\x00\x00"
    "\x00\x00\x01\x2D\x00\x04"
    MSG_DEMO_OK_STR "\x00"
    "\x00\x00\x01\x90\x00\x38"
    MSG_SCRCLOSE_TEXT_STR "\x00\x00"
    "\x00\x00\x01\x91\x00\x04"
    MSG_SCRCLOSE_OK_STR "\x00\x00"
    "\x00\x00\x01\xF4\x00\x10"
    MSG_NWS_TITLE_STR "\x00\x00"
    "\x00\x00\x01\xF5\x00\x08"
    MSG_NWS_QUEUED_STR "\x00\x00"
    "\x00\x00\x01\xF6\x00\x08"
    MSG_NWS_STARTED_STR "\x00"
    "\x00\x00\x01\xF7\x00\x0C"
    MSG_NWS_LOOKUP_STR "\x00\x00"
    "\x00\x00\x01\xF8\x00\x0C"
    MSG_NWS_CONNECT_STR "\x00\x00"
    "\x00\x00\x01\xF9\x00\x08"
    MSG_NWS_WAIT_STR "\x00"
    "\x00\x00\x01\xFA\x00\x0C"
    MSG_NWS_PROCESS_STR "\x00\x00"
    "\x00\x00\x01\xFB\x00\x0C"
    MSG_NWS_LOGIN_STR "\x00\x00"
    "\x00\x00\x01\xFC\x00\x08"
    MSG_NWS_CPS_STR "\x00"
    "\x00\x00\x01\xFD\x00\x0A"
    MSG_NWS_NEWSGROUP_STR "\x00\x00"
    "\x00\x00\x01\xFE\x00\x0A"
    MSG_NWS_UPLOAD_STR "\x00"
    "\x00\x00\x02\x58\x00\x0E"
    MSG_AUTH_TITLE_STR "\x00"
    "\x00\x00\x02\x59\x00\x22"
    MSG_AUTH_PROMPT_STR "\x00\x00"
    "\x00\x00\x02\x5A\x00\x08"
    MSG_AUTH_USERID_STR "\x00"
    "\x00\x00\x02\x5B\x00\x0A"
    MSG_AUTH_PASSWORD_STR "\x00"
    "\x00\x00\x02\x5C\x00\x04"
    MSG_AUTH_OK_STR "\x00"
    "\x00\x00\x02\x5D\x00\x08"
    MSG_AUTH_CANCEL_STR "\x00"
    "\x00\x00\x02\x5E\x00\x16"
    MSG_AUTH_PROMPT_FTP_STR "\x00"
    "\x00\x00\x02\x5F\x00\x08"
    MSG_AUTH_USERID_FTP_STR "\x00\x00"
    "\x00\x00\x02\x60\x00\x28"
    MSG_AUTH_PROMPT_PROXY_STR "\x00\x00"
    "\x00\x00\x02\xBC\x00\x16"
    MSG_FILE_SAVETITLE_STR "\x00\x00"
    "\x00\x00\x02\xBD\x00\x06"
    MSG_FILE_SAVE_STR "\x00\x00"
    "\x00\x00\x02\xBE\x00\x62"
    MSG_FILE_INCTEXT_STR "\x00\x00"
    "\x00\x00\x02\xBF\x00\x02"
    MSG_DUMMY_1_STR "\x00\x00"
    "\x00\x00\x02\xC0\x00\x1A"
    MSG_FILE_EXISTTEXT_STR "\x00\x00"
    "\x00\x00\x02\xC1\x00\x26"
    MSG_FILE_EXISTBUTTONS_STR "\x00\x00"
    "\x00\x00\x02\xC2\x00\x14"
    MSG_FILE_AREXXTITLE_STR "\x00\x00"
    "\x00\x00\x02\xC3\x00\x1C"
    MSG_FILE_HOTLISTTITLE_STR "\x00"
    "\x00\x00\x02\xC4\x00\x10"
    MSG_FILE_LOCALTITLE_STR "\x00"
    "\x00\x00\x02\xC5\x00\x06"
    MSG_FILE_OPEN_STR "\x00\x00"
    "\x00\x00\x02\xC6\x00\x2E"
    MSG_FILE_TEMPTITLE_STR "\x00"
    "\x00\x00\x02\xC7\x00\x04"
    MSG_FILE_TEMPBUTTONS_STR "\x00"
    "\x00\x00\x02\xC8\x00\x14"
    MSG_FILE_INCBUTTONS2_STR "\x00\x00"
    "\x00\x00\x02\xC9\x00\x1A"
    MSG_FILE_SETTINGSTITLE_STR "\x00"
    "\x00\x00\x02\xCA\x00\x16"
    MSG_FILE_UPLOADTITLE_STR "\x00"
    "\x00\x00\x03\x20\x00\x3E"
    MSG_TCP_ENDTCP_STR "\x00"
    "\x00\x00\x03\x21\x00\x0A"
    MSG_TCP_BUTTONS_STR "\x00\x00"
    "\x00\x00\x03\x84\x00\x0E"
    MSG_STARTUP_FONTS_STR "\x00"
    "\x00\x00\x03\x85\x00\x14"
    MSG_STARTUP_IMAGES_STR "\x00\x00"
    "\x00\x00\x03\x86\x00\x14"
    MSG_STARTUP_CACHE_STR "\x00"
    "\x00\x00\x03\xE8\x00\x0E"
    MSG_HOTL_TITLE_STR "\x00\x00"
    "\x00\x00\x03\xE9\x00\x0A"
    MSG_HOTL_ADDLINK_STR "\x00"
    "\x00\x00\x03\xEA\x00\x0C"
    MSG_HOTL_ADDGROUP_STR "\x00\x00"
    "\x00\x00\x03\xEB\x00\x08"
    MSG_HOTL_REMOVE_STR "\x00\x00"
    "\x00\x00\x03\xEC\x00\x08"
    MSG_HOTL_FOLLOW_STR "\x00"
    "\x00\x00\x03\xED\x00\x06"
    MSG_HOTL_NAME_STR "\x00"
    "\x00\x00\x03\xEE\x00\x06"
    MSG_HOTL_URL_STR "\x00\x00"
    "\x00\x00\x03\xEF\x00\x0A"
    MSG_HOTL_NEWGROUP_STR "\x00"
    "\x00\x00\x03\xF0\x00\x0A"
    MSG_HOTL_NEWLINK_STR "\x00\x00"
    "\x00\x00\x03\xF1\x00\x14"
    MSG_HOTL_TITLE_VIEWER_STR "\x00"
    "\x00\x00\x03\xF2\x00\x06"
    MSG_HOTL_VURLS_STR "\x00"
    "\x00\x00\x03\xF3\x00\x0A"
    MSG_HOTL_MANAGER_STR "\x00\x00"
    "\x00\x00\x03\xF4\x00\x16"
    MSG_HOTL_TITLE_MANAGER_STR "\x00\x00"
    "\x00\x00\x03\xF5\x00\x06"
    MSG_HOTL_LTYPE_STR "\x00"
    "\x00\x00\x03\xF6\x00\x06"
    MSG_HOTL_LTYPE_REST_STR "\x00\x00"
    "\x00\x00\x03\xF7\x00\x04"
    MSG_HOTL_LTYPE_ALL_STR "\x00"
    "\x00\x00\x03\xF8\x00\x08"
    MSG_HOTL_LTYPE_GROUPS_STR "\x00\x00"
    "\x00\x00\x03\xF9\x00\x06"
    MSG_HOTL_LSORT_STR "\x00"
    "\x00\x00\x03\xFA\x00\x06"
    MSG_HOTL_LSORT_TITLE_STR "\x00"
    "\x00\x00\x03\xFB\x00\x04"
    MSG_HOTL_LSORT_URL_STR "\x00"
    "\x00\x00\x03\xFC\x00\x06"
    MSG_HOTL_MOVEIN_STR "\x00"
    "\x00\x00\x03\xFD\x00\x08"
    MSG_HOTL_MOVEOUT_STR "\x00\x00"
    "\x00\x00\x03\xFE\x00\x08"
    MSG_HOTL_MOVE_STR "\x00"
    "\x00\x00\x03\xFF\x00\x08"
    MSG_HOTL_DEL_STR "\x00"
    "\x00\x00\x04\x00\x00\x08"
    MSG_HOTL_WHERE_STR "\x00\x00"
    "\x00\x00\x04\x01\x00\x06"
    MSG_HOTL_SHOW_STR "\x00"
    "\x00\x00\x04\x02\x00\x06"
    MSG_HOTL_SORT_STR "\x00"
    "\x00\x00\x04\x03\x00\x08"
    MSG_HOTL_PARENT_STR "\x00"
    "\x00\x00\x04\x04\x00\x08"
    MSG_HOTL_ALL_STR "\x00"
    "\x00\x00\x04\x05\x00\x0A"
    MSG_HOTL_ADDENTRY_STR "\x00\x00"
    "\x00\x00\x04\x06\x00\x0A"
    MSG_HOTL_ADDGROUP2_STR "\x00\x00"
    "\x00\x00\x04\x07\x00\x06"
    MSG_HOTL_URLS_STR "\x00"
    "\x00\x00\x04\x08\x00\x08"
    MSG_HOTL_REVERT_STR "\x00"
    "\x00\x00\x04\x09\x00\x06"
    MSG_HOTL_SAVE_STR "\x00"
    "\x00\x00\x04\x0A\x00\x06"
    MSG_HOTL_LADD_STR "\x00\x00"
    "\x00\x00\x04\x0B\x00\x06"
    MSG_HOTL_FIND_STR "\x00"
    "\x00\x00\x04\x0C\x00\x08"
    MSG_HOTL_PATTERN_STR "\x00\x00"
    "\x00\x00\x04\x0D\x00\x06"
    MSG_HOTL_NEXT_STR "\x00"
    "\x00\x00\x04\x0E\x00\x10"
    MSG_HOTL_DEFAULT_TITLE_STR "\x00\x00"
    "\x00\x00\x04\x0F\x00\x10"
    MSG_HOTL_DEFAULT_GROUP_STR "\x00"
    "\x00\x00\x04\x10\x00\x0C"
    MSG_HOTL_TITLE_ALLGROUPS_STR "\x00\x00"
    "\x00\x00\x04\x11\x00\x1E"
    MSG_HOTL_TITLE_WHERE_STR "\x00\x00"
    "\x00\x00\x04\x12\x00\x14"
    MSG_HOTL_TITLE_REST_STR "\x00\x00"
    "\x00\x00\x04\x13\x00\x0C"
    MSG_HOTL_TITLE_ALL_STR "\x00"
    "\x00\x00\x04\x14\x00\x12"
    MSG_HOTL_TITLE_ROOTGROUPS_STR "\x00"
    "\x00\x00\x04\x15\x00\x06"
    MSG_HOTL_LSORT_DATE_STR "\x00\x00"
    "\x00\x00\x04\x4C\x00\x10"
    MSG_WHIS_TITLE_STR "\x00\x00"
    "\x00\x00\x04\x4D\x00\x08"
    MSG_WHIS_FILTER_STR "\x00"
    "\x00\x00\x04\x4E\x00\x08"
    MSG_WHIS_WINDOW_STR "\x00"
    "\x00\x00\x04\x4F\x00\x08"
    MSG_WHIS_ORDER_STR "\x00\x00"
    "\x00\x00\x04\x50\x00\x0A"
    MSG_WHIS_DISPLAY_STR "\x00\x00"
    "\x00\x00\x04\x51\x00\x08"
    MSG_WHIS_ORDER_NATURAL_STR "\x00"
    "\x00\x00\x04\x52\x00\x0A"
    MSG_WHIS_ORDER_MAINLINE_STR "\x00\x00"
    "\x00\x00\x04\x53\x00\x0A"
    MSG_WHIS_ORDER_RETRIEVED_STR "\x00"
    "\x00\x00\x04\x54\x00\x06"
    MSG_WHIS_ORDER_TITLE_STR "\x00"
    "\x00\x00\x04\x55\x00\x04"
    MSG_WHIS_ORDER_URL_STR "\x00"
    "\x00\x00\x04\xB0\x00\x0E"
    MSG_CABR_TITLE_STR "\x00"
    "\x00\x00\x04\xB1\x00\x04"
    MSG_CABR_URL_STR "\x00"
    "\x00\x00\x04\xB2\x00\x06"
    MSG_CABR_DATE_STR "\x00\x00"
    "\x00\x00\x04\xB3\x00\x06"
    MSG_CABR_SIZE_STR "\x00\x00"
    "\x00\x00\x04\xB4\x00\x06"
    MSG_CABR_TYPE_STR "\x00\x00"
    "\x00\x00\x04\xB5\x00\x06"
    MSG_CABR_FILE_STR "\x00\x00"
    "\x00\x00\x04\xB6\x00\x12"
    MSG_CABR_STATUS_STR "\x00"
    "\x00\x00\x04\xB7\x00\x0A"
    MSG_CABR_SORTBY_STR "\x00\x00"
    "\x00\x00\x04\xB8\x00\x06"
    MSG_CABR_OPEN_STR "\x00"
    "\x00\x00\x04\xB9\x00\x06"
    MSG_CABR_SAVE_STR "\x00"
    "\x00\x00\x04\xBA\x00\x08"
    MSG_CABR_DELETE_STR "\x00"
    "\x00\x00\x04\xBB\x00\x06"
    MSG_CABR_FIND_STR "\x00"
    "\x00\x00\x04\xBC\x00\x08"
    MSG_CABR_PATTERN_STR "\x00\x00"
    "\x00\x00\x04\xBD\x00\x06"
    MSG_CABR_NEXT_STR "\x00"
    "\x00\x00\x05\x14\x00\x0E"
    MSG_PROGRESS_TITLE_STR "\x00"
    "\x00\x00\x05\x15\x00\x08"
    MSG_PROGRESS_CANCEL_STR "\x00\x00"
    "\x00\x00\x05\x78\x00\x48"
    MSG_FIXCACHE_WARNING_STR "\x00"
    "\x00\x00\x05\x79\x00\x0C"
    MSG_FIXCACHE_BUTTONS_STR "\x00"
    "\x00\x00\x05\x7A\x00\x1A"
    MSG_FIXCACHE_PROGRESS_STR "\x00\x00"
    "\x00\x00\x05\x7B\x00\x3C"
    MSG_FIXCACHE_DELETEIMAGES_STR "\x00\x00"
    "\x00\x00\x05\x7C\x00\x3E"
    MSG_FIXCACHE_DELETEDOCS_STR "\x00"
    "\x00\x00\x05\x7D\x00\x4A"
    MSG_FIXCACHE_ERASE_STR "\x00\x00"
    "\x00\x00\x05\x7E\x00\x8A"
    MSG_FIXCACHE_CORRUPT_STR "\x00"
    "\x00\x00\x05\xDC\x00\x0C"
    MSG_SEARCH_STRING_STR "\x00"
    "\x00\x00\x05\xDD\x00\x0E"
    MSG_SEARCH_IGNORECASE_STR "\x00\x00"
    "\x00\x00\x05\xDE\x00\x08"
    MSG_SEARCH_SEARCH_STR "\x00"
    "\x00\x00\x05\xDF\x00\x0A"
    MSG_SEARCH_FROMTOP_STR "\x00"
    "\x00\x00\x05\xE0\x00\x0C"
    MSG_SEARCH_BACKWARDS_STR "\x00\x00"
    "\x00\x00\x05\xE1\x00\x08"
    MSG_SEARCH_CANCEL_STR "\x00"
    "\x00\x00\x05\xE2\x00\x12"
    MSG_SEARCH_NOTFOUND_STR "\x00\x00"
    "\x00\x00\x05\xE3\x00\x04"
    MSG_SEARCH_OK_STR "\x00"
    "\x00\x00\x06\x40\x00\x0C"
    MSG_POPUP_LOADIMAGE_STR "\x00\x00"
    "\x00\x00\x06\x41\x00\x0E"
    MSG_POPUP_RELOADIMAGE_STR "\x00\x00"
    "\x00\x00\x06\x42\x00\x0C"
    MSG_POPUP_SAVEIMAGE_STR "\x00\x00"
    "\x00\x00\x06\x43\x00\x10"
    MSG_POPUP_DOWNLOADIMAGE_STR "\x00\x00"
    "\x00\x00\x06\x44\x00\x0C"
    MSG_POPUP_FLUSHIMAGE_STR "\x00"
    "\x00\x00\x06\x45\x00\x0C"
    MSG_POPUP_SHOWIMAGE_STR "\x00\x00"
    "\x00\x00\x06\x46\x00\x0A"
    MSG_POPUP_OPENLINK_STR "\x00"
    "\x00\x00\x06\x47\x00\x18"
    MSG_POPUP_OPENLINKNW_STR "\x00"
    "\x00\x00\x06\x48\x00\x18"
    MSG_POPUP_LOADLINK_STR "\x00"
    "\x00\x00\x06\x49\x00\x0A"
    MSG_POPUP_SAVELINK_STR "\x00"
    "\x00\x00\x06\x4A\x00\x0E"
    MSG_POPUP_DOWNLOADLINK_STR "\x00"
    "\x00\x00\x06\x4B\x00\x14"
    MSG_POPUP_ADDLINK_STR "\x00"
    "\x00\x00\x06\x4C\x00\x10"
    MSG_POPUP_RELOADFRAME_STR "\x00"
    "\x00\x00\x06\x4D\x00\x0C"
    MSG_POPUP_SAVEFRAME_STR "\x00"
    "\x00\x00\x06\x4E\x00\x0C"
    MSG_POPUP_VIEWFRAME_STR "\x00"
    "\x00\x00\x06\x4F\x00\x10"
    MSG_POPUP_SHOWFRAME_STR "\x00"
    "\x00\x00\x06\x50\x00\x14"
    MSG_POPUP_SEARCHFRAME_STR "\x00\x00"
    "\x00\x00\x06\x51\x00\x06"
    MSG_POPUP_INFOFRAME_STR "\x00\x00"
    "\x00\x00\x06\xA4\x00\x12"
    MSG_PRINTP_TITLE_STR "\x00\x00"
    "\x00\x00\x06\xA5\x00\x12"
    MSG_PRINTP_SCALE_STR "\x00"
    "\x00\x00\x06\xA6\x00\x14"
    MSG_PRINTP_PRINTBG_STR "\x00\x00"
    "\x00\x00\x06\xA7\x00\x0A"
    MSG_PRINTP_FORMFEED_STR "\x00"
    "\x00\x00\x06\xA8\x00\x08"
    MSG_PRINTP_CENTER_STR "\x00"
    "\x00\x00\x06\xA9\x00\x08"
    MSG_PRINTP_PRINT_STR "\x00\x00"
    "\x00\x00\x06\xAA\x00\x08"
    MSG_PRINTP_CANCEL_STR "\x00"
    "\x00\x00\x06\xAB\x00\x1E"
    MSG_PRINT_NOPRINTER_STR "\x00\x00"
    "\x00\x00\x06\xAC\x00\x04"
    MSG_PRINT_OK_STR "\x00"
    "\x00\x00\x06\xAD\x00\x18"
    MSG_PRINT_PROGRESS_STR "\x00"
    "\x00\x00\x06\xAE\x00\x0E"
    MSG_PRINTP_LAYOUTWIDTH_STR "\x00"
    "\x00\x00\x06\xAF\x00\x08"
    MSG_PRINTP_LW_WINDOW_STR "\x00\x00"
    "\x00\x00\x06\xB0\x00\x0A"
    MSG_PRINTP_LW_DOCUMENT_STR "\x00\x00"
    "\x00\x00\x06\xB1\x00\x08"
    MSG_PRINTP_LW_SCREEN_STR "\x00\x00"
    "\x00\x00\x07\x08\x00\x0E"
    MSG_COOKIE_TITLE_STR "\x00\x00"
    "\x00\x00\x07\x09\x00\x18"
    MSG_COOKIE_WARNING_STR "\x00\x00"
    "\x00\x00\x07\x0A\x00\x0E"
    MSG_COOKIE_NAME_STR "\x00\x00"
    "\x00\x00\x07\x0B\x00\x0E"
    MSG_COOKIE_VALUE_STR "\x00"
    "\x00\x00\x07\x0C\x00\x0C"
    MSG_COOKIE_DOMAIN_STR "\x00"
    "\x00\x00\x07\x0D\x00\x06"
    MSG_COOKIE_PATH_STR "\x00"
    "\x00\x00\x07\x0E\x00\x0A"
    MSG_COOKIE_COMMENT_STR "\x00\x00"
    "\x00\x00\x07\x0F\x00\x0A"
    MSG_COOKIE_MAXAGE_STR "\x00\x00"
    "\x00\x00\x07\x10\x00\x0A"
    MSG_COOKIE_EXPIRES_STR "\x00\x00"
    "\x00\x00\x07\x11\x00\x06"
    MSG_COOKIE_ONCE_STR "\x00"
    "\x00\x00\x07\x12\x00\x08"
    MSG_COOKIE_ACCEPT_STR "\x00"
    "\x00\x00\x07\x13\x00\x08"
    MSG_COOKIE_NEVER_STR "\x00\x00"
    "\x00\x00\x07\x14\x00\x08"
    MSG_COOKIE_CANCEL_STR "\x00"
    "\x00\x00\x07\x15\x00\x3E"
    MSG_COOKIE_SECURE_STR "\x00"
    "\x00\x00\x07\x16\x00\x40"
    MSG_COOKIE_UNSECURE_STR "\x00\x00"
    "\x00\x00\x07\x6C\x00\x0C"
    MSG_INFO_TITLE_STR "\x00"
    "\x00\x00\x07\x6D\x00\x16"
    MSG_INFO_FROMCACHE_STR "\x00\x00"
    "\x00\x00\x07\x6E\x00\x12"
    MSG_INFO_XFER_STR "\x00"
    "\x00\x00\x07\x6F\x00\x12"
    MSG_INFO_META_STR "\x00"
    "\x00\x00\x07\x70\x00\x12"
    MSG_INFO_LINK_STR "\x00\x00"
    "\x00\x00\x07\x71\x00\x18"
    MSG_INFO_CIPHER_STR "\x00"
    "\x00\x00\x07\x72\x00\x16"
    MSG_INFO_SSLLIBRARY_STR "\x00\x00"
    "\x00\x00\x07\xD0\x00\x06"
    MSG_USERBUTTON_CACHE_STR "\x00"
    "\x00\x00\x07\xD1\x00\x06"
    MSG_USERBUTTON_CLOCK_STR "\x00"
    "\x00\x00\x08\x34\x00\x14"
    MSG_AUTHEDIT_TITLE_STR "\x00"
    "\x00\x00\x08\x35\x00\x08"
    MSG_AUTHEDIT_TITLE_SERVER_STR "\x00\x00"
    "\x00\x00\x08\x36\x00\x08"
    MSG_AUTHEDIT_TITLE_USERID_STR "\x00\x00"
    "\x00\x00\x08\x37\x00\x06"
    MSG_AUTHEDIT_DEL_STR "\x00\x00"
    "\x00\x00\x08\x38\x00\x08"
    MSG_AUTHEDIT_SERVER_STR "\x00\x00"
    "\x00\x00\x08\x39\x00\x08"
    MSG_AUTHEDIT_USERID_STR "\x00"
    "\x00\x00\x08\x3A\x00\x0A"
    MSG_AUTHEDIT_PASSWORD_STR "\x00"
    "\x00\x00\x08\x98\x00\x04"
    MSG_JSPROMPT_OK_STR "\x00"
    "\x00\x00\x08\x99\x00\x08"
    MSG_JSPROMPT_CANCEL_STR "\x00"
    "\x00\x00\x08\xFC\x00\x0E"
    MSG_FORMWARN_TITLE_STR "\x00\x00"
    "\x00\x00\x08\xFD\x00\x3E"
    MSG_FORMWARN_WARNING_STR "\x00"
    "\x00\x00\x08\xFE\x00\x0C"
    MSG_FORMWARN_BUTTONS_STR "\x00"
    "\x00\x00\x09\x60\x00\x12"
    MSG_MAIL_TITLE_STR "\x00"
    "\x00\x00\x09\x61\x00\x48"
    MSG_MAIL_NOSMTPHOST_STR "\x00\x00"
    "\x00\x00\x09\x62\x00\x16"
    MSG_MAIL_NOCONNECT_STR "\x00\x00"
    "\x00\x00\x09\x63\x00\x1E"
    MSG_MAIL_MAILFAILED_STR "\x00\x00"
    "\x00\x00\x09\x64\x00\x16"
    MSG_MAIL_BUTTONS_STR "\x00\x00"
    "\x00\x00\x09\x65\x00\x14"
    MSG_MAIL_SAVETITLE_STR "\x00"
    "\x00\x00\x09\x66\x00\x0C"
    MSG_MAIL_MAILTO_TITLE_STR "\x00\x00"
    "\x00\x00\x09\x67\x00\x0C"
    MSG_MAIL_MAILTO_HEADER_STR "\x00"
    "\x00\x00\x09\x68\x00\x04"
    MSG_MAIL_TO_STR "\x00"
    "\x00\x00\x09\x69\x00\x0A"
    MSG_MAIL_SUBJECT_STR "\x00\x00"
    "\x00\x00\x09\x6A\x00\x0E"
    MSG_MAIL_MESSAGE_BODY_STR "\x00"
    "\x00\x00\x09\x6B\x00\x1C"
    MSG_MAIL_EXTRA_HEADERS_STR "\x00"
    "\x00\x00\x09\x6C\x00\x0A"
    MSG_MAIL_SEND_STR "\x00"
    "\x00\x00\x09\x6D\x00\x06"
    MSG_MAIL_RESET_STR "\x00"
    "\x00\x00\x09\x6E\x00\x08"
    MSG_MAIL_RETURN_STR "\x00\x00"
    "\x00\x00\x09\x6F\x00\x12"
    MSG_MAIL_MAIL_SENT_TITLE_STR "\x00\x00"
    "\x00\x00\x09\x70\x00\x4E"
    MSG_MAIL_MAIL_SENT_STR "\x00\x00"
    "\x00\x00\x09\xC4\x00\x16"
    MSG_NEWS_TITLE_STR "\x00"
    "\x00\x00\x09\xC5\x00\x48"
    MSG_NEWS_NONNTPHOST_STR "\x00\x00"
    "\x00\x00\x09\xC6\x00\x16"
    MSG_NEWS_NOCONNECT_STR "\x00\x00"
    "\x00\x00\x09\xC7\x00\x18"
    MSG_NEWS_POSTFAILED_STR "\x00"
    "\x00\x00\x09\xC8\x00\x16"
    MSG_NEWS_BUTTONS_STR "\x00\x00"
    "\x00\x00\x09\xC9\x00\x14"
    MSG_NEWS_SAVETITLE_STR "\x00"
    "\x00\x00\x09\xCA\x00\x06"
    MSG_NEWS_NEWS_HEADER_STR "\x00\x00"
    "\x00\x00\x09\xCB\x00\x16"
    MSG_NEWS_SUBSCRIBED_STR "\x00"
    "\x00\x00\x09\xCC\x00\x12"
    MSG_NEWS_NEW_ARTICLES_STR "\x00"
    "\x00\x00\x09\xCD\x00\x0A"
    MSG_NEWS_CATCH_UP_STR "\x00\x00"
    "\x00\x00\x09\xCE\x00\x0C"
    MSG_NEWS_UNSUBSCRIBE_STR "\x00"
    "\x00\x00\x09\xCF\x00\x0C"
    MSG_NEWS_OTHER_GROUP_STR "\x00"
    "\x00\x00\x09\xD0\x00\x0A"
    MSG_NEWS_SUBSCRIBE_STR "\x00"
    "\x00\x00\x09\xD1\x00\x06"
    MSG_NEWS_READ_STR "\x00\x00"
    "\x00\x00\x09\xD2\x00\x12"
    MSG_NEWS_SEARCH_FOR_GROUP_STR "\x00\x00"
    "\x00\x00\x09\xD3\x00\x08"
    MSG_NEWS_SEARCH_STR "\x00\x00"
    "\x00\x00\x09\xD4\x00\x84"
    MSG_NEWS_LONG_DOWNLOAD_STR "\x00"
    "\x00\x00\x09\xD5\x00\x0E"
    MSG_NEWS_NO_SUBJECT_STR "\x00\x00"
    "\x00\x00\x09\xD6\x00\x0C"
    MSG_NEWS_LINES_STR "\x00\x00"
    "\x00\x00\x09\xD7\x00\x12"
    MSG_NEWS_POST_NEW_ARTICLE_STR "\x00\x00"
    "\x00\x00\x09\xD8\x00\x0C"
    MSG_NEWS_GROUP_LIST_STR "\x00\x00"
    "\x00\x00\x09\xD9\x00\x28"
    MSG_NEWS_ERROR_NO_GROUP_STR "\x00"
    "\x00\x00\x09\xDA\x00\x22"
    MSG_NEWS_USE_FRAMES_STR "\x00\x00"
    "\x00\x00\x09\xDB\x00\x1E"
    MSG_NEWS_ERROR_NO_ARTICLE_STR "\x00"
    "\x00\x00\x09\xDC\x00\x0A"
    MSG_NEWS_FOLLOW_UP_STR "\x00"
    "\x00\x00\x09\xDD\x00\x10"
    MSG_NEWS_REPLY_STR "\x00"
    "\x00\x00\x09\xDE\x00\x0E"
    MSG_NEWS_SAVE_STR "\x00\x00"
    "\x00\x00\x09\xDF\x00\x0A"
    MSG_NEWS_SUBJECT_STR "\x00\x00"
    "\x00\x00\x09\xE0\x00\x0C"
    MSG_NEWS_NEWSGROUPS_STR "\x00"
    "\x00\x00\x09\xE1\x00\x0E"
    MSG_NEWS_ARTICLE_BODY_STR "\x00"
    "\x00\x00\x09\xE2\x00\x1C"
    MSG_NEWS_EXTRA_HEADERS_STR "\x00"
    "\x00\x00\x09\xE3\x00\x0E"
    MSG_NEWS_POST_ARTICLE_STR "\x00\x00"
    "\x00\x00\x09\xE4\x00\x06"
    MSG_NEWS_RESET_STR "\x00"
    "\x00\x00\x09\xE5\x00\x08"
    MSG_NEWS_RETURN_STR "\x00\x00"
    "\x00\x00\x09\xE6\x00\x16"
    MSG_NEWS_FOLLOWUP_TITLE_STR "\x00\x00"
    "\x00\x00\x09\xE7\x00\x0E"
    MSG_NEWS_FOLLOWUP_TO_GROUPS_STR "\x00"
    "\x00\x00\x09\xE8\x00\x28"
    MSG_NEWS_PLEASE_DELETE_STR "\x00\x00"
    "\x00\x00\x09\xE9\x00\x12"
    MSG_NEWS_REPLY_TITLE_STR "\x00\x00"
    "\x00\x00\x09\xEA\x00\x0A"
    MSG_NEWS_REPLY_TO_ADDRESS_STR "\x00"
    "\x00\x00\x09\xEB\x00\x10"
    MSG_NEWS_ARTICLE_POSTED_TITLE_STR "\x00\x00"
    "\x00\x00\x09\xEC\x00\x36"
    MSG_NEWS_ARTICLE_POSTED_STR "\x00"
    "\x00\x00\x09\xED\x00\x18"
    MSG_NEWS_DOWNLOAD_TITLE_STR "\x00"
    "\x00\x00\x09\xEE\x00\x54"
    MSG_NEWS_DOWNLOAD_BODY_STR "\x00\x00"
    "\x00\x00\x09\xEF\x00\x0E"
    MSG_NEWS_SEARCH_RESULT_STR "\x00"
    "\x00\x00\x09\xF0\x00\x34"
    MSG_NEWS_FIRST_MATCHING_STR "\x00"
    "\x00\x00\x09\xF1\x00\x2E"
    MSG_NEWS_FOUND_MATCHING_STR "\x00\x00"
    "\x00\x00\x0A\x28\x00\x0E"
    MSG_CMDWARN_TITLE_STR "\x00\x00"
    "\x00\x00\x0A\x29\x00\x24"
    MSG_CMDWARN_SHELL_STR "\x00\x00"
    "\x00\x00\x0A\x2A\x00\x22"
    MSG_CMDWARN_AREXX_STR "\x00\x00"
    "\x00\x00\x0A\x2B\x00\x0C"
    MSG_CMDWARN_BUTTONS_STR "\x00"
    "\x00\x00\x0A\x8C\x00\x0E"
    MSG_SSLWARN_SSL_TITLE_STR "\x00\x00"
    "\x00\x00\x0A\x8D\x00\x4E"
    MSG_SSLWARN_SSL_TEXT_STR "\x00"
    "\x00\x00\x0A\x8E\x00\x12"
    MSG_SSLWARN_SSL_BUTTONS_STR "\x00"
    "\x00\x00\x0A\x8F\x00\x4E"
    MSG_SSLWARN_SSL_NO_SSL_STR "\x00\x00"
    "\x00\x00\x0A\x90\x00\x0E"
    MSG_SSLWARN_CERT_TITLE_STR "\x00\x00"
    "\x00\x00\x0A\x91\x00\xF2"
    MSG_SSLWARN_CERT_TEXT_STR "\x00"
    "\x00\x00\x0A\x92\x00\x0C"
    MSG_SSLWARN_CERT_BUTTONS_STR "\x00"
    "\x00\x00\x0A\x93\x00\x3A"
    MSG_SSLWARN_SSL_NO_SSL2_STR "\x00\x00"
    "\x00\x00\x0A\xF0\x00\x16"
    MSG_UNKMIME_TITLE_STR "\x00\x00"
    "\x00\x00\x0A\xF1\x00\xE8"
    MSG_UNKMIME_TEXT_STR "\x00"
    "\x00\x00\x0A\xF2\x00\x0E"
    MSG_UNKMIME_BUTTONS_STR "\x00"
    "\x00\x00\x0A\xF3\x00\x18"
    MSG_REQUEST_COPYURL_STR "\x00\x00"
    "\x00\x00\x0B\x54\x00\x0E"
    MSG_OPENURL_TITLE_STR "\x00\x00"
    "\x00\x00\x0B\x55\x00\x1A"
    MSG_OPENURL_TEXT_STR "\x00\x00"
    "\x00\x00\x0B\x56\x00\x0C"
    MSG_OPENURL_BUTTONS_STR "\x00"
    "\x00\x00\x0B\xB8\x00\x14"
    MSG_SAVEIFF_PROGRESS_STR "\x00\x00"
    "\x00\x00\x27\x10\x00\x08"
    MSG_PROJECT_MENU_STR "\x00"
    "\x00\x00\x27\x11\x00\x0E"
    MSG_PROJECT_NEWWINDOW_STR "\x00\x00"
    "\x00\x00\x27\x12\x00\x10"
    MSG_PROJECT_CLOSEWINDOW_STR "\x00\x00"
    "\x00\x00\x27\x13\x00\x0C"
    MSG_PROJECT_OPENURL_STR "\x00\x00"
    "\x00\x00\x27\x14\x00\x0C"
    MSG_PROJECT_OPENWWW_STR "\x00\x00"
    "\x00\x00\x27\x15\x00\x10"
    MSG_PROJECT_OPENLOCAL_STR "\x00"
    "\x00\x00\x27\x16\x00\x0A"
    MSG_PROJECT_OPENSEARCH_STR "\x00\x00"
    "\x00\x00\x27\x17\x00\x10"
    MSG_PROJECT_SOURCE_STR "\x00\x00"
    "\x00\x00\x27\x18\x00\x12"
    MSG_PROJECT_SAVEHTML_STR "\x00\x00"
    "\x00\x00\x27\x19\x00\x0C"
    MSG_PROJECT_ABOUT_STR "\x00\x00"
    "\x00\x00\x27\x1A\x00\x0A"
    MSG_PROJECT_QUIT_STR "\x00"
    "\x00\x00\x27\x1B\x00\x0C"
    MSG_PROJECT_SEARCH_STR "\x00"
    "\x00\x00\x27\x1C\x00\x0C"
    MSG_PROJECT_PRINT_STR "\x00\x00"
    "\x00\x00\x27\x1D\x00\x10"
    MSG_PROJECT_EDIT_STR "\x00\x00"
    "\x00\x00\x27\x1E\x00\x0A"
    MSG_PROJECT_INFO_STR "\x00"
    "\x00\x00\x27\x1F\x00\x0A"
    MSG_PROJECT_OPENNEWS_STR "\x00"
    "\x00\x00\x27\x20\x00\x0A"
    MSG_PROJECT_ICONIFY_STR "\x00"
    "\x00\x00\x27\x21\x00\x10"
    MSG_PROJECT_SAVEIFF_STR "\x00\x00"
    "\x00\x00\x27\x74\x00\x08"
    MSG_CONTROL_MENU_STR "\x00"
    "\x00\x00\x27\x75\x00\x0C"
    MSG_CONTROL_LOADNOW_STR "\x00"
    "\x00\x00\x27\x76\x00\x0C"
    MSG_CONTROL_LOADNOWALL_STR "\x00\x00"
    "\x00\x00\x27\x77\x00\x0C"
    MSG_CONTROL_LOADNOWMAPS_STR "\x00"
    "\x00\x00\x27\x78\x00\x12"
    MSG_CONTROL_NETWORKSTATUS_STR "\x00"
    "\x00\x00\x27\x79\x00\x02"
    MSG_DUMMY_2_STR "\x00\x00"
    "\x00\x00\x27\x7A\x00\x0E"
    MSG_CONTROL_CANCEL_STR "\x00"
    "\x00\x00\x27\x7B\x00\x0E"
    MSG_CONTROL_NEXTWINDOW_STR "\x00"
    "\x00\x00\x27\x7C\x00\x12"
    MSG_CONTROL_PREVWINDOW_STR "\x00"
    "\x00\x00\x27\x7D\x00\x10"
    MSG_CONTROL_NOPROXY_STR "\x00"
    "\x00\x00\x27\x7E\x00\x08"
    MSG_CONTROL_RLOAD_STR "\x00\x00"
    "\x00\x00\x27\x7F\x00\x14"
    MSG_CONTROL_RLOADDOC_STR "\x00\x00"
    "\x00\x00\x27\x80\x00\x10"
    MSG_CONTROL_RLOADIMGS_STR "\x00\x00"
    "\x00\x00\x27\x81\x00\x1A"
    MSG_CONTROL_PLAYBGSOUND_STR "\x00\x00"
    "\x00\x00\x27\x82\x00\x0A"
    MSG_CONTROL_COPYURL_STR "\x00\x00"
    "\x00\x00\x27\x83\x00\x0A"
    MSG_CONTROL_PASTEURL_STR "\x00"
    "\x00\x00\x27\x84\x00\x0E"
    MSG_CONTROL_COPYBLOCK_STR "\x00\x00"
    "\x00\x00\x27\x85\x00\x0A"
    MSG_CONTROL_DRAGGING_STR "\x00\x00"
    "\x00\x00\x27\x86\x00\x14"
    MSG_CONTROL_BREAKJS_STR "\x00\x00"
    "\x00\x00\x27\x87\x00\x12"
    MSG_CONTROL_DEBUGJS_STR "\x00\x00"
    "\x00\x00\x27\x88\x00\x0E"
    MSG_CONTROL_RESET_STR "\x00\x00"
    "\x00\x00\x27\xD8\x00\x06"
    MSG_CACHE_MENU_STR "\x00"
    "\x00\x00\x27\xD9\x00\x02"
    MSG_DUMMY_3_STR "\x00\x00"
    "\x00\x00\x27\xDA\x00\x02"
    MSG_DUMMY_4_STR "\x00\x00"
    "\x00\x00\x27\xDB\x00\x02"
    MSG_DUMMY_5_STR "\x00\x00"
    "\x00\x00\x27\xDC\x00\x02"
    MSG_DUMMY_6_STR "\x00\x00"
    "\x00\x00\x27\xDD\x00\x14"
    MSG_CACHE_SAVEAUTH_STR "\x00"
    "\x00\x00\x27\xDE\x00\x16"
    MSG_CACHE_FLUSHAUTH_STR "\x00\x00"
    "\x00\x00\x27\xDF\x00\x12"
    MSG_CACHE_BROWSER_STR "\x00\x00"
    "\x00\x00\x27\xE0\x00\x14"
    MSG_CACHE_FLUSH_STR "\x00\x00"
    "\x00\x00\x27\xE1\x00\x14"
    MSG_CACHE_FLSHIMGSND_STR "\x00"
    "\x00\x00\x27\xE2\x00\x0C"
    MSG_CACHE_FLSHIMGS_STR "\x00\x00"
    "\x00\x00\x27\xE3\x00\x18"
    MSG_CACHE_FLSHDOCSND_STR "\x00\x00"
    "\x00\x00\x27\xE4\x00\x12"
    MSG_CACHE_DELETE_STR "\x00"
    "\x00\x00\x27\xE5\x00\x0C"
    MSG_CACHE_DELIMGS_STR "\x00\x00"
    "\x00\x00\x27\xE6\x00\x0E"
    MSG_CACHE_DELDOCS_STR "\x00"
    "\x00\x00\x27\xE7\x00\x12"
    MSG_CACHE_CLEAR_STR "\x00\x00"
    "\x00\x00\x27\xE8\x00\x16"
    MSG_CACHE_FIX_STR "\x00\x00"
    "\x00\x00\x27\xE9\x00\x18"
    MSG_CACHE_EDITAUTH_STR "\x00\x00"
    "\x00\x00\x28\x3C\x00\x0A"
    MSG_NAVIGATE_MENU_STR "\x00\x00"
    "\x00\x00\x28\x3D\x00\x08"
    MSG_NAVIGATE_BACK_STR "\x00\x00"
    "\x00\x00\x28\x3E\x00\x0A"
    MSG_NAVIGATE_FWD_STR "\x00"
    "\x00\x00\x28\x3F\x00\x08"
    MSG_NAVIGATE_HOME_STR "\x00\x00"
    "\x00\x00\x28\x40\x00\x0E"
    MSG_NAVIGATE_HISTORY_STR "\x00\x00"
    "\x00\x00\x28\xA0\x00\x08"
    MSG_HOTLIST_MENU_STR "\x00"
    "\x00\x00\x28\xA1\x00\x12"
    MSG_HOTLIST_ADDHOT_STR "\x00\x00"
    "\x00\x00\x28\xA2\x00\x10"
    MSG_HOTLIST_SHOWHOT_STR "\x00\x00"
    "\x00\x00\x28\xA3\x00\x12"
    MSG_HOTLIST_MAINT_STR "\x00\x00"
    "\x00\x00\x28\xA4\x00\x0E"
    MSG_HOTLIST_SAVE_STR "\x00\x00"
    "\x00\x00\x28\xA5\x00\x14"
    MSG_HOTLIST_RESTORE_STR "\x00\x00"
    "\x00\x00\x28\xA6\x00\x10"
    MSG_HOTLIST_AMHOTRX_STR "\x00"
    "\x00\x00\x28\xA7\x00\x0E"
    MSG_HOTLIST_AMHOT20_STR "\x00"
    "\x00\x00\x28\xA8\x00\x0A"
    MSG_HOTLIST_IBHOT_STR "\x00\x00"
    "\x00\x00\x28\xA9\x00\x12"
    MSG_HOTLIST_VIEW_STR "\x00"
    "\x00\x00\x28\xAA\x00\x14"
    MSG_HOTLIST_MGR_STR "\x00"
    "\x00\x00\x29\x04\x00\x0A"
    MSG_SETTINGS_MENU_STR "\x00\x00"
    "\x00\x00\x29\x05\x00\x0E"
    MSG_SETTINGS_LOADIMG_STR "\x00"
    "\x00\x00\x29\x06\x00\x0C"
    MSG_SETTINGS_LOADIMGALL_STR "\x00\x00"
    "\x00\x00\x29\x07\x00\x0A"
    MSG_SETTINGS_LOADIMGMAPS_STR "\x00"
    "\x00\x00\x29\x08\x00\x04"
    MSG_SETTINGS_LOADIMGOFF_STR "\x00"
    "\x00\x00\x29\x09\x00\x02"
    MSG_DUMMY_7_STR "\x00\x00"
    "\x00\x00\x29\x0A\x00\x02"
    MSG_DUMMY_8_STR "\x00\x00"
    "\x00\x00\x29\x0B\x00\x14"
    MSG_SETTINGS_BROWSER_STR "\x00"
    "\x00\x00\x29\x0C\x00\x14"
    MSG_SETTINGS_PROGRAM_STR "\x00"
    "\x00\x00\x29\x0D\x00\x14"
    MSG_SETTINGS_NETWORK_STR "\x00"
    "\x00\x00\x29\x0E\x00\x16"
    MSG_SETTINGS_CLASSACT_STR "\x00\x00"
    "\x00\x00\x29\x0F\x00\x16"
    MSG_SETTINGS_SAVEALL_STR "\x00"
    "\x00\x00\x29\x10\x00\x12"
    MSG_SETTINGS_SNAPSHOT_STR "\x00\x00"
    "\x00\x00\x29\x11\x00\x0E"
    MSG_SETTINGS_SNAPSHOTALT_STR "\x00"
    "\x00\x00\x29\x12\x00\x12"
    MSG_SETTINGS_BGIMAGES_STR "\x00"
    "\x00\x00\x29\x13\x00\x12"
    MSG_SETTINGS_BGSOUND_STR "\x00\x00"
    "\x00\x00\x29\x14\x00\x14"
    MSG_SETTINGS_SAVEAS_STR "\x00"
    "\x00\x00\x29\x15\x00\x12"
    MSG_SETTINGS_LOAD_STR "\x00\x00"
    "\x00\x00\x29\x16\x00\x10"
    MSG_SETTINGS_GUI_STR "\x00"
    "\x00\x00\x29\x68\x00\x06"
    MSG_HELP_MENU_STR "\x00\x00"
    "\x00\x00\x29\x69\x00\x0E"
    MSG_HELP_HELP_STR "\x00"
    "\x00\x00\x29\x6A\x00\x0A"
    MSG_HELP_AWEBHOME_STR "\x00"
    "\x00\x00\x29\x6B\x00\x22"
    MSG_HELP_AWEBFAQ_STR "\x00\x00"
    "\x00\x00\x29\x6C\x00\x0E"
    MSG_HELP_REGISTER_STR "\x00\x00"
    "\x00\x00\x29\xCC\x00\x06"
    MSG_AREXX_MENU_STR "\x00"
    "\x00\x00\x29\xCD\x00\x16"
    MSG_AREXX_AREXX_STR "\x00\x00"
    "\x00\x00\x4E\x20\x00\x0E"
    MSG_ERROR_CANTOPEN_STR "\x00"
    "\x00\x00\x4E\x21\x00\x1A"
    MSG_ERROR_CANTOPENV_STR "\x00"
    "\x00\x00\x4E\x22\x00\x1C"
    MSG_ERROR_NEEDOS30_STR "\x00"
    "\x00\x00\x4E\x23\x00\x20"
    MSG_ERROR_CANTQUIT_STR "\x00"
    "\x00\x00\x52\x08\x00\x06"
    MSG_EPART_ERROR_STR "\x00"
    "\x00\x00\x52\x09\x00\x16"
    MSG_EPART_RETURL_STR "\x00\x00"
    "\x00\x00\x52\x0A\x00\x50"
    MSG_EPART_ADDRSCHEME_STR "\x00"
    "\x00\x00\x52\x0B\x00\x22"
    MSG_EPART_NOLIB_STR "\x00"
    "\x00\x00\x52\x0C\x00\x28"
    MSG_EPART_NOHOST_STR "\x00"
    "\x00\x00\x52\x0D\x00\x22"
    MSG_EPART_NOCONNECT_STR "\x00\x00"
    "\x00\x00\x52\x0E\x00\x28"
    MSG_EPART_NOCONNECT_TIMEOUT_STR "\x00\x00"
    "\x00\x00\x52\x0F\x00\x38"
    MSG_EPART_NOCONNECT_REFUSED_STR "\x00\x00"
    "\x00\x00\x52\x10\x00\x36"
    MSG_EPART_NOCONNECT_RESET_STR "\x00\x00"
    "\x00\x00\x52\x11\x00\x38"
    MSG_EPART_NOCONNECT_UNREACH_STR "\x00\x00"
    "\x00\x00\x52\x12\x00\x34"
    MSG_EPART_NOCONNECT_HOSTUNREACH_STR "\x00"
    "\x00\x00\x52\x13\x00\x20"
    MSG_EPART_NOFILE_STR "\x00"
    "\x00\x00\x52\x14\x00\x30"
    MSG_EPART_XAWEB_STR "\x00\x00"
    "\x00\x00\x55\xF0\x00\x08"
    MSG_EPART_FLUSHED_HEAD_STR "\x00"
    "\x00\x00\x55\xF1\x00\x30"
    MSG_EPART_FLUSHED_MSG_STR "\x00\x00"
    "\x00\x00\x55\xF2\x00\xC8"
    MSG_EPART_FLUSHED_NORELOAD_STR "\x00"
    "\x00\x00\x55\xF3\x00\x32"
    MSG_EPART_NOLOGIN_STR "\x00\x00"
    "\x00\x00\x55\xF4\x00\x36"
    MSG_EPART_NOAWEBLIB_STR "\x00"
    "\x00\x00\x55\xF5\x00\x4C"
    MSG_EPART_NOPROGRAM_STR "\x00\x00"
    "\x00\x00\x75\x30\x00\x16"
    MSG_AWEB_EXTWINTITLE_STR "\x00"
    "\x00\x00\x75\x31\x00\x0C"
    MSG_AWEB_BYTESREAD_STR "\x00\x00"
    "\x00\x00\x75\x32\x00\x08"
    MSG_AWEB_FORMSUBMIT_STR "\x00\x00"
    "\x00\x00\x75\x33\x00\x06"
    MSG_AWEB_FORMRESET_STR "\x00"
    "\x00\x00\x75\x34\x00\x2E"
    MSG_AWEB_INDEXPROMPT_STR "\x00\x00"
    "\x00\x00\x75\x35\x00\x16"
    MSG_AWEB_NODOCTITLE_STR "\x00\x00"
    "\x00\x00\x75\x36\x00\x20"
    MSG_AWEB_SCREENTITLE_STR "\x00\x00"
    "\x00\x00\x75\x37\x00\x08"
    MSG_AWEB_HOTLISTTITLE_STR "\x00"
    "\x00\x00\x75\x38\x00\x0E"
    MSG_AWEB_GOPHERINDEX_STR "\x00\x00"
    "\x00\x00\x75\x39\x00\x0C"
    MSG_AWEB_GOPHERMENU_STR "\x00"
    "\x00\x00\x75\x3A\x00\x10"
    MSG_AWEB_WINDOWHIS_STR "\x00\x00"
    "\x00\x00\x75\x3B\x00\x06"
    MSG_AWEB_OTHER_STR "\x00"
    "\x00\x00\x75\x3C\x00\x0E"
    MSG_AWEB_LOOKUP_STR "\x00"
    "\x00\x00\x75\x3D\x00\x1C"
    MSG_AWEB_CONNECT_STR "\x00\x00"
    "\x00\x00\x75\x3E\x00\x26"
    MSG_AWEB_WAITING_STR "\x00"
    "\x00\x00\x75\x3F\x00\x1C"
    MSG_AWEB_TCPSTART_STR "\x00"
    "\x00\x00\x75\x40\x00\x1C"
    MSG_AWEB_FORMLOCATION_STR "\x00"
    "\x00\x00\x75\x41\x00\x06"
    MSG_AWEB_FORMBUTTON_STR "\x00"
    "\x00\x00\x75\x42\x00\x0E"
    MSG_AWEB_FRAME_RESIZE_STR "\x00\x00"
    "\x00\x00\x75\x43\x00\x12"
    MSG_AWEB_LOGIN_STR "\x00\x00"
    "\x00\x00\x75\x44\x00\x0C"
    MSG_AWEB_NEWSGROUP_STR "\x00"
    "\x00\x00\x75\x45\x00\x1C"
    MSG_AWEB_NEWSSCAN_STR "\x00\x00"
    "\x00\x00\x75\x46\x00\x0C"
    MSG_AWEB_NEWSSORT_STR "\x00\x00"
    "\x00\x00\x75\x47\x00\x10"
    MSG_AWEB_NEWSPOST_STR "\x00"
    "\x00\x00\x75\x48\x00\x16"
    MSG_AWEB_MAILSEND_STR "\x00\x00"
    "\x00\x00\x75\x49\x00\x10"
    MSG_AWEB_UPLOAD_STR "\x00\x00"
};

#endif /* CATCOMP_BLOCK */


/****************************************************************************/


struct LocaleInfo
{
    APTR li_LocaleBase;
    APTR li_Catalog;
};


#ifdef CATCOMP_CODE

STRPTR GetString(struct LocaleInfo *li, LONG stringNum)
{
LONG   *l;
UWORD  *w;
STRPTR  builtIn;

    l = (LONG *)CatCompBlock;

    while (*l != stringNum)
    {
        w = (UWORD *)((ULONG)l + 4);
        l = (LONG *)((ULONG)l + (ULONG)*w + 6);
    }
    builtIn = (STRPTR)((ULONG)l + 6);

#define XLocaleBase LocaleBase
#define LocaleBase li->li_LocaleBase
    
    if (LocaleBase)
        return(GetCatalogStr(li->li_Catalog,stringNum,builtIn));
#define LocaleBase XLocaleBase
#undef XLocaleBase

    return(builtIn);
}


#endif /* CATCOMP_CODE */


/****************************************************************************/


#endif /* LOCALE_H */
