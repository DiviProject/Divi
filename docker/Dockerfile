FROM ubuntu:latest

RUN apt-get update && apt-get upgrade -y

RUN apt-get -y install bsdmainutils\
	apt-utils

RUN apt-get install -y build-essential \
  curl \
  libtool \
  autotools-dev \
  autoconf \
  pkg-config \
  libssl-dev \
  libevent-dev \
  libboost-all-dev 
  
RUN apt-get install -y \
  software-properties-common
  
RUN add-apt-repository ppa:bitcoin/bitcoin

RUN apt-get update
  
COPY PIVX-3.0 /var/divi/PIVX-3.0

COPY loadDep.sh  /var/divi/loadDep.sh

RUN chmod +x  /var/divi/loadDep.sh

COPY pivxmake.sh  /var/divi/pivxmake.sh

RUN chmod +x  /var/divi/pivxmake.sh

CMD /var/divi/loadDep.sh
