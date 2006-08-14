@INTLTOOL_DESKTOP_RULE@

SUBDIRS = glade

desktop_in_files=file-roller.desktop.in.in

desktopdir = $(datadir)/applications
desktop_DATA = $(desktop_in_files:.desktop.in.in=.desktop) 

icondir = $(datadir)/icons/hicolor/48x48/apps
icon_DATA = file-roller.png

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = file-roller.schemas

if GCONF_SCHEMAS_INSTALL
install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$(schema_DATA)
endif

EXTRA_DIST = 			\
	$(desktop_in_files)	\
	$(desktop_DATA)		\
	$(icon_DATA)		\
	$(schema_DATA)


DISTCLEANFILES =                \
	$(desktop_DATA)	
