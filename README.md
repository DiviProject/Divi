# DIVI Project

DIVI Core integration/staging repository
=====================================

We wish to make it as easy to participate in Divi development as possible.  Open source software typically has very high but hidden barriers to entry to each project due to the massive and normally undocumented state space of potential operating systems, pre-loaded packages and other development environment details.  Further, regression testing turns into a nightmare if developers have used even slightly different versions of the same operating system or one of the normally numerous packages – much less different packages or a different operating system.   Life has gotten better with the Gitian builds used to ensure identical certified release packages to combat malware but setting up a Gitian build is still a dark art. 

The advent of lightweight, cross-platform containers make it, not just possible but, easy to develop and run the exact same software across all non-mobile platforms – including both major and minor cloud providers.  DIVI is built and run inside a Docker container so  *anyone* can install the free community version of Docker on their Windows, Mac or Linux machine and immediately build and run the DIVI software.  Development for the iPhone and Android wallets is unfortunately far more difficult due to an ongoing lack of a unified standard – but that is something that we will look at in the future with the increasing availability and sophistication of environments like Ionic or Xamarin. 

To build divi:
1.  Ensure docker is installed on your machine (https://store.docker.com/search?type=edition&offering=community)
2.  Create a directory and copy the divi/docker directory into it
3.  docker build -t [your tag name] .

If you don't want to build divi
1.  Ensure docker is installed on your machine (https://store.docker.com/search?type=edition&offering=community)
2.  docker pull caldwellsw/divi:base

To run divi and connect to the testnet
1.  docker run -it -p51472:51472 [your tag name or caldwellsw/divi:base]
2.  divid -debug
3.  divi-cli addnode dt01.westus.cloudapp.azure.com add
4.  divi-cli addnode dt02.westus.cloudapp.azure.com add
5.  divi-cli getpeerinfo
  It may take some time before you see other nodes in the testnet.


