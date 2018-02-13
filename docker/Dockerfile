FROM ubuntu:xenial
RUN apt-get update
RUN apt-get install -y build-essential
RUN apt-get install -y dialog
RUN apt-get install -y curl 
RUN apt-get install -y libtool 
RUN apt-get install -y autotools-dev 
RUN apt-get install -y autoconf 
RUN apt-get install -y pkg-config 
RUN apt-get install -y libssl-dev 
RUN apt-get install -y libevent-dev 
RUN apt-get install -y libboost-all-dev 
RUN apt-get install -y software-properties-common
RUN add-apt-repository ppa:bitcoin/bitcoin
RUN apt-get update
RUN apt-get -y install bsdmainutils
RUN apt-get -y install apt-utils
RUN apt-get update  

COPY PIVX-3.0 /var/divi/PIVX-3.0
COPY loadDep.sh  /var/divi/loadDep.sh
RUN chmod +x  /var/divi/loadDep.sh
COPY pivxmake.sh  /var/divi/pivxmake.sh
RUN chmod +x  /var/divi/pivxmake.sh

# CMD /var/divi/loadDep.sh
