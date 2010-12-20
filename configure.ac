dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)

AC_INIT(file-roller, 2.91.3, [http://bugzilla.gnome.org/enter_bug.cgi?product=file-roller])
AM_INIT_AUTOMAKE([1.9 foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

GNOME_COMMON_INIT
AC_PROG_CC
AC_PROG_LIBTOOL
GLIB_GSETTINGS

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(GLIB_MKENUMS, glib-mkenums)

dnl ==========================================================================
dnl
dnl If you add a version number here, you *must* add an AC_SUBST line for
dnl it too, or it will never make it into the spec file!
dnl
dnl ==========================================================================

GLIB_REQUIRED=2.25.5
GIO_REQUIRED=2.25.5
GTK_REQUIRED=2.91.1
NAUTILUS_REQUIRED=2.22.2

AC_SUBST(GLIB_REQUIRED)
AC_SUBST(GIO_REQUIRED)
AC_SUBST(GTK_REQUIRED)
AC_SUBST(NAUTILUS_REQUIRED)

dnl ===========================================================================

PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= $GTK_REQUIRED])
AC_SUBST([GTK_CFLAGS])
AC_SUBST([GTK_LIBS])

dnl ===========================================================================

AC_ARG_ENABLE(run_in_place,
              AS_HELP_STRING([--enable-run-in-place],[load ui data and extensions from the source tree]),
              [case "${enableval}" in
              yes) enable_run_in_place=yes ;;
              no) enable_run_in_place=no ;;
              *) AC_MSG_ERROR([bad value ${enableval} for --enable-run-in-place]) ;;
              esac],
              [enable_run_in_place=no])
AM_CONDITIONAL(RUN_IN_PLACE, test "x$enable_run_in_place" != xno)

dnl ===========================================================================

PKG_CHECK_MODULES(FR,			\
	glib-2.0 >= $GLIB_REQUIRED	\
	gthread-2.0			\
	gio-unix-2.0			\
	gtk+-3.0 >= $GTK_REQUIRED)
AC_SUBST(FR_CFLAGS)
AC_SUBST(FR_LIBS)

NAUTILUS_CFLAGS=""
NAUTILUS_LIBS=""
build_nautilus_actions=no
AC_ARG_ENABLE(nautilus_actions,AS_HELP_STRING([--disable-nautilus-actions],[don't build the nautilus context menu actions]),)

if test x"$enable_nautilus_actions" != xno; then
	if pkg-config --atleast-version=$NAUTILUS_REQUIRED libnautilus-extension; then
		NAUTILUS_CFLAGS=`pkg-config --cflags libnautilus-extension glib-2.0 gio-2.0`
		NAUTILUS_LIBS=`pkg-config --libs libnautilus-extension glib-2.0 gio-2.0`
		build_nautilus_actions=yes
	fi
fi
AM_CONDITIONAL(ENABLE_NAUTILUS_ACTIONS, test "x$build_nautilus_actions" = xyes)
AC_SUBST(NAUTILUS_CFLAGS)
AC_SUBST(NAUTILUS_LIBS)


dnl Checks for mkdtemp function

mkdtemp_missing=false
AC_CHECK_FUNC(mkdtemp,
    [AC_DEFINE([HAVE_MKDTEMP], 1, [Have GlibC function to make temp dirs])],
    mkdtemp_missing=true)
AM_CONDITIONAL(MKDTEMP_MISSING, test x$mkdtemp_missing = xtrue)

AC_CHECK_LIB(m, floor)

dnl ==========================================================================

AC_ARG_ENABLE(packagekit,
	      [AC_HELP_STRING([--disable-packagekit],[build without PackageKit support])],,
	      [enable_packagekit=yes])
if test "x$enable_packagekit" != "xno"; then
	AC_DEFINE(ENABLE_PACKAGEKIT, 1, [define to enable PackageKit installer])
fi

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

PKG_PROG_PKG_CONFIG
GNOME_DOC_INIT

dnl ******************************

AC_ARG_ENABLE(deprecations,AS_HELP_STRING([--enable-deprecations],[warn about deprecated usages]),)
if test "x$enable_deprecations" = "xyes"; then
        DISABLE_DEPRECATED="-DGSEAL_ENABLE -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED"
fi
AC_SUBST(DISABLE_DEPRECATED)

dnl ******************************

NAUTILUS_EXTENSION_DIR=`$PKG_CONFIG --variable=extensiondir libnautilus-extension`
AC_SUBST(NAUTILUS_EXTENSION_DIR)

dnl ******************************

AC_CONFIG_FILES([Makefile
		 file-roller.spec
		 data/Makefile
		 data/file-roller.desktop.in
		 data/ui/Makefile
		 data/icons/Makefile
		 data/icons/16x16/Makefile
		 data/icons/16x16/actions/Makefile
		 data/icons/16x16/apps/Makefile
		 data/icons/22x22/Makefile
		 data/icons/22x22/apps/Makefile
		 data/icons/24x24/Makefile
		 data/icons/24x24/actions/Makefile
		 data/icons/24x24/apps/Makefile
		 data/icons/32x32/Makefile
		 data/icons/32x32/apps/Makefile
		 data/icons/scalable/Makefile
		 data/icons/scalable/apps/Makefile
		 copy-n-paste/Makefile
		 src/Makefile
		 src/commands/Makefile
		 src/sh/Makefile
		 nautilus/Makefile
		 help/Makefile
		 po/Makefile.in])
AC_OUTPUT

echo "
Configuration:

	Source code location:   ${srcdir}
	Compiler:               ${CC}
	Internal mkdtemp:       ${mkdtemp_missing}
	Nautilus support:       ${build_nautilus_actions}
	PackageKit support:     ${enable_packagekit}
	Run in place            ${enable_run_in_place}
"
