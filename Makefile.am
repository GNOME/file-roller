## Process this file with automake to produce Makefile.in.
SUBDIRS =		\
	src		\
	help		\
	component	\
	po

@INTLTOOL_DESKTOP_RULE@

desktopdir = $(datadir)/applications
desktop_in_files=file-roller.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop) 

icondir = $(datadir)/pixmaps
icon_DATA = file-roller.png

applicationsdir = $(datadir)/application-registry
applications_DATA = file-roller.applications

mimedir = $(datadir)/mime-info
mime_DATA = file-roller.mime file-roller.keys

EXTRA_DIST = 			\
	AUTHORS			\
	HACKING			\
	MAINTAINERS		\
	NEWS			\
	README			\
	acinclude.m4		\
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
