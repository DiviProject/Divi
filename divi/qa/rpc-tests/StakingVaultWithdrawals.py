#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test StakingVault's ability to debit specific vaults
#

from test_framework import BitcoinTestFramework
from util import *

class StakingVaultStakingTest(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir,["-autocombine=0"]))
        self.nodes.append(start_node(1, self.options.tmpdir,["-autocombine=0"]))
        self.nodes.append(start_node(2, self.options.tmpdir,["-autocombine=0","-vault=1"]))

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        self.is_network_split = False

    def create_vault_encoding(self):
        owner_node = self.nodes[1]
        vault_node = self.nodes[2]
        owner_address = owner_node.getnewaddress()
        manager_address = vault_node.getnewaddress()
        self.owner_addresses.append(owner_address)
        self.manager_addresses.append(manager_address)
        self.vault_encodings.append(str(owner_address+':'+manager_address))
        assert_equal(owner_node.validateaddress(owner_address)["isvalid"], True)
        assert_equal(vault_node.validateaddress(manager_address)["isvalid"], True)
        return self.vault_encodings[-1]

    def compute_vault_balances(self,owner_node):
        vault_balances = {}
        vault_records = owner_node.getcoinavailability(True)["Vaulted"]["AllVaults"]
        for vaultRecord in vault_records:
            vault_balances[vaultRecord["vault"]] = vaultRecord["value"]

        return vault_balances

    def run_test(self):
        minting_node = self.nodes[0]
        owner_node = self.nodes[1]
        initial_balance = Decimal(10000.0)
        withdraw_amount = Decimal(5000.0)

        self.owner_addresses = []
        self.manager_addresses = []
        self.vault_encodings = []
        minting_node.setgenerate(50)
        minting_node.fundvault(self.create_vault_encoding(), initial_balance)
        minting_node.fundvault(self.create_vault_encoding(), initial_balance)
        minting_node.setgenerate(50)
        self.sync_all()

        vault_balances = self.compute_vault_balances(owner_node)
        for _, vault_value in vault_balances.items():
          assert_equal(vault_value,Decimal(10000.0))

        assert_equal(self.vault_encodings[0] in vault_balances,True)
        assert_equal(self.vault_encodings[1] in vault_balances,True)
        vault_name_to_debit = self.vault_encodings[0]
        withdraw_destination = minting_node.getnewaddress()
        owner_node.debitvaultbyname(vault_name_to_debit, withdraw_destination, withdraw_amount)
        vault_balances_after = self.compute_vault_balances(owner_node)
        assert_equal(vault_name_to_debit in vault_balances_after,False) # Debiting single utxo consumes the whole balance
        self.sync_all()
        minting_node.setgenerate(1)
        self.sync_all()
        vault_balances_after = self.compute_vault_balances(owner_node)
        assert_equal(vault_name_to_debit in vault_balances_after,True) # Vault should now be funded with change amount
        assert_greater_than(vault_balances_after[vault_name_to_debit], initial_balance-withdraw_amount-Decimal(1.0))


if __name__ == '__main__':
    StakingVaultStakingTest().main()
