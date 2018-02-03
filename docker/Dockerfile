
FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
  software-properties-common
  
RUN apt-get update && apt-get install -y \
  build-essential \
  curl \
  libtool \
  autotools-dev \
  autoconf \
  pkg-config \
  libssl-dev \
  libevent-dev \
  libboost-all-dev 
  
RUN add-apt-repository ppa:bitcoin/bitcoin
  
RUN apt-get update

COPY PIVX-3.0.tgz /var/divi/PIVX-3.0.tgz

COPY pivxmake.sh  /var/divi/pivxmake.sh

RUN chmod +x  /var/divi/pivxmake.sh

# CMD /var/divi/pivxmake.sh
