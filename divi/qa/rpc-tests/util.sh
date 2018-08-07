#!/usr/bin/env bash
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2014-2015 The Dash developers
# Copyright (c) 2015-2017 The PIVX Developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Functions used by more than one test

function echoerr {
  echo "$@" 1>&2;
}

# Usage: ExtractKey <key> "<json_object_string>"
# Warning: this will only work for the very-well-behaved
# JSON produced by divid, do NOT use it to try to
# parse arbitrary/nested/etc JSON.
function ExtractKey {
    echo $2 | tr -d ' "{}\n' | awk -v RS=',' -F: "\$1 ~ /$1/ { print \$2}"
}

function CreateDataDir {
  DIR=$1
  mkdir -p $DIR
  CONF=$DIR/divi.conf
  echo "regtest=1" >> $CONF
  echo "keypool=2" >> $CONF
  echo "rpcuser=rt" >> $CONF
  echo "rpcpassword=rt" >> $CONF
  echo "rpcwait=1" >> $CONF
  echo "walletnotify=${SENDANDWAIT} -STOP" >> $CONF
  shift
  while (( "$#" )); do
      echo $1 >> $CONF
      shift
  done
}

function AssertEqual {
  if (( $( echo "$1 == $2" | bc ) == 0 ))
  then
    echoerr "AssertEqual: $1 != $2"
    declare -f CleanUp > /dev/null 2>&1
    if [[ $? -eq 0 ]] ; then
        CleanUp
    fi
    exit 1
  fi
}

# CheckBalance -datadir=... amount account minconf
function CheckBalance {
  declare -i EXPECT="$2"
  B=$( $CLI $1 getbalance $3 $4 )
  if (( $( echo "$B == $EXPECT" | bc ) == 0 ))
  then
    echoerr "bad balance: $B (expected $2)"
    declare -f CleanUp > /dev/null 2>&1
    if [[ $? -eq 0 ]] ; then
        CleanUp
    fi
    exit 1
  fi
}

# Use: Address <datadir> [account]
function Address {
  $CLI $1 getnewaddress $2
}

# Send from to amount
function Send {
  from=$1
  to=$2
  amount=$3
  address=$(Address $to)
  txid=$( ${SENDANDWAIT} $CLI $from sendtoaddress $address $amount )
}

# Use: Unspent <datadir> <n'th-last-unspent> <var>
function Unspent {
  local r=$( $CLI $1 listunspent | awk -F'[ |:,"]+' "\$2 ~ /$3/ { print \$3 }" | tail -n $2 | head -n 1)
  echo $r
}

# Use: CreateTxn1 <datadir> <n'th-last-unspent> <destaddress>
# produces hex from signrawtransaction
function CreateTxn1 {
  TXID=$(Unspent $1 $2 txid)
  AMOUNT=$(Unspent $1 $2 amount)
  VOUT=$(Unspent $1 $2 vout)
  RAWTXN=$( $CLI $1 createrawtransaction "[{\"txid\":\"$TXID\",\"vout\":$VOUT}]" "{\"$3\":$AMOUNT}")
  ExtractKey hex "$( $CLI $1 signrawtransaction $RAWTXN )"
}

# Use: SendRawTxn <datadir> <hex_txn_data>
function SendRawTxn {
  ${SENDANDWAIT} $CLI $1 sendrawtransaction $2
}

# Use: GetBlocks <datadir>
# returns number of blocks from getinfo
function GetBlocks {
    $CLI $1 getblockcount
}
