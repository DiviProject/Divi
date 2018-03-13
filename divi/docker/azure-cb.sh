#! /bin/bash
PRESENTDIR=$(pwd)
if [ ! -d /shared/divi ] ; then
	git clone -b "$GITBRANCH" --depth 1 "$GITURI" /shared
	if [ -d /shared/divi ] ; then
  		cd /shared/divi/
  		./autogen.sh
  		./configure --disable-zerocoin --without-gui --enable-debug
  		make
  		make install
  		mkdir /root/.divi
  		cp /root/divi.conf /root/.divi/divi.conf
  		cd $PRESENTDIR
	else
  		echo "There was a problem with the clone"
	fi
else
	echo "/shared is not empty, should you be running pbr.sh?"	
fi
