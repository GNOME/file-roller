IMAGES =		\
	add.png		\
	extract.png	\
	file.png	\
	folder.png	\
	view.png

noinst_DATA = pixbufs.h
CLEANFILES = $(noinst_DATA)

pixbufs.h: $(IMAGES)
	gdk-pixbuf-csource --raw --build-list add_pixbuf $(srcdir)/add.png > $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list extract_pixbuf $(srcdir)/extract.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list view_pixbuf $(srcdir)/view.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list folder_rgba $(srcdir)/folder.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list file_rgba $(srcdir)/file.png >> $(srcdir)/pixbufs.h

EXTRA_DIST = $(IMAGES) pixbufs.h
