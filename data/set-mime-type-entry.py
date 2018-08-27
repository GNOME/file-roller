#!/usr/bin/env python3

import sys
import re

if len(sys.argv) != 3:
    print("Usage: set-mime-type-entry.py MIME_TYPES_FILE DESKTOP_FILE")
    exit(1)

mime_file_name = sys.argv[1]
mime_types = ""
with open(mime_file_name) as mime_file:
    for line in mime_file:
        mime_types += line.strip()

desktop_file_name = sys.argv[2]
with open(desktop_file_name) as desktop_file:
    for line in desktop_file:
        if line == "MimeType=@MIMETYPES@\n":
            line = "MimeType=" + mime_types + "\n"
        print(line, end='')

