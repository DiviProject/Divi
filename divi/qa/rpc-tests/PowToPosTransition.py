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

def createPoSStacks(nodes, all_nodes):
    """Makes sure all listed nodes have a stack of coins that is
    suitable for staking indefinitely.

    Ideally this should be done right on top of a clean chain."""

    # Make sure all nodes have matured coins.
    for n in nodes:
      n.setgenerate(True, 20)
      sync_blocks(nodes)
    nodes[0].setgenerate(True, 20)

    # Split those coins up into many pieces that can each be used
    # individually for staking.
    parts = 20
    value = 400
    totalValue = parts * value
    for n in nodes:
      assert n.getbalance() > totalValue
      to = {}
      for _ in range(parts):
        to[n.getnewaddress()] = value
      n.sendmany("", to)

    # Make sure to get all those transactions mined.
    sync_mempools(nodes)
    while len(nodes[0].getrawmempool()) > 0:
      nodes[0].setgenerate(True, 1)
    sync_blocks(all_nodes)

def generatePoSBlocks(nodes, index, num):
    """Generates num blocks with nodes[index], taking care of
    setting the mock time as needed to do this with PoS."""

    # Number of blocks to mine in one go, before bumping the mock time
    # further.  This must be little enough so that the blocks are still
    # accepted even if they are beyond the node time.  Ideally not too
    # small either, as that will make it slower.
    blocksPerStep = 100

    node = nodes[index]
    while True:
      bestBlock = node.getblockheader(node.getbestblockhash())
      set_node_times(nodes, bestBlock["time"])

      if num == 0:
        sync_blocks(nodes)
        return

      n = min(num, blocksPerStep)
      assert n > 0
      node.setgenerate(True, n)
      num -= n

class PowToPosTransitionTest(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir))
        self.is_network_split = False

    def run_test(self):
        posStart = 100
        targetNumberOfBlocks = posStart + 500
        node = self.nodes[0]

        print ("Setting up PoS coins...")
        createPoSStacks([node], self.nodes)
        self.sync_all()

        print ("Mining remaining PoW blocks...")
        missing = posStart - node.getblockcount()
        assert missing > 0
        node.setgenerate(True, missing)
        self.sync_all()

        print ("Trying to mine PoS blocks now...")
        missing = targetNumberOfBlocks - node.getblockcount()
        assert missing > 0
        generatePoSBlocks(self.nodes, 0, missing)

        assert_equal(node.getblockcount(), targetNumberOfBlocks)

if __name__ == '__main__':
    PowToPosTransitionTest().main()
