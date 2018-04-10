Mac OS X Build Instructions and Notes
====================================
This guide will show you how to build divid (headless client) for OSX.

Notes
-----

* Tested on OS X 10.13.

* All of the commands should be executed in a Terminal application. The
built-in one is located in `/Applications/Utilities`.

Preparation
-----------

Install XCode 9.x

You can install XCode via the AppStore, you may also want to get a Developer's 
licence from Apple if you want to gain access to the forums, and support tools

You will also need to install [Homebrew](http://brew.sh) in order to install library
dependencies.

The installation of the actual dependencies is covered in the Instructions
sections below.

Instructions: Homebrew
----------------------

#### Install dependencies using Homebrew add 'git' if you don't want 

        brew install git autoconf automake berkeley-db4 libtool boost@1.57 miniupnpc openssl pkg-config protobuf qt5 libzmq

### Building `divid`

1. Clone the github tree to get the source code and go into the directory.

        git clone https://github.com/divicoin/divi.git
        cd divi

2.  Build divid:

        ./autogen.sh
        ./configure LDFLAGS='-L/usr/local/opt/openssl/lib' CPPFLAGS='-I/usr/local/opt/openssl/include' PKG_CONFIG_PATH='/usr/local/opt/openssl/lib/pkgconfig' --with-gui=qt5
        make

3.  It is also a good idea to build and run the unit tests:

        make check

4.  (Optional) You can also install divid to your path:

        make install


Instructions: Error Messages
----------------------------
### If you get an error message about XCode being installed or configured incorrectly:

		sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
		
### You may already have a NEWER version of boost installed on your Mac, and get errors when compiling QT.
		
		brew uninstall boost
		
		brew install boost@1.57
		
#	Make sure boost@1.57 is linked
		
		brew link boost@1.57 --force



Use Qt Creator as IDE
------------------------
You can use Qt Creator as IDE, for debugging and for manipulating forms, etc.
Download Qt Creator from http://www.qt.io/download/. Download the "community edition" and only install Qt Creator (uncheck the rest during the installation process).

1. Make sure you installed everything through homebrew mentioned above
2. Do a proper ./configure --with-gui=qt5 --enable-debug
3. In Qt Creator do "New Project" -> Import Project -> Import Existing Project
4. Enter "divi-qt" as project name, enter src/qt as location
5. Leave the file selection as it is
6. Confirm the "summary page"
7. In the "Projects" tab select "Manage Kits..."
8. Select the default "Desktop" kit and select "Clang (x86 64bit in /usr/bin)" as compiler
9. Select LLDB as debugger (you might need to set the path to your installtion)
10. Start debugging with Qt Creator

Creating a release build
------------------------
You can ignore this section if you are building `divid` for your own use.

divid/divi-cli binaries are not included in the divi-Qt.app bundle.

If you are building `divid` or `divi-qt` for others, your build machine should be set up
as follows for maximum compatibility:

All dependencies should be compiled with these flags:

 -mmacosx-version-min=10.7
 -arch x86_64
 -isysroot $(xcode-select --print-path)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk

Once dependencies are compiled, see release-process.md for how the DIVI-Qt.app
bundle is packaged and signed to create the .dmg disk image that is distributed.

Running
-------

It's now available at `./divid`, provided that you are still in the `src`
directory. We have to first create the RPC configuration file, though.

Run `./divid` to get the filename where it should be put, or just try these
commands:

    echo -e "rpcuser=divirpc\nrpcpassword=$(xxd -l 16 -p /dev/urandom)" > "/Users/${USER}/Library/Application Support/DIVI/divi.conf"
    chmod 600 "/Users/${USER}/Library/Application Support/DIVI/divi.conf"

The next time you run it, it will start downloading the blockchain, but it won't
output anything while it's doing this. This process may take several hours;
you can monitor its process by looking at the debug.log file, like this:

    tail -f $HOME/Library/Application\ Support/DIVI/debug.log

Other commands:
-------

    ./divid -daemon # to start the divi daemon.
    ./divi-cli --help  # for a list of command-line options.
    ./divi-cli help    # When the daemon is running, to get a list of RPC commands
