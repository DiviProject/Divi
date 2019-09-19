# Guide to building a BTC full node on an Ubuntu 18.04.3 LTS droplet
## (0) Create a droplet with an external volume having at least 250G

## (1) Ssh into the droplet

## (2) Mount the volume (Only the first time) [Instructions should be taken directly from Digital Ocean; This is an example]
### Create a mount point for your volume:
```mkdir -p /mnt/volume_tor1_01```

### Mount your volume at the newly-created mount point:
```mount -o discard,defaults,noatime /dev/disk/by-id/scsi-0DO_Volume_volume-tor1-01 /mnt/volume_tor1_01```

### Change fstab so the volume will be mounted after a reboot
```echo '/dev/disk/by-id/scsi-0DO_Volume_volume-tor1-01 /mnt/volume_tor1_01 ext4 defaults,nofail,discard 0 0' | sudo tee -a /etc/fstab```

## (3) Install docker (if building a node based of a docker image)
```snap install docker```
// If you're logged in as root, don't bother trying to do the usual adding of docker to the commands
// that are by default executable as super user -- it will cause you tremendous pain

## (4) Download repository and checkout the branch containing the Dockerfile building the node, or alternatively download the repo of interest and build according to the instructions there
```git clone https://github.com/ChrisCates/Divi.git && \
cd Divi &&\
git checkout @ChrisCates/AtomicSwaps```


## (5) Build docker image
```docker build -t [image_name] -f /path/to/Dockerfile .```

## (6) Run the image on the mounted volume
```docker run -d --mount type=bind,source=/abs/path/to/mounted/volume,target=[/abs/path/directory/on/container] [image_name]```

## (7) Launch the daemon, and the cli, pointing to the mounted volume
e.g If [/abs/path/directory/on/container] == /external/

From within the docker container (accessible with ```docker exec -ti [CONTAINER_ID] bash ``` and safely exiting with ctrl+p,ctrl+q) you can set the droplet downloading blockchain data with:

```btc_init="/app/bitcoin/src/bitcoind -datadir=/external/"```

```btc="/app/bitcoin/src/bitcoin-cli -datadir=/external/"```

```$btc_init --daemon```