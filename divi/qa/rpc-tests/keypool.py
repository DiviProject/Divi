#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the wallet keypool, and interaction with wallet encryption/locking

# Add python-bitcoinrpc to module search path:
import os
import sys
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-bitcoinrpc"))

import json
import shutil
import subprocess
import tempfile
import traceback

from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
from test_framework import BitcoinTestFramework
from util import *


def check_array_result(object_array, to_match, expected):
    """
    Pass in array of JSON objects, a dictionary with key/value pairs
    to match against, and another dictionary with expected key/value
    pairs.
    """
    num_matched = 0
    for item in object_array:
        all_match = True
        for key,value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        for key,value in expected.items():
            if item[key] != value:
                raise AssertionError("%s : expected %s=%s"%(str(item), str(key), str(value)))
            num_matched = num_matched+1
    if num_matched == 0:
        raise AssertionError("No objects matched %s"%(str(to_match)))


class KeypoolTest (BitcoinTestFramework):

    def setup_network(self):
        self.nodes = start_nodes(1, self.options.tmpdir)

    def run_test(self):
        # Encrypt wallet and wait to terminate
        self.nodes[0].encryptwallet('test')
        wait_bitcoinds()
        # Restart node 0
        self.setup_network()
        node = self.nodes[0]

        # Keep creating keys
        addr = node.getnewaddress()
        try:
            addr = node.getnewaddress()
            raise AssertionError('Keypool should be exhausted after one address')
        except JSONRPCException,e:
            assert(e.error['code']==-12)

        # put three new keys in the keypool
        node.walletpassphrase('test', 12000)
        node.keypoolrefill(3)
        node.walletlock()

        # There are separate key pools for change addresses and normal ones.
        # Each pot gets three now.
        assert_equal(node.getwalletinfo()["keypoolsize"], 6)

        # drain the keys
        addr = set()
        addr.add(node.getrawchangeaddress())
        addr.add(node.getrawchangeaddress())
        addr.add(node.getrawchangeaddress())
        # assert that three unique addresses were returned
        assert(len(addr) == 3)
        # the next one should fail
        try:
            addr = node.getrawchangeaddress()
            raise AssertionError('Keypool should be exhausted after three addresses')
        except JSONRPCException,e:
            assert(e.error['code']==-12)
        assert_equal(node.getwalletinfo()["keypoolsize"], 3)

        # also drain the normal addresses now
        addr = set()
        addr.add(node.getnewaddress())
        addr.add(node.getnewaddress())
        addr.add(node.getnewaddress())
        assert(len(addr) == 3)
        try:
            addr = node.getnewaddress()
            raise AssertionError('Keypool should be exhausted after three addresses')
        except JSONRPCException,e:
            assert(e.error['code']==-12)
        assert_equal(node.getwalletinfo()["keypoolsize"], 0)


if __name__ == '__main__':
    KeypoolTest ().main ()
