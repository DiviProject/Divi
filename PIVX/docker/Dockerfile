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
rsync && \
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

# FOR DEVELOPERS ONLY -- EDIT this to your development git location
ENV GITPATH github.com/Divicoin/Divi

RUN echo 'git clone -b "master" --depth 1 "https://$GITPATH" /shared' > /home/ubuntu/gitclone.sh
RUN chmod +x /home/ubuntu/gitclone.sh
RUN /bin/bash /home/ubuntu/gitclone.sh

CMD /bin/bash

# I was doing this part when I was doing the unix build, probably why my first ones were 3+GB.
# It seems to work without this, but when I ran the test_pivx there were 3 errors I think are related to the Genesis Block.
  # cd /shared/vso/pivx/depends
  # sudo make
  # sudo make install

# Byrd: here is where you've been starting...
# cd /shared/vso/pivx/
# ./autogen.sh
# ./configure --disable-zerocoin --without-gui --enable-debug
# sudo make
# sudo make install

# cd /home/ubuntu
# pivxd -debug 
# 
# Fix pivx.conf and run again
# pivxd -debug

