SUBDIRS = icons

desktopdir = $(datadir)/applications
desktop_in_in_files = file-roller.desktop.in.in
desktop_in_files = $(desktop_in_in_files:.desktop.in.in=.desktop.in) 
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop) 
@INTLTOOL_DESKTOP_RULE@

matchdir = $(datadir)/file-roller
match_DATA = packages.match

servicedir = $(datadir)/dbus-1/services
service_in_files = org.gnome.FileRoller.service.in
service_DATA = $(service_in_files:.service.in=.service)
$(service_DATA): $(service_in_files) Makefile
	$(AM_V_GEN) $(SED) -e "s|\@bindir\@|$(bindir)|" $< > $@

gsettingsschema_in_files = org.gnome.FileRoller.gschema.xml.in
gsettings_SCHEMAS = $(gsettingsschema_in_files:.xml.in=.xml)
.PRECIOUS: $(gsettings_SCHEMAS)

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

convertdir = $(datadir)/GConf/gsettings
convert_DATA = file-roller.convert

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

EXTRA_DIST = 				\
	file-roller.convert		\
	org.gnome.ArchiveManager1.xml	\
	packages.match.in		\
	packages.match			\
	$(desktop_in_in_files)		\
	$(desktop_in_files)		\
	$(desktop_DATA)			\
	$(gsettingsschema_in_files)	\
	$(service_in_files)		\
	$(NULL)

DISTCLEANFILES =			\
	$(desktop_in_files)		\
	$(desktop_DATA)			\
	$(service_DATA)			\
	$(NULL)

CLEANFILES = 				\
        $(gsettings_SCHEMAS)		\
        $(NULL)

dist-hook:
	cd $(distdir); rm -f $(CLEANFILES)

-include $(top_srcdir)/git.mk
