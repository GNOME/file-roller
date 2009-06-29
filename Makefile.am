## Process this file with automake to produce Makefile.in.
if ENABLE_NAUTILUS_ACTIONS
NAUTILUS = nautilus
else
NAUTILUS =
endif

SUBDIRS =		\
	copy-n-paste	\
	data		\
	src		\
	help		\
	$(NAUTILUS)	\
	po

distcleancheck_listfiles = find . -type f -print | grep -v 'omf\.out'

distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper' | grep -v 'omf' | grep -v 'figures'


EXTRA_DIST = 			\
	AUTHORS			\
	ChangeLog.pre-2-26	\
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

GITIGNOREFILES = build .cdt* .cproject .project .settings/

DISTCHECK_CONFIGURE_FLAGS = --disable-scrollkeeper

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
	fi

dist: ChangeLog

ChangeLog:
	@echo Creating $@
	@if test -d $(top_srcdir)/.git; then \
		(GIT_DIR=$(top_srcdir)/.git $(top_srcdir)/missing --run git log FILE_ROLLER_2_26_1.. --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
		 && mv -f $@.tmp $@ \
		 || ($(RM) $@.tmp; \
		     echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
		     (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
		test -f $@ || \
		(echo A git checkout and git-log is required to generate ChangeLog >&2 && \
		echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog
