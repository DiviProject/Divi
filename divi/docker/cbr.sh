#! /bin/bash
PRESENTDIR=$(pwd)
# try to run clone
git clone -b "$GITBRANCH" --depth 1 "$GITURI" /shared
cd /shared/divi/
./autogen.sh
./configure --disable-zerocoin --without-gui --enable-debug
make
sudo make install
mkdir /home/ubuntu/.divx
cp /home/ubuntu/divx.conf /home/ubuntu/.divx/divx.conf
divxd -debug
cd $PRESENTDIR