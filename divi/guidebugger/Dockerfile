FROM ubuntu:xenial
MAINTAINER Mark Waser <mark@artificialgeneralintelligenceinc.com>
WORKDIR /shared
RUN apt-get update && \
apt-get install -yq software-properties-common && \
add-apt-repository ppa:bitcoin/bitcoin 

RUN apt-get update && \
apt-get install -yq --no-install-recommends \
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

RUN apt-get install -y lxde xauth openssh-server
RUN wget --output-document=/home/ubuntu/vscode.deb https://go.microsoft.com/fwlink/?LinkID=760868 
RUN dpkg -i /home/ubuntu/vscode.deb
RUN apt-get install -f

USER ubuntu
COPY cb.sh /home/ubuntu/cb.sh
RUN sudo dos2unix /home/ubuntu/cb.sh 
RUN sudo chmod +x /home/ubuntu/cb.sh

COPY pbr.sh /home/ubuntu/pbr.sh
RUN sudo dos2unix /home/ubuntu/pbr.sh
RUN sudo chmod +x /home/ubuntu/pbr.sh

COPY init.sh /usr/local/bin/init.sh
RUN sudo dos2unix /usr/local/bin/init.sh
RUN sudo chmod u+x /usr/local/bin/init.sh

COPY divi.conf /home/ubuntu/divi.conf
RUN sudo dos2unix /home/ubuntu/divi.conf

COPY torrc /etc/tor/torrc
RUN sudo dos2unix /etc/tor/torrc

COPY launch.json /home/ubuntu/launch.json
RUN sudo dos2unix /home/ubuntu/launch.json

COPY c_cpp_properties.json /home/ubuntu/c_cpp_properties.json
RUN sudo dos2unix /home/ubuntu/c_cpp_properties.json

RUN sudo mkdir /var/run/sshd
COPY ssh_config /etc/ssh/ssh_config
RUN sudo dos2unix /etc/ssh/ssh_config
COPY guisshd_config /etc/ssh/sshd_config
RUN sudo dos2unix /etc/ssh/sshd_config

ENV GITBRANCH caldwell
ENV GITURI https://raropw5qsohn7txdy3aug3wmgjwvbyxgdsckh3asjym6u5nzwjhq@agii.visualstudio.com/_git/DiviProject

# docker run -p 51472:51472 -p 22:22 --detach -it dcaldwellsw/divi:guidebugger
# docker exec -it [containerid] /bin/bash 
# /home/ubuntu/cb.sh


