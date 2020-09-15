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
from PowToPosTransition import generatePoSBlocks


def createVaultPoSStacks(nodes, all_nodes):
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
    mintingNode = nodes[0]
    for vaultNode in [all_nodes[1]]:
      for _ in range(parts):
        mintingNode.fundvault(vaultNode.getnewaddress(),value)

    # Make sure to get all those transactions mined.
    sync_mempools(nodes)
    while len(nodes[0].getrawmempool()) > 0:
      nodes[0].setgenerate(True, 1)
    sync_blocks(all_nodes)

class StakingVaultFunding(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, []))
        self.nodes.append(start_node(1, self.options.tmpdir, []))
        self.nodes.append(start_node(2, self.options.tmpdir, []))
        connect_nodes(self.nodes[1], 0)
        connect_nodes(self.nodes[2], 0)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        miningNode = self.nodes[0]
        vaultNode = self.nodes[1]
        createVaultPoSStacks([miningNode], self.nodes)
        self.sync_all()
        powBlocks = 100
        # Mine missing PoW blocks
        cnt = miningNode.getblockcount()
        missingBlocks = max(powBlocks - cnt,0)
        generatePoSBlocks([miningNode], 0, missingBlocks)

        # Send funds to vault
        self.sync_all()
        assert_equal(vaultNode.getbalance(), 8000.0)
        walletInfo = miningNode.getwalletinfo()
        totalMiningNodeBalance = float(walletInfo["immature_balance"]) + float(walletInfo["balance"])
        feeTolerance = 1.0
        perBlockReward = 1250.0
        assert totalMiningNodeBalance > perBlockReward*powBlocks - feeTolerance
        assert totalMiningNodeBalance <= perBlockReward*powBlocks


if __name__ == '__main__':
    StakingVaultFunding().main()
