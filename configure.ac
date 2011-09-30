dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(flvmerger, 1.0)


AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE([-Wall])

AC_PROG_CC

PKG_CHECK_MODULES(FFMPEG,[libavformat libavutil])

AC_CONFIG_FILES(Makefile src/Makefile)
AC_OUTPUT

