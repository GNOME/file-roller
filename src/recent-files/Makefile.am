INCLUDES = \
	$(FR_CFLAGS) 						\
	-DG_DISABLE_DEPRECATED					\
	-DGDK_DISABLE_DEPRECATED				\
	-DGTK_DISABLE_DEPRECATED				\
	-DGDK_PIXBUF_DISABLE_DEPRECATED				\
	-DGNOME_DISABLE_DEPRECATED			

EGG_FILES =				\
	egg-recent.h			\
        egg-recent-model.c		\
        egg-recent-model.h		\
        egg-recent-item.c		\
        egg-recent-item.h		\
        egg-recent-view.c		\
        egg-recent-view.h		\
        egg-recent-view-bonobo.c	\
        egg-recent-view-bonobo.h	\
        egg-recent-view-gtk.c		\
        egg-recent-view-gtk.h		\
        egg-recent-util.c		\
	egg-recent-util.h		\
	egg-recent-vfs-utils.c		\
	egg-recent-vfs-utils.h

noinst_LIBRARIES = librecent.a

librecent_a_SOURCES = $(EGG_FILES)

EGGDIR=$(srcdir)/../../../libegg/libegg/recent-files
regenerate-built-sources:
	EGGFILES="$(EGG_FILES)" EGGDIR="$(EGGDIR)" $(srcdir)/update-from-egg.sh

