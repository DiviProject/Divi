#! /bin/bash

cd /shared/$1/
./autogen.sh
./configure --disable-zerocoin --without-gui --enable-debug
make
sudo make install
