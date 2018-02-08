FROM ubuntu:xenial
RUN apt-get update
# RUN apt-get install -y dialog
# RUN apt-get install -y apt-utils
RUN apt-get install -y bsdmainutils
RUN apt-get install -y build-essential
RUN apt-get install -y libtool
RUN apt-get install -y autotools-dev
RUN apt-get install -y autoconf
RUN apt-get install -y pkg-config
RUN apt-get install -y libssl-dev
RUN apt-get install -y libevent-dev
RUN apt-get install -y software-properties-common
RUN apt-get install -y dos2unix
RUN add-apt-repository ppa:bitcoin/bitcoin
RUN apt-get update
RUN apt-get -qq -y install curl
RUN apt-get install -y git
RUN apt-get install -y miniupnpc
RUN apt-get install -y gettext
RUN apt-get install -y vim
RUN apt-get install -y wget
RUN mkdir /var/divi
RUN mkdir /var/divi/PIVX
RUN apt-get update
RUN apt-get install -qq -y libdb4.8-dev libdb4.8++-dev
RUN apt-get install -qq -y libboost-all-dev
COPY bdbbuild.sh /var/divi/PIVX
RUN chmod +x  /var/divi/PIVX/bdbbuild.sh

# run these after the rest of the build completes.

# Windows only
# dos2unix /var/divi/bdbbuild.sh

# All...
# git clone -b 3.0 "https://github.com/PIVX-Project/PIVX.git"

# image is taken here
# cd /var/divi/PIVX
# ./autogen.sh
# cd ../
# ./bdbbuild.sh
# cd PIVX
# make
# make install
