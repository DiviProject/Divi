#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test balance categories computation for wallets (unconfirmed -> immature -> confirmed)
#

from test_framework import BitcoinTestFramework
from util import *
from messages import *
from script import *
import codecs
from decimal import Decimal
class WalletBalances (BitcoinTestFramework):

    def setup_network(self):
        #self.nodes = start_nodes(2, self.options.tmpdir)
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args))
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False
        self.sender = self.nodes[0]
        self.receiver = self.nodes[1]

    def run_test(self):
        for blockCount in range(1,25):
            self.sender.setgenerate(1)
            walletinfo = self.sender.getwalletinfo()
            assert_equal(walletinfo["immature_balance"], min(Decimal(blockCount*1250.0), Decimal(25000.0)) )
            assert_equal(walletinfo["unconfirmed_balance"], Decimal(0.0) )
            assert_equal(walletinfo["balance"], Decimal(max(blockCount-20,0)*1250 ))
        
        walletinfo = self.sender.getwalletinfo()
        starting_balance = walletinfo["balance"]
        assert_greater_than(walletinfo["balance"], Decimal(1250.0))
        addr = self.receiver.getnewaddress()
        self.sender.sendtoaddress(addr, 1000.0)
        walletinfo = self.sender.getwalletinfo()
        assert_equal(walletinfo["balance"], starting_balance - Decimal(1250.0))
        assert_greater_than(walletinfo["unconfirmed_balance"],Decimal(249.0))

if __name__ == '__main__':
    WalletBalances().main ()
