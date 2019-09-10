FROM ubuntu:bionic

RUN apt-get update
RUN apt-get install apt-utils -y
RUN apt-get install bsdmainutils -y
RUN apt-get install software-properties-common -y
RUN add-apt-repository ppa:bitcoin/bitcoin -y
RUN apt-get update

RUN apt-get install make -y
RUN apt-get install gcc -y
RUN apt-get install g++ -y
RUN apt-get install pkg-config -y
RUN apt-get install autoconf -y
RUN apt-get install libtool -y
RUN apt-get install libboost-all-dev -y
RUN apt-get install libssl-dev -y
RUN apt-get install libevent-dev -y
RUN apt-get install libdb4.8-dev libdb4.8++-dev -y

WORKDIR /app
COPY . .

RUN ./autogen.sh
RUN ./configure
RUN make

CMD ["bash"]
