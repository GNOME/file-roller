INCLUDES =						\
	-DFR_DATADIR=\"$(datadir)\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(DISABLE_DEPRECATED)				\
	$(NAUTILUS_CFLAGS)				\
	$(FR_CFLAGS)					\
	$(JSON_GLIB_CFLAGS)				\
	$(LIBNOTIFY_CFLAGS)

nautilus_extensiondir=$(NAUTILUS_EXTENSION_DIR)

nautilus_extension_LTLIBRARIES=libnautilus-fileroller.la

libnautilus_fileroller_la_SOURCES =     \
	nautilus-fileroller.c           \
	nautilus-fileroller.h           \
	fileroller-module.c

libnautilus_fileroller_la_LDFLAGS = -module -avoid-version -no-undefined
libnautilus_fileroller_la_LIBADD  = $(NAUTILUS_LIBS)

-include $(top_srcdir)/git.mk
