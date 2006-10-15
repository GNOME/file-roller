IMAGES =			\
	add.png			\
	add-16.png		\
	add-folder.png		\
	add-folder-16.png	\
	extract.png		\
	extract-16.png		\
	file.png		\
	folder.png		\
	view.png

noinst_DATA = pixbufs.h
CLEANFILES = $(noinst_DATA)

pixbufs.h: $(IMAGES)
	gdk-pixbuf-csource --raw --build-list add_24_rgba $(srcdir)/add.png > $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list add_16_rgba $(srcdir)/add-16.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list add_folder_24_rgba $(srcdir)/add-folder.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list add_folder_16_rgba $(srcdir)/add-folder-16.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list extract_24_rgba $(srcdir)/extract.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list extract_16_rgba $(srcdir)/extract-16.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list view_24_rgba $(srcdir)/view.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list folder_rgba $(srcdir)/folder.png >> $(srcdir)/pixbufs.h
	gdk-pixbuf-csource --raw --build-list file_rgba $(srcdir)/file.png >> $(srcdir)/pixbufs.h

EXTRA_DIST = $(IMAGES) pixbufs.h
