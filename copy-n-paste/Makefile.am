INCLUDES = $(DISABLE_DEPRECATED) \
	   -UGETTEXT_PACKAGE

noinst_LTLIBRARIES = libeggsmclient.la

libeggsmclient_la_LIBADD = $(GTK_LIBS)
libeggsmclient_la_CFLAGS = $(GTK_CFLAGS)

libeggsmclient_la_SOURCES = 			\
	eggdesktopfile.h 			\
	eggdesktopfile.c

if WITH_SMCLIENT
libeggsmclient_la_SOURCES += 			\
	eggsmclient.h 				\
	eggsmclient.c 				\
	eggsmclient-private.h 			\
	eggsmclient-xsmp.c
endif

-include $(top_srcdir)/git.mk
