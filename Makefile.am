## Process this file with automake to produce Makefile.in.
SUBDIRS =		\
	src		\
	help		\
	component	\
	nautilus	\
	po


desktopdir = $(datadir)/applications
desktop_in_files=file-roller.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop) 
@INTLTOOL_DESKTOP_RULE@

icondir = $(datadir)/pixmaps
icon_DATA = file-roller.png

applicationsdir = $(datadir)/application-registry
applications_DATA = file-roller.applications

mimedir = $(datadir)/mime-info
mime_DATA = file-roller.mime file-roller.keys

distcleancheck_listfiles = find . -type f -print | grep -v 'omf\.out'

distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper' | grep -v 'omf' | grep -v 'figures'


EXTRA_DIST = 			\
	AUTHORS			\
	HACKING			\
	MAINTAINERS		\
	NEWS			\
	README			\
	file-roller.spec	\
	file-roller.spec.in	\
	intltool-merge.in	\
	intltool-update.in	\
	intltool-extract.in	\
	omf.make		\
	xmldocs.make		\
	$(desktop_in_files)	\
	$(desktop_DATA)		\
	$(applications_DATA)	\
	$(mime_DATA)		\
	$(icon_DATA)

DISTCLEANFILES =                        \
	po/.intltool-merge-cache        \
	$(desktop_DATA)                 \
	intltool-extract                \
	intltool-merge                  \
	intltool-update
