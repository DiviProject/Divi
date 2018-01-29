#! /usr/bin/env bash

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "Starting Build Process -------------------------------------------------"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

echo "MiniUPnPc Build ......"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
cd miniupnpc-2.0
make
sudo make install
sleep 1

echo "Berkeley DB Build ......"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
cd /var/divi/PIVX-3.0
./bdbBuild.sh

sleep 1
echo "depends Build ......"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
cd /var/divi/PIVX-3.0/depends
sudo make
sudo make install

sleep 1
echo "PIVX Build ......"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
cd /var/divi/PIVX-3.0
./autogen.sh
./configure
sudo make
sudo make install

echo "DONE ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"