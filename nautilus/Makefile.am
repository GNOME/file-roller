if ENABLE_DEPRECATIONS
DISABLE_DEPRECATED = -DG_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -DGNOME_VFS_DISABLE_DEPRECATED -DGNOME_DISABLE_DEPRECATED -DBONOBO_DISABLE_DEPRECATED
else
DISABLE_DEPRECATED =
endif

INCLUDES =						\
	-DFR_DATADIR=\"$(datadir)\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(DISABLE_DEPRECATED)				\
	$(NAUTILUS_CFLAGS)				\
	$(FR_CFLAGS)

nautilus_extensiondir=$(NAUTILUS_EXTENSION_DIR)

nautilus_extension_LTLIBRARIES=libnautilus-fileroller.la

libnautilus_fileroller_la_SOURCES =     \
	nautilus-fileroller.c           \
	nautilus-fileroller.h           \
	fileroller-module.c

libnautilus_fileroller_la_LDFLAGS = -module -avoid-version -no-undefined
libnautilus_fileroller_la_LIBADD  = $(NAUTILUS_LIBS)

-include $(top_srcdir)/git.mk
