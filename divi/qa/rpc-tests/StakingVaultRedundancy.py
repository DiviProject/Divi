#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test StakingVault's ability to split utxos across servers
#

from test_framework import BitcoinTestFramework
from authproxy import AuthServiceProxy, JSONRPCException
from util import *
import os
import shutil
import random
from datetime import datetime
from PowToPosTransition import generatePoSBlocks

class StakingVaultRedundancy(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir,["-debug","-autocombine=0"]))
        self.nodes.append(start_node(1, self.options.tmpdir,["-debug","-autocombine=0","-spendzeroconfchange"]))
        self.nodes.append(start_node(2, self.options.tmpdir,["-debug","-autocombine=0","-vault=1"]))
        self.nodes.append(start_node(3, self.options.tmpdir,["-debug","-autocombine=0","-vault=1"]))

        self.vault_keys_node = self.nodes[0]
        self.owner_node = self.nodes[1]
        self.vault1 = self.nodes[2]
        self.vault2 = self.nodes[3]

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 0, 3)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 1, 3)
        connect_nodes_bi(self.nodes, 2, 3)
        self.is_network_split = False

    def createVaultStacks(self):
      self.owner_node.setgenerate(True,40)
      sync_blocks(self.nodes)
      vaultOwnerAddress = self.owner_node.getnewaddress()
      addr = self.vault_keys_node.getnewaddress()

      key = self.vault_keys_node.dumpprivkey(addr)
      stop_node(self.vault_keys_node,0)
      self.nodes[0] = None

      self.vault1.importprivkey(key)
      self.vault2.importprivkey(key)

      txDetails = []
      vault_encoding = vaultOwnerAddress+":"+addr
      for txCount in range(30):
        vaultdata = self.owner_node.fundvault(vault_encoding,400)
        txDetails.append(vaultdata)

      self.owner_node.setgenerate(True,1)
      sync_blocks(self.nodes)
      for txCount in range(30):
        if txCount % 2 == 1:
          self.vault1.addvault(vault_encoding,txDetails[txCount]["txhash"])
        else:
          self.vault2.addvault(vault_encoding,txDetails[txCount]["txhash"])

      self.owner_node.setgenerate(True,100- self.owner_node.getblockcount())
      sync_blocks(self.nodes)

    def run_test(self):
        print ("Setting up PoS coins...")
        self.createVaultStacks()
        assert_equal(self.vault1.getbalance()+self.vault2.getbalance(), Decimal(30*400.0))

        print ("Vault trying to mine PoS blocks now...")
        startVault2Balance = self.vault2.getbalance()
        for _ in range(15):
          self.vault1.setgenerate(True,1)
          sync_blocks(self.nodes)
          assert_equal(self.vault2.getbalance(),startVault2Balance)

        self.owner_node.setgenerate(True,20)
        sync_blocks(self.nodes)

        startVault1Balance = self.vault1.getbalance()
        for _ in range(15):
          self.vault2.setgenerate(True,1)
          sync_blocks(self.nodes)
          assert_equal(self.vault1.getbalance(),startVault1Balance)

        self.owner_node.setgenerate(True,20)
        sync_blocks(self.nodes)

if __name__ == '__main__':
    StakingVaultRedundancy().main()
