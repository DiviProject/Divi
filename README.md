[![Divi Banner](./divi-banner.jpg)](https://diviproject.org)

<center>
	[![License](https://img.shields.io/badge/license-MIT-blue.svg)](./COPYING)
	[![Build Status](https://travis-ci.org/DiviProject/Divi.svg?branch=posv3)](https://travis-ci.org/DiviProject/Divi)
	[![codecov](https://codecov.io/gh/DiviProject/Divi/branch/posv3/graph/badge.svg)](https://codecov.io/gh/DiviProject/Divi)
</center>

## What is Divi?

**Divi is crypto made easy.**

Spend, earn, or withdraw money in just a few taps. Top-up , cash out, earn income, buy a coffee, exchange money – all in a single tap. Because thats how it should be.

Divi is a next-generation blockchain protocol that enables any user to begin earning, sending, and spending cryptocurrency easily, without the need for advanced technical knowledge. Divi uses a state-of-the-art Proof of Stake consensus mechanism that offers opportunities for users to stake their coins or allocate their coins to Masternodes, which secure the network and verify transactions.

For more information, as well as an immediately usable version of the Divi Project software, visit our website's [download page](https://diviproject.org/downloads), or read our [whitepaper](https://wiki.diviproject.org/#whitepaper).

## Build and Compilation

To build from source on UNIX systems, follow these instructions.

### System requirements

C++ compilers are memory-hungry. It is recommended to have at least 1 GB of
memory available when compiling DIVI Core. With 512MB of memory or less
compilation will take much longer due to swap throttling.

### Dependencies

These dependencies are required:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 libssl      | SSL Support      | Secure communications
 libboost    | Boost            | C++ Library

Optional dependencies:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 miniupnpc   | UPnP Support     | Firewall-jumping support
 libdb4.8    | Berkeley DB      | Wallet storage (only needed when wallet enabled)
 qt          | GUI              | GUI toolkit (only needed when GUI enabled)
 protobuf    | Payments in GUI  | Data interchange format used for payment protocol (only needed when GUI enabled)
 libqrencode | QR codes in GUI  | Optional for generating QR codes (only needed when GUI enabled)

For the versions used in the release, see [release-process.md](./divi/doc/release-process.md) under *Fetch and build inputs*.

For additional information about dependencies see [build-unix.md](./divi/doc/build-unix.md)

```bash
./autogen.sh
./configure --disable-tests --without-gui
make
make install # optional
```

You can also compile with Docker see [build-docker.md](./divi/doc/build-docker).

### Documentation

For extensive documentation on the build process go to [wiki.diviproject.org](https://wiki.diviproject.org).

### Issues and Pull Request 

Issues and pull requests open on the repository. Please try to follow our Issue and Pull Request guidelines.