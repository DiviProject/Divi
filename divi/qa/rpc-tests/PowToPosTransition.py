#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test pow-to-pos
#

from test_framework import BitcoinTestFramework
from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
from util import *
import os
import shutil
import random
from datetime import datetime

def createPoSStacks(node,node0_address,blocksToMine):
    node.setgenerate(True,blocksToMine)
    node.sendtoaddress(node0_address,60000.0)
    return True

class PowToPosTransitionTest(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.options.nocleanup = True
        self.nodes.append(start_node(0, self.options.tmpdir))
        self.is_network_split = False

    def run_test(self):
        targetNumberOfBlocks = 101
        try:
            node = self.nodes[0]
            node0_address = node.getnewaddress("")
            for j in range(2):
                createPoSStacks(node,node0_address,50)
            set_node_times(self.nodes, int(datetime.utcnow().strftime('%s')) + 60*60)
            while True:
                node.setgenerate(True,1)
                if node.getblockcount() == targetNumberOfBlocks:
                    break
        except Exception as e:
            print('Error: {0}'.format(str(e)))

        self.sync_all()
        assert_equal(self.nodes[0].getblockcount(), targetNumberOfBlocks)
if __name__ == '__main__':
    PowToPosTransitionTest().main()
