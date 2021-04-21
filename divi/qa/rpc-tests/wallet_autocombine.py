#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Copyright (c) 2021 The Divi Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework import BitcoinTestFramework
from util import *
import decimal

class WalletAutocombine (BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=["-debug","-combinethreshold=2000"]))
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False
        self.sender = self.nodes[0]
        self.receiver = self.nodes[1]

    def mint_blocks_for_autocombine(self):
        self.sender.setgenerate(True, 1)
        self.sync_all()
        self.sender.setgenerate(True, 1)
        self.sync_all()

    def autocombine_respects_limit(self):
        addr = self.receiver.getnewaddress()
        self.sender.sendtoaddress(addr, 1000.0)
        self.sender.sendtoaddress(addr, 1000.0)
        self.sender.sendtoaddress(addr, 1000.0)
        self.sender.sendtoaddress(addr, 1000.0)
        self.sender.sendtoaddress(addr, 4000.0)
        self.mint_blocks_for_autocombine()
        utxos = self.receiver.listunspent()
        assert_equal(len(utxos),2)

    def autocombine_pay_small_fee(self):
        addr = self.receiver.getnewaddress()
        balance_before = self.receiver.getbalance()
        self.sender.sendtoaddress(addr, 100.0)
        self.sender.sendtoaddress(addr, 1.0)
        self.mint_blocks_for_autocombine()
        balance_after = self.receiver.getbalance()
        assert_greater_than(balance_after,balance_before + decimal.Decimal(100.98))

    def run_test(self):
        self.sender.setgenerate(True, 40)
        self.sync_all()
        self.autocombine_respects_limit()
        self.autocombine_pay_small_fee()


if __name__ == '__main__':
    WalletAutocombine().main ()
