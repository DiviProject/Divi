
Debian
====================
This directory contains files used to package divxd/divx-qt
for Debian-based Linux systems. If you compile divxd/divx-qt yourself, there are some useful files here.

## divx: URI support ##


divx-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install divx-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your divxqt binary to `/usr/bin`
and the `../../share/pixmaps/divx128.png` to `/usr/share/pixmaps`

divx-qt.protocol (KDE)

