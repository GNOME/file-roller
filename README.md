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
* Apple Disk Image (.dmg) [Read-only mode]
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
    * gzip (.tar.gz, .tgz)
    * brotli (.tar.br)
    * bzip (.tar.bz, .tbz)
    * bzip2 (.tar.bz2, .tbz2)
    * bzip3 (.tar.bz3, .tbz3)
    * compress (.tar.Z, .taz)
    * lrzip (.tar.lrz, .tlrz)
    * lzip (.tar.lz, .tlz)
    * lzop (.tar.lzo, .tzo)
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
  bzip2 (.bz2), bzip3 (.bz3), compress (.Z), lrzip (.lrz), lzip (.lz),
  lzop (.lzo), rzip(.rz), and xz (.xz), Zstandard (.zst).

## Useful links

* Homepage: https://gitlab.gnome.org/GNOME/file-roller/
* Report issues: https://gitlab.gnome.org/GNOME/file-roller/issues/
* Discuss: https://discourse.gnome.org/tags/c/applications/7/file-roller
* Donate: https://www.gnome.org/donate/
* Translate: https://l10n.gnome.org/module/file-roller/

## Dependencies

In order to build this program from the source code you need a working
GNOME environment, with the development tools installed.

In addition to

* C compiler such as gcc
* [Meson](https://mesonbuild.com/)
* [ninja](https://ninja-build.org/)
* pkg-config

you need the following libraries (including their development files):

* glib ≥ 2.38
* gtk ≥ 3.22.0
* libhandy ≥1.5.0
* libportal ≥ 0.5
* libportal-gtk3 ≥ 0.5
* libnautilus-extension-4 ≥ 43.beta (optional for Nautilus extension)
* libarchive ≥ 3.1.900a (optional format support)
* json-glib-1.0 ≥ 0.14.0 (optional for unarchiver back-end support)

Alternately, with [Nix package manager](https://nixos.org/nix/), you can just run `nix-shell` in the project directory, and it will drop you into a shell with all the required dependencies.

## Building locally

1. Clone this repository and enter the project directory:

    ```bash
    git clone git@ssh.gitlab.gnome.org:GNOME/file-roller.git
    cd file-roller
    ```

2. Configure the build. You can set the prefix, relative to which file-roller would be installed, as well as other build flags described in `meson_options.txt` file:

    ```bash
    meson setup _build --prefix=/usr/local
    ```

3. Build the program:

    ```bash
    ninja -C _build
    ```

Then you can either start the built program from the build directory:

```bash
meson devenv -C _build/ file-roller
```

or install it to the previously configured prefix:

```bash
meson install -C _build
```

## Licensing

This program is released under the terms of the GNU General Public
License (GNU GPL) version 2 or greater.
You can find a copy of the license in the file COPYING.
