#!/bin/sh

# If the _FILES folder contains only one file/folder then moves its content 
# in the extraction folder.

extract_here_dir=$1
extract_to_dir=$2

if test ! -d "$extract_here_dir" || test ! -d "$extract_to_dir"; then
	exit
fi

filename=
nfiles=0
for f in `ls $extract_here_dir`; do
	filename=$f
	nfiles=`expr $nfiles + 1`
	if [ "$nfiles" -gt "1" ]; then
		# Do not move the files if the directory contains more 
		# than one file.
		exit
	fi
done

if test -z $filename; then
	exit
fi

if test -e "${extract_to_dir}/${filename}"; then
	n=2
	while test -e "${extract_to_dir}/${filename} (${n})"; do
	  n=`expr $n + 1`
	done
	new_filename="${filename} (${n})"
else
	new_filename=$filename
fi

mv -f "$extract_here_dir/$filename" "$extract_to_dir/$new_filename"
rm -rf "$extract_here_dir"
