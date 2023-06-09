/**************************************************************************
                               NETPBM
                           pm_config.in.h
***************************************************************************
  This file provides platform-dependent definitions for all Netpbm
  libraries and the programs that use them.

  The make files generate pm_config.h by copying this file and adding
  other stuff.  The Netpbm programs #include pm_config.h.

  Wherever possible, Netpbm handles customization via the make files
  instead of via this file.  However, Netpbm's make file philosophy
  discourages lining up a bunch of -D options on every compile, so a 
  #define here would be preferable to a -D compile option.

**************************************************************************/

#if defined(USG) || defined(SVR4) || defined(VMS) || defined(__SVR4)
#define SYSV
#endif
#if !( defined(BSD) || defined(SYSV) || defined(MSDOS) || defined(__amigaos__))
/* CONFIGURE: If your system is >= 4.2BSD, set the BSD option; if you're a
** System V site, set the SYSV option; if you're IBM-compatible, set MSDOS;
** and if you run on an Amiga, set AMIGA. If your compiler is ANSI C, you're
** probably better off setting SYSV - all it affects is string handling.
*/
#define BSD
/* #define SYSV */
/* #define MSDOS */
#endif

/* Switch macros like _POSIX_SOURCE are supposed to add features from
   the indicated standard to the C library.  A source file defines one
   of these macros to declare that it uses features of that standard
   as opposed to conflicting features of other standards (e.g. the
   POSIX foo() subroutine might do something different from the X/Open
   foo() subroutine).  Plus, this forces the coder to understand upon
   what feature sets his program relies.

   But some C library developers have misunderstood this and think of these
   macros like the old __ansi__ macro, which tells the C library, "Don't 
   have any features that aren't in the ANSI standard."  I.e. it's just
   the opposite -- the macro subtracts features instead of adding them.

   This means that on some platforms, Netpbm programs must define
   _POSIX_SOURCE, and on others, it must not.  Netpbm's POSIX_IS_IMPLIED 
   macro indicates that we're on a platform where we need not define
   _POSIX_SOURCE (and probably must not).

   The problematic C libraries treat _XOPEN_SOURCE the same way.
*/
#if defined(__OpenBSD__) || defined (__NetBSD__) || defined(__bsdi__) || defined(__APPLE__)
#define POSIX_IS_IMPLIED
#endif


/* CONFIGURE: If you have an X11-style rgb color names file, define its
** path here.  This is used by PPM to parse color names into rgb values.
** If you don't have such a file, comment this out and use the alternative
** hex and decimal forms to specify colors (see ppm/pgmtoppm.1 for details).  */
/* There was some evidence before Netpbm 9.1 that the rgb database macros
   might be already set right now.  I couldn't figure out how, so I changed
   their meanings and they are now set unconditionally.  -Bryan 00.05.03.
*/
#ifdef VMS
#define RGB_DB1 "PBMplus_Dir:RGB.TXT"
#define RGB_DB2 "PBMplus_Dir:RGB.TXT"
#define RGB_DB3 "PBMplus_Dir:RGB.TXT"
#else
#define RGB_DB1 "/usr/lib/X11/rgb.txt"
#define RGB_DB2 "/usr/share/X11/rgb.txt"
#define RGB_DB3 "/usr/X11R6/lib/X11/rgb.txt"
#endif

/* CONFIGURE: This is the name of an environment variable that tells
** where the color names database is.  If the environment variable isn't
** set, Netpbm tries the hardcoded defaults set above.
*/
#define RGBENV "RGBDEF"    /* name of env-var */

#if (defined(SYSV) || defined(__amigaos__))

#include <string.h>
/* Before Netpbm 9.1, rand and srand were macros for random and
   srandom here.  This caused a failure on a SunOS 5.6 system, which
   is SYSV, but has both rand and random declared (with different
   return types).  The macro caused the prototype for random to be a
   second prototype for rand.  Before 9.1, Netpbm programs called
   random() and on a SVID system, that was really a call to rand().
   We assume all modern systems have rand() itself, so now Netpbm
   always calls rand() and if we find a platform that doesn't have
   rand(), we will add something here for that platform.  -Bryan 00.04.26
#define random rand
#define srandom(s) srand(s)
extern void srand();
extern int rand();
*/
/* Before Netpbm 9.15, there were macro definitions of index() and 
   rindex() here, but there are no longer any invocations of those 
   functions in Netpbm, except in the VMS-only code, so there's no
   reason for them.
*/

#ifndef __SASC
#ifndef _DCC    /* Amiga DICE Compiler */
#define bzero(dst,len) memset(dst,0,len)
#define bcopy(src,dst,len) memcpy(dst,src,len)
#define bcmp memcmp
#endif /* _DCC */
#endif /* __SASC */

#endif /*SYSV or Amiga*/

/* We should change all of Netpbm to use uint32_t instead of uint32n,
   because we now have a strategy for ensuring that uint32_t is defined.
   But we're going to wait a while in case our uint32_t strategy doesn't
   work.  04.08.24.
*/
typedef uint32_t uint32n;
typedef int32_t int32n;

#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
/* 
   Before Netpbm 9.0, atoi() and exit() were declared for everybody
   except MSDOS and Amiga, and time() and write() were declared for
   everybody except MSDOS, Amiga, and __osf__.  fcntl.h, time.h, and
   stlib.h were included for MSDOS and Amiga, and unistd.h was included
   for everyone except VMS, MSDOS, and Amiga.  With the netbsd patches,
   atoi(), exit(), time(), and write() were not declared for __NetBSD__.

   We're hoping that all current systems have the standard header
   files, and will reinstate some of these explicit declarations if we
   hear otherwise.  

   If it turns out to be this easy, we should just move these inclusions
   to the source files that actually need them.
   
   -Bryan 2000.04.13

extern int atoi();
extern void exit();
extern long time();
extern int write(); 
*/

/* CONFIGURE: On most BSD systems, malloc() gets declared in stdlib.h, on
** system V, it gets declared in malloc.h. On some systems, malloc.h
** doesn't declare these, so we have to do it here. On other systems,
** for example HP/UX, it declares them incompatibly.  And some systems,
** for example Dynix, don't have a malloc.h at all.  A sad situation.
** If you have compilation problems that point here, feel free to tweak
** or remove these declarations.
*/
#ifdef BSD
#include <stdlib.h>
#endif
#if (defined(SYSV) && !defined(VMS))
#include <malloc.h>
#endif
/* extern char* malloc(); */
/* extern char* realloc(); */
/* extern char* calloc(); */

/* CONFIGURE: Some systems don't have vfprintf(), which we need for the
** error-reporting routines.  If you compile and get a link error about
** this routine, uncomment the first define, which gives you a vfprintf
** that uses the theoretically non-portable but fairly common routine
** _doprnt().  If you then get a link error about _doprnt, or
** message-printing doesn't look like it's working, try the second
** define instead.
*/
/* #define NEED_VFPRINTF1 */
/* #define NEED_VFPRINTF2 */

/* CONFIGURE: Some systems don't have strstr(), which some routines need.
** If you compile and get a link error about this routine, uncomment the
** define, which gives you a strstr.
*/
/* #define NEED_STRSTR */

/* CONFIGURE: Set this option if your compiler uses strerror(errno)
** instead of sys_errlist[errno] for error messages.
*/
#define A_STRERROR

/* CONFIGURE: If your system has the setmode() function, set HAVE_SETMODE.
** If you do, and also the O_BINARY file mode, pm_init() will set the mode
** of stdin and stdout to binary for all Netpbm programs.
** You need this with Cygwin (Windows).
*/
#ifdef __CYGWIN__
#define HAVE_SETMODE
#endif

/* #define HAVE_SETMODE */

#ifdef __amigaos__
#include <clib/exec_protos.h>
#define getpid() ((pid_t)FindTask(NULL))
#endif

#ifdef DJGPP
#define HAVE_SETMODE
#define lstat stat
#endif /* DJGPP */

/*  CONFIGURE: Netpbm uses __inline__ to declare functions that should
    be compiled as inline code.  GNU C recognizes the __inline__ keyword.
    If your compiler recognizes any other keyword for this, you can set
    it here.
*/
#if !defined(__GNUC__)
  #if (!defined(__inline__))
    #if (defined(__sgi) || defined(_AIX))
      #define __inline__ __inline
    #else   
      #define __inline__
    #endif
  #endif
#endif

/* CONFIGURE: Some systems seem to need more than standard program linkage
   to get a data (as opposed to function) item out of a library.

   On Windows mingw systems, it seems you have to #include <import_mingw.h>
   and #define EXTERNDATA DLL_IMPORT  .  2001.05.19
*/
#define EXTERNDATA extern

/* only Pnmstitch uses UNREFERENCED_PARAMETER today (and I'm not sure why),
   but it might come in handy some day.
*/
#if (!defined(UNREFERENCED_PARAMETER))
# if (defined(__GNUC__))
#  define UNREFERENCED_PARAMETER(x)
# elif (defined(__USLC__) || defined(_M_XENIX))
#  define UNREFERENCED_PARAMETER(x) ((x)=(x))
# else
#  define UNREFERENCED_PARAMETER(x) (x)
# endif
#endif

/* In GNU, _LFS_LARGEFILE means the "off_t" functions (ftello, etc.) are
   available.  In AIX, _AIXVERSION_430 means it's AIX Version 4.3.0 or
   better, which seems to mean the "off_t" functions are available.
*/
#if defined(_LFS_LARGEFILE) || defined(_AIXVERSION_430)
typedef off_t pm_filepos;
#define FTELLO ftello
#define FSEEKO fseeko
#else
typedef long int pm_filepos;
#define FTELLO ftell
#define FSEEKO fseek
#endif

#if defined(_PLAN9)
#define TMPDIR "/tmp"
#else
/* Use POSIX value P_tmpdir from libc */
#define TMPDIR P_tmpdir
#endif

/* Note that if you _don't_ have mkstemp(), you'd better have a safe
   mktemp() or otherwise not be concerned about its unsafety.  On some
   systems, use of mktemp() makes it possible for a hacker to cause a
   Netpbm program to access a file of the hacker's choosing when the
   Netpbm program means to access its own temporary file.
*/
#ifdef __MINGW32__
  #define HAVE_MKSTEMP 0
#else
  #define HAVE_MKSTEMP 1
#endif

