#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test automatic rescan trigger on seed recovery
#
from test_framework import BitcoinTestFramework
from util import *
import time

class RescanTest(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.is_network_split = False
        self.nodes.append(start_node(0, self.options.tmpdir))
        self.nodes.append(start_node(1, self.options.tmpdir))
        connect_nodes(self.nodes[0],1)

    def shutdown_backedup_node(self):
        stop_node (self.nodes[1], 1)
        self.nodes[1] = None
        drop_wallet(self.options.tmpdir,1)

    def restart_backedup_node_with_mnemonic(self):
        args = ["-mnemonic="+str(self.mnemonic), "-mocktime="+str(self.mocktime),"-keypool=10"]
        self.nodes[1] = start_node(1, self.options.tmpdir,extra_args=args)

    def restart_backedup_node_with_hdseed(self):
        args = ["-hdseed="+str(self.hdseed), "-mocktime="+str(self.mocktime),"-keypool=10"]
        self.nodes[1] = start_node(1, self.options.tmpdir,extra_args=args)

    def run_test(self):
        hdinfo = self.nodes[1].dumphdinfo()
        self.mnemonic = hdinfo["mnemonic"]
        self.hdseed = hdinfo["hdseed"]
        self.nodes[1].setgenerate(5)
        self.sync_all()
        self.nodes[0].setgenerate(20)
        self.sync_all()
        expected_balance = Decimal(5*1250)
        assert_equal(self.nodes[1].getbalance(),expected_balance)
        self.shutdown_backedup_node()
        self.mocktime = int(time.time()) + 3*3600
        self.nodes[0].setmocktime(self.mocktime)
        self.nodes[0].setgenerate(20)
        self.restart_backedup_node_with_mnemonic()
        connect_nodes(self.nodes[0],1)
        self.sync_all()
        assert_equal(self.nodes[1].dumphdinfo()["hdseed"],self.hdseed)
        assert_equal(self.nodes[1].getbalance(),expected_balance)

        self.shutdown_backedup_node()
        self.restart_backedup_node_with_hdseed()
        connect_nodes(self.nodes[0],1)
        self.sync_all()
        assert_equal(self.nodes[1].dumphdinfo()["hdseed"],self.hdseed)
        assert_equal(self.nodes[1].getbalance(),expected_balance)


if __name__ == '__main__':
    RescanTest().main()
