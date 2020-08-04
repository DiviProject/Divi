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

function CreateDataDir {
  DIR=$1
  mkdir -p $DIR
  CONF=$DIR/divi.conf
  echo "regtest=1" >> $CONF
  echo "keypool=2" >> $CONF
  echo "rpcuser=rt" >> $CONF
  echo "rpcpassword=rt" >> $CONF
  echo "rpcwait=1" >> $CONF
  shift
  while (( "$#" )); do
      echo $1 >> $CONF
      shift
  done
}

# CheckBalance -datadir=... amount account minconf
# We check that the balance is within one coin of the expected value,
# to allow for variance due to fees.
function CheckBalance {
  declare -i EXPECT="$2"
  B=$( $CLI $1 getbalance $3 $4 )
  if (( $( echo "($B - $EXPECT < 1) && ($EXPECT - $B < 1)" | bc ) == 0 ))
  then
    echoerr "bad balance: $B (expected $2)"
    declare -f CleanUp > /dev/null 2>&1
    if [[ $? -eq 0 ]] ; then
        CleanUp
    fi
    exit 1
  fi
}
