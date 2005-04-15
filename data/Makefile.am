SUBDIRS = glade

desktopdir = $(datadir)/applications
desktop_in_files=file-roller.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop) 
@INTLTOOL_DESKTOP_RULE@

applicationsdir = $(datadir)/application-registry
applications_DATA = file-roller.applications

icondir = $(datadir)/pixmaps
icon_DATA = file-roller.png

schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = file-roller.schemas

if SCHEMAS_INSTALL
install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$(schema_DATA)
endif

mimedir = $(datadir)/mime-info
mime_DATA = file-roller.mime file-roller.keys

EXTRA_DIST = 			\
	$(applications_DATA)	\
	$(desktop_in_files)	\
	$(desktop_DATA)		\
	$(mime_DATA)		\
	$(icon_DATA)		\
	$(schema_DATA)


DISTCLEANFILES =                \
	$(desktop_DATA)	
