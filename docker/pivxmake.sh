#! /usr/bin/env bash

# echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
# echo "Starting Build Process -------------------------------------------------"
# echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
# apt-get -y install libdb4.8-dev libdb4.8++-dev
# apt-get -y install bsdmainutils
# apt-get update
# 
# cd /var/divi
# tar xzvf PIVX-3.0.tgz
# 
# echo "MiniUPnPc Build ......"
# echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
# cd /var/divi/PIVX-3.0/miniupnpc-2.0
# make
# make install
# sleep 1
# 
# echo "Berkeley DB Build ......"
# echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
# cd /var/divi/PIVX-3.0
# 
# 
# # Pick some path to install BDB to, here we create a directory within the pivx directory
# BDB_PREFIX="/var/divi/PIVX-3.0/db4"
# mkdir -p $BDB_PREFIX
# 
# # Build the library and install to our prefix
# cd db-4.8.30.NC/build_unix/
# #  Note: Do a static build so that it can be embedded into the exectuable, instead of having to find a .so at runtime
# ../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX
# make install
# 
# cd /var/divi/PIVX-3.0
# #/var/divi/PIVX-3.0/configure LDFLAGS="-L${BDB_PREFIX}/lib/" CPPFLAGS="-I${BDB_PREFIX}/include/"
# 
# sleep 1

cd /var/divi/PIVX-3.0
echo "depends Build ......"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
cd /var/divi/PIVX-3.0/depends
make
make install

sleep 1
echo "PIVX Build ......"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "AUTOGEN  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
cd /var/divi/PIVX-3.0
./autogen.sh

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "CONFIGURE ~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
./configure LDFLAGS="-L${BDB_PREFIX}/lib/" CPPFLAGS="-I${BDB_PREFIX}/include/"

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "MAKE      ~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
make

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "INSTALL    ~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
make install

echo "DONE ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"