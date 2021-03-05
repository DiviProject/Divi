#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test StakingVault's ability to successfully stake
#

from test_framework import BitcoinTestFramework
from authproxy import AuthServiceProxy, JSONRPCException
from util import *
import os
import shutil
import random
from datetime import datetime
from PowToPosTransition import generatePoSBlocks

def createVaultPoSStacks(ownerNodes,vaultNode):
    """Makes sure all listed nodes have a stack of coins that is
    suitable for staking indefinitely.

    Ideally this should be done right on top of a clean chain."""

    # Make sure all nodes have matured coins.
    minting_node = ownerNodes[0]
    owner_node = ownerNodes[1]

    all_nodes = ownerNodes[:]
    all_nodes.append(vaultNode)

    owner_node.setgenerate(True, 40)
    sync_blocks(all_nodes)
    minting_node.setgenerate(True, 21)
    sync_blocks(all_nodes)

    # Split those coins up into many pieces that can each be used
    # individually for staking.
    parts = 40
    value = 1249.865
    funding_data = []
    vault_node_address = vaultNode.getnewaddress()
    vault_encoding = ""
    for node in [owner_node]:
      assert node.getbalance() >= parts*value, "Result: {}".format(node.getbalance())
      node_address=node.getnewaddress()
      vault_encoding = node_address+":"+vault_node_address
      for _ in range(parts):
        funding_data.append( node.fundvault(vault_encoding,value) )

    # Make sure to get all those transactions mined.
    sync_mempools(all_nodes)
    while len(minting_node.getrawmempool()) > 0:
      minting_node.setgenerate(True, 1)
    sync_blocks(all_nodes)
    for funding_datum in funding_data:
      assert vaultNode.addvault(funding_datum["vault"],funding_datum["txhash"])["succeeded"]

class StakingVaultStakingTest(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir,["-debug","-autocombine=0"]))
        self.nodes.append(start_node(1, self.options.tmpdir,["-debug","-autocombine=0"]))
        self.nodes.append(start_node(2, self.options.tmpdir,["-debug","-autocombine=0"]))

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        self.is_network_split = False

    def run_test(self):
        posStart = 100
        targetNumberOfBlocks = posStart + 100
        minting_node = self.nodes[0]
        owner_node = self.nodes[1]

        print ("Setting up PoS coins...")
        ownerNodes = self.nodes[:-1]
        vaultNode = self.nodes[-1]
        createVaultPoSStacks(ownerNodes,vaultNode)
        self.sync_all()

        print ("Mining remaining PoW blocks...")
        missing = posStart - minting_node.getblockcount()
        assert missing > 0
        minting_node.setgenerate(True, missing)
        self.sync_all()

        print ("Vault trying to mine PoS blocks now...")
        missing = targetNumberOfBlocks - minting_node.getblockcount()
        assert missing > 0
        generatePoSBlocks(self.nodes, -1, missing)

        assert_equal(vaultNode.getblockcount(), targetNumberOfBlocks)
        fee = 100
        assert_equal(owner_node.getwalletinfo()["unconfirmed_balance"],0)
        expectedMinimumUnconfirmedBalance = vaultNode.getcoinavailability()["Stakable"]-fee
        owner_node.reclaimvaultfunds(owner_node.getnewaddress(),expectedMinimumUnconfirmedBalance)
        assert_greater_than(owner_node.getwalletinfo()["unconfirmed_balance"],expectedMinimumUnconfirmedBalance)

if __name__ == '__main__':
    StakingVaultStakingTest().main()
