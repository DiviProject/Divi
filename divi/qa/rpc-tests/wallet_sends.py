#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test ZMQ interface
#

from test_framework import BitcoinTestFramework
from util import *
import decimal

class WalletSends (BitcoinTestFramework):

    def setup_network(self):
        #self.nodes = start_nodes(2, self.options.tmpdir)
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args))
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False

    def ensure_change_output_only_for_positive_change_amounts(self):
        sender = self.nodes[0]
        receiver = self.nodes[1]
        addr = receiver.getnewaddress()
        sender.setgenerate(True, 30)
        self.sync_all()
        sender.sendtoaddress(addr, 5000.0)
        sender.setgenerate(True, 1)
        self.sync_all()
        receiver.sendtoaddress(sender.getnewaddress(),receiver.getbalance()-decimal.Decimal(0.499950) )
        self.sync_all()

    def run_test(self):
        self.ensure_change_output_only_for_positive_change_amounts()


if __name__ == '__main__':
    WalletSends().main ()
