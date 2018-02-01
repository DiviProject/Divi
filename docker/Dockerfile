
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

COPY PIVX-3.0 /var/divi/PIVX-3.0

COPY bdbBuild.sh /var/divi/PIVX-3.0/bdbBuild.sh

RUN chmod +x /var/divi/PIVX-3.0/bdbBuild.sh

COPY pivxmake.sh  /var/divi/PIVX-3.0/pivxmake.sh

RUN chmod +x  /var/divi/PIVX-3.0/pivxmake.sh

CMD /var/divi/PIVX-3.0/pivxmake.sh

