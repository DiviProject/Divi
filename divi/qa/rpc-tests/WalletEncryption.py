#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the wallet pre-encryption keys, and interaction with wallet encryption/locking

import json
import shutil
import subprocess
import tempfile
import traceback

from test_framework import BitcoinTestFramework
from util import *

class WalletEncryptionTest (BitcoinTestFramework):

    def setup_network(self):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes(self.nodes[0],1)

    def run_test(self):
        # Encrypt wallet and wait to terminate
        self.nodes[0].setgenerate(25)
        balance_before = self.nodes[0].getbalance()

        self.nodes[0].encryptwallet('test')
        node = self.nodes[0]
        balance_after = self.nodes[0].getbalance()
        assert_equal(balance_before,balance_after)
        amount_to_send = balance_after - Decimal(1.0)
        node.walletpassphrase('test', 12000)
        node.sendtoaddress(self.nodes[1].getnewaddress(), amount_to_send)
        node.setgenerate(1)
        sync_blocks(self.nodes)
        assert_greater_than(node.getbalance(),1250.0)
        assert_greater_than(1251.0,node.getbalance())
        assert_equal(self.nodes[1].getbalance(), amount_to_send)


if __name__ == '__main__':
    WalletEncryptionTest ().main ()
