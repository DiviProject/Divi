#! /bin/bash
git clone -b "$GITBRANCH" --depth 1 "$GITURI" /shared 
cd /shared/divi/
./autogen.sh
./configure --disable-zerocoin --without-gui --enable-debug
make
sudo make install
mkdir .divx
cp ./divx.conf ./.divx/divx.conf
divxd -debug
