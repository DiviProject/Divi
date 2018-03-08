
Debian
====================
This directory contains files used to package divid/divi-qt
for Debian-based Linux systems. If you compile divid/divi-qt yourself, there are some useful files here.

## divi: URI support ##


divi-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install divi-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your diviqt binary to `/usr/bin`
and the `../../share/pixmaps/divi128.png` to `/usr/share/pixmaps`

divi-qt.protocol (KDE)

