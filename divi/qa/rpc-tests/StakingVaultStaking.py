#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test pow-to-pos
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
    all_nodes = ownerNodes[:]
    all_nodes.append(vaultNode)
    for node in ownerNodes:
      node.setgenerate(True, 40)
      sync_blocks(all_nodes)
    ownerNodes[0].setgenerate(True, 21)

    # Split those coins up into many pieces that can each be used
    # individually for staking.
    parts = 40
    value = 400
    funding_data = []
    vault_node_address = vaultNode.getnewaddress()
    for node in ownerNodes:
      assert node.getbalance() > parts*value
      node_address=node.getnewaddress()
      vault_encoding = node_address+":"+vault_node_address
      for _ in range(parts):
        funding_data.append( node.fundvault(vault_encoding,value) )
    # Make sure to get all those transactions mined.
    sync_mempools(all_nodes)
    while len(ownerNodes[0].getrawmempool()) > 0:
      ownerNodes[0].setgenerate(True, 1)
    sync_blocks(all_nodes)
    for funding_datum in funding_data:
      assert vaultNode.addvaultscript(funding_datum["vault"],funding_datum["txhash"])["succeeded"]

class StakingVaultStakingTest(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir,["-debug","-autocombine=0"]))
        self.nodes.append(start_node(1, self.options.tmpdir,["-debug","-autocombine=0"]))
        stakingVaultForkActivationTime = 2000000000
        set_node_times(self.nodes,stakingVaultForkActivationTime)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False

    def run_test(self):
        posStart = 100
        targetNumberOfBlocks = posStart + 100
        firstOwnerNode = self.nodes[0]

        print ("Setting up PoS coins...")
        ownerNodes = self.nodes[:-1]
        vaultNode = self.nodes[-1]
        createVaultPoSStacks(ownerNodes,vaultNode)
        self.sync_all()

        print ("Mining remaining PoW blocks...")
        missing = posStart - firstOwnerNode.getblockcount()
        assert missing > 0
        firstOwnerNode.setgenerate(True, missing)
        self.sync_all()

        print ("Vault trying to mine PoS blocks now...")
        missing = targetNumberOfBlocks - firstOwnerNode.getblockcount()
        assert missing > 0
        balanceBeforeVaultStaking = vaultNode.getcoinavailability()["Stakable"]
        generatePoSBlocks(self.nodes, -1, missing)
        generatePoSBlocks(self.nodes, 0, 20)

        assert_equal(vaultNode.getblockcount(), targetNumberOfBlocks+20)
        balanceAfterVaultStaking = balanceBeforeVaultStaking + missing*456
        assert_near(balanceAfterVaultStaking, vaultNode.getcoinavailability()["Stakable"],1000)
        fee = 100
        expectedUnconfirmedBalance = vaultNode.getcoinavailability()["Stakable"]-fee
        firstOwnerNode.reclaimvaultfunds(firstOwnerNode.getnewaddress(),expectedUnconfirmedBalance)
        assert_equal(firstOwnerNode.getwalletinfo()["unconfirmed_balance"],expectedUnconfirmedBalance)

if __name__ == '__main__':
    StakingVaultStakingTest().main()
