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

class WalletNotifications (BitcoinTestFramework):

    def setup_network(self):
        #self.nodes = start_nodes(2, self.options.tmpdir)
        self.nodes = []
        self.tx_log_file = self.options.tmpdir+"/"+"tx.log"
        print("Logging txs in {}".format(self.tx_log_file))
        args = ['-debug','-walletnotify=echo %s >> {}'.format(self.tx_log_file)]# In bash, scripts should be passed in single quotes -walletnotify='echo %s'
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=["-debug"]))
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False

    def count_unique_txs_logged(self):
        return len(set (open(self.tx_log_file).readlines() ) )

    def send_some_funds_around(self):
        sender = self.nodes[0]
        receiver = self.nodes[1]
        addr = receiver.getnewaddress()
        sender.setgenerate( 30)
        self.sync_all()
        assert_equal(self.count_unique_txs_logged(),30)
        self.sync_all()
        sender.sendtoaddress(addr, 5000.0)
        self.sync_all()
        assert_equal(self.count_unique_txs_logged(),31)
        sender.setgenerate( 1)
        self.sync_all()
        assert_equal(self.count_unique_txs_logged(),32)
        receiver.sendtoaddress(sender.getnewaddress(),receiver.getbalance()-decimal.Decimal(0.499950) )
        self.sync_all()
        assert_equal(self.count_unique_txs_logged(),33)

    def run_test(self):
        self.send_some_funds_around()


if __name__ == '__main__':
    WalletNotifications().main ()
