#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework import BitcoinTestFramework
from util import *

class WalletAccounts (BitcoinTestFramework):

    def setup_network(self, split=False):
        args = [['-spendzeroconfchange','-keypool=10']]*2
        self.nodes = start_nodes(2, self.options.tmpdir, extra_args=args)
        connect_nodes(self.nodes[0],1)
        self.is_network_split=False

    def mine_blocks(self):
        print ("Mining blocks...")
        self.nodes[0].loadwallet("mining_wallet0.dat")
        self.nodes[0].setgenerate(25)

    def mine_tx(self):
        sync_mempools(self.nodes)
        self.nodes[0].setgenerate(1)
        sync_blocks(self.nodes)

    def run_test (self):
        self.mine_blocks()
        sync_blocks(self.nodes)
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        addr1 = node1.getnewaddress()
        addr2 = node1.getnewaddress()
        acc1 = "alloc->blasphemy"
        acc2 = "alloc->boundless_blasphemy"
        node1.setaccount(addr1,acc1)
        node1.setaccount(addr2,acc2)

        node0.sendtoaddress(addr1,1000)
        self.mine_tx()
        acc_balances = node1.listaccounts()
        print("accounts (step 1) {} {}".format(acc_balances,node1.getwalletinfo()))

        assert_equal(acc_balances[acc1], 1000.0)
        node1.sendtoaddress(addr2,999.0)

        self.mine_tx()
        acc_balances = node1.listaccounts()
        print("accounts (step 2) {}".format(acc_balances))

        assert_near(acc_balances[acc1], Decimal(0.0),Decimal(0.01))
        assert_near(acc_balances[''], Decimal(1.0),Decimal(0.01))
        assert_equal(999.0,acc_balances[acc2])
        node1.sendtoaddress(addr2,500.0)
        self.mine_tx()
        acc_balances = node1.listaccounts()
        print("accounts (step 3) {}".format(acc_balances))
        assert_near(acc_balances[''], Decimal(500.0),Decimal(0.01))
        assert_near(acc_balances[acc1], Decimal(0.0),Decimal(0.01))
        assert_equal(500.0,acc_balances[acc2])

        node1.sendtoaddress(addr2,400.0,"sweep_funds")
        self.mine_tx()
        acc_balances = node1.listaccounts()
        assert_near(acc_balances[''], Decimal(0.0),Decimal(0.01))
        assert_near(acc_balances[acc1], Decimal(0.0),Decimal(0.01))
        assert_near(acc_balances[acc2], Decimal(1000.0),Decimal(0.01))


if __name__ == '__main__':
    WalletAccounts ().main ()
