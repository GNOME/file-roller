INCLUDES =						\
	-DG_DISABLE_DEPRECATED				\
	-DGTK_DISABLE_DEPRECATED			\
	-DGNOME_DISABLE_DEPRECATED			\
	-DDATADIR=\"$(datadir)\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(DISABLE_DEPRECATED_CFLAGS)			\
	$(FR_CFLAGS)

bonobodir = $(libdir)/bonobo
bonobo_LTLIBRARIES = libfile-roller-component.la

libfile_roller_component_la_SOURCES =			\
	file-roller-component.c				\
	file-roller-component.h				\
	libmain.c

libfile_roller_component_la_LIBADD = $(FR_LIBS)
libfile_roller_component_la_LDFLAGS = -module -avoid-version

server_in_files = File_Roller_Component.server.in.in

serverdir = $(libdir)/bonobo/servers
server_DATA = $(server_in_files:.server.in.in=.server)
$(server_in_files:.server.in.in=.server.in): $(server_in_files)
	sed -e "s|\@BONOBODIR\@|$(bonobodir)|" $< > $@

@INTLTOOL_SERVER_RULE@

EXTRA_DIST = $(server_in_files)
CLEANFILES = $(server_DATA) $(server_DATA).in
