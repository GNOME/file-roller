#include <config.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#ifndef USE_STABLE_LIBGNOMEUI
#include <libgnomeui/gnome-icon-theme.h>
#include <libgnomeui/gnome-icon-lookup.h>
#endif
#ifdef HAVE_RSVG
#include <librsvg/rsvg.h>
#endif
#include <math.h>
#include "egg-recent-util.h"

#define EGG_RECENT_UTIL_HOSTNAME_SIZE 512
#define ICON_SIZE_STANDARD 20
#define ICON_SIZE_MAX 24

/* ripped out of gedit2 */
gchar* 
egg_recent_util_escape_underlines (const gchar* text)
{
	GString *str;
	gint length;
	const gchar *p;
 	const gchar *end;

  	g_return_val_if_fail (text != NULL, NULL);

    	length = strlen (text);

	str = g_string_new ("");

  	p = text;
  	end = text + length;

  	while (p != end)
    	{
      		const gchar *next;
      		next = g_utf8_next_char (p);

		switch (*p)
        	{
       			case '_':
          			g_string_append (str, "__");
          			break;
        		default:
          			g_string_append_len (str, p, next - p);
          			break;
        	}

      		p = next;
    	}

	return g_string_free (str, FALSE);
}

static gboolean
path_represents_svg_image (const char *path)
{
        /* Synchronous mime sniffing is a really bad idea here
         * since it's only useful for people adding custom icons,
         * and if they're doing that, they can behave themselves
         * and use a .svg extension.
         */
        return path != NULL && strstr (path, ".svg") != NULL;
}

/* This loads an SVG image, scaling it to the appropriate size. */
#ifdef HAVE_RSVG
static GdkPixbuf *
load_pixbuf_svg (const char *path,
		 guint size_in_pixels,
		 guint base_size)
{
	double zoom;
	GdkPixbuf *pixbuf;

	if (base_size != 0) {
		zoom = (double)size_in_pixels / base_size;

		pixbuf = rsvg_pixbuf_from_file_at_zoom_with_max (path, zoom, zoom, ICON_SIZE_STANDARD, ICON_SIZE_STANDARD, NULL);
	} else {
		pixbuf = rsvg_pixbuf_from_file_at_max_size (path,
							    size_in_pixels,
							    size_in_pixels,
							    NULL);
	}

	if (pixbuf == NULL) {
		return NULL;
	}
	
	return pixbuf;
}
#endif

static GdkPixbuf *
scale_icon (GdkPixbuf *pixbuf,
	    double *scale)
{
	guint width, height;

	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);

	width = floor (width * *scale + 0.5);
	height = floor (height * *scale + 0.5);
	
	return gdk_pixbuf_scale_simple (pixbuf, width, height, GDK_INTERP_BILINEAR);
}

static GdkPixbuf *
load_icon_file (char          *filename,
		guint          base_size,
		guint          nominal_size)
{
	GdkPixbuf *pixbuf, *scaled_pixbuf;
	int width, height, size;
	double scale;

	if (path_represents_svg_image (filename)) {
#ifdef HAVE_RSVG
		pixbuf = load_pixbuf_svg (filename,
					  nominal_size,
					  base_size);
#else
		/* svg file, and no svg support... */
		return NULL;
#endif
	} else {
		pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

		if (pixbuf == NULL) {
			return NULL;
		}
		
		if (base_size == 0) {
			width = gdk_pixbuf_get_width (pixbuf); 
			height = gdk_pixbuf_get_height (pixbuf);
			size = MAX (width, height);
			if (size > ICON_SIZE_MAX) {
				base_size = size;
			} else {
				/* Don't scale up small icons */
				base_size = ICON_SIZE_STANDARD;
			}
		}
		
		if (base_size != nominal_size) {
			scale = (double)nominal_size/base_size;
			scaled_pixbuf = scale_icon (pixbuf, &scale);
			g_object_unref (pixbuf);
			pixbuf = scaled_pixbuf;
		}
	}

	return pixbuf;
}

#ifndef USE_STABLE_LIBGNOMEUI
GdkPixbuf *
egg_recent_util_get_icon (GnomeIconTheme *theme, const gchar *uri,
			  const gchar *mime_type)
{
	gchar *icon;
	gchar *filename;
	const GnomeIconData *icon_data;
	int base_size;
	GdkPixbuf *pixbuf;
	
	icon = gnome_icon_lookup (theme, NULL, uri, NULL, NULL,
				  mime_type, 0, NULL);

	g_return_val_if_fail (icon != NULL, NULL);

	filename = gnome_icon_theme_lookup_icon (theme, icon,
						 ICON_SIZE_STANDARD,
						 &icon_data,
						 &base_size);

	pixbuf = load_icon_file (filename, base_size, ICON_SIZE_STANDARD);
	
	
	return pixbuf;
}
#endif /* !USE_STABLE_LIBGNOMEUI */

gchar *
egg_recent_util_get_unique_id (void)
{
	char hostname[EGG_RECENT_UTIL_HOSTNAME_SIZE];
	time_t the_time;
	guint32 rand;
	int pid;
	
	gethostname (hostname, EGG_RECENT_UTIL_HOSTNAME_SIZE);
	
	time (&the_time);
	rand = g_random_int ();
	pid = getpid ();

	return g_strdup_printf ("%s-%d-%d-%d", hostname, (int)time, (int)rand, (int)pid);
}
