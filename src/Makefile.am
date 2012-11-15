SUBDIRS = commands sh ui

bin_PROGRAMS = file-roller
noinst_PROGRAMS = test-server

if MKDTEMP_MISSING
MKDTEMP_FILES=mkdtemp.c mkdtemp.h
else
MKDTEMP_FILES=
endif

if RUN_IN_PLACE
privdatadir = $(abs_top_srcdir)/data/
uidir = $(abs_top_srcdir)/data/ui
privexecdir = $(abs_top_builddir)/src/commands/
shdir = $(abs_top_srcdir)/src/sh/
applications_dir = $(abs_top_builddir)/data
else
privdatadir = $(datadir)/file-roller/
uidir = $(datadir)/file-roller/ui
privexecdir = $(libexecdir)/file-roller/
shdir = $(libexecdir)/file-roller/
applications_dir = $(datadir)/applications
endif

INCLUDES =						\
	-I$(top_srcdir)/copy-n-paste/			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-DFR_PREFIX=\"$(prefix)\"           		\
        -DFR_SYSCONFDIR=\"$(sysconfdir)\"   		\
        -DFR_DATADIR=\"$(datadir)\"     	    	\
        -DPRIVDATADIR=\"$(privdatadir)\"		\
        -DUI_DIR=\"$(uidir)\"				\
        -DFR_LIBDIR=\"$(libdir)\"  	         	\
        -DPKG_DATA_DIR=\"$(pkgdatadir)\"                \
	-DPIXMAPSDIR=\""$(datadir)/pixmaps"\"		\
	-DGLADEDIR=\""$(gladedir)"\" 			\
	-DPRIVEXECDIR=\"$(privexecdir)\"		\
	-DSHDIR=\"$(shdir)\"				\
	-DAPPLICATIONS_DIR=\"$(applications_dir)\"	\
	$(DISABLE_DEPRECATED)				\
	$(FR_CFLAGS)					\
	$(JSON_GLIB_CFLAGS)				\
	$(LIBNOTIFY_CFLAGS)				\
	$(LIBARCHIVE_CFLAGS)

BUILT_SOURCES =			\
	fr-marshal.c		\
	fr-marshal.h		\
	fr-enum-types.h		\
	fr-enum-types.c		\
	fr-resources.c		\
	fr-resources.h		\
	$(NULL)

COMMON_SOURCES = 			\
	actions.h			\
	actions.c			\
	dlg-add.c			\
	dlg-add.h			\
	dlg-ask-password.c		\
	dlg-ask-password.h		\
	dlg-batch-add.c			\
	dlg-batch-add.h			\
	dlg-delete.c			\
	dlg-delete.h			\
	dlg-extract.c			\
	dlg-extract.h			\
	dlg-open-with.c			\
	dlg-open-with.h			\
	dlg-package-installer.c		\
	dlg-package-installer.h		\
	dlg-password.c			\
	dlg-password.h			\
	dlg-prop.c			\
	dlg-prop.h			\
	dlg-update.c			\
	dlg-update.h			\
	egg-macros.h			\
	eggtreemultidnd.c		\
	eggtreemultidnd.h		\
	file-data.c			\
	file-data.h			\
	file-utils.c			\
	file-utils.h			\
	fr-archive.c			\
	fr-archive.h			\
	fr-command.c			\
	fr-command.h			\
	fr-command-7z.c			\
	fr-command-7z.h			\
	fr-command-ace.c		\
	fr-command-ace.h		\
	fr-command-alz.c		\
	fr-command-alz.h		\
	fr-command-ar.c			\
	fr-command-ar.h			\
	fr-command-arj.c		\
	fr-command-arj.h		\
	fr-command-cfile.c		\
	fr-command-cfile.h		\
	fr-command-cpio.c		\
	fr-command-cpio.h		\
	fr-command-dpkg.c		\
	fr-command-dpkg.h		\
	fr-command-iso.c		\
	fr-command-iso.h		\
	fr-command-jar.h		\
	fr-command-jar.c		\
	fr-command-lha.c		\
	fr-command-lha.h		\
	fr-command-lrzip.c		\
	fr-command-lrzip.h		\
	fr-command-rar.c		\
	fr-command-rar.h		\
	fr-command-rpm.c		\
	fr-command-rpm.h		\
	fr-command-tar.c		\
	fr-command-tar.h		\
	fr-command-unstuff.c		\
	fr-command-unstuff.h		\
	fr-command-zip.c		\
	fr-command-zip.h		\
	fr-command-zoo.c		\
	fr-command-zoo.h		\
	fr-error.c			\
	fr-error.h			\
	fr-file-selector-dialog.c	\
	fr-file-selector-dialog.h	\
	fr-init.c			\
	fr-init.h			\
	fr-list-model.c			\
	fr-list-model.h			\
	fr-new-archive-dialog.c		\
	fr-new-archive-dialog.h		\
	fr-stock.c			\
	fr-stock.h			\
	fr-process.c			\
	fr-process.h			\
	fr-window.c			\
	fr-window.h			\
	gio-utils.c			\
	gio-utils.h			\
	glib-utils.c			\
	glib-utils.h			\
	gth-icon-cache.c		\
	gth-icon-cache.h		\
	gth-menu-button.c		\
	gth-menu-button.h		\
	gtk-utils.c			\
	gtk-utils.h			\
	java-utils.c			\
	java-utils.h			\
	open-file.c			\
	open-file.h			\
	preferences.c			\
	preferences.h			\
	rar-utils.c			\
	rar-utils.h			\
	typedefs.h			\
	ui.h				\
	$(MKDTEMP_FILES)		\
	$(BUILT_SOURCES)

if ENABLE_JSON_GLIB
COMMON_SOURCES +=			\
	fr-command-unarchiver.h		\
	fr-command-unarchiver.c
endif

if ENABLE_LIBARCHIVE
COMMON_SOURCES +=			\
	fr-archive-libarchive.h		\
	fr-archive-libarchive.c
endif

fr-marshal.h: fr-marshal.list 
	$(AM_V_GEN)( $(GLIB_GENMARSHAL) $< --header --prefix=fr_marshal > $@ )

fr-marshal.c: fr-marshal.list $(GLIB_GENMARSHAL)
	$(AM_V_GEN)( echo "#include \"fr-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --body --prefix=fr_marshal >> $@ )

fr-enum-types.h: typedefs.h 
	$(AM_V_GEN)( $(GLIB_MKENUMS) \
		--fhead "#ifndef FR_ENUM__TYPES_H\n#define FR_ENUM_TYPES_H\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
		--fprod "/* enumerations from \"@filename@\" */\n" \
		--vhead "GType @enum_name@_get_type (void);\n#define FR_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
		--ftail "G_END_DECLS\n\n#endif /* FR_ENUM_TYPES_H */" \
		$^> xgen-$(@F) \
	&& mv -f xgen-$(@F) fr-enum-types.h )
	
fr-enum-types.c: typedefs.h
	$(AM_V_GEN)( $(GLIB_MKENUMS) \
		--fhead "#include <glib-object.h>\n#include \"fr-enum-types.h\"\n" \
		--fprod "\n/* enumerations from \"@filename@\" */\n#include \"@filename@\"" \
		--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" \
		--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
		--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
		$^> xgen-$(@F) \
	&& mv -f xgen-$(@F) fr-enum-types.c )

RESOURCES = $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir=$(srcdir) $(srcdir)/file-roller.gresource.xml)

fr-resources.c: file-roller.gresource.xml $(RESOURCES)
	$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name fr $(srcdir)/file-roller.gresource.xml

fr-resources.h: file-roller.gresource.xml $(RESOURCES)
	$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name fr $(srcdir)/file-roller.gresource.xml

file_roller_SOURCES = 			\
	$(COMMON_SOURCES)		\
	main.c

file_roller_LDADD =					\
	$(top_builddir)/copy-n-paste/libeggsmclient.la	\
	$(FR_LIBS)					\
	$(JSON_GLIB_LIBS)				\
	$(LIBNOTIFY_LIBS)				\
	$(LIBARCHIVE_LIBS)

if ENABLE_MAGIC
file_roller_LDADD += $(MAGIC_LIBS)
endif

test_server_SOURCES = test-server.c
test_server_FLAGS = $(FR_CFLAGS) $(JSON_GLIB_CFLAGS)
test_server_LDADD = $(FR_LIBS) $(JSON_GLIB_LIBS)

EXTRA_DIST = 				\
	file-roller.gresource.xml	\
	fr-marshal.list 

CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	cd $(distdir); rm -f $(CLEANFILES)

-include $(top_srcdir)/git.mk
