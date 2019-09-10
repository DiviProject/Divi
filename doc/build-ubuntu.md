### Ubuntu Configuration

First install the official version of Berkeley DB via the Bitcoin PPA Repository.

```bash
sudo add-apt-repository ppa:bitcoin/bitcoin
sudo apt-get update
sudo apt-get install libdb4.8-dev -y
sudo apt-get install libdb4.8++-dev -y
```

Then install the required dependencies.

```bash
sudo apt-get install make gcc g++ pkg-config autoconf libtool libboost-all-dev libssl-dev libevent-dev -y
```

You can also install each dependency manually.

```bash
sudo apt-get install pkg-config -y
sudo apt-get install autoconf -y
sudo apt-get install libtool -y
sudo apt-get install make -y
sudo apt-get install gcc -y
sudo apt-get install g++ -y
sudo apt-get install libboost-all-dev -y
sudo apt-get install libssl-dev -y
sudo apt-get install libevent-dev -y
```

Now you configure and compile with `make`

```bash
./autogen.sh
./configure
make
```

Once that's doen you should have the `divid` daemon runnable on `./src/divid`.
