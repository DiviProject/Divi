#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test staking vault funding
#

from test_framework import BitcoinTestFramework
from util import *
import os
import shutil
import random
from datetime import datetime
from PowToPosTransition import generatePoSBlocks


def createVaultPoSStacks(nodes):
    """Makes sure all listed nodes have a stack of coins that is
    suitable for staking indefinitely.

    Ideally this should be done right on top of a clean chain."""

    # Make sure all nodes have matured coins.
    mintingNode = nodes[0]
    for node in [nodes[0],nodes[2]]:
      node.setgenerate( 25)
      sync_blocks(nodes)
    mintingNode.setgenerate( 20)

    # Split those coins up into many pieces that can each be used
    # individually for staking.
    parts = 20
    value = 400
    for _ in range(parts):
      mintingNode.sendtoaddress(mintingNode.getnewaddress(),value)

    # Make sure to get all those transactions mined.
    sync_mempools(nodes)
    while len(nodes[0].getrawmempool()) > 0:
      nodes[0].setgenerate( 1)
    sync_blocks(nodes)

class StakingVaultFunding(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, []))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-vault=1"]))
        self.nodes.append(start_node(2, self.options.tmpdir, []))
        connect_nodes(self.nodes[1], 0)
        connect_nodes(self.nodes[2], 0)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        miningNode = self.nodes[0]
        vaultNode = self.nodes[1]
        createVaultPoSStacks(self.nodes)
        self.sync_all()
        powBlocks = 100
        # Mine missing PoW blocks
        cnt = miningNode.getblockcount()
        missingBlocks = max(powBlocks - cnt,0)
        generatePoSBlocks([miningNode], 0, missingBlocks)

        self.sync_all()
        sync_blocks(self.nodes)
        self.nodes[2].setgenerate(20)
        sync_blocks(self.nodes)

        # Send funds to vault
        self.sync_all()
        totalMined = 1250.0*float(powBlocks-25)
        intendedVaultedAmount = 8000.0
        miningNodeAllocations = miningNode.getcoinavailability()
        managedFunds = miningNodeAllocations["Stakable"] - miningNodeAllocations["Spendable"]
        assert_near(miningNodeAllocations["Spendable"], totalMined,1.0)
        assert_near(miningNodeAllocations["Stakable"], totalMined,1.0)
        assert_near(miningNodeAllocations["Vaulted"], 0.0,1e-10)
        assert_equal(miningNodeAllocations["Spendable"]+managedFunds+miningNodeAllocations["Vaulted"], miningNode.getbalance())
        vaultFundingData = miningNode.fundvault(vaultNode.getnewaddress(),intendedVaultedAmount)

        self.sync_all()
        sync_blocks(self.nodes)
        self.nodes[2].setgenerate(1)
        sync_blocks(self.nodes)

        # Miner node has funds as expected and sends to a vault node
        miningNodeAllocations = miningNode.getcoinavailability()
        managedFunds = miningNodeAllocations["Stakable"] - miningNodeAllocations["Spendable"]
        assert_near(miningNodeAllocations["Spendable"], totalMined - intendedVaultedAmount,5.0)
        assert_near(miningNodeAllocations["Stakable"], totalMined - intendedVaultedAmount,5.0)
        assert_near(miningNodeAllocations["Vaulted"], intendedVaultedAmount,1e-10)
        assert_equal(miningNodeAllocations["Spendable"] +managedFunds+ miningNodeAllocations["Vaulted"], miningNode.getbalance())

        # Vault node has not accepted the responsibility to stake on behalf of the Miner node
        vaultNodeAllocations = vaultNode.getcoinavailability()
        assert_near(vaultNodeAllocations["Spendable"], 0.0,1e-10)
        assert_near(vaultNodeAllocations["Stakable"], 0.0,1e-10)
        assert_near(vaultNodeAllocations["Vaulted"], 0.0,1e-10)
        assert_equal(0.0, vaultNode.getstakingstatus()["staking_balance"])

        # Vault node has now accepted the responsibility to stake on behalf of the Miner node
        vaultEncoding = vaultFundingData["vault"][0]["encoding"]
        txhash = vaultFundingData["txhash"]
        vaultNode.addvault(vaultEncoding,txhash)
        vaultNodeAllocations = vaultNode.getcoinavailability()
        managedFunds = vaultNodeAllocations["Stakable"] - vaultNodeAllocations["Spendable"]
        assert_near(vaultNodeAllocations["Spendable"], 0.0,1e-10)
        assert_near(vaultNodeAllocations["Stakable"], intendedVaultedAmount,1e-10)
        assert_near(vaultNodeAllocations["Vaulted"], 0.0,1e-10)
        assert_equal(vaultNodeAllocations["Spendable"] +managedFunds+ vaultNodeAllocations["Vaulted"], vaultNode.getstakingstatus()["staking_balance"])

        miningNode.fundvault(vaultFundingData["vault"][0]["encoding"],intendedVaultedAmount)
        miningNode.setgenerate(1)
        self.sync_all()
        assert_equal(vaultNode.getcoinavailability()["Stakable"], Decimal(2*intendedVaultedAmount))



if __name__ == '__main__':
    StakingVaultFunding().main()
