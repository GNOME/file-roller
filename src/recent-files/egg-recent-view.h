#ifndef __EGG_RECENT_VIEW_H__
#define __EGG_RECENT_VIEW_H__


#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>
#include "egg-recent-model.h"

#define GNOME_TYPE_RECENT_VIEW             (egg_recent_view_get_type ())
#define EGG_RECENT_VIEW(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNOME_TYPE_RECENT_VIEW, EggRecentView))
#define EGG_RECENT_VIEW_CLASS(vtable)    (G_TYPE_CHECK_CLASS_CAST ((vtable), GNOME_TYPE_RECENT_VIEW, EggRecentViewClass))
#define GNOME_IS_RECENT_VIEW(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNOME_TYPE_RECENT_VIEW))
#define GNOME_IS_RECENT_VIEW_CLASS(vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), GNOME_TYPE_RECENT_VIEW))
#define EGG_RECENT_VIEW_GET_CLASS(inst)  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GNOME_TYPE_RECENT_VIEW, EggRecentViewClass))

typedef struct _EggRecentView       EggRecentView;
typedef struct _EggRecentViewClass  EggRecentViewClass;

struct _EggRecentViewClass
{
	GTypeInterface		   base_iface;
  
	/* vtable, not signals */
	void (* do_set_model)			(EggRecentView *view,
						 EggRecentModel *model);
	EggRecentModel * (* do_get_model)	(EggRecentView *view);
};

GtkType	egg_recent_view_get_type		(void) G_GNUC_CONST;
void	egg_recent_view_set_list		(EggRecentView *view,
						 GSList *list);
void	egg_recent_view_clear			(EggRecentView *view);
EggRecentModel *egg_recent_view_get_model	(EggRecentView *view);
void	egg_recent_view_set_model		(EggRecentView *view,
						 EggRecentModel *model);


#endif /* __EGG_RECENT_VIEW_H__ */
