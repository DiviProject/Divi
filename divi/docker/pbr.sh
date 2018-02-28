#! /bin/bash
PRESENTDIR={pwd}
# attempt to pull and then rebuild...
cd /shared/divi/
if git pull ; then
  ./autogen.sh
  ./configure --disable-zerocoin --without-gui --enable-debug
  make
  sudo make install
  divxd -debug
else 
	echo "Could not pull the selected git branch."
fi
cd $PRESENTDIR