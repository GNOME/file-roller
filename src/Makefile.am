SUBDIRS=icons recent-files
DIST_SUBDIRS=icons recent-files

bin_PROGRAMS = file-roller

glade_DATA =				\
	file_roller.glade		
gladedir = $(datadir)/file-roller/glade

if ENABLE_DEPRECATIONS
DISABLE_DEPRECATED = -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGNOME_VFS_DISABLE_DEPRECATED -DGNOME_DISABLE_DEPRECATED -DBONOBO_DISABLE_DEPRECATED
else
DISABLE_DEPRECATED =
endif

INCLUDES =						\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src/recent-files		\
	-I$(top_builddir)				\
	-DFR_PREFIX=\"$(prefix)\"           		\
        -DFR_SYSCONFDIR=\"$(sysconfdir)\"   		\
        -DFR_DATADIR=\"$(datadir)\"     	    	\
        -DFR_LIBDIR=\"$(libdir)\"  	         	\
	-DPIXMAPSDIR=\""$(datadir)/pixmaps"\"		\
	-DGLADEDIR=\""$(gladedir)"\" 			\
	-DICONDIR=\""$(datadir)/file-roller/icons"\"	\
	$(DISABLE_DEPRECATED)				\
	$(FR_CFLAGS)

BUILT_SOURCES =			\
	fr-marshal.c		\
	fr-marshal.h

file_roller_SOURCES = 			\
	actions.h			\
	actions.c			\
	bookmarks.c			\
	bookmarks.h			\
	dlg-add-files.c			\
	dlg-add-files.h			\
	dlg-add-folder.c		\
	dlg-add-folder.h		\
	dlg-batch-add.c			\
	dlg-batch-add.h			\
	dlg-delete.c			\
	dlg-delete.h			\
	dlg-extract.c			\
	dlg-extract.h			\
	dlg-open-with.c			\
	dlg-open-with.h			\
	dlg-password.c			\
	dlg-password.h			\
	dlg-prop.c			\
	dlg-prop.h			\
	eggtreemultidnd.c		\
	eggtreemultidnd.h		\
	ephy-ellipsizing-label.c	\
	ephy-ellipsizing-label.h	\
	file-data.c			\
	file-data.h			\
	file-list.c			\
	file-list.h			\
	file-utils.c			\
	file-utils.h			\
	fr-archive.c			\
	fr-archive.h			\
	fr-command.c			\
	fr-command.h			\
	fr-command-ar.c			\
	fr-command-ar.h			\
	fr-command-arj.c		\
	fr-command-arj.h		\
	fr-command-cfile.c		\
	fr-command-cfile.h		\
	fr-command-iso.c		\
	fr-command-iso.h		\
	fr-command-lha.c		\
	fr-command-lha.h		\
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
	fr-command-7z.c			\
	fr-command-7z.h			\
	fr-error.c			\
	fr-error.h			\
	fr-stock.c			\
	fr-stock.h			\
	fr-process.c			\
	fr-process.h			\
	gconf-utils.c			\
	gconf-utils.h			\
	gnome-vfs-helpers.c		\
	gnome-vfs-helpers.h		\
	gtk-utils.c			\
	gtk-utils.h			\
	main.c				\
	main.h				\
	preferences.c			\
	preferences.h			\
	typedefs.h			\
	ui.h				\
	utf8-fnmatch.c			\
	utf8-fnmatch.h			\
	window.h			\
	window.c			\
	$(BUILT_SOURCES)

fr-marshal.h: fr-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=fr_marshal > $@

fr-marshal.c: fr-marshal.list $(GLIB_GENMARSHAL)
	echo "#include \"fr-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --body --prefix=fr_marshal >> $@

file_roller_LDADD =					\
	$(top_builddir)/src/recent-files/librecent.a	\
	$(FR_LIBS)

schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = file-roller.schemas

if SCHEMAS_INSTALL
install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$(schema_DATA)
endif

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(schema_DATA)		\
	fr-marshal.list

CLEANFILES = $(BUILT_SOURCES)
