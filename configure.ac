AC_INIT([lpel], [1.x], [daniel.prokesch@gmail.com])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([build-aux])

AM_INIT_AUTOMAKE([1.11 tar-ustar no-define foreign dist-bzip2 subdir-objects silent-rules])
AM_SILENT_RULES([yes])

AC_PROG_CC_C99
AC_USE_SYSTEM_EXTENSIONS
AM_PROG_AS

LT_PREREQ([2.4])
LT_INIT

AC_ARG_WITH([pcl], [AC_HELP_STRING([--with-pcl=DIR], [look up PCL sources in DIR])], [], [with_pcl=no])
LIBPCL_LA=
if test x"$with_pcl" != xno; then
   if test -r "$with_pcl/lib/libpcl.la"; then
     LIBPCL_LA=$with_pcl/lib/libpcl.la
   elif test -r "$with_pcl/pcl/libpcl.la"; then
     LIBPCL_LA=$with_pcl/pcl/libpcl.la
   else
     AC_MSG_WARN([Cannot find libpcl.la, cannot use PCL])
     with_pcl=no
   fi
fi
if test x"$with_pcl" != xno; then
  CPPFLAGS="$CPPFLAGS -I$with_pcl/include"
  AC_SUBST([LIBPCL_LA])
  AC_DEFINE([HAVE_PCL], [1], [Define to 1 if PCL is available.])
fi

AC_ARG_WITH([mctx], [AC_HELP_STRING([--with-mctx=TYPE], [use context handling TYPE (can be pcl,i386-linux,x86_64-linux,x86_64-linux-mem,ucontext)])],
            [], [with_mctx=pcl])
case $with_mctx in
   pcl) 
      if test x"$with_pcl" = xno; then
         AC_MSG_ERROR([Context type "pcl" was specified but PCL is not found. Use --with-pcl or select another type with --with-mctx.])
      fi
      AC_DEFINE([USE_MCTX_PCL], [1], [Define to 1 to use the PCL back-end.])
      ;;
   i386-linux)
      AC_DEFINE([USE_MCTX_X86], [1], [Define to 1 to use the linux i386 assembly back-end.])
      ;;
   x86_64-linux)
      AC_DEFINE([USE_MCTX_X86_64], [1], [Define to 1 to use the linux x86_64 assembly back-end.])
      ;;
   x86_64-linux-mem)
      AC_DEFINE([USE_MCTX_X86_64_MEM], [1], [Define to 1 to use the linux x86_64 assembly back-end with memory something.])
      ;;
   ucontext)
      AC_DEFINE([USE_MCTX_UCONTEXT], [1], [Define to 1 to use the ucontext/swapcontext back-end.])
      ;;
esac

AM_CONDITIONAL([USE_MCTX_PCL], [test x"$with_pcl" != xno -a $with_mctx = pcl])
AM_CONDITIONAL([USE_MCTX_X86], [test $with_mctx = i386-linux])
AM_CONDITIONAL([USE_MCTX_X86_64], [test $with_mctx = x86_64-linux])
AM_CONDITIONAL([USE_MCTX_X86_64_MEM], [test $with_mctx = x86_64-linux-mem])





AC_LANG_PUSH([C])

AX_PTHREAD
if test "x$ax_pthread_ok" = "xno"; then
   AC_MSG_ERROR([Cannot build without a working POSIX thread implementation.])
fi
LIBS="$LIBS $PTHREAD_LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

AC_CHECK_FUNCS([sysconf])
AC_CHECK_FUNCS([pthread_spin_init])
AC_CHECK_FUNCS([pthread_setaffinity_np])

AC_SEARCH_LIBS([sem_init], [rt], 
               [AC_DEFINE([HAVE_POSIX_SEMAPHORES],[1],[Set to 1 if sem_init and semaphores are available.])])
AC_SEARCH_LIBS([clock_gettime], [rt], 
               [AC_DEFINE([HAVE_POSIX_TIMERS],[1],[Set to 1 if clock_gettime and POSIX timers are available.])])
AC_SEARCH_LIBS([cap_get_proc], [cap], 
               [AC_DEFINE([HAVE_PROC_CAPABILITIES],[1],[Set to 1 if cap_get_proc and the processor capability API are available.])])

AC_LANG_POP([C])

# FIXME: mctx selection

AC_OUTPUT([Makefile])
