#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework import BitcoinTestFramework
from util import *
import shutil

class MultiWalletTest (BitcoinTestFramework):

    def setup_network(self, split=False):
        args = [['-spendzeroconfchange'], ['-spendzeroconfchange'], ['-spendzeroconfchange', '-wallet=mining_wallet.dat']]
        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=args)
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    def mine_blocks(self,block_count):
        self.nodes[2].loadwallet("mining_wallet.dat")
        self.nodes[2].setgenerate(block_count)
        self.nodes[2].loadwallet("non_mining_wallet.dat")

    def run_test (self):
        print ("Mining blocks...")

        self.nodes[0].setgenerate( 2)
        self.sync_all()
        self.nodes[1].setgenerate( 12)
        self.sync_all()
        self.mine_blocks(25)
        self.sync_all()

        assert_equal(self.nodes[0].getbalance(), 2500)
        assert_equal(self.nodes[1].getbalance(), 15000)
        assert_equal(self.nodes[2].getbalance(), 0)

        self.nodes[1].backupwallet(self.options.tmpdir + "/node"+str(0)+"/regtest/wallet_owned_by_node1.dat")
        self.nodes[0].loadwallet("wallet_owned_by_node1.dat")
        assert_equal(self.nodes[0].getbalance(), self.nodes[1].getbalance())

        self.nodes[0].loadwallet("wallet.dat")
        assert_equal(self.nodes[0].getbalance(), 2500)

        self.nodes[0].loadwallet("wallet_owned_by_node1.dat")
        amountToSendFirst = float(random.randint(1,14000))
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), amountToSendFirst)

        self.nodes[0].loadwallet("wallet.dat")
        amountToSendSecond = float(random.randint(1,2000))
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), amountToSendSecond)

        sync_mempools(self.nodes)
        self.mine_blocks(1)
        sync_blocks(self.nodes)

        assert_near(self.nodes[0].getbalance(), 2500.0 - amountToSendSecond,0.001)
        assert_near(self.nodes[1].getbalance(), 15000.0 - amountToSendFirst, 0.001)
        assert_equal(self.nodes[2].getbalance(), amountToSendFirst + amountToSendSecond)


if __name__ == '__main__':
    MultiWalletTest ().main ()
