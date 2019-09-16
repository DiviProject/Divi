FROM ubuntu:bionic

RUN apt-get update
RUN apt-get install apt-utils -y
RUN apt-get install bsdmainutils -y
RUN apt-get install software-properties-common -y
RUN apt-get install golang -y
RUN add-apt-repository ppa:bitcoin/bitcoin -y
RUN add-apt-repository ppa:git-core/ppa

RUN apt-get update
RUN apt-get install git -y
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

# Configure and Compile Divi
RUN ./autogen.sh
RUN ./configure
RUN make

# Configure and Compile Bitcoin
RUN git clone https://github.com/bitcoin/bitcoin --depth 1

WORKDIR /app/bitcoin

RUN ./autogen.sh
RUN ./configure
RUN make

WORKDIR /app/src/atomic-swap
RUN sh config.sh

CMD ["bash"]
