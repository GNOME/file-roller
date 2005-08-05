## Process this file with automake to produce Makefile.in.
SUBDIRS =		\
	data		\
	src		\
	help		\
	component	\
	nautilus	\
	po

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
	gnome-doc-utils.make

DISTCLEANFILES =                        \
	gnome-doc-utils.make		\
	po/.intltool-merge-cache        \
	intltool-extract                \
	intltool-merge                  \
	intltool-update

DISTCHECK_CONFIGURE_FLAGS = --disable-scrollkeeper
