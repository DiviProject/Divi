## Definition

This document describes how to start new network based on Divi code. 

We will breakdown whole process to list of steps.

1. Coin naming
2. Address format
3. P2P and RPC ports
4. Fixed seeds
5. Genesis block
6. PoW -> PoS transition 
7. DNS seeding
8. HD Wallet configuration
9. Changing version

### Coin naming

Code has many mentions of Divi, everything related to names(except license info) needs to be changed. Suggested approach is to find & replace every Divi, DIVI and divi strings except license headers.

### Address format

`chainparams.cpp` contains code that configures format of P2PKH, P2SH, private key, ext pub/priv keys. 

```
base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 30);
base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 13);
base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 212);
base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x02)(0x2D)(0x25)(0x33).convert_to_container<std::vector<unsigned char> >();
base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x02)(0x21)(0x31)(0x2B).convert_to_container<std::vector<unsigned char> >();
// 	BIP44 coin type is from https://github.com/satoshilabs/slips/blob/master/slip-0044.md
base58Prefixes[EXT_COIN_TYPE] = boost::assign::list_of(0x80)(0x00)(0x00)(0x77).convert_to_container<std::vector<unsigned char> >();

```

Change those values to get different format. 

Refer to this table to get relation of byte value to final symbol https://en.bitcoin.it/wiki/List_of_address_prefixes

### P2P and RPC ports

P2P network port can be changed in `chainparams.cpp` in `CMainParams` class at line `nDefaultPort = 51472;`

RPC port can be changed in `chainparamsbase.cpp` in `CBaseMainParams` class at line `nRPCPort = 51473;`

### Fixed seeds

Fixed seeds can be added in `chainparams.cpp` at line `vSeeds.push_back(CDNSSeedData("178.62.195.16", "178.62.195.16"));`

### Genesis block

To start new chain we need to create new genesis block, complete instructions are provided in separate article.

### PoW -> PoS transition

Initially chain starts as PoW and transitions to PoS at block height which is set in `chainparams.cpp` at line `nLastPOWBlock = 100;`. 

To start PoW we need to have at least one connected peer and run `setgenerate true`

For PoS transition we need to have stakeable balance(aged for 1 hour), wallet has to be unlocked and we need to have at least 3 connected peers for masternode network sync. One thing to mention is that if `mnsync status` doesn't change, it remains in some fixed state then you will need to stop daemon, clean `netfulfilled.dat` and `mncache.dat` and start again with 3 peers. Status of staking can be checked using `getstakingstatus`.

### DNS seeding

Seeder itself is located under this repo: https://github.com/Divicoin/divi-seeder, it needs to be forked and core parameters has to be changed, sample of those changes can be checked at github history. 

Address for seeder needs to be hardcoded in `chainparams.cpp` at line `vSeeds.push_back(CDNSSeedData("autoseeds.diviseed.diviproject.org", "autoseeds.diviseed.diviproject.org"));`

### HD Wallet configuration

Divi wallet supports BIP44 to correctly support it we need to provide coin type which is registered here: https://github.com/satoshilabs/slips/blob/master/slip-0044.md

Value in code needs to be changed in `chainparams.cpp` at line `nExtCoinType = 301;`

### Wallet version

Wallet version is set in few places:

1. `configure.ac`
2. `clientversion.h` 

Changing values in those files is sufficient to get new version. 

