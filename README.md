# File Roller

An archive manager utility for the GNOME Environment.

## What is it ?

File Roller is an archive manager for the GNOME environment.  This means
that you can create and modify archives; view the content of an archive;
view and modify a file contained in the archive; extract files from the
archive.

File Roller is only a front-end (a graphical interface) to archiving programs
like tar and zip. The supported file types are:

* 7-Zip Compressed File (.7z)
* WinAce Compressed File (.ace)
* ALZip Compressed File (.alz)
* AIX Small Indexed Archive  (.ar)
* ARJ Compressed Archive (.arj)
* Cabinet File (.cab)
* UNIX CPIO Archive (.cpio)
* Debian Linux Package (.deb) [Read-only mode]
* ISO-9660 CD Disc Image (.iso) [Read-only mode]
* Java Archive (.jar)
* Java Enterprise archive (.ear)
* Java Web Archive (.war)
* LHA Archive (.lzh, .lha)
* WinRAR Compressed Archive (.rar)
* RAR Archived Comic Book (.cbr)
* RPM Linux Package (.rpm) [Read-only mode]
* Tape Archive File:
* uncompressed (.tar)
* or compressed with:
  * gzip (.tar.gz , .tgz)
  * brotli (.tar.br)
  * bzip (.tar.bz , .tbz)
  * bzip2 (.tar.bz2 , .tbz2)
  * compress (.tar.Z , .taz)
  * lrzip (.tar.lrz , .tlrz)
  * lzip (.tar.lz , .tlz)
  * lzop (.tar.lzo , .tzo)
  * 7zip (.tar.7z)
  * xz (.tar.xz)
  * Zstandard (.tar.zst, .tzst)
* Snap packages (.snap)
* Squashfs images (.sqsh)
* Stuffit Archives (.bin, .sit)
* ZIP Archive (.zip)
* ZIP Archived Comic Book (.cbz)
* ZOO Compressed Archive File (.zoo)
* Single files compressed with gzip (.gz), brotli (.br), bzip (.bz),
  bzip2 (.bz2), compress (.Z), lrzip (.lrz), lzip (.lz), lzop (.lzo),
  rzip(.rz), and xz (.xz), Zstandard (.zst).

## Useful links

* Homepage: https://wiki.gnome.org/Apps/FileRoller
* Report issues: https://gitlab.gnome.org/GNOME/file-roller/issues/
* Donate: https://www.gnome.org/friends/
* Translate: https://wiki.gnome.org/TranslationProject

## Licensing

This program is released under the terms of the GNU General Public
License (GNU GPL) version 2 or greater.
You can find a copy of the license in the file COPYING.

## Dependencies

In order to build this program from the source code you need a working
GNOME environment version 3.x, with the development tools installed
properly.

Also you need the following libraries:

* glib >= 2.38
* gtk+ >= 3.22.0
* libnautilus-extension >= 3.28.0 (optional)
* libarchive >= 3.1.900a (optional)

## Install

```bash
mkdir build
cd build
meson ..
ninja
ninja install
```
