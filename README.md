# AWeb 3

This is AWeb 3 APL open source version, an HTML 3/4 web browser for Amiga.

## [amigazen project](http://www.amigazen.com)

*A web, suddenly*

*Forty years meditation*

*Minds awaken, free*

**amigazen project** is using modern software development tools and methods to update and rerelease classic Amiga open source software. Projects include a new AWeb, a new Amiga Python 2, and the ToolKit project - a universal SDK for Amiga.

Key to the amigazen project approach is ensuring every project can be built with the same common set of development tools and configurations, so the ToolKit project was created to provide a standard configuration for Amiga development. All *amigazen project* releases will be guaranteed to build against the ToolKit standard so that anyone can download and begin contributing straightaway without having to tailor the toolchain for their own setup.

The original authors of the *AWeb* software are not affiliated with the amigazen project. This software is redistributed on terms described in the documentation, particularly the file LICENSE or LICENSE.md

The amigazen project philosophy philosophy is based on openness:

*Open* to anyone and everyone	- *Open* source and free for all	- *Open* your mind and create!

PRs for all projects are gratefully received at [GitHub](https://github.com/amigazen/). While the focus now is on classic 68k software, it is intended that all amigazen project releases can be ported to other Amiga-like systems including AROS and MorphOS where feasible.

## About AWeb 3

AWeb is one of the most sophisticated web browsers (for its time) ever released on the Amiga platform. The original author, Yvon Rozijn, kindly made AWeb open source under the AWeb Public License. 

This project's first aim is to update the code so it builds against the NDK3.2, which largely means replacing the ClassAct UI APIs with the equivalent ReAction versions, as well as updating the networking code to work properly with RoadShow and the latest AmiSSL, and ensuring it can be built easily out of the box against the ToolKit standard by anyone with an Amiga computer.

## HTML Standards Support

AWeb 3 supports HTML standards from the 1990s era web browsing. The browser implements:

- **HTML 2.0**: Full support for the official HTML 2.0 standard
- **HTML 3.2**: Full support (W3C Recommendation from 1996)
- **HTML 4.0**: Many features supported, including experimental CSS1/CSS2 subset via inline styles and external stylesheets
- **XHTML 1.0/1.1**: Support for parsing and rendering XHTML 1.0/1.1 (and XHTML-MP) in strict mode with CDATA and self-closing tags (experimental)

AWeb also supports many browser-specific extensions from Netscape and Microsoft Internet Explorer of the era, as well as some features from the abandoned HTML 3.0 draft.

### HTML Element Support

| Element | Support | Notes |
|---------|---------|-------|
| **Document Structure** | | |
| `HTML`, `HEAD`, `BODY` | ✅ Full | Standard document structure |
| `TITLE`, `BASE`, `ISINDEX` | ✅ Full | Document metadata |
| `META`, `LINK` | ✅ Full | Meta information and links |
| **Text Formatting** | | |
| Headings (`H1`-`H6`) | ✅ Full | Six levels of headings |
| `P`, `BR`, `HR` | ✅ Full | Paragraphs, line breaks, horizontal rules |
| `PRE`, `LISTING`, `XMP` | ✅ Full | Preformatted text |
| `CENTER`, `DIV` | ✅ Full | Text alignment and division |
| `NOBR`, `WBR` | ✅ Partial | Tolerant mode only (Netscape extension) |
| **Text Style** | | |
| `B`, `I`, `U`, `STRIKE` | ✅ Full | Bold, italic, underline, strikethrough |
| `TT`, `CODE`, `SAMP`, `KBD`, `VAR` | ✅ Full | Monospace and code styling |
| `EM`, `STRONG`, `CITE`, `DFN` | ✅ Full | Emphasis and citation |
| `BIG`, `SMALL`, `SUB`, `SUP` | ✅ Full | Size and positioning |
| `FONT`, `BASEFONT` | ✅ Full | Font face, size, color (deprecated in HTML 4) |
| `BLINK` | ✅ Partial | Tolerant mode only (Netscape extension) |
| **Lists** | | |
| `UL`, `OL`, `LI` | ✅ Full | Unordered and ordered lists |
| `DL`, `DT`, `DD` | ✅ Full | Definition lists |
| `DIR`, `MENU` | ✅ Full | Directory and menu lists |
| **Links and Images** | | |
| `A` (anchor) | ✅ Full | Hyperlinks and anchors |
| `IMG` | ✅ Full | Images with all standard attributes |
| `MAP`, `AREA` | ✅ Full | Client-side image maps |
| **Tables** | | |
| `TABLE`, `CAPTION` | ✅ Full | Table structure |
| `TR`, `TD`, `TH` | ✅ Full | Table rows and cells |
| `THEAD`, `TFOOT`, `TBODY` | ✅ Full | Table sections |
| `COLGROUP`, `COL` | ✅ Full | Column grouping |
| **Forms** | | |
| `FORM` | ✅ Full | Form container |
| `INPUT` | ✅ Full | All input types (text, password, checkbox, radio, submit, reset, hidden, etc.) |
| `SELECT`, `OPTION` | ✅ Full | Dropdown and list boxes |
| `TEXTAREA` | ✅ Full | Multi-line text input |
| `BUTTON` | ✅ Full | Button element |
| **Frames** | | |
| `FRAMESET`, `FRAME` | ✅ Full | Frame-based layouts |
| `NOFRAMES`, `IFRAME` | ✅ Full | Fallback and inline frames |
| **Embedded Content** | | |
| `OBJECT`, `PARAM` | ✅ Full | Embedded objects |
| `EMBED` | ✅ Partial | Tolerant mode only (Netscape extension) |
| `BGSOUND` | ✅ Partial | Tolerant mode only (Internet Explorer extension) |
| `SCRIPT`, `NOSCRIPT` | ✅ Full | JavaScript support |
| `STYLE` | ✅ Partial | Style element (inline CSS subset support) |
| `ICON` | ✅ Full | AWeb-specific icon support |

### HTML Features

| Feature | Support | Notes |
|---------|---------|-------|
| Character Entities | ✅ Full | HTML entities (`&nbsp;`, `&amp;`, etc.) |
| Numeric Entities | ✅ Full | `&#nnn;` and `&#xhhh;` formats |
| XML Entities | ✅ Partial | Many XML character entities mapped to Latin-1 |
| Tables | ✅ Full | Including backgrounds, borders, colspan, rowspan |
| Forms | ✅ Full | GET/POST, all input types, form validation |
| Frames | ✅ Full | Framesets, targeting, frame navigation |
| Client-side Image Maps | ✅ Full | Including maps defined in other documents |
| Background Images | ✅ Full | On `BODY` and table elements |
| Font Colors/Faces/Sizes | ✅ Full | Font styling attributes |
| Meta Refresh | ✅ Full | Client-pull mechanism |
| **Advanced Features** | | |
| CSS Style Sheets | ✅ Partial | Experimental inline and external CSS (subset of CSS1/CSS2 properties). Supported areas include text/font properties, colors and backgrounds, basic layout (margin/padding/border/position), list styling, and simple grid layouts; many table/layout properties remain limited|
| XHTML 1.0/1.1 | ✅ Partial | XHTML 1.0/1.1 and XHTML-MP parsing/rendering with DOCTYPE/XML detection, self-closing tags, and CDATA support|
| URL Schemes (data:, cid:) | ✅ Full | `data:` URLs for inline resources and `cid:` URLs for multipart MIME Content-ID references (HTML email) as per RFC 2397 and RFC 2111|

### HTML Modes

AWeb offers three HTML parsing modes to handle the wide variety of HTML found on 1990s era websites:

1. **Strict Mode**: Only recognizes official HTML standards
2. **Tolerant Mode**: Recognizes browser-specific extensions and recovers from common HTML errors
3. **Compatible Mode**: Attempts to handle severely malformed HTML by relaxing parsing rules

## JavaScript Support

AWeb implements **JavaScript 1.5 (ECMA 262-3)**, providing comprehensive JavaScript language features and browser object model support. This includes all core JavaScript 1.1 features plus significant enhancements from the ECMAScript 3 standard.

### JavaScript Language Features

| Feature | Support | Notes |
|---------|---------|-------|
| **Data Types** | | |
| Numbers | ✅ Full | Integer and floating-point |
| Strings | ✅ Full | String literals and operations |
| Booleans | ✅ Full | `true` and `false` |
| `null`, `undefined` | ✅ Full | Null and undefined values |
| Objects | ✅ Full | Object literals and constructors |
| Arrays | ✅ Full | Array literals and methods |
| Functions | ✅ Full | Function declarations and expressions |
| **Operators** | | |
| Arithmetic | ✅ Full | `+`, `-`, `*`, `/`, `%`, `++`, `--` |
| Comparison | ✅ Full | `==`, `!=`, `===`, `!==`, `<`, `>`, `<=`, `>=` |
| Logical | ✅ Full | `&&`, `||`, `!` |
| Bitwise | ✅ Full | `&`, `|`, `^`, `~`, `<<`, `>>`, `>>>` |
| Assignment | ✅ Full | `=`, `+=`, `-=`, etc. |
| Ternary | ✅ Full | `? :` conditional operator |
| **Control Flow** | | |
| `if`/`else` | ✅ Full | Conditional statements |
| `while`, `do/while` | ✅ Full | Looping constructs |
| `for`, `for/in` | ✅ Full | Iteration with `for` loops |
| `switch` | ✅ Full | Multi-way branching (JavaScript 1.5) |
| `break`, `continue` | ✅ Full | Loop control |
| `return` | ✅ Full | Function return values |
| `try`/`catch` | ✅ Full | Exception handling (JavaScript 1.5) |
| **Functions** | | |
| Function declarations | ✅ Full | Named and anonymous functions |
| Arguments object | ✅ Full | Access to function arguments |
| `this` keyword | ✅ Full | Context object reference |
| `new` operator | ✅ Full | Object instantiation |
| `typeof`, `delete` | ✅ Full | Type checking and property deletion |
| `void`, `in` | ✅ Full | Void operator and property checking |
| `with` statement | ✅ Full | Scope manipulation |
| `var` declarations | ✅ Full | Variable declarations |

### Browser Object Model (BOM)

| Object | Support | Notes |
|--------|---------|-------|
| **window** | ✅ Full | Top-level window object |
| `window.open()` | ✅ Full | Open new windows (with banner suppression option) |
| `window.close()` | ✅ Full | Close windows |
| `window.alert()`, `confirm()`, `prompt()` | ✅ Full | Dialog boxes |
| `window.setTimeout()`, `clearTimeout()` | ✅ Full | Timed execution |
| `window.status`, `defaultStatus` | ✅ Full | Status bar text |
| `window.location` | ✅ Full | Current URL and navigation |
| `window.history` | ✅ Full | Browser history navigation |
| `window.navigator` | ✅ Full | Browser information |
| `window.document` | ✅ Full | Document object model |
| `window.frames` | ✅ Full | Frame array access |
| **document** | ✅ Full | Document object |
| `document.write()`, `writeln()` | ✅ Full | Dynamic content generation |
| `document.forms[]` | ✅ Full | Form collection |
| `document.images[]` | ✅ Full | Image collection |
| `document.links[]` | ✅ Full | Link collection |
| `document.anchors[]` | ✅ Full | Anchor collection |
| `document.applets[]` | ✅ Full | Applet collection (Java) |
| `document.embeds[]` | ✅ Full | Embedded object collection |
| `document.cookie` | ✅ Full | Cookie access |
| `document.URL`, `referrer` | ✅ Full | Document location |
| `document.title` | ✅ Full | Document title |
| `document.bgColor`, `fgColor`, `linkColor`, etc. | ✅ Full | Document colors |
| `document.lastModified` | ✅ Full | Document modification date |
| **form** | ✅ Full | Form object |
| `form.elements[]` | ✅ Full | Form field collection |
| `form.submit()`, `reset()` | ✅ Full | Form submission |
| `form.action`, `method`, `target` | ✅ Full | Form attributes |
| **form elements** | ✅ Full | `text`, `textarea`, `select`, `checkbox`, `radio`, `button` objects |
| **location** | ✅ Full | URL location object |
| `location.href`, `protocol`, `host`, `pathname`, etc. | ✅ Full | URL components |
| `location.reload()` | ✅ Full | Reload current page |
| **history** | ✅ Full | Browser history |
| `history.back()`, `forward()`, `go()` | ✅ Full | History navigation |
| **navigator** | ✅ Full | Browser information |
| `navigator.appCodeName` | ✅ Full | Application code name |
| `navigator.appName` | ✅ Full | Application name |
| `navigator.appVersion` | ✅ Full | Application version |
| `navigator.userAgent` | ✅ Full | User agent string |
| `navigator.javaEnabled()` | ✅ Full | Check if Java is enabled (but always returns false) |
| `navigator.taintEnabled()` | ✅ Full | Check if data tainting is enabled |
| `navigator.platform` | ❌ | Not implemented in JavaScript 1.1 |

### JavaScript Built-in Objects

| Object | Support | Notes |
|--------|---------|-------|
| **String** | ✅ Full | String object and methods (including `match`, `replace`, `search`, `split` with RegExp support) |
| **Number** | ✅ Full | Number object and methods |
| **Boolean** | ✅ Full | Boolean object |
| **Array** | ✅ Full | Array methods (`join`, `reverse`, `sort`, `concat`, `slice`, `splice`, `push`, `pop`, `shift`, `unshift`) |
| **Math** | ✅ Full | Mathematical functions and constants |
| **Date** | ✅ Full | Date and time handling |
| **Object** | ✅ Full | Base object type with prototype methods |
| **Function** | ✅ Full | Function constructor with `apply()` and `call()` |
| **RegExp** | ✅ Full | Regular expression objects (JavaScript 1.5) |

### JavaScript Event Handlers

| Event Handler | Support | Notes |
|---------------|---------|-------|
| `onClick` | ✅ Full | Mouse click events |
| `onLoad`, `onUnload` | ✅ Full | Page load/unload events |
| `onSubmit`, `onReset` | ✅ Full | Form events |
| `onChange` | ✅ Full | Form field change events |
| `onFocus`, `onBlur` | ✅ Full | Focus events |
| `onMouseOver`, `onMouseOut` | ✅ Full | Mouse hover events |
| `onSelect` | ✅ Full | Text selection events |
| `onError`, `onAbort` | ✅ Full | Image and object error events |

### JavaScript 1.5 (ECMA 262-3) Features

AWeb implements the following JavaScript 1.5 enhancements beyond JavaScript 1.1:

- ✅ `switch` statement - Multi-way branching
- ✅ Regular Expressions (`RegExp` object) - Pattern matching with `test()`, `exec()`, and properties
- ✅ Exception handling (`try`/`catch`) - Error handling with try/catch blocks
- ✅ Enhanced `Array` methods - `concat()`, `slice()`, `splice()`, `push()`, `pop()`, `shift()`, `unshift()`
- ✅ Enhanced `String` methods - `match()`, `replace()`, `search()`, `split()` with RegExp support
- ✅ `Function.prototype.apply()` and `call()` - Function invocation methods
- ✅ `Object.prototype` methods - `hasOwnProperty()`, `propertyIsEnumerable()`, `isPrototypeOf()`, `toLocaleString()`
- ✅ Dynamic garbage collection - Automatic memory management during script execution
- ✅ Fastidious and Omnivorous error modes - Configurable error handling behavior

### JavaScript Limitations

Features **not** currently supported:

- ❌ DOM manipulation methods (`getElementById`, `getElementsByTagName`, `createElement`, etc.)

As of AWeb 3.6 alpha 7 the following previously unsupported features are now supported:
- ✅ XMLHttpRequest (AJAX) - asynchronous HTTP requests from JavaScript
- ✅ `finally` clause in try/catch blocks

### JavaScript Tools

AWeb includes two JavaScript development tools:

- **AWebJS**: Standalone JavaScript interpreter for testing scripts outside the browser
- **JavaScript Debugger**: Built-in step-through debugger with variable inspection and expression evaluation

## Roadmap

The first AWeb APL open source release was version 3.4, in 2003 from the _AWeb Open Source Development Team_.

Since then one further "3.5" beta release - or rather, 12 beta releases of 3.5 - were made for both OS4 and classic Amiga, and even MorphOS and AROS too, however the sourcecode to the 68k release of 3.5 seems to no longer be available to the public in a buildable form, if it ever was with dependencies on a very specific, and undocumented, GCC based toolchain. Snapshots for versions for other platforms such as OS4 can be found, such as on os4depot, but the developers of 3.5 did not seem to bother to maintain compatibility with the classic Amiga build tools used by the original AWeb.

Thus, this version 3.6 is derived directly from the 3.4 source code release, with the intent to cherry-pick improvements from the 3.5 branch where the changes have added value. 

The roadmap for AWeb under amigazen project is, for now:

### AWeb 3.6

The first release from amigazen project is version 3.6. The goals of this release are simply:

- Ensure AWeb can be built with supported SDKs
- Set sensible default prefs based on conventions that emerged in the years since AWeb was still state of the art
- Package both binary and sourcecode releases properly for ease of distribution and installation 
- Refactor the HTTP and SSL code to work better with modern web servers using HTTP/1.1, gzip, chunked encoding and modern TLS using AmiSSL

The main changes in version 3.6 compared to version 3.4 are:
- Stable re-release of AWeb 3.4 functionality built against support SDKs i.e. ReAction, Roadshow (with INet225 support disabled), P96 (replacing no longer supported Cybergraphics libraries), AmiSSL 5.2 and NDK3.2
- Adding HTTP/1.1, chunked encoding and gzip compressed http streams support with a refactored http module
- Renamed the application to simply _AWeb_, not AWeb-II or AWeb3, just _AWeb_ and the assign is also now just _AWeb:_ and will be automatically created on launch if it does not already exist
- Refactoring the SSL module code to stabilise it and update it to use AmiSSL 5.20 or later
- Changing default configuration settings to sensible values including white default background, Cookies accepted by default, and scalable fonts

**What's New in 3.6 Alpha 7:**
- **Improved font related CSS support:** font-family and font-size (size keywords, px, pt, em, ex, %)
- Many more new optimisations, bug fixes, and improvements to the CSS engine and JavaScript engine
- **Experimental XMLHttpRequest API:** for AJAX-style sites, with matching ARexx commands for script-driven HTTP
- **Works on OS4:** with AmiSSL 5, mousewheel scrolling, background image support, and no more DSI and ISI errors on application startup
- **AWeb is now a commodity:** and can be managed by Exchange, with Show/Hide mapped to the Iconify feature
- **Experimental ttengine.library support:** for native TrueType/OpenType fonts with anti-aliasing on full-colour screenmodes, automatically when TTEngine is installed
- **FTP module now supports FTPS:** (FTP over TLS, e.g. implicit TLS on port 990)
- **Full JavaScript 1.5:** AWebJS now supports require(), delay(), and print()
- **AWeb 3.5 merge:** All relevant new features, bug fixes and optimisations from AWeb 3.5 up to revision 3.5.09 are now integrated into AWeb 3.6

**What's New in 3.6 Alpha 6:**
- **Enhanced CSS Support:** More CSS properties including padding, margin (with auto), position, borders, vertical-align, text-align, display: none, overflow clipping, clear for floats, and min/max width/height constraints. CSS colors can be specified in hex or by name
- **HTTP Range Support (RFC 7233):** Automatic resume of incomplete requests for faster page loading
- **view-source: Protocol:** View any URL as plain text source
- **Mozilla Bookmarks Import:** Import Mozilla bookmarks.html files into AWeb Hotlist
- **Protoweb.org:** ARexx script for easy Protoweb.org proxy setup
- **Connection Management:** Improved connection cancellation when navigating or reloading pages
- **Plugin Updates:** PNG plugin updated to libpng 1.6.43, GIF plugin fixed so it doesn't crash as it did when AWeb 3.5 changes were first integrated
- **AmiSSL 5.25:** Rebuilt with latest AmiSSL SDK, prefers CHACHA20-POLY1305 ciphers for better Amiga performance
- **68020 Optimization:** All AWeb binaries are now built with default 68020 optimization enabled
- **Mousewheel Support:** Added mousewheel scrolling on OS4 and OS3.2
- **Features from AWeb 3.5:** Reintegrated INS/DEL elements, bgalign, background image optimizations, JavaScript improvements, form element enhancements, DefIcons support, printer improvements, and many bug fixes
- **Bug Fixes:** Fixed CSS loading with cached files, CSS rendering issues, SSL_CTX management, redirect handling, WBStartup crashes, and many other critical bugs

**What's New in 3.6 Alpha 5:**
- **CSS Support:** Experimental inline CSS and external stylesheet support for a subset of CSS1 and CSS2 properties (text, layout, background, border, list properties, and basic grid layout)
- **XHTML 1.0 Support:** Added XHTML 1.0 parsing and rendering with strict mode enforcement and CDATA section support
- **Gemini and Spartan Protocols:** Experimental support for gemini:// and spartan:// protocols, new gopher-like networks, and gemtext markup
- **AWebView:** New standalone HTML and Markdown file viewer built using LOCALONLY configuration, taking up less RAM by excluding network features
- **Enhanced UTF-8:** UTF-8 support now handles mapping some 3-byte and 4-byte characters to Latin-1
- **TLS Security Improvements:** TLS connections now safely fail immediately instead of prompting for unsecure fallback, removed option to downgrade HTTPS to HTTP
- **Plugin Updates:** PNG plugin updated to 1.0.69 and now uses same zlib as main program; JFIF and GIF plugins updated with 3.5 improvements
- **Gopher Enhancements:** Reintegrated enhanced Gopher protocol support from AWeb 3.5 with improved type handling
- **Bug Fixes:** Fixed CSS parser infinite loops, CSS length value parsing, link color issues, CDATA parsing, TLS cleanup, chunked encoding, and OS4 compatibility issues

**What's New in 3.6 Alpha 4:**
- **about:plugins:** Added about:plugins to list all loaded AWebPlugin and AWebLib modules
- **file:// Protocol Directory Browsing:** The file:// protocol now supports browsing directories and viewing mounted volumes
- **Enhanced HTTPS Security:** HTTPS connections now deny weak ciphers and TLS < 1.2, with improved CN/SAN name matching, wildcard support, chain validation and SNI
- **Font Selection Improvements:** FONT tags now search for named fonts first, then mapped alternatives, then fallback fonts
- **FORM Element Font Rendering:** FORM elements now render using the current font of their container element
- **theoldnet.com Integration:** Added theoldnet.com as default bookmark with tested proxy support for browsing archived websites
- **Features from AWeb 3.5:** Reintegrated JavaScript dynamic garbage collection, ETags for caching, Content-Disposition parsing, and 302/307 redirect support
- **Bug Fixes:** Fixed entity rendering in all HTML modes, proxy settings now save correctly, fixed PNG plugin infinite loop bug

**What's New in 3.6 Alpha 3:**
- **about: Protocol Support:** Added about.aweblib plugin providing special internal URLs (about:home, about:blank, about:fonts, about:version)
- **Markdown Rendering:** Markdown documents loaded from files or http(s) URLs are now rendered as HTML on the fly
- **Enhanced SSL/TLS Security:** Fixed hostname verification according to RFC 6125, added TLS 1.3 support, fixed critical race conditions and memory safety issues
- **Improved HTTP Handling:** Better error reporting, redirect loop protection, increased buffer size for modern headers, fixed large file transfer truncation issues
- **User Interface Improvements:** Smarter window sizing, system contextual mouse pointers, improved quit behavior
- **Additional XML Entities:** Added support for remaining Latin-1 XML entities
- **MIME Type Handling:** Enhanced content-type detection and support for recognising RSS/Atom feeds (rendered as plain text)

See [CHANGELOG.md](CHANGELOG.md) for complete details of all changes.

## Frequently Asked Questions

### What is the ultimate aim of the new AWeb project?

The Amiga actually had one of the first graphical web browsers - AMosaic reimplemented Mosaic natively for Amiga using MUI, and came out not long after the Mac and Windows versions of Mosaic. This was later followed by IBrowse, Voyager and AWeb itself. A browser called Web Cruiser was announced by Finale Development, the team behind ClassAct, but was quickly cancelled as it became clear the Amiga platform would no longer be commercially viable.

Of these, the closest thing to a browser integrating tightly into the Amiga operating system is AWeb, due to its use of the ClassAct, now ReAction, user interface toolkit, as well as DataTypes, ARexx and extending the Amiga's BOOPSI and shared library architecture for its own plugin system. 

When AWeb was originally developed, web standards were in a state of rapid flux and many websites relied on plugin technologies such as Shockwave Flash and Java Applets, making it a challenge for any browser developer to keep up let alone one working on their own. Ironically in 2025 the needs of a 'classic web browser' are lesser as both Flash and Applets have died off completely, although modern browsers employ a variety of new standards to provide similar capabilities for interactive 'web apps'.

The release of AWeb as open source was a generous act by its author, and the code itself a masterpiece of software design, one that deserves continued effort to "finish the job" started all those years ago.

In this context, finishing the job means both completing support for the final versions of those web standards and, with the benefit of hindsight, the way those features ended up being used in practice. It also means leveraging the AWeb architecture in new ways to extend the Amiga platform to provide reusable services such as HTML rendering and HTTP processing in a way that other platforms have enjoyed in the intervening years and true to the Amiga platform's famous modularity.

### Which features from the various releases of AWeb 3.5 have already been cherry-picked for inclusion in AWeb 3.6?

For a full list of new features, bug fixes and improvements cherry-picked from AWeb 3.5, see the [CHANGELOG.md]

### Are there any features from the various releases of AWeb 3.5 that are not going to be reintegrated into AWeb 3.6?

The implementation of HTTP/1.1, gzip and chunked encoding, as well as new features such as HTTP Range and keep-alive support, in AWeb 3.6 is vastly more sophisticated than the simplistic version in AWeb 3.5, it is an all new implementation, so no code from the AWeb 3.5 version has been reintegrated. Similarly the AmiSSL 5 integration is all new.

Other changes from AWeb 3.5 that have been reviewed and rejected are:
- FTP module changes were integrated, tested, and found to be completely broken. Rather than fix the AWeb 3.5 code, which made unnecessary changes to otherwise already working code, the changes were reverted out completely. AWeb's original FTP module already worked correctly.
- Support for multiple MIME entries for a given mime type was a kludge for a server side issue rarely encountered, and complicated the UI unnecessarily
- New About dialog (erroneously called splash screen in the AWeb 3.5 documentation) was overcomplicated and ugly, AWeb 3.6 keeps the original AWeb 3.4 About dialog
- Plugin API and version numbering was changed unnecessarily in AWeb 3.5. AWeb 3.6 Plugin API remains consistent with AWeb 3.4
- Borders around images was made a user configurable setting with additional AWebCfg settings. This is both unnecessary - images should simply not have borders unless styled as such - and rendered it necessary to localise the new settings
- Double buffering implementation was found to cause many regressions, as well as using twice as much memory

As of AWeb 3.6 Alpha 7 the only significant remaining differences between AWeb 3.5 and AWeb 3.6 are the user interface layout changes and configuration settings

### What is amigazen project's plan for the future of AWeb?

The first release 3.6 is designed to be a stable rebuild of version 3.4 updated to build with the latest NDK and relevant third party SDKs such as AmiSSL 5 and RoadShow, still also using SAS/C like the original release. The most important changes from the various beta releases of 3.5 will also be cherry picked for inclusion where they don't break compatibility with classic Amiga.

Later releases will then add new features missing from AWeb's standards support incrementally.

The intention will be to eventually reach a version of AWeb that implements at least some of CSS including CSS2, HTML standards up to XHTML and XMLHttpRequest() in JavaScript, with a DOM, which should give AWeb compatibility up to the equivalent of about 2008 to 2010 era web content

### Does the new AWeb work on Workbench 3.1, 3.5 or 3.9?

The new AWeb is built and tested using the NDK 3.2 but should still be compatible with Workbench 3.1 with ClassAct, or the 3.5 and 3.9 releases with ReAction. Likewise the network support is built with the Roadshow SDK but should work with any bsdsocket.library implementation, and is most heavily tested with the UAE version for obvious reasons.

New features such as system settings for contextual mouse pointers are implemented in using progressive upgrade techniques at runtime wherever possible, for example with branching code paths that check if intuition.library 47 or higher is available.

### Will AWeb support modern websites?

Since the advent of HTML5, CSS3, WebAssembly, WebGL and more new web technologies, the browser has become an operating system unto itself. Indeed, many popular applications are these days written to use the Electron runtime, built on Chromium and Blink and are thus hybrid web apps, not truly native apps. This is why there are really only 3 browser engines left - WebKit as supported by Apple, Blink, originally forked from WebKit and used in Chromium which is the foundation of most current web browsers from Google Chrome itself to Microsoft Edge and new browsers from AI startups, and the venerable Gecko used by Firefox, now in its new Rust based iteration. 

Some legacy forks remain such as the Goanna continuation of the original C based Gecko code, but the sheer cost involved in maintaining a browser engine in 2025 means no it's not realistic to expect AWeb can support these features on any platform let alone on classic Amiga with its legacy constraints.

### What happened to the AWeb Open Source Development Team?

No idea, but their website at aweb.sunsite.dk has been deactivated since 2009 and their sourcecode only available in snapshots across various Amiga file libraries, in fact they seem to have frequently done their work not in compliance with the AWeb Public License. Some of their members remain active on other Amiga development projects mostly for OS4. It is now the year 2025. Some of their content, such as their ticket boards, can still be viewed on archive.org including a tentative plan for an AWeb 4 based on KHTML - the very same engine that later became WebKit - but it seems the sourcecode repository for any of their versions was never public or has been lost to history.

### Will this new version of AWeb be available for OS4, AROS or MorphOS?

Ports of AWeb - version 3.5 - already exist, so at some point it should be possible to re-merge the platform specific changes in. Unfortunately most of the work done on the 3.5 beta releases made no effort to retain compatibility with a native Amiga classic builds, so this new release starts again updating from the original 3.4 release from Yvon Rozijn.

The first priority however is to get a good, stable build of AWeb in the form Yvon left it in when he gave it to the community.

The current AWeb 3.6 runs on OS4 as a 68k binary and as of version 3.6 alpha 7 all known OS4 specific issues are fixed.

### Can I contribute to the new AWeb?

Yes please! Whether code, testing or translations and documentation, all contributions are welcome and will remain free and open source in the spirit and letter of the AWeb Public License. See also (CONTRIBUTING.md)

## Contact 

- At GitHub https://github.com/amigazen/aweb3/ 
- on the web at http://www.amigazen.com/aweb/ (Amiga browser compatible)
- or email aweb@amigazen.com

## Acknowledgements

*Amiga* is a trademark of **Amiga Inc**. 

Original AWeb by Yvon Rozijn, released as AWeb APL to the open source community