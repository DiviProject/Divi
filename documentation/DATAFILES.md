## Definition

__Data dir__ is a special folder that holds all information regarding blockchain and p2p network.

## Location

Data dir is located in platform specific folder, currently 3 systems are supportred:

- Windows: %AppData%/Roaming/DIVI
- Linux: ~/.divi
- OSX: 

Data dir location can be changed on startup using `-datadir=path/to/dir` startup argument

## Files description

- `blocks`: folder that contains database of blocks that were downloaded from p2p network. 
- `chainstate`: folder that contains database of blocks index which is needed to navigate blocks
- `wallet.dat`: the most important file. Stores private keys for your addresses. Lossing this automatically means lossing access to all of yours divi coins.
- `masternode.conf`: stores configration for masternodes that are controlled by this wallet. This file needs to be filled if users is willing to run a masternode.
- `peers.dat`: database that stores information about peers in p2p network. Used by client to quikly connect to well known peers.
- `mncache.dat`: database that stores information about masternodes that are known to this node. Used to prevent spamming network with masternode requests on each run.
- `debug.log`: debug prints from client. Usually is needed to solve some problem. 
- `divi.conf`: special file which holds startup configuration. Instead of using startup arguments when calling divid, better approach is to put them into divi.conf, line by line. 
