SUBDIRS=icons recent-files
DIST_SUBDIRS=icons recent-files

bin_PROGRAMS = file-roller

glade_DATA =				\
	file_roller.glade		\
	file_roller_prop.glade
gladedir = $(datadir)/file-roller/glade

DISABLE_DEPRECATED = -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGNOME_VFS_DISABLE_DEPRECATED -DGNOME_DISABLE_DEPRECATED	-DBONOBO_DISABLE_DEPRECATED

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

file_roller_SOURCES = 		\
	bookmarks.c		\
	bookmarks.h		\
	dlg-add.c		\
	dlg-add.h		\
	dlg-batch-add.c		\
	dlg-batch-add.h		\
	dlg-delete.c		\
	dlg-delete.h		\
	dlg-extract.c		\
	dlg-extract.h		\
	dlg-open-with.c		\
	dlg-open-with.h		\
	dlg-password.c		\
	dlg-password.h		\
	dlg-preferences.c	\
	dlg-preferences.h	\
	dlg-prop.c		\
	dlg-prop.h		\
	dlg-viewer.c		\
	dlg-viewer.h		\
	dlg-viewer-or-app.c	\
	dlg-viewer-or-app.h	\
	eggtreemultidnd.c	\
	eggtreemultidnd.h	\
	file-data.c		\
	file-data.h		\
	file-list.c		\
	file-list.h		\
	file-utils.c		\
	file-utils.h		\
	fr-archive.c		\
	fr-archive.h		\
	fr-command.c		\
	fr-command.h		\
	fr-command-cfile.c	\
	fr-command-cfile.h	\
	fr-command-lha.c	\
	fr-command-lha.h	\
	fr-command-rar.c	\
	fr-command-rar.h	\
	fr-command-tar.c	\
	fr-command-tar.h	\
	fr-command-zip.c	\
	fr-command-zip.h	\
	fr-process.c		\
	fr-process.h		\
	gconf-utils.c		\
	gconf-utils.h		\
	gnome-vfs-helpers.c	\
	gnome-vfs-helpers.h	\
	gtk-utils.c		\
	gtk-utils.h		\
	main.c			\
	main.h			\
	menu-callbacks.c	\
	menu-callbacks.h	\
	menu.h			\
	preferences.c		\
	preferences.h		\
	toolbar.h		\
	typedefs.h		\
	window.h		\
	window.c		\
	$(BUILT_SOURCES)

fr-marshal.h: fr-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=fr_marshal > $@

fr-marshal.c: fr-marshal.list $(GLIB_GENMARSHAL)
	echo "#include \"fr-marshal.h\"" > $@ && \
	$(GLIB_GENMARSHAL) $< --body --prefix=fr_marshal >> $@

file_roller_LDADD =					\
	@LIBOBJS@					\
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
	fnmatch.c		\
	fnmatch.h		\
	fr-marshal.list

CLEANFILES = $(BUILT_SOURCES)
