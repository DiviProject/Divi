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

chainparams.cpp contains code that configures format of P2PKH, P2SH, private key, ext pub/priv keys. 

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

P2P network port can be changed in chainparams.cpp in `CMainParams` class at line `nDefaultPort = 51472;`

RPC port can be changed in chainparamsbase.cpp in `CBaseMainParams` class at line `nRPCPort = 51473;`

