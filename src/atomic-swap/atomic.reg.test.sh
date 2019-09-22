#!/bin/bash

### Atomic test cases

## Divi Regression Testing
echo "Starting divi daemon on port 18444 and rpcport 51473"
../divid -server -rest -regtest -rpcuser=divi_regtest -rpcpassword=divi_regtest -port=18444 -rpcport=51473 -daemon
sleep 2.5

echo "Testing to make sure divi-cli works"
../divi-cli -regtest -rpcuser=divi_regtest -rpcpassword=divi_regtest -rpcport=51473 getblockcount

echo "Create new divi address"
DIVI_ADDRESS=$(../divi-cli -regtest -rpcuser=divi_regtest -rpcpassword=divi_regtest -rpcport=51473 getnewaddress "" "legacy")

echo "Mine 125 divi blocks"
../divi-cli -regtest -rpcuser=divi_regtest -rpcpassword=divi_regtest -rpcport=51473 generatetoaddress 125 $DIVI_ADDRESS

echo "Build the binary for divi atomic swaps"
cd divi
go build
cd ..

echo "Initiate atomic swap for Divi"
./divi/divi -regtest -s 127.0.0.1:51473 --rpcuser=divi_regtest --rpcpass=divi_regtest initiate $DIVI_ADDRESS 1.0

## Bitcoin Regression Testing
echo "Starting bitcoin daemon on port 18445 rpcport 51374"
../../bitcoin/src/bitcoind -server -rest -regtest -rpcuser=btc_regtest -rpcpassword=btc_regtest -port=18445 -rpcport=51374 -daemon
sleep 2.5

echo "Testing to make sure bitcoin-cli works"
../../bitcoin/src/bitcoin-cli -regtest -rpcuser=btc_regtest -rpcpassword=btc_regtest -rpcport=51374 getblockcount

echo "Create new bitcoin address"
BITCOIN_ADDRESS=$(../../bitcoin/src/bitcoin-cli -regtest -rpcuser=btc_regtest -rpcpassword=btc_regtest -rpcport=51374 getnewaddress "" "legacy")

echo "Mine 25 bitcoin blocks"
../../bitcoin/src/bitcoin-cli -regtest -rpcuser=btc_regtest -rpcpassword=btc_regtest -rpcport=51374 generatetoaddress 125 $BITCOIN_ADDRESS

echo "Get address balance"
../../bitcoin/src/bitcoin-cli -regtest -rpcuser=btc_regtest -rpcpassword=btc_regtest -rpcport=51374 getreceivedbyaddress $BITCOIN_ADDRESS

echo "Build the binary for bitcoin atomic swaps"
cd btc
go build
cd ..

echo "Initiate atomic swap for BTC"
./btc/btc -regtest -s 127.0.0.1:51374 --rpcuser=btc_regtest --rpcpass=btc_regtest initiate $BITCOIN_ADDRESS 1.0
