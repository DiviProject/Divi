#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework import BitcoinTestFramework
from util import *
from decimal import Decimal

class VaultWhitelisting(BitcoinTestFramework):
    def setup_network(self, split=False):
        daemon_flags = [["-spendzeroconfchange"]] * 3
        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=daemon_flags)
        connect_nodes(self.nodes[0],1)
        connect_nodes(self.nodes[1],2)
        connect_nodes(self.nodes[0],2)
        self.is_network_split=False
        self.sync_all()

    def restart_node(self, nodeId, daemon_flags):
        stop_node (self.nodes[nodeId], nodeId)
        self.nodes[nodeId] = None
        self.nodes[nodeId] = start_node(nodeId, self.options.tmpdir, extra_args=daemon_flags)

    def run_test (self):
        self.block_producer = self.nodes[0]
        self.vault_owner = self.nodes[1]
        self.vault_manager = self.nodes[2]

        self.block_producer.setgenerate(30)
        sync_blocks(self.nodes)

        vault_owner_address = self.vault_owner.getnewaddress()
        self.block_producer.sendtoaddress(vault_owner_address,10000.0)
        self.block_producer.setgenerate(1)
        sync_blocks(self.nodes)

        vault_manager_address = self.vault_manager.getnewaddress()
        vault_funding_details = self.vault_owner.fundvault(vault_manager_address,9500.0)
        raw_tx_details = self.vault_owner.getrawtransaction(vault_funding_details["txhash"])
        tx_details = self.vault_owner.decoderawtransaction(raw_tx_details)
        scriptPubKeysHex = [ output["scriptPubKey"]["hex"] for output in tx_details["vout"] if output["scriptPubKey"]["type"] == "vault" ]
        assert_equal(len(scriptPubKeysHex), 1)

        print("Wait for restart")
        self.restart_node(2, ["-spendzeroconfchange","-whitelisted_vault="+scriptPubKeysHex[0]])
        print("Restart finished")

        print("Updating vault manager reference")
        self.vault_manager = self.nodes[2]
        print("Re-connecting vault manager")
        connect_nodes(self.vault_manager,0)
        connect_nodes(self.vault_manager,1)
        print("Waiting for block sync")
        sync_blocks(self.nodes)
        print("Waiting for mempool sync")
        sync_mempools([self.block_producer, self.vault_owner])

        print("Done sync")
        self.block_producer.setgenerate(1)
        sync_blocks(self.nodes)

        assert_equal(self.vault_manager.getcoinavailability()["Stakable"], Decimal(9500.0))


if __name__ == '__main__':
    VaultWhitelisting().main ()