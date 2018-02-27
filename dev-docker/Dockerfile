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
COPY gitclone.sh /home/ubuntu/gitclone.sh
COPY genconf.sh /home/ubuntu/genconf.sh
COPY runbuild.sh /home/ubuntu/runbuild.sh

# Always starts in /shared for some reason, we can't put scripts there and pull repo into that so:
# cd /home/ubuntu

# In the container's command prompt execute gitclone.sh with parameters
#                   BRANCH GIT_URI
# EX: ./gitclone.sh master https://github.com/Divicoin/Divi.git

# After the clone run the build:
#                   FOLDER
# EX: ./runbuild.sh pivx
# EX: ./runbuild.sh divi

# run "divxd -debug" or "pivxd -debug" or "mkdir /home/ubuntu/.pivx" or "mkdir /home/ubuntu/.divx"
# Generate the conf file for this node:
# 				 FOLDER RPCUSER RPCPASSWORD
# EX: ./genconf.sh divx diviABC ABCD1234EFGH5678IJKL90ABCD1234EFGH5678IJKL90


