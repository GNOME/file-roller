#!/bin/sh
appstreamcli news-to-metainfo --limit=3 $MESON_DIST_ROOT/NEWS $MESON_DIST_ROOT/data/org.gnome.FileRoller.appdata.xml.in $MESON_DIST_ROOT/data/org.gnome.FileRoller.appdata.xml.in
