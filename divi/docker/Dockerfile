FROM ubuntu:xenial
MAINTAINER Mark Waser <mark@artificialgeneralintelligenceinc.com>
ENV DEBIAN_FRONTEND noninteractive
WORKDIR /shared
RUN apt-get update && \
apt-get --no-install-recommends -yq install software-properties-common && \
add-apt-repository ppa:bitcoin/bitcoin 
RUN apt-get update && \
apt-get --no-install-recommends -yq install \
wget \
libdb4.8-dev \
libdb4.8++-dev \
libboost-all-dev \
libssl-dev \
libevent-dev \
locales \
git-core \
build-essential \
ca-certificates \
autoconf \
automake \
pkg-config \
libtool \
autotools-dev \
bsdmainutils \
vim \
ruby \
sudo \
rsync \
dos2unix \
tor \
net-tools && \
apt-get -yq purge grub > /dev/null 2>&1 || true && \
apt-get -y dist-upgrade && \
locale-gen en_US.UTF-8 && \
update-locale LANG=en_US.UTF-8 && \
useradd -d /home/ubuntu -m -s /bin/bash ubuntu && echo "ubuntu:ubuntu" | chpasswd  && adduser ubuntu sudo && \
chown -R ubuntu.ubuntu /shared/ && \
echo 'ubuntu ALL=(root) NOPASSWD: ALL' > /etc/sudoers.d/ubuntu && \
chown root.root /etc/sudoers.d/ubuntu && \
chmod 0400 /etc/sudoers.d/ubuntu && \
chown -R ubuntu.ubuntu /home/ubuntu
USER ubuntu
COPY cbr.sh /home/ubuntu/cbr.sh
COPY pbr.sh /home/ubuntu/pbr.sh
COPY divi.conf /home/ubuntu/divi.conf
COPY torrc /etc/tor/torrc
RUN sudo dos2unix /home/ubuntu/cbr.sh 
RUN sudo dos2unix /home/ubuntu/pbr.sh
RUN sudo dos2unix /home/ubuntu/divi.conf
RUN sudo dos2unix /etc/tor/torrc
RUN sudo /etc/init.d/tor start
ENV GITBRANCH master
ENV GITURI https://github.com/Divicoin/Divi
RUN /home/ubuntu/cbr.sh
