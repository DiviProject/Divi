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
RUN apt-get install libssl1.0-dev -y
RUN apt-get install libevent-dev -y
RUN apt-get install libdb4.8-dev libdb4.8++-dev -y
RUN apt-get install libzmq3-dev -y
RUN apt-get install python3-zmq -y
RUN pip3 install zmq

WORKDIR /app
COPY . .

WORKDIR /app/src/GMock
RUN autoreconf -fvi

WORKDIR /app
RUN make distclean || true
RUN ./autogen.sh
RUN ./configure --disable-tests --without-gui
RUN make

RUN apt-get -y install sudo
RUN useradd -rm -d /home/ubuntu -s /bin/bash -g root -G sudo -u 1001 ubuntu
RUN echo "ubuntu:ubuntu" | chpasswd && adduser ubuntu sudo
USER ubuntu

CMD ["bash"]
