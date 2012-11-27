dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])

AC_INIT([file-roller],[3.6.3],[http://bugzilla.gnome.org/enter_bug.cgi?product=file-roller],[file-roller])
AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz tar-ustar])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
LT_INIT
PKG_PROG_PKG_CONFIG
GLIB_GSETTINGS

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(GLIB_MKENUMS, glib-mkenums)
AC_PATH_PROG(GLIB_COMPILE_RESOURCES, glib-compile-resources)

GLIB_REQUIRED=2.29.14
GIO_REQUIRED=2.25.5
GTK_REQUIRED=3.4.0
NAUTILUS_REQUIRED=2.22.2
JSON_GLIB_REQUIRED=0.14.0
LIBNOTIFY_REQUIRED=0.4.3
LIBARCHIVE_REQUIRED=3.0.0

dnl ===========================================================================

PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= $GTK_REQUIRED])
AC_SUBST([GTK_CFLAGS])
AC_SUBST([GTK_LIBS])

dnl ===========================================================================

AC_ARG_ENABLE(debug,
	      AS_HELP_STRING([--enable-debug], [enable compilation of debugging messages]),
	      [case "${enableval}" in
	      yes) ENABLE_DEBUG=yes ;;
	      no) ENABLE_DEBUG=no ;;
	      *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
	      esac],
	      [ENABLE_DEBUG=no])
if test x$ENABLE_DEBUG = xyes; then
	AC_DEFINE(DEBUG, 1, [enable compilation of debugging messages])
	CFLAGS="$CFLAGS -g -O0 -DDEBUG"
fi

dnl ===========================================================================

if test x$ENABLE_DEBUG = xyes; then
WARN_CFLAGS="-Wall -Wcast-align -Wtype-limits -Wclobbered -Wempty-body -Wignored-qualifiers -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wno-sign-compare"
else
WARN_CFLAGS="-Wall"
fi

for option in $WARN_CFLAGS; do
	SAVE_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS $option"
	AC_MSG_CHECKING([whether gcc understands $option])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[has_option=yes],[has_option=no])
	if test x$has_option = xyes; then
		WARNINGS="$WARNINGS $option"
	fi
	AC_MSG_RESULT($has_option)
	CFLAGS="$SAVE_CFLAGS"
	unset has_option
	unset SAVE_CFLAGS
done
unset option
CFLAGS="$CFLAGS $WARNINGS"

dnl ===========================================================================
dnl
dnl run_in_place
dnl
AC_ARG_ENABLE([run_in_place],
	      AS_HELP_STRING([--enable-run-in-place],
			     [load ui data and extensions from source tree]),,
	      [enable_run_in_place=no])

AM_CONDITIONAL(RUN_IN_PLACE, test x"$enable_run_in_place" != x"no")

dnl ===========================================================================

PKG_CHECK_MODULES(FR,			\
	glib-2.0 >= $GLIB_REQUIRED	\
	gthread-2.0			\
	gio-unix-2.0			\
	gtk+-3.0 >= $GTK_REQUIRED)
AC_SUBST(FR_CFLAGS)
AC_SUBST(FR_LIBS)

dnl ******************************
dnl
dnl nautilus actions
dnl
AC_ARG_ENABLE([nautilus_actions],
	      [AS_HELP_STRING([--disable-nautilus-actions],
			      [do not build the nautilus context menu actions])],,
	      [enable_nautilus_actions=yes])

if test x"$enable_nautilus_actions" != x"no" ; then
	PKG_CHECK_MODULES(NAUTILUS,					\
			  		  [libnautilus-extension >= $NAUTILUS_REQUIRED	\
			 		   glib-2.0					\
			  		   gio-2.0],
		  			  [enable_nautilus_actions=yes],
					  [enable_nautilus_actions=no])
	NAUTILUS_EXTENSION_DIR=`$PKG_CONFIG --variable=extensiondir libnautilus-extension`
fi

AC_SUBST(NAUTILUS_CFLAGS)
AC_SUBST(NAUTILUS_LIBS)
AC_SUBST(NAUTILUS_EXTENSION_DIR)
AM_CONDITIONAL(ENABLE_NAUTILUS_ACTIONS, test x"$enable_nautilus_actions" != x"no")

dnl ******************************
dnl
dnl json-glib-1.0
dnl
PKG_CHECK_MODULES(JSON_GLIB,
		  [json-glib-1.0 >= $JSON_GLIB_REQUIRED
		   glib-2.0],
		  [enable_json_glib=yes],
		  [enable_json_glib=no])

if test x$enable_json_glib = xyes ; then
	AC_DEFINE(HAVE_JSON_GLIB, 1, [Define to 1 if json-glib support is included])
fi

AC_SUBST(JSON_GLIB_CFLAGS)
AC_SUBST(JSON_GLIB_LIBS)

AM_CONDITIONAL(ENABLE_JSON_GLIB, test x"$enable_json_glib" != x"no")

dnl ******************************
dnl
dnl Checks for mkdtemp function
dnl
mkdtemp_missing=false
AC_CHECK_FUNC(mkdtemp,
    [AC_DEFINE([HAVE_MKDTEMP], 1, [Have GlibC function to make temp dirs])],
    mkdtemp_missing=true)
AM_CONDITIONAL(MKDTEMP_MISSING, test x$mkdtemp_missing = xtrue)

AC_CHECK_LIB(m, floor)

dnl ==========================================================================
dnl
dnl packagekit
dnl
AC_ARG_ENABLE(packagekit,
	      [AS_HELP_STRING([--disable-packagekit],[build without PackageKit support])],,
	      [enable_packagekit=yes])
if test "x$enable_packagekit" != "xno"; then
	AC_DEFINE(ENABLE_PACKAGEKIT, 1, [define to enable PackageKit installer])
fi

dnl ===========================================================================
dnl
dnl libnotify
dnl
AC_ARG_ENABLE(notification,[AC_HELP_STRING([--enable-notification],[enable operation completion notification [default=yes]])],, [enable_notification="yes"])
AM_CONDITIONAL(ENABLE_NOTIFICATION, test "x$enable_notification" = xyes)

LIBNOTIFY_LIBS=""
LIBNOTIFY_CFLAGS=""
if test x"$enable_notification" = xyes; then
   AC_MSG_CHECKING(for notification support)

   if pkg-config --atleast-version=$LIBNOTIFY_REQUIRED libnotify; then
      LIBNOTIFY_LIBS=`pkg-config --libs libnotify`
      LIBNOTIFY_CFLAGS=`pkg-config --cflags libnotify`
      AC_DEFINE(ENABLE_NOTIFICATION, 1, [Have libnotify])
   else
      enable_notification=no
   fi
fi
AC_MSG_RESULT($enable_notification)
AC_SUBST(LIBNOTIFY_LIBS)
AC_SUBST(LIBNOTIFY_CFLAGS)

dnl ******************************
dnl
dnl magic
dnl
AC_ARG_ENABLE([magic],
              AS_HELP_STRING([--enable-magic], [use libmagic to detect file type]),,
              [enable_magic=yes])

if test x"$enable_magic" = x"yes" ; then
	save_LIBS="$LIBS"
	LIBS="$LIBS -lmagic"
	AC_MSG_CHECKING([whether libmagic works])
	AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <magic.h>
int main () { magic_t m = magic_open(MAGIC_NONE); }
])],
		       [AC_MSG_RESULT([yes])],
		       [AC_MSG_RESULT([no])
		        enable_magic=no])
	LIBS="$save_LIBS"
fi

if test x"$enable_magic" = x"yes" ; then
	MAGIC_CFLAGS=
	MAGIC_LIBS=-lmagic

	AC_SUBST(MAGIC_CFLAGS)
	AC_SUBST(MAGIC_LIBS)

	AC_DEFINE(ENABLE_MAGIC, 1, [define to enable magic])
fi

AM_CONDITIONAL(ENABLE_MAGIC, test x"$enable_magic" != x"no")

dnl ===========================================================================
dnl
dnl libarchive
dnl
AC_ARG_ENABLE([libarchive],
			  [AC_HELP_STRING([--enable-libarchive], [enable code that requires libarchive [default=yes]])],,
			  [enable_libarchive="yes"])

LIBARCHIVE_LIBS=""
LIBARCHIVE_CFLAGS=""
if test x"$enable_libarchive" = xyes; then
   AC_MSG_CHECKING(for libarchive support)

   if pkg-config --atleast-version=$LIBARCHIVE_REQUIRED libarchive; then
      LIBARCHIVE_LIBS=`pkg-config --libs libarchive`
      LIBARCHIVE_CFLAGS=`pkg-config --cflags libarchive`
      AC_DEFINE(ENABLE_LIBARCHIVE, 1, [define to enable libarchive support])
   else
      enable_libarchive=no
   fi
fi
AC_MSG_RESULT($enable_libarchive)
AC_SUBST(LIBARCHIVE_LIBS)
AC_SUBST(LIBARCHIVE_CFLAGS)
AM_CONDITIONAL(ENABLE_LIBARCHIVE, test x"$enable_libarchive" = xyes)

dnl ===========================================================================
dnl
dnl SM Client
dnl
GDK_TARGET="$($PKG_CONFIG --variable target gdk-2.0)"

AC_MSG_CHECKING([which smclient backend to use])
AC_ARG_WITH([smclient],
  [AS_HELP_STRING([--with-smclient=no|auto|xsmp],
		  [Setting smclient backend (default:no)])],,
		  [with_smclient=no])

if test "$with_smclient" == "auto"; then
  case "$GDK_TARGET" in
    x11) with_smclient=xsmp ;;
    *) with_smclient=no ;;
  esac
fi

AC_MSG_RESULT([$with_smclient])

if test "$with_smclient" != "no"; then
  AC_DEFINE([WITH_SMCLIENT],[1],[Define if smclient is enabled])

  case "$with_smclient" in
    xsmp) SMCLIENT_PKGS="sm >= 1.0.0 ice" ;;
    *) SMCLIENT_PKGS="" ;;
  esac

  PKG_CHECK_MODULES([SMCLIENT],[$SMCLIENT_PKGS])
  AC_SUBST([SMCLIENT_CFLAGS])
  AC_SUBST([SMCLIENT_LIBS])
  AC_DEFINE(USE_SMCLIENT, 1, [Use a session menager])
fi

AM_CONDITIONAL([WITH_SMCLIENT],[test "$with_smclient" != "no"])
AM_CONDITIONAL([WITH_SMCLIENT_XSMP],[test "$with_smclient" = "xsmp"])
AM_CONDITIONAL([WITH_SMCLIENT_WIN32],[test "$with_smclient" = "win32"])

dnl ******************************

IT_PROG_INTLTOOL([0.40.0])

GETTEXT_PACKAGE=file-roller
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [GetText Package])

if test "x${prefix}" = "xNONE"; then
  AC_DEFINE_UNQUOTED(LOCALEDIR, "${ac_default_prefix}/${DATADIRNAME}/locale", [Locale dir])
else
  AC_DEFINE_UNQUOTED(LOCALEDIR, "${prefix}/${DATADIRNAME}/locale", [Locale dir])
fi

dnl ******************************

YELP_HELP_INIT

dnl ******************************

AC_ARG_ENABLE(deprecated,
              AS_HELP_STRING([--disable-deprecated],[warn about deprecated usages]),
              [disable_deprecated=yes],
              [disable_deprecated=no])
if test "x$disable_deprecated" = "xyes"; then
        DISABLE_DEPRECATED="-DGSEAL_ENABLE -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED"
fi
AC_SUBST(DISABLE_DEPRECATED)

dnl ******************************

AC_CONFIG_FILES([Makefile
		 file-roller.spec
		 data/Makefile
		 data/file-roller.desktop.in
		 data/icons/Makefile
		 data/icons/16x16/Makefile
		 data/icons/16x16/actions/Makefile
		 data/icons/16x16/apps/Makefile
		 data/icons/22x22/Makefile
		 data/icons/22x22/apps/Makefile
		 data/icons/24x24/Makefile
		 data/icons/24x24/actions/Makefile
		 data/icons/24x24/apps/Makefile
		 data/icons/256x256/Makefile
		 data/icons/256x256/apps/Makefile
		 data/icons/32x32/Makefile
		 data/icons/32x32/apps/Makefile
		 data/icons/48x48/Makefile
		 data/icons/48x48/apps/Makefile
		 copy-n-paste/Makefile
		 src/Makefile
		 src/commands/Makefile
		 src/sh/Makefile
		 src/ui/Makefile
		 nautilus/Makefile
		 help/Makefile
		 po/Makefile.in])
AC_OUTPUT

echo "
Configuration:

	Source code location    : ${srcdir}
	Prefix                  : ${prefix}
	Compiler                : ${CC}
	CFLAGS                  : ${CFLAGS}
	Enable debug code       : ${ENABLE_DEBUG}
	Disable deprecated code : ${disable_deprecated}
	Run in place            : ${enable_run_in_place}
	Internal mkdtemp        : ${mkdtemp_missing}
	Nautilus module         : ${enable_nautilus_actions}
	PackageKit support      : ${enable_packagekit}
	Use libmagic            : ${enable_magic}
	JSON support            : ${enable_json_glib}
	Notifications support   : ${enable_notification}
	Use libarchive          : ${enable_libarchive}
"
