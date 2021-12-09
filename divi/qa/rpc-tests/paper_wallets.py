#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test for encrypted paper wallets
#

from test_framework import BitcoinTestFramework
from util import *
from messages import *
from script import *
import codecs
from decimal import Decimal
class PaperWallets (BitcoinTestFramework):

    def setup_network(self):
        #self.nodes = start_nodes(2, self.options.tmpdir)
        self.nodes = []
        self.args = []
        self.is_network_split = False

    def create_paper_wallet(self,password="password"):
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=["-listen=0","-maxconnections=0"]))
        node=self.nodes[0]
        result = node.bip38paperwallet(password)
        node.stop()
        self.nodes[0] = None
        return result

    def create_additional_nodes(self):
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=self.args))
        self.nodes.append (start_node(2, self.options.tmpdir, extra_args=self.args))
        connect_nodes (self.nodes[1], 2)

    def run_test(self):
        paper_wallet = self.create_paper_wallet()
        self.create_additional_nodes()
        self.nodes[1].setgenerate(True,24)
        sync_blocks(self.nodes)
        self.nodes[1].sendtoaddress(paper_wallet["Address"],2000.0)
        self.nodes[1].setgenerate(True,1)
        sync_blocks(self.nodes)
        import_result = self.nodes[2].bip38decrypt(paper_wallet["Encrypted Key"],"password")
        attempts_to_sync = 0
        while self.nodes[2].getblockcount() < 25 and attempts_to_sync < 10:
            time.sleep(0.1)
        assert_equal(self.nodes[2].getblockcount(),25)
        assert_equal(self.nodes[2].getbalance(),2000.0)
        assert_equal(import_result["Address"],paper_wallet["Address"])


if __name__ == '__main__':
    PaperWallets().main ()
