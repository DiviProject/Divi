#! /bin/bash
PRESENTDIR=$(pwd)
cd /shared
# attempt to pull and then rebuild...
if [ -d /shared/divi ] ; then
	PULLRESLT=$(git pull)
	if [[ ($PULLRESLT==0 || $PULLRESLT=="Already up-to-date.")]] ; then
  		cd /shared/divi/
		#./autogen.sh
  		#./configure --disable-zerocoin --without-gui --enable-debug
  		make
  		sudo make install
  		divid -debug
  		cd $PRESENTDIR
	else 
		echo "git pull ERROR:$PULLRESLT"
	fi
else 
	echo "There is no /shared/divi folder, do you need to run cbr.sh instead?"
fi
