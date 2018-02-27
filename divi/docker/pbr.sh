#! /bin/bash
git pull 
cd /shared/divi/
./autogen.sh
./configure --disable-zerocoin --without-gui --enable-debug
make
sudo make install
divxd -debug
