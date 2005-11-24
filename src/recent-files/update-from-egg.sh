#!/bin/sh

#EGGDIR=/opt/src/gnome/libegg/libegg/recent-files
EGGFILES="egg-recent.h egg-recent-model.c egg-recent-model.h egg-recent-item.c egg-recent-item.h egg-recent-view.c egg-recent-view.h egg-recent-view-bonobo.c egg-recent-view-bonobo.h egg-recent-view-gtk.c egg-recent-view-gtk.h egg-recent-util.c egg-recent-util.h"

function die() {
  echo $*
  exit 1
}

if test -z "$EGGDIR"; then
   echo "Must set EGGDIR"
   exit 1
fi

if test -z "$EGGFILES"; then
   echo "Must set EGGFILES"
   exit 1
fi

for FILE in $EGGFILES; do
  if cmp -s $EGGDIR/$FILE $FILE; then
     echo "File $FILE is unchanged"
  else
     cp $EGGDIR/$FILE $FILE || die "Could not move $EGGDIR/$FILE to $FILE"
     echo "Updated $FILE"
  fi
done
