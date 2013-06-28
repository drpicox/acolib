AC_INIT([acolib], [1.1.1], [david.rodenas@gmail.com])


dnl On estem ?
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET


dnl Volem un autoconf "contemporani"
AC_PREREQ(2.59)

dnl Inicialitzem Automake
AM_INIT_AUTOMAKE([dist-bzip2])

dnl Per comprovar que hi ha el codi
AC_CONFIG_SRCDIR(src/acolib.h)

dnl Establim el config header
AC_CONFIG_HEADERS([config.h])


dnl Les proves en C
AC_LANG([C])

dnl Comprovem install
AC_PROG_INSTALL

dnl Comprovem que hi ha compilador
AC_PROG_CC

dnl Comprovem que hi ha preprocessador
AC_PROG_CPP

dnl Comprovem que hi ha c++
dnl AC_PROG_CXX

dnl Ranlib, imperatiu per a libs
AC_PROG_LIBTOOL
dnl AC_PROG_RANLIB

dnl Libtool :D
dnl AC_PROG_LIBTOOL



dnl It would be a good idea to make this a substitutable variable
AC_ARG_ENABLE(trace,
[  --enable-trace=METHOD    enable trace, methods: none|simple
                             [none]])
trace=none
test -n "$enable_trace" &&
	test "$enable_trace" != yes && test "$enable_userdir" != no &&
		trace=$enable_trace
AC_SUBST(trace)

dnl test "$trace" == "paraver" && trace_library="-lompitrace"
AC_SUBST(trace_library)



dnl It would be a good idea to make this a substitutable variable
dnl AC_ARG_ENABLE(thread,
dnl [  --enable-thread=METHOD    enable trace, methods: pthread
dnl                             [pthread]])
dnl thread=pthread
dnl test -n "$enable_thread" &&
dnl 	test "$enable_thread" != yes && test "$enable_thread" != no &&
dnl 		thread=$enable_thread
dnl AC_SUBST(thread)
dnl 
dnl test "$thread" == "pthread" && thread_library="-lpthread"
dnl AC_SUBST(thread_library)




dnl   AC_MESSAGE[  --enable-benchmarking	disable all debugging messages and checks (default is disabled)], 
dnl   AC_ARG_ENABLE(
dnl      [benchmarking], 
dnl      [AS_HELP_STRING([--enable-benchmarking],[disable all debugging messages and checks (default is disabled)])], 
dnl      [AC_DEFINE([NDEBUG],[],[Disables all debug messages and checks])] , 
dnl      [])
 
dnl Comprovacions addicionals
dnl AC_HEADER_STDC
dnl AC_CHECK_HEADERS(string.h)

dnl AC_FUNC_ALLOCA
dnl AC_C_CONST

dnl AC_SUBST

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 test/Makefile])

AC_OUTPUT
