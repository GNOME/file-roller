#!/bin/sh

filename=$2

JOLIET=true
ROCK_RIDGE=true

ISOINFO=`isoinfo -d -i $filename`
if echo $ISOINFO | grep "NO Joliet present" >/dev/null 2>&1; then
        JOLIET=false  
fi
if echo $ISOINFO | grep "NO Rock Ridge present" >/dev/null 2>&1; then
        ROCK_RIDGE=false
fi

iso_extensions=""
if test $JOLIET = true; then
  iso_extensions="$iso_extensions -J"
fi
if test $ROCK_RIDGE = true; then
  iso_extensions="$iso_extensions -R"
fi

isoinfo $iso_extensions $*
