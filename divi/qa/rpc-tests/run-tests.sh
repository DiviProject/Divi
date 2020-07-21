#!/bin/sh

# This is a convenience script that runs all regtests that
# are supposed to work for Divi.  It is essentially a "variant" of
# qa/pull-tester/rpc-tests.sh that is not meant to be run automatically
# but just manually during development.

set -ex

./listtransactions.py
./wallet.py
