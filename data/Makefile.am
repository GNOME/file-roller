SUBDIRS = ui icons

desktopdir = $(datadir)/applications
desktop_in_in_files = file-roller.desktop.in.in
desktop_in_files = $(desktop_in_in_files:.desktop.in.in=.desktop.in) 
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop) 
@INTLTOOL_DESKTOP_RULE@

matchdir = $(datadir)/file-roller
match_DATA = packages.match

gsettings_SCHEMAS = org.gnome.file-roller.gschema.xml
@GSETTINGS_RULES@

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-cache
uninstall-hook: update-cache
update-cache:
	if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

EXTRA_DIST = 			\
	$(desktop_in_in_files)	\
	$(desktop_in_files)	\
	$(desktop_DATA)		\
	packages.match.in	\
	packages.match		\
	$(gsettings_SCHEMAS)

DISTCLEANFILES =                \
	$(desktop_in_files)	\
	$(desktop_DATA)	

-include $(top_srcdir)/git.mk
