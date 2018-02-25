DIVX Core
=====================

Setup
---------------------
[DIVX Core](http://divx.org/wallet) is the original DIVX client and it builds the backbone of the network. However, it downloads and stores the entire history of DIVX transactions; depending on the speed of your computer and network connection, the synchronization process can take anywhere from a few hours to a day or more. Thankfully you only have to do this once.

Running
---------------------
The following are some helpful notes on how to run DIVX on your native platform.

### Unix

Unpack the files into a directory and run:

- bin/32/divx-qt (GUI, 32-bit) or bin/32/divxd (headless, 32-bit)
- bin/64/divx-qt (GUI, 64-bit) or bin/64/divxd (headless, 64-bit)

### Windows

Unpack the files into a directory, and then run divx-qt.exe.

### OSX

Drag DIVX-Qt to your applications folder, and then run DIVX-Qt.

### Need Help?

* See the documentation at the [DIVX Wiki](https://en.bitcoin.it/wiki/Main_Page) ***TODO***
for help and more information.
* Ask for help on [BitcoinTalk](https://bitcointalk.org/index.php?topic=1262920.0) or on the [DIVX Forum](http://forum.divx.org/).
* Join one of our Slack groups [DIVX Slack Groups](https://divx.org/slack-logins/).

Building
---------------------
The following are developer notes on how to build DIVX on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OSX Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The Divx repo's [root README](https://github.com/Divi-Project/Divi/blob/master/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Multiwallet Qt Development](multiwallet-qt.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Source Code Documentation (External Link)](https://dev.visucore.com/bitcoin/doxygen/) ***TODO***
- [Translation Process](translation_process.md)
- [Unit Tests](unit-tests.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Dnsseed Policy](dnsseed-policy.md)

### Resources

* Discuss on the [BitcoinTalk](https://bitcointalk.org/index.php?topic=1262920.0) or the [DIVX](http://forum.divx.org/) forum.
* Join the [DIVX-Dev](https://divx-dev.slack.com/) Slack group ([Sign-Up](https://divx-dev.herokuapp.com/)).

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)

License
---------------------
Distributed under the [MIT/X11 software license](http://www.opensource.org/licenses/mit-license.php).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
