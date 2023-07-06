#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework import BitcoinTestFramework
from util import *

class MultiWalletMiningTest (BitcoinTestFramework):

    def setup_network(self, split=False):
        args = [['-spendzeroconfchange']]
        self.nodes = start_nodes(1, self.options.tmpdir, extra_args=args)
        self.is_network_split=False

    def mine_blocks(self):
        self.nodes[0].loadwallet("mining_wallet0.dat")
        self.nodes[0].setgenerate(25)
        self.nodes[0].loadwallet("mining_wallet1.dat")
        self.nodes[0].setgenerate(25)
        self.nodes[0].loadwallet("mining_wallet2.dat")
        self.nodes[0].setgenerate(25)

    def run_test (self):
        print ("Mining blocks...")
        self.mine_blocks()
        self.nodes[0].loadwallet("mining_wallet0.dat")
        mining_wallet0_info = self.nodes[0].getwalletinfo()
        self.nodes[0].loadwallet("mining_wallet1.dat")
        mining_wallet1_info = self.nodes[0].getwalletinfo()
        expected_balance = Decimal(25*1250.0)
        assert_equal(mining_wallet0_info['balance'], expected_balance)
        assert_equal(mining_wallet1_info['balance'], expected_balance)
        assert_equal(True, mining_wallet0_info['active_wallet']!=mining_wallet1_info['active_wallet'])

if __name__ == '__main__':
    MultiWalletMiningTest ().main ()
