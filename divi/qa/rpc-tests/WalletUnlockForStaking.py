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

class WalletUnlockForStakingTest (BitcoinTestFramework):

    def setup_network(self):
        self.nodes = start_nodes(1, self.options.tmpdir)

    def run_test(self):
        ## Encrypt wallet and wait to terminate
        self.nodes[0].setgenerate(25)
        wallet_info = self.nodes[0].getwalletinfo()
        assert_equal(wallet_info["encryption_status"],"unencrypted")
        self.nodes[0].encryptwallet('test')
        #self.nodes[0] = None
        #wait_bitcoinds()

        ## Restart nodes
        #self.setup_network()
        node = self.nodes[0]

        ## Check wallet status is locked
        wallet_info = node.getwalletinfo()
        assert_equal(wallet_info["encryption_status"],"locked")

        ## Check wallet status is unlocked after unlock
        node.walletpassphrase('test', 12000)
        wallet_info = node.getwalletinfo()
        assert_equal(wallet_info["encryption_status"],"unlocked")

        ## Check wallet status is locked after re-lock
        node.walletlock()
        wallet_info = node.getwalletinfo()
        assert_equal(wallet_info["encryption_status"],"locked")

        ## Check wallet status is unlocked for staking
        node.walletpassphrase('test', 12000, True)
        wallet_info = node.getwalletinfo()
        assert_equal(wallet_info["encryption_status"],"unlocked-for-staking")

        ## Check wallet status goes from unlocked-for-staking to unlocked-for-staking
        wallet_info_before = node.getwalletinfo()
        assert_equal(wallet_info_before["encryption_status"],"unlocked-for-staking")
        node.walletpassphrase('test', 5)
        wallet_info_before_unlock = node.getwalletinfo()
        assert_equal(wallet_info_before_unlock["encryption_status"],"unlocked")
        time.sleep(6.0)
        wallet_info_after = node.getwalletinfo()
        assert_equal(wallet_info_after["encryption_status"],"unlocked-for-staking")


if __name__ == '__main__':
    WalletUnlockForStakingTest ().main ()
